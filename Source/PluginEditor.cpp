#include "PluginProcessor.h"
#include "PluginEditor.h"

VocalCleanerAudioProcessorEditor::VocalCleanerAudioProcessorEditor (VocalCleanerAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    auto setupKnob = [this] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 20);
        s.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff60cdff));
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

    auto setupLabel = [this] (juce::Label& l, const juce::String& t, float sz, bool bold)
    {
        l.setText (t, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (sz, bold ? juce::Font::bold : juce::Font::plain)));
        l.setColour (juce::Label::textColourId, juce::Colour (0xffe8e8e8));
        addAndMakeVisible (l);
    };
    setupLabel (title,    "nvh the noise", 22.0f, true);
    setupLabel (subtitle, "Real-time vocal denoise (RNNoise) - runs on this track", 12.0f, false);
    subtitle.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0a6));
    setupLabel (mixL,  "DENOISE AMOUNT", 11.0f, true);
    setupLabel (gainL, "OUTPUT GAIN",   11.0f, true);
    mixL.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0a6));
    gainL.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0a6));

    setSize (460, 320);
    startTimerHz (30);
}

VocalCleanerAudioProcessorEditor::~VocalCleanerAudioProcessorEditor() { stopTimer(); }

void VocalCleanerAudioProcessorEditor::timerCallback()
{
    auto smooth = [] (float cur, float target)
    {
        return target > cur ? target : cur * 0.82f + target * 0.18f; // fast attack, slow release
    };
    inMeter  = smooth (inMeter,  proc.inLevel.load());
    outMeter = smooth (outMeter, proc.outLevel.load());
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

    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (52); // header
    auto meters = area.removeFromRight (96);
    meters.removeFromBottom (16);
    auto inR  = meters.removeFromLeft (40).reduced (4, 0);
    meters.removeFromLeft (8);
    auto outR = meters.removeFromLeft (40).reduced (4, 0);
    drawMeter (g, inR,  inMeter,  "IN");
    drawMeter (g, outR, outMeter, "OUT");
}

void VocalCleanerAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto header = r.removeFromTop (48);
    title.setBounds (header.removeFromTop (28));
    subtitle.setBounds (header);

    r.removeFromRight (96); // meter column (painted)
    r.removeFromTop (6);

    auto knobs = r.removeFromTop (180);
    auto half = knobs.getWidth() / 2;
    auto left = knobs.removeFromLeft (half);
    auto right = knobs;

    mixL.setBounds (left.removeFromTop (16));
    mixS.setBounds (left.reduced (6));
    gainL.setBounds (right.removeFromTop (16));
    gainS.setBounds (right.reduced (6));
}
