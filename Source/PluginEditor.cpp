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

    // ── Button attachments ───────────────────────────────────────────────────
    namBypassAtt_ = std::make_unique<ButtonAttachment>(apvts, ParamID::NamBypass, namBypassBtn_);
    irBypassAtt_  = std::make_unique<ButtonAttachment>(apvts, ParamID::IrBypass,  irBypassBtn_);

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

    // Bypass buttons: set toggle-aware colours once and let JUCE handle rendering.
    // toggle-OFF (not bypassed) → grey  |  toggle-ON (bypassed) → amber
    for (auto* btn : { &namBypassBtn_, &irBypassBtn_ })
    {
        btn->setColour(juce::TextButton::buttonColourId,   juce::Colour(0xFF333333));
        btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(kAccent));
        btn->setColour(juce::TextButton::textColourOffId,  juce::Colour(kTextPrimary));
        btn->setColour(juce::TextButton::textColourOnId,   juce::Colours::black);
    }

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
            const auto irPath = processor_.getIrFilePath();
            if (irPath.isNotEmpty())
                irFileLabel_.setText(juce::File(irPath).getFileNameWithoutExtension(),
                                     juce::dontSendNotification);
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
}

void GuitarAmpEditor::updateStatusBar()
{
    const auto& eng = processor_.getNamEngine();
    juce::String namStatus;

    switch (eng.getLoadState())
    {
        case NamEngine::LoadState::Idle:    namStatus = "No model"; break;
        case NamEngine::LoadState::Loading: namStatus = "Loading model..."; break;
        case NamEngine::LoadState::Loaded:  namStatus = "Model: " + eng.getModelName() + " \u2713"; break;
        case NamEngine::LoadState::Error:   namStatus = "Model: ERROR"; break;
    }

    juce::String irStatus;
    if (processor_.isIrLoaded())
    {
        const auto irPath = processor_.getIrFilePath();
        const auto irName = irPath.isNotEmpty()
            ? juce::File(irPath).getFileNameWithoutExtension()
            : juce::String("loaded");
        irStatus = "IR: " + irName + " \u2713";
    }
    else
    {
        irStatus = "IR: none";
    }

    statusLabel_.setText(namStatus + "  |  " + irStatus,
                         juce::dontSendNotification);

    // Colour the status text based on state
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
