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

    setSize (420, 280);
}

BreathAudioProcessorEditor::~BreathAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
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
    g.drawFittedText ("BREATH", 0, 12, getWidth(), 30, juce::Justification::centred, 1);

    // Subtitle
    g.setColour (colText.withAlpha (0.5f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawFittedText ("organic modulator", 0, 38, getWidth(), 16, juce::Justification::centred, 1);

    // Divider
    g.setColour (colKnobTrack.withAlpha (0.6f));
    g.drawHorizontalLine (58, 30.0f, (float)(getWidth() - 30));
}

void BreathAudioProcessorEditor::resized()
{
    const int sliderSize = 120;
    const int labelH     = 20;
    const int topOffset  = 65;
    const int totalW     = getWidth();
    const int colW       = totalW / 3;

    for (int i = 0; i < 3; ++i)
    {
        juce::Slider& s = (i == 0) ? rateSlider  : (i == 1) ? depthSlider  : shapeSlider;
        juce::Label&  l = (i == 0) ? rateLabel   : (i == 1) ? depthLabel   : shapeLabel;

        int cx = colW * i + colW / 2 - sliderSize / 2;
        l.setBounds (cx, topOffset, sliderSize, labelH);
        s.setBounds (cx, topOffset + labelH, sliderSize, sliderSize);
    }
}
