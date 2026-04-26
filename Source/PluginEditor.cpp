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
void BreathVisualizer::paint (juce::Graphics& g)
{
    g.fillAll (colKnobTrack.withAlpha (0.2f));

    if (processor == nullptr) return;

    const int w = getWidth();
    const int h = getHeight();
    const float shape = processor->currentShape.load (std::memory_order_relaxed);
    const float phase = processor->currentPhase.load (std::memory_order_relaxed);
    const float depth = processor->currentDepthSmoothed.load (std::memory_order_relaxed);
    const float lfo   = processor->currentLfo.load   (std::memory_order_relaxed);
    const float rate  = processor->currentRate.load  (std::memory_order_relaxed);

    g.setColour (colKnobTrack);
    g.drawRect (0, 0, w, h, 1);

    // Draw center line
    g.setColour (colKnobTrack.withAlpha (0.5f));
    g.drawHorizontalLine (h / 2, 0.0f, (float)w);

    const float freq = juce::MathConstants<float>::twoPi / (float)w;
    g.setColour (colAccent.withAlpha (0.8f));
    juce::Path wave;

    // Depth = wave amplitude. depth=0 → flat, depth=1 → full wave
    const float ampMod = depth * 0.38f;

    for (int x = 0; x < w; ++x)
    {
        const float phaseX = (float)x / (float)w;  // [0, 1)
        // Three target waveforms in [-1, 1]
        const float sinVal  = std::sin ((float)x * freq);
        const float sawUpVal   =  2.0f * phaseX - 1.0f;
        const float sawDownVal =  1.0f - 2.0f * phaseX;

        float val;
        if (shape <= 0.5f)
        {
            const float t = shape * 2.0f;
            val = (1.0f - t) * sinVal + t * sawUpVal;
        }
        else
        {
            const float t = (shape - 0.5f) * 2.0f;
            val = (1.0f - t) * sawUpVal + t * sawDownVal;
        }

        // Apply depth modulation
        const float y = (float)h * 0.5f - val * (float)h * ampMod;
        if (x == 0)
            wave.startNewSubPath ((float)x, y);
        else
            wave.lineTo ((float)x, y);
    }

    g.strokePath (wave, juce::PathStrokeType (1.5f));

    // Draw position dot following the curve at current phase
    const float dotX = phase * (float)w;
    const float sinValDot  = std::sin (phase * juce::MathConstants<float>::twoPi);
    const float sawUpValDot   =  2.0f * phase - 1.0f;
    const float sawDownValDot =  1.0f - 2.0f * phase;

    float valDot;
    if (shape <= 0.5f)
    {
        const float t = shape * 2.0f;
        valDot = (1.0f - t) * sinValDot + t * sawUpValDot;
    }
    else
    {
        const float t = (shape - 0.5f) * 2.0f;
        valDot = (1.0f - t) * sawUpValDot + t * sawDownValDot;
    }

    const float dotY = (float)h * 0.5f - valDot * (float)h * ampMod;
    g.setColour (colAccent);
    g.fillEllipse (dotX - 4.0f, dotY - 4.0f, 8.0f, 8.0f);

    // Label with rate info
    g.setColour (colText.withAlpha (0.5f));
    g.setFont (juce::FontOptions (16.0f));
    const int rateHz = (int)(rate * 100.0f);
    const juce::String rateStr = juce::String (rateHz) + " Hz";
    g.drawFittedText (rateStr, 4, 2, w - 8, 24, juce::Justification::left, 1);

    // Depth indicator
    const float depthBars = depth * 5.0f;  // Up to 5 bars
    for (int i = 0; i < (int)depthBars; ++i)
    {
        g.setColour (colAccent.withAlpha (0.3f + 0.2f * (float)i / 5.0f));
        g.fillRect ((float)(w - 16 - i * 3), (float)(h - 20), 2.0f, 12.0f);
    }
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

    // Setup visualizer
    breathViz.processor = &p;
    addAndMakeVisible (breathViz);

    setSize (840, 900);
    startTimer (30);  // ~33fps update
}

BreathAudioProcessorEditor::~BreathAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BreathAudioProcessorEditor::timerCallback()
{
    breathViz.repaint();
}

void BreathAudioProcessorEditor::setupSlider (juce::Slider& slider, juce::Label& label,
                                               const juce::String& text, const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 140, 40);
    slider.setColour (juce::Slider::textBoxTextColourId, colText);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (26.0f, juce::Font::bold));
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
    g.setFont (juce::FontOptions (44.0f, juce::Font::bold));
    g.drawFittedText ("BREATH", 0, 20, getWidth(), 60, juce::Justification::centred, 1);

    // Subtitle
    g.setColour (colText.withAlpha (0.5f));
    g.setFont (juce::FontOptions (20.0f));
    g.drawFittedText ("organic modulator", 0, 72, getWidth(), 32, juce::Justification::centred, 1);

    // Divider
    g.setColour (colKnobTrack.withAlpha (0.6f));
    g.drawHorizontalLine (116, 60.0f, (float)(getWidth() - 60));
}

void BreathAudioProcessorEditor::resized()
{
    const int sliderSize = 240;
    const int labelH     = 40;
    const int vizHeight  = 280;
    const int topOffset  = 130;
    const int totalW     = getWidth();
    const int colW       = totalW / 3;

    for (int i = 0; i < 3; ++i)
    {
        juce::Slider& s = (i == 0) ? rateSlider : (i == 1) ? depthSlider : shapeSlider;
        juce::Label&  l = (i == 0) ? rateLabel  : (i == 1) ? depthLabel  : shapeLabel;

        int cx = colW * i + colW / 2 - sliderSize / 2;
        l.setBounds (cx, topOffset, sliderSize, labelH);
        s.setBounds (cx, topOffset + labelH, sliderSize, sliderSize);
    }

    breathViz.setBounds(40, topOffset + labelH + sliderSize + 10, totalW - 80, vizHeight);
}
