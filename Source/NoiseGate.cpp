#include "NoiseGate.h"

void NoiseGate::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;

    // Coefficient for a 1-pole IIR smoother: c = exp(-2π / (ms * sr / 1000))
    auto coeff = [&](float ms) -> float {
        return std::exp(-juce::MathConstants<float>::twoPi /
                        (ms * static_cast<float>(sampleRate) / 1000.0f));
    };

    attackCoeff_  = coeff(kAttackMs);
    releaseCoeff_ = coeff(kReleaseMs);
    gainState_    = 0.0f;
}

void NoiseGate::reset()
{
    gainState_ = 0.0f;
}

void NoiseGate::process(float* data, int numSamples, float thresholdDb)
{
    // If threshold is at or below the bypass floor, pass audio unchanged.
    if (thresholdDb <= kBypassThresholdDb)
    {
        gainState_ = 1.0f;
        return;
    }

    const float thresholdLinear = juce::Decibels::decibelsToGain(thresholdDb);

    for (int i = 0; i < numSamples; ++i)
    {
        const float absLevel = std::abs(data[i]);
        // Desired gain: 1 if above threshold, 0 if below.
        const float desiredGain = (absLevel >= thresholdLinear) ? 1.0f : 0.0f;

        // Smooth with different coefficients for attack vs. release.
        const float coeff = (desiredGain > gainState_) ? attackCoeff_ : releaseCoeff_;
        gainState_ = desiredGain + coeff * (gainState_ - desiredGain);

        data[i] *= gainState_;
    }
}
