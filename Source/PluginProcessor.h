#pragma once

#include <JuceHeader.h>
#include "NamEngine.h"
#include "NoiseGate.h"

// ─────────────────────────────────────────────────────────────────────────────
// Parameter IDs
// ─────────────────────────────────────────────────────────────────────────────
namespace ParamID
{
    inline constexpr const char* InputGain    = "inputGain";
    inline constexpr const char* GateThresh   = "gateThresh";
    inline constexpr const char* Bass         = "bass";
    inline constexpr const char* Mid          = "mid";
    inline constexpr const char* Treble       = "treble";
    inline constexpr const char* OutputLevel  = "outputLevel";
    inline constexpr const char* NamBypass    = "namBypass";
    inline constexpr const char* IrBypass     = "irBypass";
}

// ─────────────────────────────────────────────────────────────────────────────
class GuitarAmpProcessor  : public juce::AudioProcessor
{
public:
    GuitarAmpProcessor();
    ~GuitarAmpProcessor() override;

    // ── AudioProcessor overrides ─────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // ── File loading ─────────────────────────────────────────────────────────

    /** Load a .nam model file (async). Callback on message thread. */
    void loadNamModel(const juce::String& path,
                      std::function<void(NamEngine::LoadState, juce::String)> cb);

    /** Load an IR .wav file (async via juce::dsp::Convolution). */
    void loadIrFile(const juce::String& path);

    void clearNamModel() { namEngine_.clearModel(); }
    void clearIrFile();

    // ── Accessors for the editor ─────────────────────────────────────────────

    juce::AudioProcessorValueTreeState& getApvts() { return apvts_; }
    NamEngine& getNamEngine()                       { return namEngine_; }

    juce::String getNamFilePath() const { return namFilePath_; }
    juce::String getIrFilePath()  const { return irFilePath_;  }

    /** True if an IR file has been loaded into the convolution engine. */
    bool isIrLoaded() const noexcept { return irLoaded_; }

private:
    // ── APVTS ────────────────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState apvts_;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── DSP chain objects ────────────────────────────────────────────────────
    NamEngine namEngine_;
    NoiseGate noiseGate_;   // mono gate, one instance

    // EQ filters (stereo – one per channel)
    using IIRFilter = juce::dsp::IIR::Filter<float>;
    using IIRCoeffs = juce::dsp::IIR::Coefficients<float>;

    std::array<IIRFilter, 2> bassFilter_;
    std::array<IIRFilter, 2> midFilter_;
    std::array<IIRFilter, 2> trebleFilter_;

    // Convolution IR
    juce::dsp::Convolution convolution_;
    bool irLoaded_ = false;

    // Mono work buffer
    juce::AudioBuffer<float> monoBuffer_;

    // ── State / paths ────────────────────────────────────────────────────────
    juce::String namFilePath_;
    juce::String irFilePath_;

    // ── Helpers ──────────────────────────────────────────────────────────────
    void updateEqCoefficients(double sampleRate);
    void processEq(juce::AudioBuffer<float>& buffer);

    double currentSampleRate_ = 44100.0;
    int    currentBlockSize_  = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarAmpProcessor)
};
