#pragma once
#include "PluginProcessor.h"

class VocalCleanerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit VocalCleanerAudioProcessorEditor (VocalCleanerAudioProcessor&);
    ~VocalCleanerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawMeter (juce::Graphics&, juce::Rectangle<int>, float level, const juce::String& label);
    StudioParams gatherStudio() const;

    VocalCleanerAudioProcessor& proc;

    // --- real-time denoise section ---
    juce::Label title, subtitle, mixL, gainL;
    juce::Slider mixS, gainS;
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttach> mixA, gainA;
    float inMeter = 0.0f, outMeter = 0.0f;

    // --- Studio Process section ---
    juce::Label studioTitle, studioSub, serverL, modeL, myVoiceL, tuneL;
    juce::TextEditor serverUrl;
    juce::TextButton loadBtn { "Load take..." };
    juce::Label      takeLabel;
    juce::ToggleButton enhanceBtn { "Enhance" }, regenBtn { "Regenerate" }, sunoBtn { "Suno Polish" };
    juce::ComboBox   modelBox, languageBox, keyBox, scaleBox;
    juce::ToggleButton autotuneBtn { "Autotune" };
    juce::Slider     reverbS;           // Suno Polish reverb amount (rotary knob)
    juce::Label      reverbL;
    juce::TextButton setVoiceBtn { "Set my voice..." };
    juce::Label      myVoiceName;
    juce::TextButton processBtn { "Process" }, previewBtn { "Preview" },
                     stopBtn { "Stop" }, saveBtn { "Save as..." };
    juce::Label      statusLabel;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String chosenTakePath;
    int lastStatusVersion = -1;
    int lastMyVoiceVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalCleanerAudioProcessorEditor)
};
