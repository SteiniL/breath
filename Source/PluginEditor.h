/*
  ==============================================================================
    Breath - Organic Tremolo/Filter Modulator
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// LFO waveform visualizer
struct LfoVisualizer : public juce::Component
{
    BreathAudioProcessor* processor = nullptr;
    std::array<float, 256> waveformBuffer{};
    int bufferIndex = 0;

    void paint (juce::Graphics& g) override;
    void timerCallback();
};

// Modulation intensity meter
struct DepthMeter : public juce::Component
{
    BreathAudioProcessor* processor = nullptr;

    void paint (juce::Graphics& g) override;
};

// Shape morphing waveform display
struct ShapeVisualizer : public juce::Component
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

    // Visualization components
    LfoVisualizer lfoViz;
    DepthMeter depthMeter;
    ShapeVisualizer shapeViz;

    void setupSlider (juce::Slider& slider, juce::Label& label,
                      const juce::String& text, const juce::String& suffix);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreathAudioProcessorEditor)
};
