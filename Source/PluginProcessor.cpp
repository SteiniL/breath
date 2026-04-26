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
    rateVariation = 1.0f;
    depthVariation = 1.0f;

    filterL = FilterState{};
    filterR = FilterState{};
    shelfL  = ShelfState{};
    shelfR  = ShelfState{};

    pitchBufL.fill (0.0f);
    pitchBufR.fill (0.0f);
    pitchWritePos = 0;

    noiseL = noiseR = 0.0f;
    noiseLpf1L = noiseLpf1R = 0.0f;
    noiseLpf2L = noiseLpf2R = 0.0f;
    noiseSeed = 12345;

    // Precompute high-shelf constants for 6kHz, shelf-slope S=1
    // With S=1: alpha = sin(w0) / sqrt(2)  (the (A+1/A)*(1/S-1)+2 term reduces to 2)
    const float w0shelf = 2.0f * juce::MathConstants<float>::pi * 6000.0f / (float)sr;
    shelfCosW0 = std::cos (w0shelf);
    shelfAlpha = std::sin (w0shelf) / std::sqrt (2.0f);

    cutoffSmoothL = cutoffSmoothR = 8000.0f;
    phaseJitterSmooth = 0.0f;
    clickEnvL = clickEnvR = 0.0f;
    for (auto& f : formantL) f = FormantState{};
    for (auto& f : formantR) f = FormantState{};

    // Peaking EQ biquads to color noise like vocal-tract breath formants
    auto computePeaking = [&](int idx, float fc, float gainDb, float Q)
    {
        const float A     = std::pow (10.0f, gainDb / 40.0f);
        const float w0    = 2.0f * juce::MathConstants<float>::pi * fc / (float)sr;
        const float alpha = std::sin (w0) / (2.0f * Q);
        const float cosw0 = std::cos (w0);
        const float a0inv = 1.0f / (1.0f + alpha / A);
        formantCoeffs[idx][0] = (1.0f + alpha * A)  * a0inv;  // b0
        formantCoeffs[idx][1] = (-2.0f * cosw0)     * a0inv;  // b1
        formantCoeffs[idx][2] = (1.0f - alpha * A)  * a0inv;  // b2
        formantCoeffs[idx][3] = (-2.0f * cosw0)     * a0inv;  // a1
        formantCoeffs[idx][4] = (1.0f - alpha / A)  * a0inv;  // a2
    };
    computePeaking (0,  800.0f, 4.0f, 2.0f);   // chest/throat resonance
    computePeaking (1, 2200.0f, 3.0f, 2.5f);   // nasal passage
    computePeaking (2, 3400.0f, 3.0f, 3.0f);   // front of mouth

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

// Asymmetric breath curve: slow inhale (0→0.5), fast exhale (0.5→1)
float BreathAudioProcessor::computeAsymmetricBreath (double phase) const
{
    const double twoPi = juce::MathConstants<double>::twoPi;
    double normalized = std::fmod (phase, twoPi) / twoPi;  // [0, 1)

    if (normalized < 0.5)
    {
        // Inhale: very slow rise. Power 1.5 → stretches low values, concentrated at start.
        float inhale = (float)std::pow (normalized * 2.0, 1.5);
        return inhale;
    }
    else
    {
        // Exhale: very fast decay. Power 0.6 → compressed exhale, quick return.
        float exhale = (float)std::pow ((normalized - 0.5) * 2.0, 0.6);
        return 1.0f - exhale;
    }
}

// Returns LFO value in [0, 1] for an arbitrary phase in [0, 2pi)
float BreathAudioProcessor::computeLfo (float shape, double phase) const
{
    const double twoPi = juce::MathConstants<double>::twoPi;

    float sinVal = 0.5f + 0.5f * (float)std::sin (phase);
    float breathVal = computeAsymmetricBreath (phase);
    float sawVal = (float)(std::fmod (phase, twoPi) / twoPi);
    float shVal  = shLastValue;

    float lfoValue;
    if (shape <= 0.5f)
    {
        // Interpolate sine → breath
        float t = shape * 2.0f;
        lfoValue = (1.0f - t) * sinVal + t * breathVal;
    }
    else
    {
        // Interpolate breath → sawtooth → S&H
        float t = (shape - 0.5f) * 2.0f;
        lfoValue = (1.0f - t) * breathVal + t * sawVal;
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

void BreathAudioProcessor::applyBreathNoise (float& sampleL, float& sampleR, float lfoL, float lfoR, float depth)
{
    auto lcgNoise = [this]() -> float
    {
        noiseSeed = noiseSeed * 1103515245u + 12345u;
        return ((noiseSeed >> 8) & 0xFFFFFF) / 16777216.0f * 2.0f - 1.0f;
    };

    float newNoiseL = lcgNoise();
    float newNoiseR = lcgNoise();

    // Dynamic LPF: airier (brighter) during inhale, breathier (darker) on exhale
    const float lpfL = 0.1f + 0.30f * lfoL;
    const float lpfR = 0.1f + 0.30f * lfoR;
    noiseLpf1L = noiseLpf1L * (1.0f - lpfL) + newNoiseL * lpfL;
    noiseLpf1R = noiseLpf1R * (1.0f - lpfR) + newNoiseR * lpfR;
    noiseLpf2L = noiseLpf2L * (1.0f - lpfL) + noiseLpf1L * lpfL;
    noiseLpf2R = noiseLpf2R * (1.0f - lpfR) + noiseLpf1R * lpfR;

    // Apply formant peaking EQs to sculpt noise like vocal-tract breath character
    auto applyBq = [&](float x, FormantState& fs, int k) -> float
    {
        const float out = formantCoeffs[k][0] * x + fs.z1;
        fs.z1 = formantCoeffs[k][1] * x - formantCoeffs[k][3] * out + fs.z2;
        fs.z2 = formantCoeffs[k][2] * x - formantCoeffs[k][4] * out;
        return out;
    };

    float nsL = noiseLpf2L, nsR = noiseLpf2R;
    for (int k = 0; k < 3; ++k)
    {
        nsL = applyBq (nsL, formantL[k], k);
        nsR = applyBq (nsR, formantR[k], k);
    }

    // Boost noise at mid-breath transition (~lfo=0.5) — peak airflow turbulence
    const float transL = 1.0f + 2.5f * std::exp (-30.0f * (lfoL - 0.5f) * (lfoL - 0.5f));
    const float transR = 1.0f + 2.5f * std::exp (-30.0f * (lfoR - 0.5f) * (lfoR - 0.5f));

    sampleL += nsL * depth * lfoL * 0.065f * transL;
    sampleR += nsR * depth * lfoR * 0.065f * transR;
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
    auto* dataR = (totalOut > 1) ? buffer.getWritePointer (1) : nullptr;

    // Max pitch-vibrato delay: ~5ms → at rate=1Hz gives ≈±0.5 semitones
    const float maxPitchDelay = 0.005f * (float)sampleRate;
    // Clamp to buffer capacity with headroom for interpolation
    const float maxPitchDelayClamped = juce::jmin (maxPitchDelay, (float)(kPitchBufSize - 2));

    for (int i = 0; i < numSamples; ++i)
    {
        const float rate  = smoothRate.getNextValue() * rateVariation;
        const float depth = smoothDepth.getNextValue() * depthVariation;
        const float shape = smoothShape.getNextValue();

        // Phase jitter: smoothed noise adds ±1.2% timing variation per sample
        phaseJitterSmooth = 0.998f * phaseJitterSmooth + 0.002f * (rng.nextFloat() * 2.0f - 1.0f);
        lfoPhase += twoPi * rate * (1.0 + 0.012 * (double)phaseJitterSmooth) / sampleRate;

        if (lfoPhase >= twoPi)
        {
            lfoPhase -= twoPi;
            shLastValue = juce::Random::getSystemRandom().nextFloat();

            // Generate new random variations for next breath cycle
            rateVariation  = 0.96f + 0.08f * rng.nextFloat();
            depthVariation = 0.97f + 0.06f * rng.nextFloat();

        }

        // R channel uses a ~50ms phase-delayed LFO for stereo width
        const double phaseOffsetR = std::fmod (twoPi * rate * 0.050, twoPi);
        double phaseR = std::fmod (lfoPhase - phaseOffsetR + twoPi, twoPi);

        const float lfoL = computeLfo (shape, lfoPhase);
        const float lfoR = (dataR != nullptr) ? computeLfo (shape, phaseR) : lfoL;

        // Update UI visualization state
        currentLfo.store (lfoL, std::memory_order_relaxed);
        currentShape.store (shape, std::memory_order_relaxed);
        currentDepth.store (depth, std::memory_order_relaxed);
        currentRate.store (rate / 100.0f, std::memory_order_relaxed);  // Normalize to [0,1]
        currentPhase.store ((float)(lfoPhase / twoPi), std::memory_order_relaxed);  // Normalize to [0,1)

        // ---- Existing: Volume modulation ----
        dataL[i] *= 1.0f - depth * lfoL;
        if (dataR != nullptr)
            dataR[i] *= 1.0f - depth * lfoR;

        // ---- Filter modulation (asymmetric smoothing) ----
        // Filter closes slowly (inhale builds up), opens fast (exhale releases quickly)
        const float ctgtL = 8000.0f - depth * lfoL * 7200.0f;
        const float ctgtR = 8000.0f - depth * lfoR * 7200.0f;
        const float alphaL = (ctgtL < cutoffSmoothL) ? 0.9985f : 0.985f;
        const float alphaR = (ctgtR < cutoffSmoothR) ? 0.9985f : 0.985f;
        cutoffSmoothL = alphaL * cutoffSmoothL + (1.0f - alphaL) * ctgtL;
        cutoffSmoothR = alphaR * cutoffSmoothR + (1.0f - alphaR) * ctgtR;
        updateFilter (filterL, dataL[i], cutoffSmoothL);
        if (dataR != nullptr)
            updateFilter (filterR, dataR[i], cutoffSmoothR);

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
            const float drive = 1.0f + amt * 0.3f; // Softer saturation to reduce clipping
            const float sat   = std::tanh (s * drive) / drive;
            return (1.0f - amt) * s + amt * sat;
        };

        dataL[i] = applySat (dataL[i], depth * lfoL * 0.5f);
        if (dataR != nullptr)
            dataR[i] = applySat (dataR[i], depth * lfoR * 0.5f);

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
