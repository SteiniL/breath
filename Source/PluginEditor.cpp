/*
  ==============================================================================
    Breath - Organic Tremolo/Filter Modulator
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

static const juce::Colour colBackground  { 0xff1a1a2e };
static const juce::Colour colKnob        { 0xff4a90d9 };
static const juce::Colour colKnobTrack   { 0xff2a5a8a };
static const juce::Colour colText        { 0xffe0e0f0 };
static const juce::Colour colAccent      { 0xff7ec8e3 };

//==============================================================================
void LfoVisualizer::paint (juce::Graphics& g)
{
    g.fillAll (colKnobTrack.withAlpha (0.2f));

    if (processor == nullptr) return;

    const int w = getWidth();
    const int h = getHeight();
    const float lfo = processor->currentLfo.load (std::memory_order_relaxed);

    // Draw center line
    g.setColour (colKnobTrack);
    g.drawHorizontalLine (h / 2, 0.0f, (float)w);

    // Draw current waveform display (rolling buffer)
    g.setColour (colAccent);
    g.drawRect (0, 0, w, h, 1);

    // Draw LFO level as vertical bar (0-1 mapped to bottom-top)
    float barH = lfo * (float)h;
    g.setColour (colAccent.withAlpha (0.8f));
    g.fillRect (2.0f, (float)(h - (int)barH), (float)(w - 4), barH);

    // Draw frequency indicator
    g.setColour (colText.withAlpha (0.5f));
    g.setFont (juce::FontOptions (9.0f));
    g.drawFittedText ("LFO", 4, 2, w - 8, 12, juce::Justification::left, 1);
}

void DepthMeter::paint (juce::Graphics& g)
{
    g.fillAll (colKnobTrack.withAlpha (0.2f));

    if (processor == nullptr) return;

    const int w = getWidth();
    const int h = getHeight();
    const float depth = processor->currentDepth.load (std::memory_order_relaxed);
    const float lfo = processor->currentLfo.load (std::memory_order_relaxed);

    // Modulation effect: depth * lfo
    float modAmount = depth * lfo;

    g.setColour (colKnobTrack);
    g.drawRect (0, 0, w, h, 1);

    // Left side: dry signal level
    int dryLevel = (int)((1.0f - depth) * (float)h);
    g.setColour (colText.withAlpha (0.4f));
    g.fillRect (4.0f, (float)(h - dryLevel), (float)(w / 2 - 6), (float)dryLevel);

    // Right side: modulated signal (depth * lfo)
    int modLevel = (int)(modAmount * (float)h);
    g.setColour (colAccent.withAlpha (0.8f));
    g.fillRect ((float)(4 + w / 2), (float)(h - modLevel), (float)(w / 2 - 6), (float)modLevel);

    g.setColour (colText.withAlpha (0.5f));
    g.setFont (juce::FontOptions (9.0f));
    g.drawFittedText ("Depth", 4, 2, w - 8, 12, juce::Justification::left, 1);
}

void ShapeVisualizer::paint (juce::Graphics& g)
{
    g.fillAll (colKnobTrack.withAlpha (0.2f));

    if (processor == nullptr) return;

    const int w = getWidth();
    const int h = getHeight();
    const float shape = processor->currentShape.load (std::memory_order_relaxed);

    g.setColour (colKnobTrack);
    g.drawRect (0, 0, w, h, 1);

    // Draw waveform segments based on shape morphing
    g.setColour (colAccent.withAlpha (0.8f));

    if (shape <= 0.5f)
    {
        // Sine morphing toward breath curve
        float t = shape * 2.0f;  // [0,1]
        float freq = 2.0f * juce::MathConstants<float>::pi / (float)w;
        juce::Path wave;
        for (int x = 0; x < w; ++x)
        {
            float sinVal = std::sin ((float)x * freq);
            float mixVal = (1.0f - t) * sinVal + t * (2.0f * (float)x / (float)w - 1.0f);
            float y = (float)h * 0.5f - mixVal * (float)h * 0.35f;
            if (x == 0)
                wave.startNewSubPath ((float)x, y);
            else
                wave.lineTo ((float)x, y);
        }
        g.strokePath (wave, juce::PathStrokeType (1.5f));
    }
    else
    {
        // Sawtooth and S&H
        float t = (shape - 0.5f) * 2.0f;  // [0,1]
        juce::Path wave;
        for (int x = 0; x < w; ++x)
        {
            float sawVal = 2.0f * ((float)x / (float)w) - 1.0f;
            float randVal = -1.0f + 2.0f * fmodf ((float)x * 73.f, 1.0f);  // Pseudo-random
            float y = (float)h * 0.5f - ((1.0f - t) * sawVal + t * randVal) * (float)h * 0.35f;
            if (x == 0)
                wave.startNewSubPath ((float)x, y);
            else
                wave.lineTo ((float)x, y);
        }
        g.strokePath (wave, juce::PathStrokeType (1.5f));
    }

    g.setColour (colText.withAlpha (0.5f));
    g.setFont (juce::FontOptions (9.0f));
    g.drawFittedText ("Shape", 4, 2, w - 8, 12, juce::Justification::left, 1);
}

struct BreathLookAndFeel : public juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override
    {
        const float radius = (float)juce::jmin (width / 2, height / 2) - 6.0f;
        const float centreX = (float)x + (float)width  * 0.5f;
        const float centreY = (float)y + (float)height * 0.5f;
        const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // Track arc
        juce::Path track;
        track.addArc (centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f,
                      rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (colKnobTrack);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

        // Value arc
        juce::Path valueArc;
        valueArc.addArc (centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f,
                         rotaryStartAngle, angle, true);
        g.setColour (colAccent);
        g.strokePath (valueArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

        // Knob body
        const float knobRadius = radius * 0.6f;
        g.setColour (colKnob);
        g.fillEllipse (centreX - knobRadius, centreY - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);

        // Pointer line
        const float pointerLen = knobRadius * 0.7f;
        const float px = centreX + std::sin (angle) * pointerLen;
        const float py = centreY - std::cos (angle) * pointerLen;
        g.setColour (colBackground);
        g.drawLine (centreX, centreY, px, py, 2.5f);

        // Subtle glow ring
        g.setColour (colAccent.withAlpha (0.15f));
        g.drawEllipse (centreX - knobRadius, centreY - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f, 1.5f);
    }
};

//==============================================================================
BreathAudioProcessorEditor::BreathAudioProcessorEditor (BreathAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    breathLookAndFeel = std::make_unique<BreathLookAndFeel>();
    setLookAndFeel (breathLookAndFeel.get());

    setupSlider (rateSlider,  rateLabel,  "RATE",  " Hz");
    setupSlider (depthSlider, depthLabel, "DEPTH", "");
    setupSlider (shapeSlider, shapeLabel, "SHAPE", "");

    rateAttachment  = std::make_unique<SliderAttachment> (p.apvts, "rate",  rateSlider);
    depthAttachment = std::make_unique<SliderAttachment> (p.apvts, "depth", depthSlider);
    shapeAttachment = std::make_unique<SliderAttachment> (p.apvts, "shape", shapeSlider);

    rateSlider.textFromValueFunction  = [](double v) { return juce::String (v, 2) + " Hz"; };
    depthSlider.textFromValueFunction = [](double v) { return juce::String (v, 2); };
    shapeSlider.textFromValueFunction = [](double v) { return juce::String (v, 2); };
    rateSlider.setNumDecimalPlacesToDisplay (2);
    depthSlider.setNumDecimalPlacesToDisplay (2);
    shapeSlider.setNumDecimalPlacesToDisplay (2);

    // Setup visualizers
    lfoViz.processor = &p;
    addAndMakeVisible (lfoViz);

    depthMeter.processor = &p;
    addAndMakeVisible (depthMeter);

    shapeViz.processor = &p;
    addAndMakeVisible (shapeViz);

    setSize (420, 380);
    startTimer (30);  // ~33fps update
}

BreathAudioProcessorEditor::~BreathAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BreathAudioProcessorEditor::timerCallback()
{
    lfoViz.repaint();
    depthMeter.repaint();
    shapeViz.repaint();
}

void BreathAudioProcessorEditor::setupSlider (juce::Slider& slider, juce::Label& label,
                                               const juce::String& text, const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
    slider.setColour (juce::Slider::textBoxTextColourId, colText);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, colText);
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);
}

//==============================================================================
void BreathAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colBackground);

    // Title
    g.setColour (colAccent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawFittedText ("BREATH", 0, 10, getWidth(), 30, juce::Justification::centred, 1);

    // Subtitle
    g.setColour (colText.withAlpha (0.5f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawFittedText ("organic modulator", 0, 36, getWidth(), 16, juce::Justification::centred, 1);

    // Divider
    g.setColour (colKnobTrack.withAlpha (0.6f));
    g.drawHorizontalLine (58, 30.0f, (float)(getWidth() - 30));
}

void BreathAudioProcessorEditor::resized()
{
    const int sliderSize = 120;
    const int labelH     = 20;
    const int vizHeight  = 60;
    const int topOffset  = 65;
    const int totalW     = getWidth();
    const int colW       = totalW / 3;

    for (int i = 0; i < 3; ++i)
    {
        juce::Slider& s = (i == 0) ? rateSlider  : (i == 1) ? depthSlider  : shapeSlider;
        juce::Label&  l = (i == 0) ? rateLabel   : (i == 1) ? depthLabel   : shapeLabel;
        juce::Component& viz = (i == 0) ? (juce::Component&)lfoViz : (i == 1) ? (juce::Component&)depthMeter : (juce::Component&)shapeViz;

        int cx = colW * i + colW / 2 - sliderSize / 2;
        l.setBounds (cx, topOffset, sliderSize, labelH);
        s.setBounds (cx, topOffset + labelH, sliderSize, sliderSize);
        viz.setBounds (colW * i + 10, topOffset + labelH + sliderSize + 5, colW - 20, vizHeight);
    }
}
