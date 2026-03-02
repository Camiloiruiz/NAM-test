#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// ─────────────────────────────────────────────────────────────────────────────
// LabelledKnob  – rotary slider with a label below it
// ─────────────────────────────────────────────────────────────────────────────
class LabelledKnob : public juce::Component
{
public:
    explicit LabelledKnob(const juce::String& labelText);

    juce::Slider& getSlider() { return slider_; }

    void resized() override;
    void paint(juce::Graphics&) override {}

private:
    juce::Slider slider_;
    juce::Label  label_;
};

// ─────────────────────────────────────────────────────────────────────────────
// GuitarAmpEditor
// ─────────────────────────────────────────────────────────────────────────────
class GuitarAmpEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit GuitarAmpEditor(GuitarAmpProcessor&);
    ~GuitarAmpEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // ── Colours ──────────────────────────────────────────────────────────────
    static constexpr uint32_t kBackground  = 0xFF1A1A1A;
    static constexpr uint32_t kPanel       = 0xFF252525;
    static constexpr uint32_t kAccent      = 0xFFD4620A;  // amber
    static constexpr uint32_t kTextPrimary = 0xFFE8E8E8;
    static constexpr uint32_t kTextDim     = 0xFF888888;
    static constexpr uint32_t kGreen       = 0xFF44BB44;
    static constexpr uint32_t kRed         = 0xFFBB4444;

    GuitarAmpProcessor& processor_;

    // ── NAM row ───────────────────────────────────────────────────────────────
    juce::Label       namFileLabel_;
    juce::TextButton  namBrowseBtn_  { "Browse" };
    juce::TextButton  namBypassBtn_  { "Bypass" };

    // ── IR row ────────────────────────────────────────────────────────────────
    juce::Label       irFileLabel_;
    juce::TextButton  irBrowseBtn_   { "Browse" };
    juce::TextButton  irBypassBtn_   { "Bypass" };

    // ── Knobs ─────────────────────────────────────────────────────────────────
    LabelledKnob inputKnob_  { "INPUT"  };
    LabelledKnob gateKnob_   { "GATE"   };
    LabelledKnob bassKnob_   { "BASS"   };
    LabelledKnob midKnob_    { "MID"    };
    LabelledKnob trebleKnob_ { "TREBLE" };
    LabelledKnob outputKnob_ { "OUTPUT" };

    // ── Status bar ────────────────────────────────────────────────────────────
    juce::Label statusLabel_;

    // ── APVTS attachments ────────────────────────────────────────────────────
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> inputAtt_, gateAtt_, bassAtt_,
                                       midAtt_, trebleAtt_, outputAtt_;
    // NOTE: bypass buttons are managed manually (onClick + timer sync)
    //       so that visual feedback is immediate and reliable on Windows.

    // ── File chooser ─────────────────────────────────────────────────────────
    std::unique_ptr<juce::FileChooser> fileChooser_;

    // ── Timer → poll load state ──────────────────────────────────────────────
    void timerCallback() override;
    void updateStatusBar();
    void refreshBypassVisual(juce::TextButton& btn, bool bypassed);
    void syncBypassButtons();   // called from timer to handle DAW automation

    void styleKnob(juce::Slider&);
    void styleButton(juce::TextButton&, bool isActive = false);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarAmpEditor)
};
