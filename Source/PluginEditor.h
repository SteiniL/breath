/*
  ==============================================================================
    Breath - Organic Tremolo/Filter Modulator
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class BreathAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    BreathAudioProcessorEditor (BreathAudioProcessor&);
    ~BreathAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BreathAudioProcessor& audioProcessor;

    juce::Slider rateSlider, depthSlider, shapeSlider;
    juce::Label  rateLabel,  depthLabel,  shapeLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> rateAttachment;
    std::unique_ptr<SliderAttachment> depthAttachment;
    std::unique_ptr<SliderAttachment> shapeAttachment;

    std::unique_ptr<juce::LookAndFeel_V4> breathLookAndFeel;

    void setupSlider (juce::Slider& slider, juce::Label& label,
                      const juce::String& text, const juce::String& suffix);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreathAudioProcessorEditor)
};
