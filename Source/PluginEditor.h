/*
  ==============================================================================
    Breath - Organic Tremolo/Filter Modulator
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// Combined waveform + depth modulation visualizer
struct BreathVisualizer : public juce::Component
{
    BreathAudioProcessor* processor = nullptr;

    void paint (juce::Graphics& g) override;
};

class BreathAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
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

    // Visualization component
    BreathVisualizer breathViz;

    void setupSlider (juce::Slider& slider, juce::Label& label,
                      const juce::String& text, const juce::String& suffix);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreathAudioProcessorEditor)
};
