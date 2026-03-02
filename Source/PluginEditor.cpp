#include "PluginEditor.h"

// ═════════════════════════════════════════════════════════════════════════════
// LabelledKnob
// ═════════════════════════════════════════════════════════════════════════════

LabelledKnob::LabelledKnob(const juce::String& labelText)
{
    slider_.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
    addAndMakeVisible(slider_);

    label_.setText(labelText, juce::dontSendNotification);
    label_.setJustificationType(juce::Justification::centred);
    label_.setFont(juce::Font(11.0f, juce::Font::bold));
    label_.setColour(juce::Label::textColourId, juce::Colour(0xFFAAAAAA));
    addAndMakeVisible(label_);
}

void LabelledKnob::resized()
{
    auto area = getLocalBounds();
    const int labelH = 16;
    label_ .setBounds(area.removeFromBottom(labelH));
    slider_.setBounds(area);
}

// ═════════════════════════════════════════════════════════════════════════════
// GuitarAmpEditor
// ═════════════════════════════════════════════════════════════════════════════

GuitarAmpEditor::GuitarAmpEditor(GuitarAmpProcessor& p)
    : AudioProcessorEditor(&p), processor_(p)
{
    setSize(700, 400);
    setResizable(true, false);
    setResizeLimits(560, 320, 1050, 600);

    auto& apvts = processor_.getApvts();

    // ── Knob attachments ─────────────────────────────────────────────────────
    inputAtt_  = std::make_unique<SliderAttachment>(apvts, ParamID::InputGain,   inputKnob_ .getSlider());
    gateAtt_   = std::make_unique<SliderAttachment>(apvts, ParamID::GateThresh,  gateKnob_  .getSlider());
    bassAtt_   = std::make_unique<SliderAttachment>(apvts, ParamID::Bass,        bassKnob_  .getSlider());
    midAtt_    = std::make_unique<SliderAttachment>(apvts, ParamID::Mid,         midKnob_   .getSlider());
    trebleAtt_ = std::make_unique<SliderAttachment>(apvts, ParamID::Treble,      trebleKnob_.getSlider());
    outputAtt_ = std::make_unique<SliderAttachment>(apvts, ParamID::OutputLevel, outputKnob_.getSlider());

    // ── Bypass buttons – fully manual (no ButtonAttachment).
    //    onClick toggles the APVTS parameter and refreshes the visual immediately.
    //    timerCallback() syncs the visual if the parameter is changed externally
    //    (DAW automation).
    {
        auto setupBypass = [&](juce::TextButton& btn, const char* paramId)
        {
            const bool curVal =
                apvts.getRawParameterValue(paramId)->load() > 0.5f;
            refreshBypassVisual(btn, curVal);

            btn.onClick = [this, &btn, paramId]()
            {
                auto* param = processor_.getApvts().getParameter(paramId);
                const bool newVal = !(param->getValue() > 0.5f);
                param->setValueNotifyingHost(newVal ? 1.0f : 0.0f);
                refreshBypassVisual(btn, newVal);
            };
        };

        setupBypass(namBypassBtn_, ParamID::NamBypass);
        setupBypass(irBypassBtn_,  ParamID::IrBypass);
    }

    // ── NAM row setup ─────────────────────────────────────────────────────────
    namFileLabel_.setText("No model loaded", juce::dontSendNotification);
    namFileLabel_.setFont(juce::Font(13.0f));
    namFileLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextPrimary));
    namFileLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xFF2D2D2D));
    namFileLabel_.setJustificationType(juce::Justification::centredLeft);

    namBrowseBtn_.onClick = [this]()
    {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Select NAM model file",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*.nam");

        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                {
                    namFileLabel_.setText("Loading...", juce::dontSendNotification);
                    processor_.loadNamModel(
                        result.getFullPathName(),
                        [this](NamEngine::LoadState state, juce::String msg)
                        {
                            const bool ok = (state == NamEngine::LoadState::Loaded);
                            namFileLabel_.setText(
                                ok ? processor_.getNamEngine().getModelName()
                                   : "Error: " + msg,
                                juce::dontSendNotification);
                            updateStatusBar();
                        });
                }
            });
    };

    // ── IR row setup ──────────────────────────────────────────────────────────
    irFileLabel_.setText("No IR loaded", juce::dontSendNotification);
    irFileLabel_.setFont(juce::Font(13.0f));
    irFileLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextPrimary));
    irFileLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xFF2D2D2D));
    irFileLabel_.setJustificationType(juce::Justification::centredLeft);

    irBrowseBtn_.onClick = [this]()
    {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Select Impulse Response file",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*.wav;*.ir;*.aif;*.aiff");

        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                {
                    processor_.loadIrFile(result.getFullPathName());
                    irFileLabel_.setText(result.getFileNameWithoutExtension(),
                                         juce::dontSendNotification);
                    updateStatusBar();
                }
            });
    };

    // ── Status bar ────────────────────────────────────────────────────────────
    statusLabel_.setFont(juce::Font(12.0f));
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextDim));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);

    // ── Style all controls ────────────────────────────────────────────────────
    styleKnob(inputKnob_ .getSlider());
    styleKnob(gateKnob_  .getSlider());
    styleKnob(bassKnob_  .getSlider());
    styleKnob(midKnob_   .getSlider());
    styleKnob(trebleKnob_.getSlider());
    styleKnob(outputKnob_.getSlider());

    styleButton(namBrowseBtn_);
    styleButton(irBrowseBtn_);
    // Bypass buttons are already coloured by setupBypass() above.

    // ── Add children ──────────────────────────────────────────────────────────
    addAndMakeVisible(namFileLabel_);
    addAndMakeVisible(namBrowseBtn_);
    addAndMakeVisible(namBypassBtn_);
    addAndMakeVisible(irFileLabel_);
    addAndMakeVisible(irBrowseBtn_);
    addAndMakeVisible(irBypassBtn_);
    addAndMakeVisible(inputKnob_);
    addAndMakeVisible(gateKnob_);
    addAndMakeVisible(bassKnob_);
    addAndMakeVisible(midKnob_);
    addAndMakeVisible(trebleKnob_);
    addAndMakeVisible(outputKnob_);
    addAndMakeVisible(statusLabel_);

    // ── Initialise labels from existing processor state (e.g. after reopening) ─
    {
        const auto& eng = processor_.getNamEngine();
        if (eng.getLoadState() == NamEngine::LoadState::Loaded ||
            eng.getLoadState() == NamEngine::LoadState::Loading)
        {
            if (eng.getModelName().isNotEmpty())
                namFileLabel_.setText(eng.getModelName(), juce::dontSendNotification);
        }

        if (processor_.isIrLoaded())
        {
            const auto irName = processor_.getIrFileName();
            if (irName.isNotEmpty())
                irFileLabel_.setText(irName, juce::dontSendNotification);
        }
    }

    updateStatusBar();
    startTimerHz(10);  // Poll load state at 10 Hz
}

GuitarAmpEditor::~GuitarAmpEditor()
{
    stopTimer();
}

// ─── Paint ────────────────────────────────────────────────────────────────────

void GuitarAmpEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBackground));

    const auto w = getWidth();
    const auto h = getHeight();

    // Top panel background
    g.setColour(juce::Colour(kPanel));
    g.fillRect(0, 0, w, 110);

    // Divider line
    g.setColour(juce::Colour(kAccent));
    g.fillRect(0, 109, w, 2);

    // Plugin title – drawn in its own strip at the very top, clear of the buttons
    g.setColour(juce::Colour(kAccent));
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    g.drawText("GUITAR AMP", getLocalBounds().removeFromTop(20).reduced(8, 0),
               juce::Justification::centredRight);

    // Status bar background
    g.setColour(juce::Colour(0xFF181818));
    g.fillRect(0, h - 28, w, 28);
}

// ─── Resized ──────────────────────────────────────────────────────────────────

void GuitarAmpEditor::resized()
{
    const int w        = getWidth();
    const int h        = getHeight();
    const int margin   = 8;
    const int btnW     = 64;
    const int rowH     = 34;
    const int statusH  = 28;

    // ── Top panel: NAM row (y=24, below title strip) ─────────────────────────
    {
        int y = 24;
        namBypassBtn_.setBounds(w - margin - btnW,       y, btnW, rowH);
        namBrowseBtn_.setBounds(w - margin - btnW*2 - 4, y, btnW, rowH);
        namFileLabel_.setBounds(margin, y, w - margin*2 - btnW*2 - 12, rowH);
    }

    // ── Top panel: IR row (y=64) ──────────────────────────────────────────────
    {
        int y = 64;
        irBypassBtn_.setBounds(w - margin - btnW,       y, btnW, rowH);
        irBrowseBtn_.setBounds(w - margin - btnW*2 - 4, y, btnW, rowH);
        irFileLabel_.setBounds(margin, y, w - margin*2 - btnW*2 - 12, rowH);
    }

    // ── Knob row (below divider) ──────────────────────────────────────────────
    const int knobAreaTop  = 116;
    const int knobAreaH    = h - knobAreaTop - statusH - margin;
    const int knobW        = (w - margin * 2) / 6;

    std::array<LabelledKnob*, 6> knobs {
        &inputKnob_, &gateKnob_, &bassKnob_, &midKnob_, &trebleKnob_, &outputKnob_
    };

    for (int i = 0; i < 6; ++i)
        knobs[i]->setBounds(margin + i * knobW, knobAreaTop, knobW, knobAreaH);

    // ── Status bar ────────────────────────────────────────────────────────────
    statusLabel_.setBounds(margin, h - statusH + 4, w - margin * 2, statusH - 4);
}

// ─── Timer ────────────────────────────────────────────────────────────────────

void GuitarAmpEditor::timerCallback()
{
    updateStatusBar();
    syncBypassButtons();
}

void GuitarAmpEditor::refreshBypassVisual(juce::TextButton& btn, bool bypassed)
{
    // Set both on- and off-colour IDs to the same value so the result is
    // independent of the button's internal toggle state.
    const auto bg   = bypassed ? juce::Colour(kAccent)       : juce::Colour(0xFF3A3A3A);
    const auto text = bypassed ? juce::Colours::black         : juce::Colour(kTextDim);

    btn.setColour(juce::TextButton::buttonColourId,   bg);
    btn.setColour(juce::TextButton::buttonOnColourId, bg);
    btn.setColour(juce::TextButton::textColourOffId,  text);
    btn.setColour(juce::TextButton::textColourOnId,   text);
    btn.repaint();
}

void GuitarAmpEditor::syncBypassButtons()
{
    // Keeps visuals in sync when the parameter is changed externally (DAW automation).
    auto& apvts = processor_.getApvts();
    const bool namBypassed = apvts.getRawParameterValue(ParamID::NamBypass)->load() > 0.5f;
    const bool irBypassed  = apvts.getRawParameterValue(ParamID::IrBypass) ->load() > 0.5f;

    // Only repaint if state has changed (avoid redundant work every 100 ms).
    const bool namBtn = namBypassBtn_.findColour(juce::TextButton::buttonColourId)
                            == juce::Colour(kAccent);
    const bool irBtn  = irBypassBtn_.findColour(juce::TextButton::buttonColourId)
                            == juce::Colour(kAccent);

    if (namBtn != namBypassed) refreshBypassVisual(namBypassBtn_, namBypassed);
    if (irBtn  != irBypassed)  refreshBypassVisual(irBypassBtn_,  irBypassed);
}

void GuitarAmpEditor::updateStatusBar()
{
    // Build the checkmark via charToString so its Unicode encoding is
    // guaranteed regardless of platform string literal handling.
    static const juce::String kCheck = " " + juce::String::charToString(0x2713);

    const auto& eng = processor_.getNamEngine();
    juce::String namStatus;

    switch (eng.getLoadState())
    {
        case NamEngine::LoadState::Idle:    namStatus = "No model"; break;
        case NamEngine::LoadState::Loading: namStatus = "Loading model..."; break;
        case NamEngine::LoadState::Loaded:  namStatus = "Model: " + eng.getModelName() + kCheck; break;
        case NamEngine::LoadState::Error:   namStatus = "Model: ERROR"; break;
    }

    juce::String irStatus;
    if (processor_.isIrLoaded())
    {
        // Use the filename stored directly by the processor to avoid any
        // re-parse / encoding ambiguity when building the status string.
        const auto irName = processor_.getIrFileName();
        irStatus = "IR: " + (irName.isNotEmpty() ? irName : juce::String("loaded")) + kCheck;
    }
    else
    {
        irStatus = "IR: none";
    }

    statusLabel_.setText(namStatus + "  |  " + irStatus,
                         juce::dontSendNotification);

    const bool hasError = (eng.getLoadState() == NamEngine::LoadState::Error);
    statusLabel_.setColour(juce::Label::textColourId,
                           hasError ? juce::Colour(kRed) : juce::Colour(kTextDim));
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

void GuitarAmpEditor::styleKnob(juce::Slider& s)
{
    s.setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(kAccent));
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF3A3A3A));
    s.setColour(juce::Slider::thumbColourId,               juce::Colour(0xFFE8E8E8));
    s.setColour(juce::Slider::textBoxTextColourId,         juce::Colour(kTextDim));
    s.setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colour(0xFF1E1E1E));
    s.setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
}

void GuitarAmpEditor::styleButton(juce::TextButton& b, bool isActive)
{
    const auto bgColour    = isActive ? juce::Colour(kAccent) : juce::Colour(0xFF333333);
    const auto textColour  = isActive ? juce::Colours::black  : juce::Colour(kTextPrimary);

    b.setColour(juce::TextButton::buttonColourId,    bgColour);
    b.setColour(juce::TextButton::buttonOnColourId,  juce::Colour(kAccent));
    b.setColour(juce::TextButton::textColourOffId,   textColour);
    b.setColour(juce::TextButton::textColourOnId,    juce::Colours::black);
    b.repaint();
}
