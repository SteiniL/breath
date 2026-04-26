/*
  ==============================================================================
    Breath - Organic Tremolo/Filter Modulator
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout BreathAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "rate", 1 }, "Rate",
        juce::NormalisableRange<float> (0.1f, 100.0f, 0.0f, 0.25f),
        1.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "depth", 1 }, "Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.5f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "shape", 1 }, "Shape",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));

    return layout;
}

BreathAudioProcessor::BreathAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                    #if ! JucePlugin_IsMidiEffect
                     #if ! JucePlugin_IsSynth
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                     #endif
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                    #endif
                      ),
#else
    :
#endif
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

BreathAudioProcessor::~BreathAudioProcessor() {}

//==============================================================================
const juce::String BreathAudioProcessor::getName() const { return JucePlugin_Name; }
bool BreathAudioProcessor::acceptsMidi() const  { return false; }
bool BreathAudioProcessor::producesMidi() const { return false; }
bool BreathAudioProcessor::isMidiEffect() const { return false; }
double BreathAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int BreathAudioProcessor::getNumPrograms()    { return 1; }
int BreathAudioProcessor::getCurrentProgram() { return 0; }
void BreathAudioProcessor::setCurrentProgram (int) {}
const juce::String BreathAudioProcessor::getProgramName (int) { return {}; }
void BreathAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void BreathAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    lfoPhase    = 0.0;
    shLastValue = 0.0f;

    filterL = FilterState{};
    filterR = FilterState{};
    shelfL  = ShelfState{};
    shelfR  = ShelfState{};

    pitchBufL.fill (0.0f);
    pitchBufR.fill (0.0f);
    pitchWritePos = 0;

    // Precompute high-shelf constants for 6kHz, shelf-slope S=1
    // With S=1: alpha = sin(w0) / sqrt(2)  (the (A+1/A)*(1/S-1)+2 term reduces to 2)
    const float w0shelf = 2.0f * juce::MathConstants<float>::pi * 6000.0f / (float)sr;
    shelfCosW0 = std::cos (w0shelf);
    shelfAlpha = std::sin (w0shelf) / std::sqrt (2.0f);

    auto smoothTime = 0.02f;
    smoothRate.reset  (sr, smoothTime);
    smoothDepth.reset (sr, smoothTime);
    smoothShape.reset (sr, smoothTime);

    smoothRate.setCurrentAndTargetValue  (*apvts.getRawParameterValue ("rate"));
    smoothDepth.setCurrentAndTargetValue (*apvts.getRawParameterValue ("depth"));
    smoothShape.setCurrentAndTargetValue (*apvts.getRawParameterValue ("shape"));
}

void BreathAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool BreathAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    auto inputSet = layouts.getMainInputChannelSet();
    if (inputSet != juce::AudioChannelSet::disabled()
     && inputSet != layouts.getMainOutputChannelSet())
        return false;
   #endif
    return true;
  #endif
}
#endif

// Returns LFO value in [0, 1] for an arbitrary phase in [0, 2pi)
float BreathAudioProcessor::computeLfo (float shape, double phase) const
{
    const double twoPi = juce::MathConstants<double>::twoPi;

    float sinVal = 0.5f + 0.5f * (float)std::sin (phase);
    float sawVal = (float)(phase / twoPi);
    float shVal  = shLastValue;

    float lfoValue;
    if (shape <= 0.5f)
    {
        float t = shape * 2.0f;
        lfoValue = (1.0f - t) * sinVal + t * sawVal;
    }
    else
    {
        float t = (shape - 0.5f) * 2.0f;
        lfoValue = (1.0f - t) * sawVal + t * shVal;
    }

    return lfoValue;
}

void BreathAudioProcessor::updateFilter (FilterState& fs, float& sample, float cutoff)
{
    const float omega = 2.0f * juce::MathConstants<float>::pi * cutoff / (float)sampleRate;
    const float a0    = 1.0f - std::exp (-omega);

    float out = a0 * sample + (1.0f - a0) * fs.z1;
    fs.z1  = out;
    sample = out;
}

void BreathAudioProcessor::updateShelf (ShelfState& ss, float& sample, float gainDb)
{
    // High-shelf biquad, 6kHz, S=1.
    // At A=1 (gainDb=0) the filter is mathematically identical to bypass (b_i/a_i = 1).
    const float A      = std::pow (10.0f, gainDb / 40.0f);
    const float sqrtA  = std::sqrt (A);
    const float tsAa   = 2.0f * sqrtA * shelfAlpha; // 2*sqrt(A)*alpha

    const float A1  = A + 1.0f;
    const float Am1 = A - 1.0f;
    const float Am1c = Am1 * shelfCosW0;
    const float A1c  = A1  * shelfCosW0;

    const float b0 =  A * (A1 + Am1c + tsAa);
    const float b1 = -2.0f * A * (Am1 + A1c);
    const float b2 =  A * (A1 + Am1c - tsAa);
    const float a0 =  A1 - Am1c + tsAa;
    const float a1 =  2.0f * (Am1 - A1c);
    const float a2 =  A1 - Am1c - tsAa;

    // Transposed Direct Form II
    const float invA0 = 1.0f / a0;
    const float out   = (b0 * sample + ss.z1) * invA0;
    ss.z1  = b1 * sample - a1 * out + ss.z2;
    ss.z2  = b2 * sample - a2 * out;
    sample = out;
}

void BreathAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalIn   = getTotalNumInputChannels();
    const int totalOut  = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    smoothRate.setTargetValue  (*apvts.getRawParameterValue ("rate"));
    smoothDepth.setTargetValue (*apvts.getRawParameterValue ("depth"));
    smoothShape.setTargetValue (*apvts.getRawParameterValue ("shape"));

    const double twoPi = juce::MathConstants<double>::twoPi;

    auto* dataL = buffer.getWritePointer (0);
    auto* dataR = (totalIn > 1) ? buffer.getWritePointer (1) : nullptr;

    // Max pitch-vibrato delay: ~5ms → at rate=1Hz gives ≈±0.5 semitones
    const float maxPitchDelay = 0.005f * (float)sampleRate;
    // Clamp to buffer capacity with headroom for interpolation
    const float maxPitchDelayClamped = juce::jmin (maxPitchDelay, (float)(kPitchBufSize - 2));

    for (int i = 0; i < numSamples; ++i)
    {
        const float rate  = smoothRate.getNextValue();
        const float depth = smoothDepth.getNextValue();
        const float shape = smoothShape.getNextValue();

        // Advance LFO phase; trigger S&H at cycle boundary
        lfoPhase += twoPi * rate / sampleRate;
        if (lfoPhase >= twoPi)
        {
            lfoPhase -= twoPi;
            shLastValue = juce::Random::getSystemRandom().nextFloat();
        }

        // R channel uses a ~50ms phase-delayed LFO for stereo width
        const double phaseOffsetR = twoPi * rate * 0.050;
        double phaseR = std::fmod (lfoPhase - phaseOffsetR + twoPi, twoPi);

        const float lfoL = computeLfo (shape, lfoPhase);
        const float lfoR = (dataR != nullptr) ? computeLfo (shape, phaseR) : lfoL;

        // ---- Existing: Volume modulation ----
        dataL[i] *= 1.0f - depth * lfoL;
        if (dataR != nullptr)
            dataR[i] *= 1.0f - depth * lfoR;

        // ---- Existing: Filter modulation ----
        const float cutoffL = 8000.0f - depth * lfoL * 7200.0f;
        const float cutoffR = 8000.0f - depth * lfoR * 7200.0f;
        updateFilter (filterL, dataL[i], cutoffL);
        if (dataR != nullptr)
            updateFilter (filterR, dataR[i], cutoffR);

        // ---- New 1: Pitch modulation (chorus-style delay-line vibrato) ----
        // Write current post-filter samples into circular delay buffers
        const int wPos = pitchWritePos & (kPitchBufSize - 1);
        pitchBufL[wPos] = dataL[i];
        if (dataR != nullptr)
            pitchBufR[wPos] = dataR[i];

        auto readPitchBuf = [&] (const std::array<float, kPitchBufSize>& buf,
                                 float delaySamples) -> float
        {
            delaySamples = juce::jlimit (0.0f, maxPitchDelayClamped, delaySamples);
            const int d0   = (int)delaySamples;
            const float fr = delaySamples - (float)d0;
            const int r0   = (wPos - d0 + kPitchBufSize) & (kPitchBufSize - 1);
            const int r1   = (r0 - 1 + kPitchBufSize)   & (kPitchBufSize - 1);
            return buf[r0] * (1.0f - fr) + buf[r1] * fr;
        };

        dataL[i] = readPitchBuf (pitchBufL, depth * maxPitchDelayClamped * lfoL);
        if (dataR != nullptr)
            dataR[i] = readPitchBuf (pitchBufR, depth * maxPitchDelayClamped * lfoR);

        pitchWritePos = (pitchWritePos + 1) & (kPitchBufSize - 1);

        // ---- New 2: Asymmetric harmonic saturation ----
        // Crossfade between dry and softclipped; amt=0 at depth=0 or lfo=0 → transparent
        auto applySat = [] (float s, float amt) -> float
        {
            if (amt < 1e-6f) return s;
            const float drive = 1.0f + amt * 1.0f; // 1.0..2.0
            const float sat   = std::tanh (s * drive) / drive;
            return (1.0f - amt) * s + amt * sat;
        };

        dataL[i] = applySat (dataL[i], depth * lfoL);
        if (dataR != nullptr)
            dataR[i] = applySat (dataR[i], depth * lfoR);

        // ---- New 3: High-shelf brightness sweep (+0..+2dB at 6kHz) ----
        updateShelf (shelfL, dataL[i], 9.0f * depth * lfoL);
        if (dataR != nullptr)
            updateShelf (shelfR, dataR[i], 9.0f * depth * lfoR);
    }
}

//==============================================================================
bool BreathAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* BreathAudioProcessor::createEditor()
{
    return new BreathAudioProcessorEditor (*this);
}

void BreathAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void BreathAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BreathAudioProcessor();
}
