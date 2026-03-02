#include "PluginProcessor.h"
#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Parameter layout
// ─────────────────────────────────────────────────────────────────────────────

juce::AudioProcessorValueTreeState::ParameterLayout
GuitarAmpProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto addGain = [&](const char* id, const char* name, float minDb, float maxDb, float def)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            id, name,
            juce::NormalisableRange<float>(minDb, maxDb, 0.1f),
            def,
            juce::AudioParameterFloatAttributes().withLabel("dB")));
    };

    addGain(ParamID::InputGain,   "Input Gain",   -12.0f, 12.0f,  0.0f);
    addGain(ParamID::GateThresh,  "Gate Thresh",  -80.0f,  0.0f, -80.0f);
    addGain(ParamID::Bass,        "Bass",         -12.0f, 12.0f,  0.0f);
    addGain(ParamID::Mid,         "Mid",          -12.0f, 12.0f,  0.0f);
    addGain(ParamID::Treble,      "Treble",       -12.0f, 12.0f,  0.0f);
    addGain(ParamID::OutputLevel, "Output Level", -40.0f, 12.0f,  0.0f);

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        ParamID::NamBypass, "NAM Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        ParamID::IrBypass,  "IR Bypass",  false));

    return { params.begin(), params.end() };
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

GuitarAmpProcessor::GuitarAmpProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "Parameters", createParameterLayout())
{
}

GuitarAmpProcessor::~GuitarAmpProcessor() = default;

// ─────────────────────────────────────────────────────────────────────────────
// Bus layout
// ─────────────────────────────────────────────────────────────────────────────

bool GuitarAmpProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Accept mono or stereo in/out, both matching.
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() &&
        out != juce::AudioChannelSet::stereo())
        return false;

    return in == out || in == juce::AudioChannelSet::mono();
}

// ─────────────────────────────────────────────────────────────────────────────
// Prepare / Release
// ─────────────────────────────────────────────────────────────────────────────

void GuitarAmpProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate_ = sampleRate;
    currentBlockSize_  = samplesPerBlock;

    // Force EQ coefficient rebuild on next block (sample rate may have changed).
    lastBassDb_ = lastMidDb_ = lastTrebleDb_ = 99999.0f;

    namEngine_.prepare(sampleRate, samplesPerBlock);
    noiseGate_.prepare(sampleRate);

    // Mono work buffer
    monoBuffer_.setSize(1, samplesPerBlock);

    // EQ
    updateEqCoefficients(sampleRate);

    // Convolution
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<uint32_t>(samplesPerBlock);
    spec.numChannels      = static_cast<uint32_t>(
        getTotalNumOutputChannels());

    convolution_.prepare(spec);

    // Latency: resampler delay (0 at 48 kHz)
    setLatencySamples(namEngine_.getLatencySamples());
}

void GuitarAmpProcessor::releaseResources()
{
    namEngine_.release();
    noiseGate_.reset();
    convolution_.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// EQ helpers
// ─────────────────────────────────────────────────────────────────────────────

void GuitarAmpProcessor::updateEqCoefficients(double sampleRate)
{
    juce::dsp::ProcessSpec monoSpec { sampleRate, static_cast<uint32_t>(currentBlockSize_), 1 };

    // Retrieve current dB values (may be called from prepareToPlay where APVTS
    // values are already set to their defaults).
    const float bassDb   = apvts_.getRawParameterValue(ParamID::Bass)  ->load();
    const float midDb    = apvts_.getRawParameterValue(ParamID::Mid)   ->load();
    const float trebleDb = apvts_.getRawParameterValue(ParamID::Treble)->load();

    // Low shelf @ 250 Hz
    *bassFilter_[0].coefficients = *IIRCoeffs::makeLowShelf(
        sampleRate, 250.0, 0.707, juce::Decibels::decibelsToGain(bassDb));
    *bassFilter_[1].coefficients = *bassFilter_[0].coefficients;

    // Peaking @ 800 Hz, Q = 0.7
    *midFilter_[0].coefficients = *IIRCoeffs::makePeakFilter(
        sampleRate, 800.0, 0.7, juce::Decibels::decibelsToGain(midDb));
    *midFilter_[1].coefficients = *midFilter_[0].coefficients;

    // High shelf @ 3500 Hz
    *trebleFilter_[0].coefficients = *IIRCoeffs::makeHighShelf(
        sampleRate, 3500.0, 0.707, juce::Decibels::decibelsToGain(trebleDb));
    *trebleFilter_[1].coefficients = *trebleFilter_[0].coefficients;

    for (auto& f : bassFilter_)   f.prepare(monoSpec);
    for (auto& f : midFilter_)    f.prepare(monoSpec);
    for (auto& f : trebleFilter_) f.prepare(monoSpec);
}

void GuitarAmpProcessor::processEq(juce::AudioBuffer<float>& buffer)
{
    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    const float bassDb   = apvts_.getRawParameterValue(ParamID::Bass)  ->load();
    const float midDb    = apvts_.getRawParameterValue(ParamID::Mid)   ->load();
    const float trebleDb = apvts_.getRawParameterValue(ParamID::Treble)->load();

    // Only rebuild coefficients when a knob has actually moved.
    // IIRCoeffs::make* allocates and computes sin/cos/tan – expensive every block.
    if (bassDb != lastBassDb_ || midDb != lastMidDb_ || trebleDb != lastTrebleDb_)
    {
        lastBassDb_   = bassDb;
        lastMidDb_    = midDb;
        lastTrebleDb_ = trebleDb;

        *bassFilter_[0].coefficients = *IIRCoeffs::makeLowShelf(
            currentSampleRate_, 250.0, 0.707, juce::Decibels::decibelsToGain(bassDb));
        *midFilter_[0].coefficients = *IIRCoeffs::makePeakFilter(
            currentSampleRate_, 800.0, 0.7, juce::Decibels::decibelsToGain(midDb));
        *trebleFilter_[0].coefficients = *IIRCoeffs::makeHighShelf(
            currentSampleRate_, 3500.0, 0.707, juce::Decibels::decibelsToGain(trebleDb));

        // Mirror coefficients to channel 1.
        *bassFilter_[1].coefficients   = *bassFilter_[0].coefficients;
        *midFilter_[1].coefficients    = *midFilter_[0].coefficients;
        *trebleFilter_[1].coefficients = *trebleFilter_[0].coefficients;
    }

    for (int ch = 0; ch < std::min(numCh, 2); ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        juce::dsp::AudioBlock<float> block(&data, 1, static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> ctx(block);

        bassFilter_[ch]  .process(ctx);
        midFilter_[ch]   .process(ctx);
        trebleFilter_[ch].process(ctx);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// processBlock
// ─────────────────────────────────────────────────────────────────────────────

void GuitarAmpProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();

    // Clear any output channels that have no corresponding input.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear(ch, 0, numSamples);

    // ── 1. Input Gain ─────────────────────────────────────────────────────────
    const float inputGainDb = apvts_.getRawParameterValue(ParamID::InputGain)->load();
    const float inputGainLin = juce::Decibels::decibelsToGain(inputGainDb);
    for (int ch = 0; ch < numIn; ++ch)
        buffer.applyGain(ch, 0, numSamples, inputGainLin);

    // ── 2. Sum to mono (NAM is mono) ──────────────────────────────────────────
    monoBuffer_.setSize(1, numSamples, false, false, true);
    monoBuffer_.clear();

    if (numIn >= 2)
    {
        // Average L and R
        monoBuffer_.addFrom(0, 0, buffer, 0, 0, numSamples, 0.5f);
        monoBuffer_.addFrom(0, 0, buffer, 1, 0, numSamples, 0.5f);
    }
    else
    {
        monoBuffer_.copyFrom(0, 0, buffer, 0, 0, numSamples);
    }

    float* mono = monoBuffer_.getWritePointer(0);

    // ── 3. Noise Gate ─────────────────────────────────────────────────────────
    const float gateDb = apvts_.getRawParameterValue(ParamID::GateThresh)->load();
    noiseGate_.process(mono, numSamples, gateDb);

    // ── 4. NAM Model ──────────────────────────────────────────────────────────
    const bool namBypass = apvts_.getRawParameterValue(ParamID::NamBypass)->load() > 0.5f;
    namEngine_.process(mono, mono, numSamples, namBypass);

    // ── 5. Write mono back to stereo buffer ───────────────────────────────────
    for (int ch = 0; ch < numOut; ++ch)
        buffer.copyFrom(ch, 0, monoBuffer_, 0, 0, numSamples);

    // ── 6. EQ (post-NAM, pre-IR) ─────────────────────────────────────────────
    processEq(buffer);

    // ── 7. IR Convolution ────────────────────────────────────────────────────
    const bool irBypass = apvts_.getRawParameterValue(ParamID::IrBypass)->load() > 0.5f;
    if (!irBypass)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        convolution_.process(ctx);
    }

    // ── 8. Output Level ───────────────────────────────────────────────────────
    const float outDb  = apvts_.getRawParameterValue(ParamID::OutputLevel)->load();
    const float outLin = juce::Decibels::decibelsToGain(outDb);
    buffer.applyGain(outLin);
}

// ─────────────────────────────────────────────────────────────────────────────
// File loading
// ─────────────────────────────────────────────────────────────────────────────

void GuitarAmpProcessor::loadNamModel(
    const juce::String& path,
    std::function<void(NamEngine::LoadState, juce::String)> cb)
{
    namFilePath_ = path;
    namEngine_.loadModel(path, std::move(cb));
}

void GuitarAmpProcessor::loadIrFile(const juce::String& path)
{
    irFilePath_ = path;
    irFileName_ = juce::File(path).getFileNameWithoutExtension();
    irLoaded_   = true;

    convolution_.loadImpulseResponse(
        juce::File(path),
        juce::dsp::Convolution::Stereo::no,   // use mono IR
        juce::dsp::Convolution::Trim::yes,
        0,                                     // max IR size (0 = no limit)
        juce::dsp::Convolution::Normalise::yes);
}

void GuitarAmpProcessor::clearIrFile()
{
    irFilePath_.clear();
    irFileName_.clear();
    irLoaded_ = false;
    convolution_.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// State persistence
// ─────────────────────────────────────────────────────────────────────────────

void GuitarAmpProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();

    // Save file paths as child XML elements.
    if (namFilePath_.isNotEmpty())
        state.setProperty("namFilePath", namFilePath_, nullptr);
    if (irFilePath_.isNotEmpty())
        state.setProperty("irFilePath", irFilePath_, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void GuitarAmpProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml) return;

    auto state = juce::ValueTree::fromXml(*xml);
    apvts_.replaceState(state);

    // Restore NAM file path
    if (state.hasProperty("namFilePath"))
    {
        juce::String p = state.getProperty("namFilePath").toString();
        if (juce::File(p).existsAsFile())
        {
            namFilePath_ = p;
            namEngine_.loadModel(p, [](NamEngine::LoadState, juce::String) {});
        }
    }

    // Restore IR file path
    if (state.hasProperty("irFilePath"))
    {
        juce::String p = state.getProperty("irFilePath").toString();
        if (juce::File(p).existsAsFile())
            loadIrFile(p);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Editor
// ─────────────────────────────────────────────────────────────────────────────

juce::AudioProcessorEditor* GuitarAmpProcessor::createEditor()
{
    return new GuitarAmpEditor(*this);
}

// ─────────────────────────────────────────────────────────────────────────────
// Plugin entry point
// ─────────────────────────────────────────────────────────────────────────────

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GuitarAmpProcessor();
}
