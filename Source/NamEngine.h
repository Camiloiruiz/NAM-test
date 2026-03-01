#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <future>
#include <memory>
#include <string>

// NAM Core forward declarations – full headers included in .cpp
namespace nam { class DSP; }

/**
 * NamEngine
 * ---------
 * Thread-safe wrapper around NeuralAmpModelerCore.
 *
 * Responsibilities:
 *  - Async loading of .nam files on a background thread.
 *  - Lock-free pointer swap so the audio thread is never blocked.
 *  - Sample-rate conversion: NAM models are trained at 48 kHz.
 *    At 48 kHz the resampler is bypassed entirely (zero extra latency).
 *    At all other rates, up-/down-sampling is applied and the latency
 *    is reported through getLatencySamples().
 *  - prewarm() called on the new model before making it active.
 *
 * Usage (audio thread):
 *   engine.prepare(sampleRate, maxBlockSize);
 *   engine.process(monoInput, monoOutput, numSamples, bypass);
 *
 * Usage (message/GUI thread):
 *   engine.loadModel("path/to/model.nam", callback);
 *   engine.setModelPath() / engine.getModelPath() for state persistence.
 */
class NamEngine
{
public:
    NamEngine();
    ~NamEngine();

    // ── Lifecycle ────────────────────────────────────────────────────────────

    /** Called from prepareToPlay(). Thread-safe. */
    void prepare(double sampleRate, int maxBlockSize);

    /** Called from releaseResources(). */
    void release();

    // ── Audio thread ─────────────────────────────────────────────────────────

    /**
     * Process a mono buffer in-place.
     * @param input      Read-only input samples.
     * @param output     Output samples (may alias input).
     * @param numSamples Number of frames to process.
     * @param bypass     If true, copies input→output without model processing.
     */
    void process(const float* input, float* output, int numSamples, bool bypass);

    /**
     * Latency introduced by the resampler (0 at 48 kHz).
     * Report this via AudioProcessor::setLatencySamples().
     */
    int getLatencySamples() const noexcept { return latencySamples_; }

    // ── Model loading (call from GUI / non-audio thread) ─────────────────────

    enum class LoadState { Idle, Loading, Loaded, Error };

    /** Asynchronously load a .nam file. Calls @p callback on the message thread. */
    void loadModel(const juce::String& filePath,
                   std::function<void(LoadState, juce::String /*msg*/)> callback);

    /** Discard the currently loaded model (safe to call from any thread). */
    void clearModel();

    LoadState  getLoadState()  const noexcept;
    juce::String getModelPath() const { return juce::String(modelPath_); }
    juce::String getModelName() const;

private:
    // ── Internal helpers ─────────────────────────────────────────────────────

    /** Called on the background loading thread. */
    void doLoad(const std::string& path,
                std::function<void(LoadState, juce::String)> callback);

    void rebuildResamplers();

    // ── Data ─────────────────────────────────────────────────────────────────

    double hostSampleRate_  = 48000.0;
    int    maxBlockSize_    = 512;
    int    latencySamples_  = 0;

    static constexpr double kNamSampleRate = 48000.0;

    // Active model pointer – swapped atomically from background → audio thread.
    // Ownership: the background thread writes a new raw pointer; the audio
    // thread reads it.  Old models are retired via pendingDelete_.
    std::atomic<nam::DSP*> activeModel_  { nullptr };
    std::atomic<nam::DSP*> pendingDelete_{ nullptr };

    // Staging: background thread builds a new model, then atomically publishes it.
    std::unique_ptr<nam::DSP> stagingModel_;

    // Metadata
    std::string  modelPath_;
    juce::String modelName_;
    std::atomic<LoadState> loadState_{ LoadState::Idle };

    // Background loading
    std::future<void> loadFuture_;

    // ── Resampling ────────────────────────────────────────────────────────────
    // We use JUCE's LagrangeInterpolator (4-point) for both up- and down-sampling.
    // At 48 kHz both interpolators are skipped entirely.

    bool needsResampling_ = false;
    double upsampleRatio_   = 1.0;  // hostRate → 48 kHz
    double downsampleRatio_ = 1.0;  // 48 kHz  → hostRate

    juce::LagrangeInterpolator upsampler_;
    juce::LagrangeInterpolator downsampler_;

    // Intermediate buffers (48 kHz domain)
    std::vector<float> resampledInput_;
    std::vector<float> resampledOutput_;

    // Mutex protecting stagingModel_ during construction (background thread only)
    std::mutex loadMutex_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamEngine)
};
