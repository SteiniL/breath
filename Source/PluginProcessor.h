/*
  ==============================================================================
    Breath - Organic Tremolo/Filter Modulator
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class BreathAudioProcessor : public juce::AudioProcessor
{
public:
    BreathAudioProcessor();
    ~BreathAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // LFO state
    double lfoPhase = 0.0;
    float shLastValue = 0.0f;
    double sampleRate = 44100.0;

    // Smoothed params
    juce::SmoothedValue<float> smoothRate;
    juce::SmoothedValue<float> smoothDepth;
    juce::SmoothedValue<float> smoothShape;

    // Per-channel lowpass filter state (biquad)
    struct FilterState
    {
        float z1 = 0.0f, z2 = 0.0f;
    };
    FilterState filterL, filterR;

    // Per-channel high-shelf biquad state
    struct ShelfState
    {
        float z1 = 0.0f, z2 = 0.0f;
    };
    ShelfState shelfL, shelfR;

    // Precomputed high-shelf constants (6kHz, S=1, sample-rate-dependent)
    float shelfCosW0 = 0.0f;
    float shelfAlpha = 0.0f;

    // Pitch modulation delay lines (chorus-style vibrato, 512 = 2^9)
    static constexpr int kPitchBufSize = 2048;
    std::array<float, kPitchBufSize> pitchBufL{};
    std::array<float, kPitchBufSize> pitchBufR{};
    int pitchWritePos = 0;

    // Computes LFO value in [0,1] from an arbitrary phase value
    float computeLfo (float shape, double phase) const;

    void updateFilter (FilterState& fs, float& sample, float cutoff);
    void updateShelf  (ShelfState& ss, float& sample, float gainDb);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreathAudioProcessor)
};
