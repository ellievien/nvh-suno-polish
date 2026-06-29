#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout VocalCleanerAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Denoise Amount",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gain", 1 }, "Output Gain",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

VocalCleanerAudioProcessor::VocalCleanerAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

void VocalCleanerAudioProcessor::prepareToPlay (double sampleRate, int)
{
    core.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    setLatencySamples (core.getLatencySamples());
}

bool VocalCleanerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void VocalCleanerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const float mix  = apvts.getRawParameterValue ("mix")->load() * 0.01f;
    const float gain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gain")->load());

    auto lv = core.process (buffer.getArrayOfWritePointers(),
                            buffer.getNumChannels(), buffer.getNumSamples(), mix, gain);
    inLevel.store (lv.in);
    outLevel.store (lv.out);
}

void VocalCleanerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState().createXml())
        copyXmlToBinary (*state, destData);
}

void VocalCleanerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VocalCleanerAudioProcessor::createEditor()
{
    return new VocalCleanerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalCleanerAudioProcessor();
}
