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

    // Real-time state for UI visualization
    std::atomic<float> currentLfo { 0.0f };
    std::atomic<float> currentShape { 0.0f };
    std::atomic<float> currentDepth { 0.0f };
    std::atomic<float> currentRate { 0.5f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // LFO state
    double lfoPhase = 0.0;
    float shLastValue = 0.0f;
    double sampleRate = 44100.0;

    // Organic breath variations per cycle
    float rateVariation = 1.0f;      // ±2.5% random variation per cycle
    float depthVariation = 1.0f;     // ±2% random variation per cycle
    juce::Random rng;                // Random number generator for variations

    // Breath noise state
    float noiseL = 0.0f, noiseR = 0.0f;  // Current noise sample per channel
    float noiseLpf1L = 0.0f, noiseLpf1R = 0.0f;  // 1-pole LP filter state (low shelf)
    float noiseLpf2L = 0.0f, noiseLpf2R = 0.0f;  // 1-pole LP filter state (high shelf)
    uint32_t noiseSeed = 12345;      // LCG seed for reproducible noise

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

    // Asymmetric breath curve: slow inhale, faster exhale
    float computeAsymmetricBreath (double phase) const;

    // Generate filtered breath noise modulated by LFO
    void applyBreathNoise (float& sampleL, float& sampleR, float lfoL, float lfoR, float depth);

    void updateFilter (FilterState& fs, float& sample, float cutoff);
    void updateShelf  (ShelfState& ss, float& sample, float gainDb);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreathAudioProcessor)
};
