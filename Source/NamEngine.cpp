#include "NamEngine.h"

// NAM Core headers
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include <filesystem>

// ─────────────────────────────────────────────────────────────────────────────
NamEngine::NamEngine() = default;

NamEngine::~NamEngine()
{
    clearModel();

    // Wait for any in-flight load to finish before we go away.
    if (loadFuture_.valid())
        loadFuture_.wait();

    // Clean up any pending-delete model.
    delete pendingDelete_.exchange(nullptr);
    delete activeModel_.exchange(nullptr);
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void NamEngine::prepare(double sampleRate, int maxBlockSize)
{
    hostSampleRate_ = sampleRate;
    maxBlockSize_   = maxBlockSize;

    needsResampling_ = (std::abs(sampleRate - kNamSampleRate) > 0.5);

    if (needsResampling_)
    {
        upsampleRatio_   = kNamSampleRate / sampleRate;   // > 1 when sampleRate < 48k
        downsampleRatio_ = sampleRate / kNamSampleRate;   // < 1 when sampleRate < 48k

        // Worst-case 48 kHz buffer size
        const int namMaxBlock = static_cast<int>(
            std::ceil(maxBlockSize * upsampleRatio_) + 32);

        resampledInput_ .resize(static_cast<size_t>(namMaxBlock), 0.0f);
        resampledOutput_.resize(static_cast<size_t>(namMaxBlock), 0.0f);
        dblInput_ .resize(static_cast<size_t>(namMaxBlock), 0.0);
        dblOutput_.resize(static_cast<size_t>(namMaxBlock), 0.0);

        upsampler_  .reset();
        downsampler_.reset();

        // Latency: LagrangeInterpolator introduces a fractional delay.
        // Report conservative 1-sample latency at non-native rates.
        latencySamples_ = 1;
    }
    else
    {
        resampledInput_ .clear();
        resampledOutput_.clear();
        dblInput_ .resize(static_cast<size_t>(maxBlockSize), 0.0);
        dblOutput_.resize(static_cast<size_t>(maxBlockSize), 0.0);
        latencySamples_ = 0;
    }

    // If a model is already loaded, reset it for the new block size / rate.
    nam::DSP* mdl = activeModel_.load(std::memory_order_acquire);
    if (mdl)
        mdl->Reset(kNamSampleRate, maxBlockSize_);
}

void NamEngine::release()
{
    upsampler_.reset();
    downsampler_.reset();
}

// ─── Audio thread ─────────────────────────────────────────────────────────────

void NamEngine::process(const float* input, float* output, int numSamples, bool bypass)
{
    // Retire any model that was swapped out.
    nam::DSP* toDelete = pendingDelete_.exchange(nullptr, std::memory_order_acq_rel);
    if (toDelete)
        delete toDelete;

    nam::DSP* mdl = activeModel_.load(std::memory_order_acquire);

    if (bypass || mdl == nullptr)
    {
        if (input != output)
            std::copy(input, input + numSamples, output);
        return;
    }

    if (!needsResampling_)
    {
        // Direct processing – convert float→double, call NAM, convert back.
        // nam::DSP::process takes double** (array of channel pointers).
        for (int i = 0; i < numSamples; ++i)
            dblInput_[static_cast<size_t>(i)] = static_cast<double>(input[i]);
        double* inPtr  = dblInput_.data();
        double* outPtr = dblOutput_.data();
        mdl->process(&inPtr, &outPtr, numSamples);
        for (int i = 0; i < numSamples; ++i)
            output[i] = static_cast<float>(dblOutput_[static_cast<size_t>(i)]);
    }
    else
    {
        // ── Upsample host → 48 kHz ───────────────────────────────────────────
        const int namSamples = static_cast<int>(
            upsampler_.process(upsampleRatio_,
                               input,
                               resampledInput_.data(),
                               numSamples));

        // ── Run NAM at 48 kHz (float→double→float) ───────────────────────────
        for (int i = 0; i < namSamples; ++i)
            dblInput_[static_cast<size_t>(i)] = static_cast<double>(resampledInput_[static_cast<size_t>(i)]);
        double* inPtr  = dblInput_.data();
        double* outPtr = dblOutput_.data();
        mdl->process(&inPtr, &outPtr, namSamples);
        for (int i = 0; i < namSamples; ++i)
            resampledOutput_[static_cast<size_t>(i)] = static_cast<float>(dblOutput_[static_cast<size_t>(i)]);

        // ── Downsample 48 kHz → host ─────────────────────────────────────────
        downsampler_.process(downsampleRatio_,
                             resampledOutput_.data(),
                             output,
                             numSamples);
    }
}

// ─── Load state accessors ────────────────────────────────────────────────────

NamEngine::LoadState NamEngine::getLoadState() const noexcept
{
    return loadState_.load(std::memory_order_relaxed);
}

juce::String NamEngine::getModelName() const
{
    return modelName_;
}

// ─── Model loading ────────────────────────────────────────────────────────────

void NamEngine::loadModel(const juce::String& filePath,
                          std::function<void(LoadState, juce::String)> callback)
{
    // Don't start a new load while one is in progress.
    if (loadState_.load() == LoadState::Loading)
        return;

    loadState_.store(LoadState::Loading, std::memory_order_relaxed);
    modelPath_ = filePath.toStdString();
    modelName_ = juce::File(filePath).getFileNameWithoutExtension();

    std::string pathCopy = modelPath_;

    loadFuture_ = std::async(std::launch::async, [this, pathCopy, cb = std::move(callback)]()
    {
        doLoad(pathCopy, std::move(cb));
    });
}

void NamEngine::doLoad(const std::string& path,
                       std::function<void(LoadState, juce::String)> callback)
{
    std::unique_ptr<nam::DSP> newModel;

    try
    {
        // Disambiguate: get_dsp has overloads for json and filesystem::path.
        newModel = nam::get_dsp(std::filesystem::path(path));

        if (!newModel)
            throw std::runtime_error("nam::get_dsp returned null");

        // Prepare the model.
        newModel->Reset(kNamSampleRate, maxBlockSize_);
        newModel->prewarm();
    }
    catch (const std::exception& e)
    {
        loadState_.store(LoadState::Error, std::memory_order_relaxed);

        juce::MessageManager::callAsync([cb = std::move(callback), msg = juce::String(e.what())]()
        {
            cb(LoadState::Error, msg);
        });
        return;
    }

    // Atomically swap in the new model; queue old model for deletion on audio thread.
    nam::DSP* rawNew = newModel.release();
    nam::DSP* rawOld = activeModel_.exchange(rawNew, std::memory_order_acq_rel);

    // Store the old model for deferred deletion on the audio thread.
    // If there was already a pending delete, clean it up here (background thread).
    delete pendingDelete_.exchange(rawOld, std::memory_order_acq_rel);

    loadState_.store(LoadState::Loaded, std::memory_order_relaxed);

    juce::MessageManager::callAsync([cb = std::move(callback)]()
    {
        cb(LoadState::Loaded, "Model loaded successfully");
    });
}

void NamEngine::clearModel()
{
    nam::DSP* old = activeModel_.exchange(nullptr, std::memory_order_acq_rel);
    delete old;
    loadState_.store(LoadState::Idle, std::memory_order_relaxed);
    modelPath_.clear();
    modelName_.clear();
}
