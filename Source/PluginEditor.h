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

    VocalCleanerAudioProcessor& proc;

    juce::Label title, subtitle, mixL, gainL;
    juce::Slider mixS, gainS;
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttach> mixA, gainA;

    float inMeter = 0.0f, outMeter = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalCleanerAudioProcessorEditor)
};
