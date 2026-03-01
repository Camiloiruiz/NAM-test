#pragma once
#include <JuceHeader.h>

/**
 * Simple downward noise gate placed before the NAM model.
 *
 * Attack  ~1 ms   – fast, to avoid clicks when signal crosses threshold.
 * Release ~50 ms  – medium, to avoid pumping.
 *
 * The gate outputs a gain coefficient [0,1] applied to every sample.
 * All state is per-instance; create one NoiseGate per channel or use a
 * summed mono level for a stereo pair.
 */
class NoiseGate
{
public:
    NoiseGate() = default;

    /** Call from prepareToPlay(). */
    void prepare(double sampleRate);

    /**
     * Process a block of mono float samples in-place.
     * @param data        Pointer to float samples (read/write).
     * @param numSamples  Number of samples in the block.
     * @param thresholdDb Gate open threshold in dB (e.g. -60.0f).
     *                    Pass a very negative value (≤ -80) to bypass.
     */
    void process(float* data, int numSamples, float thresholdDb);

    /** Reset internal state (e.g. after a transport stop). */
    void reset();

private:
    double sampleRate_  = 44100.0;
    float  gainState_   = 0.0f;   // current gate gain [0,1]

    float attackCoeff_  = 0.0f;
    float releaseCoeff_ = 0.0f;

    static constexpr float kAttackMs  = 1.0f;
    static constexpr float kReleaseMs = 50.0f;

    // Threshold below which we consider the gate effectively disabled
    static constexpr float kBypassThresholdDb = -79.0f;
};
