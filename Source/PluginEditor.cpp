#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    void styleLabel (juce::Label& l, const juce::String& text, float size, bool bold,
                     juce::Colour col = juce::Colour (0xffe8e8e8))
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)));
        l.setColour (juce::Label::textColourId, col);
    }
}

VocalCleanerAudioProcessorEditor::VocalCleanerAudioProcessorEditor (VocalCleanerAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    const auto accent = juce::Colour (0xff60cdff);
    const auto grey    = juce::Colour (0xff9aa0a6);

    // ---- real-time denoise section ----
    auto setupKnob = [this, accent] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 20);
        s.setColour (juce::Slider::rotarySliderFillColourId, accent);
        s.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a3a3a));
        s.setColour (juce::Slider::thumbColourId, juce::Colours::white);
        s.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0x22ffffff));
        addAndMakeVisible (s);
    };
    setupKnob (mixS);
    setupKnob (gainS);
    mixA  = std::make_unique<SliderAttach> (proc.apvts, "mix",  mixS);
    gainA = std::make_unique<SliderAttach> (proc.apvts, "gain", gainS);

    styleLabel (title,    "nvh suno polish", 22.0f, true);
    styleLabel (subtitle, "Real-time denoise + Studio Process: enhance / regenerate / Suno polish", 12.0f, false, grey);
    styleLabel (mixL,  "DENOISE AMOUNT", 11.0f, true, grey);
    styleLabel (gainL, "OUTPUT GAIN",    11.0f, true, grey);
    for (auto* l : { &title, &subtitle, &mixL, &gainL })
        addAndMakeVisible (l);

    // ---- Studio Process section ----
    styleLabel (studioTitle, "Studio Process", 16.0f, true);
    styleLabel (studioSub,   "Offline: clean up or re-voice a whole take via your local ComfyUI.", 11.5f, false, grey);
    styleLabel (serverL,     "ComfyUI server", 11.0f, true, grey);
    styleLabel (modeL,       "MODE", 11.0f, true, grey);
    styleLabel (myVoiceL,    "Clone reference", 11.0f, true, grey);
    for (auto* l : { &studioTitle, &studioSub, &serverL, &modeL, &myVoiceL })
        addAndMakeVisible (l);

    serverUrl.setMultiLine (false);
    serverUrl.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2b2b2b));
    serverUrl.setColour (juce::TextEditor::outlineColourId, juce::Colour (0x33ffffff));
    serverUrl.setColour (juce::TextEditor::textColourId, juce::Colours::white);
    serverUrl.setText (proc.lastStudio.serverUrl.isNotEmpty() ? proc.lastStudio.serverUrl
                                                              : juce::String ("http://127.0.0.1:8188"), false);
    addAndMakeVisible (serverUrl);

    addAndMakeVisible (loadBtn);
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Select a vocal take",
                     juce::File{}, "*.wav;*.flac;*.mp3;*.m4a;*.ogg;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f.existsAsFile())
                {
                    chosenTakePath = f.getFullPathName();
                    takeLabel.setText (f.getFileName(), juce::dontSendNotification);
                }
            });
    };
    styleLabel (takeLabel, "(no take loaded)", 12.0f, false, grey);
    addAndMakeVisible (takeLabel);

    enhanceBtn.setRadioGroupId (1001);
    regenBtn.setRadioGroupId (1001);
    sunoBtn.setRadioGroupId (1001);
    for (auto* t : { &enhanceBtn, &regenBtn, &sunoBtn })
    {
        addAndMakeVisible (t);
        t->setColour (juce::ToggleButton::textColourId, juce::Colour (0xffe8e8e8));
    }
    enhanceBtn.setToggleState (proc.lastStudio.mode == 0, juce::dontSendNotification);
    regenBtn.setToggleState   (proc.lastStudio.mode == 1, juce::dontSendNotification);
    sunoBtn.setToggleState    (proc.lastStudio.mode == 2, juce::dontSendNotification);
    enhanceBtn.setTooltip ("DeepFilterNet restoration - keeps your performance, removes noise/hiss.");
    regenBtn.setTooltip ("Transcribe the take and re-speak it in your stored cloned voice.");
    sunoBtn.setTooltip ("Suno-style produced vocal: clean -> autotune -> EQ/comp/saturation/width/reverb -> loud master.");

    auto fillCombo = [] (juce::ComboBox& c, juce::StringArray items, const juce::String& sel)
    {
        for (int i = 0; i < items.size(); ++i) c.addItem (items[i], i + 1);
        c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2b2b2b));
        c.setText (sel, juce::dontSendNotification);
    };
    fillCombo (modelBox,    { "0.6B", "1.7B" }, proc.lastStudio.model);
    fillCombo (languageBox, { "Auto", "English", "Tagalog", "Chinese", "Japanese", "Korean", "French",
                              "German", "Spanish", "Portuguese", "Russian", "Italian" }, proc.lastStudio.language);
    fillCombo (keyBox,   { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, proc.lastStudio.key);
    fillCombo (scaleBox, { "major", "minor", "chromatic" }, proc.lastStudio.scale);
    for (auto* c : { &modelBox, &languageBox, &keyBox, &scaleBox })
        addAndMakeVisible (c);

    styleLabel (tuneL, "Suno Polish - key / scale / autotune", 11.0f, true, grey);
    addAndMakeVisible (tuneL);
    addAndMakeVisible (autotuneBtn);
    autotuneBtn.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffe8e8e8));
    autotuneBtn.setToggleState (proc.lastStudio.autotune, juce::dontSendNotification);
    autotuneBtn.setTooltip ("Snap pitch toward the chosen key/scale. Turn off to keep the take's natural pitch.");

    setupKnob (reverbS);
    reverbS.setRange (0.0, 100.0, 1.0);
    reverbS.setValue (proc.lastStudio.reverb * 100.0, juce::dontSendNotification);
    reverbS.setTextValueSuffix (" %");
    reverbS.setTooltip ("Reverb amount for Suno Polish (0 = dry, 100 = lush space).");
    styleLabel (reverbL, "REVERB", 11.0f, true, grey);
    addAndMakeVisible (reverbL);

    addAndMakeVisible (setVoiceBtn);
    setVoiceBtn.setTooltip ("Pick a clean clip of your own voice (5-15s) to use for Regenerate mode.");
    setVoiceBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Select a clean clip of your voice (5-15s)",
                     juce::File{}, "*.wav;*.flac;*.mp3;*.m4a;*.ogg;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f.existsAsFile())
                {
                    auto url = serverUrl.getText().trim();
                    if (url.isEmpty()) url = "http://127.0.0.1:8188";
                    proc.setMyVoice (url, f);
                }
            });
    };
    styleLabel (myVoiceName, "(none set)", 12.0f, false, grey);
    addAndMakeVisible (myVoiceName);

    addAndMakeVisible (processBtn);
    processBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0067c0));
    processBtn.onClick = [this] { proc.startStudioProcess (gatherStudio()); };

    addAndMakeVisible (previewBtn);
    previewBtn.onClick = [this] { proc.triggerPreview(); };
    addAndMakeVisible (stopBtn);
    stopBtn.onClick = [this] { proc.stopPlayback(); };

    addAndMakeVisible (saveBtn);
    saveBtn.onClick = [this]
    {
        if (! proc.hasClip()) return;
        chooser = std::make_unique<juce::FileChooser> ("Save processed take as",
                     juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("nvh_studio.wav"),
                     "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f != juce::File{})
                {
                    if (! f.hasFileExtension ("wav")) f = f.withFileExtension ("wav");
                    const bool ok = proc.saveClipTo (f);
                    statusLabel.setText (ok ? "Saved: " + f.getFullPathName() : "Save failed.",
                                         juce::dontSendNotification);
                }
            });
    };

    styleLabel (statusLabel, proc.getStatusMessage(), 12.0f, false);
    statusLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (statusLabel);

    // restore + sync
    chosenTakePath = proc.lastStudio.takePath;
    if (chosenTakePath.isNotEmpty())
        takeLabel.setText (juce::File (chosenTakePath).getFileName(), juce::dontSendNotification);
    {
        auto url = serverUrl.getText().trim();
        proc.queryMyVoice (url.isEmpty() ? juce::String ("http://127.0.0.1:8188") : url);
    }

    setSize (520, 792);
    startTimerHz (20);
}

VocalCleanerAudioProcessorEditor::~VocalCleanerAudioProcessorEditor() { stopTimer(); }

StudioParams VocalCleanerAudioProcessorEditor::gatherStudio() const
{
    StudioParams p;
    p.serverUrl = serverUrl.getText().trim();
    if (p.serverUrl.isEmpty()) p.serverUrl = "http://127.0.0.1:8188";
    p.takePath  = chosenTakePath;
    p.mode      = sunoBtn.getToggleState() ? 2 : (regenBtn.getToggleState() ? 1 : 0);
    p.model     = modelBox.getText();
    p.language  = languageBox.getText();
    p.key       = keyBox.getText();
    p.scale     = scaleBox.getText();
    p.autotune  = autotuneBtn.getToggleState();
    p.cleanFirst = true; // Suno polish always cleans first
    p.reverb    = (float) (reverbS.getValue() / 100.0);
    return p;
}

void VocalCleanerAudioProcessorEditor::timerCallback()
{
    auto smooth = [] (float cur, float target)
    {
        return target > cur ? target : cur * 0.82f + target * 0.18f; // fast attack, slow release
    };
    inMeter  = smooth (inMeter,  proc.inLevel.load());
    outMeter = smooth (outMeter, proc.outLevel.load());

    const int sv = proc.getStatusVersion();
    if (sv != lastStatusVersion)
    {
        lastStatusVersion = sv;
        statusLabel.setText (proc.getStatusMessage(), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId,
                               proc.getStatusIsError() ? juce::Colour (0xffff7043) : juce::Colour (0xffb9f6ca));
    }

    const int mv = proc.getMyVoiceVersion();
    if (mv != lastMyVoiceVersion)
    {
        lastMyVoiceVersion = mv;
        auto n = proc.getMyVoiceName();
        myVoiceName.setText (n.isEmpty() ? "(none set)" : n, juce::dontSendNotification);
    }

    const bool busy = proc.isBusy();
    processBtn.setEnabled (! busy);
    setVoiceBtn.setEnabled (! busy);
    saveBtn.setEnabled (proc.hasClip());
    previewBtn.setEnabled (proc.hasClip());

    repaint();
}

void VocalCleanerAudioProcessorEditor::drawMeter (juce::Graphics& g, juce::Rectangle<int> r, float level, const juce::String& label)
{
    g.setColour (juce::Colour (0xff141414));
    g.fillRoundedRectangle (r.toFloat(), 4.0f);

    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    auto fill = r.toFloat().reduced (3.0f);
    const float h = fill.getHeight() * norm;
    auto bar = fill.withTop (fill.getBottom() - h);

    juce::ColourGradient grad (juce::Colour (0xff34c759), bar.getBottomLeft(),
                               juce::Colour (0xffff453a), bar.getTopLeft(), false);
    grad.addColour (0.7, juce::Colour (0xffffd60a));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bar, 3.0f);

    g.setColour (juce::Colour (0xff9aa0a6));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (label, r.withY (r.getBottom() + 2).withHeight (14), juce::Justification::centred);
}

void VocalCleanerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
    g.setColour (juce::Colour (0xff262626));
    g.fillRect (getLocalBounds().removeFromTop (66));

    // meters in the denoise section
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (52);              // header
    auto denoise = area.removeFromTop (180);
    auto meters = denoise.removeFromRight (96);
    meters.removeFromBottom (16);
    auto inR  = meters.removeFromLeft (40).reduced (4, 0);
    meters.removeFromLeft (8);
    auto outR = meters.removeFromLeft (40).reduced (4, 0);
    drawMeter (g, inR,  inMeter,  "IN");
    drawMeter (g, outR, outMeter, "OUT");

    // divider above the Studio Process card
    auto full = getLocalBounds().reduced (16);
    const int dividerY = 16 + 52 + 180 + 6;
    g.setColour (juce::Colour (0x22ffffff));
    g.drawHorizontalLine (dividerY, (float) full.getX(), (float) full.getRight());

    // Studio Process card background
    juce::Rectangle<int> card (full.getX() - 6, dividerY + 8, full.getWidth() + 12, getHeight() - dividerY - 20);
    g.setColour (juce::Colour (0xff242424));
    g.fillRoundedRectangle (card.toFloat(), 8.0f);
}

void VocalCleanerAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);

    auto header = r.removeFromTop (48);
    title.setBounds (header.removeFromTop (28));
    subtitle.setBounds (header);
    r.removeFromTop (4);

    // denoise knobs (meters drawn in paint occupy the right 96px)
    auto denoise = r.removeFromTop (180);
    denoise.removeFromRight (96);
    auto half = denoise.getWidth() / 2;
    auto left  = denoise.removeFromLeft (half);
    auto right = denoise;
    mixL.setBounds (left.removeFromTop (16));
    mixS.setBounds (left.reduced (6));
    gainL.setBounds (right.removeFromTop (16));
    gainS.setBounds (right.reduced (6));

    r.removeFromTop (22); // divider gap

    // ---- Studio Process card contents ----
    auto s = r.reduced (4, 2);
    studioTitle.setBounds (s.removeFromTop (22));
    studioSub.setBounds (s.removeFromTop (18));
    s.removeFromTop (6);

    serverL.setBounds (s.removeFromTop (14));
    serverUrl.setBounds (s.removeFromTop (24));
    s.removeFromTop (8);

    auto takeRow = s.removeFromTop (28);
    loadBtn.setBounds (takeRow.removeFromLeft (120));
    takeRow.removeFromLeft (8);
    takeLabel.setBounds (takeRow);
    s.removeFromTop (8);

    modeL.setBounds (s.removeFromTop (14));
    auto modeRow = s.removeFromTop (24);
    enhanceBtn.setBounds (modeRow.removeFromLeft (110));
    regenBtn.setBounds (modeRow.removeFromLeft (130));
    sunoBtn.setBounds (modeRow.removeFromLeft (140));
    s.removeFromTop (8);

    // Regenerate: model + language
    auto comboRow = s.removeFromTop (26);
    auto cHalf = comboRow.getWidth() / 2;
    modelBox.setBounds (comboRow.removeFromLeft (cHalf).reduced (0, 1).withTrimmedRight (6));
    languageBox.setBounds (comboRow.reduced (0, 1));
    s.removeFromTop (8);

    // Suno Polish: key + scale + autotune, then the reverb knob
    tuneL.setBounds (s.removeFromTop (14));
    auto tuneRow = s.removeFromTop (26);
    keyBox.setBounds (tuneRow.removeFromLeft (80).reduced (0, 1));
    tuneRow.removeFromLeft (6);
    scaleBox.setBounds (tuneRow.removeFromLeft (120).reduced (0, 1));
    tuneRow.removeFromLeft (10);
    autotuneBtn.setBounds (tuneRow);
    s.removeFromTop (6);

    auto rvRow = s.removeFromTop (74);
    auto rvCol = rvRow.removeFromLeft (96);
    reverbL.setBounds (rvCol.removeFromTop (14));
    reverbS.setBounds (rvCol);
    s.removeFromTop (6);

    myVoiceL.setBounds (s.removeFromTop (14));
    auto voiceRow = s.removeFromTop (28);
    setVoiceBtn.setBounds (voiceRow.removeFromLeft (120));
    voiceRow.removeFromLeft (8);
    myVoiceName.setBounds (voiceRow);
    s.removeFromTop (10);

    auto btnRow = s.removeFromTop (30);
    processBtn.setBounds (btnRow.removeFromLeft (110));
    btnRow.removeFromLeft (6);
    previewBtn.setBounds (btnRow.removeFromLeft (90));
    btnRow.removeFromLeft (6);
    stopBtn.setBounds (btnRow.removeFromLeft (70));
    btnRow.removeFromLeft (6);
    saveBtn.setBounds (btnRow.removeFromLeft (100));
    s.removeFromTop (8);

    statusLabel.setBounds (s);
}
