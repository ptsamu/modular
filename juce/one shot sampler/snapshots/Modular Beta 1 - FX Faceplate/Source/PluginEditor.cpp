#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float eqMinFrequency = 20.0f;
constexpr float eqMaxFrequency = 20000.0f;

float frequencyFromPosition (float position)
{
    return eqMinFrequency * std::pow (eqMaxFrequency / eqMinFrequency, juce::jlimit (0.0f, 1.0f, position));
}

float positionFromFrequency (float frequency)
{
    return std::log (juce::jlimit (eqMinFrequency, eqMaxFrequency, frequency) / eqMinFrequency)
         / std::log (eqMaxFrequency / eqMinFrequency);
}

juce::dsp::IIR::Coefficients<float>::Ptr makeEqCoefficients (double sampleRate, float frequency, float q, float gainDb, int type)
{
    const auto safeRate = juce::jmax (8000.0, sampleRate);
    const auto safeFrequency = juce::jlimit (eqMinFrequency, (float) safeRate * 0.49f, frequency);
    const auto safeQ = juce::jlimit (0.2f, 8.0f, q);
    const auto gain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, gainDb));
    switch (type)
    {
        case 1: return juce::dsp::IIR::Coefficients<float>::makeLowPass (safeRate, safeFrequency, safeQ);
        case 2: return juce::dsp::IIR::Coefficients<float>::makeHighPass (safeRate, safeFrequency, safeQ);
        case 3: return juce::dsp::IIR::Coefficients<float>::makeNotch (safeRate, safeFrequency, safeQ);
        case 4: return juce::dsp::IIR::Coefficients<float>::makeLowShelf (safeRate, safeFrequency, safeQ, gain);
        case 5: return juce::dsp::IIR::Coefficients<float>::makeHighShelf (safeRate, safeFrequency, safeQ, gain);
        default: return juce::dsp::IIR::Coefficients<float>::makePeakFilter (safeRate, safeFrequency, safeQ, gain);
    }
}

void drawModuleIcon (juce::Graphics& g, int effect, juce::Point<float> centre, juce::Colour colour, float scale = 1.0f)
{
    g.setColour (colour);
    const auto line = juce::PathStrokeType (1.25f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    if (effect == -1) // add module
    {
        for (int i = -2; i <= 2; ++i)
        {
            g.fillEllipse (centre.x + i * 8.0f * scale - 1.7f * scale, centre.y - 1.7f * scale, 3.4f * scale, 3.4f * scale);
            g.fillEllipse (centre.x - 1.7f * scale, centre.y + i * 8.0f * scale - 1.7f * scale, 3.4f * scale, 3.4f * scale);
        }
        return;
    }

    if (effect == 0 || effect == 4) // delay / flanger
    {
        g.drawEllipse (centre.x - 18.0f * scale, centre.y - 14.0f * scale, 26.0f * scale, 28.0f * scale, 1.0f * scale);
        g.drawEllipse (centre.x - 8.0f * scale, centre.y - 14.0f * scale, 26.0f * scale, 28.0f * scale, 1.0f * scale);
    }
    else if (effect == 1) // reverb
    {
        for (int ring = 1; ring <= 3; ++ring)
            g.drawEllipse (centre.x - ring * 7.0f * scale, centre.y - ring * 7.0f * scale,
                           ring * 14.0f * scale, ring * 14.0f * scale, 1.1f * scale);
    }
    else if (effect == 2) // chorus
    {
        for (int wave = -1; wave <= 1; ++wave)
        {
            juce::Path path;
            path.startNewSubPath (centre.x - 18.0f * scale, centre.y + wave * 7.0f * scale);
            path.cubicTo (centre.x - 8.0f * scale, centre.y - 8.0f * scale + wave * 7.0f * scale,
                          centre.x + 7.0f * scale, centre.y + 8.0f * scale + wave * 7.0f * scale,
                          centre.x + 18.0f * scale, centre.y + wave * 7.0f * scale);
            g.strokePath (path, line);
        }
    }
    else if (effect == 3) // phaser
    {
        g.drawEllipse (centre.x - 17.0f * scale, centre.y - 17.0f * scale, 34.0f * scale, 34.0f * scale, 1.0f * scale);
        g.drawLine (centre.x - 17.0f * scale, centre.y, centre.x + 17.0f * scale, centre.y, 1.0f * scale);
    }
    else if (effect == 5) // EQ / drive dial
    {
        g.drawEllipse (centre.x - 20.0f * scale, centre.y - 20.0f * scale, 40.0f * scale, 40.0f * scale, 1.1f * scale);
        for (int tick = 0; tick < 10; ++tick)
        {
            const auto angle = -2.3f + (float) tick * 0.51f;
            const auto a = centre + juce::Point<float> (std::cos (angle), std::sin (angle)) * (13.0f * scale);
            const auto b = centre + juce::Point<float> (std::cos (angle), std::sin (angle)) * (16.0f * scale);
            g.drawLine (a.x, a.y, b.x, b.y, 1.2f * scale);
        }
        g.fillEllipse (centre.x - 2.0f * scale, centre.y - 2.0f * scale, 4.0f * scale, 4.0f * scale);
    }
    else // flutter
    {
        for (int i = 0; i < 5; ++i)
            g.drawLine (centre.x - 16.0f * scale + i * 8.0f * scale, centre.y - 14.0f * scale,
                        centre.x - 16.0f * scale + i * 8.0f * scale, centre.y + 14.0f * scale, 1.1f * scale);
    }
}
}

void FrameworksLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                               float position, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (10.0f);
    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto dial = bounds.withSizeKeepingCentre (diameter, diameter);
    const auto centre = dial.getCentre();
    const auto radius = diameter * 0.5f;
    const auto angle = startAngle + position * (endAngle - startAngle);
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId).isTransparent()
                          ? juce::Colour (0xffeb6c2a)
                          : slider.findColour (juce::Slider::rotarySliderFillColourId);

    // Flat and deliberately quiet: no rings, tick marks, gradients or shadows.
    // Just seven widely spaced dots give the control some rhythm without clutter.
    for (int dot = 0; dot < 7; ++dot)
    {
        const auto dotAngle = startAngle + (float) dot / 6.0f * (endAngle - startAngle);
        const auto point = centre + juce::Point<float> (std::cos (dotAngle), std::sin (dotAngle)) * (radius * 1.18f);
        g.setColour (juce::Colour (0xff77736d).withAlpha (0.52f));
        g.fillEllipse (point.x - 1.25f, point.y - 1.25f, 2.5f, 2.5f);
    }
    g.setColour (juce::Colour (0xff3d3d3d));
    g.fillEllipse (dial);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (dial.reduced (0.5f), 0.8f);
    const auto markerStart = centre + juce::Point<float> (std::cos (angle), std::sin (angle)) * (radius * 0.30f);
    const auto markerEnd = centre + juce::Point<float> (std::cos (angle), std::sin (angle)) * (radius * 0.75f);
    g.setColour (accent);
    g.drawLine ({ markerStart, markerEnd }, juce::jmax (1.5f, radius * 0.055f));
}

void TextureLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float position, float startAngle, float endAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (10.0f);
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto dial = bounds.withSizeKeepingCentre (size, size);
    const auto centre = dial.getCentre();
    const auto radius = size * 0.5f;
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto angle = startAngle + position * (endAngle - startAngle);

    for (int dot = 0; dot < 7; ++dot)
    {
        const auto dotAngle = startAngle + (float) dot / 6.0f * (endAngle - startAngle);
        const auto point = centre + juce::Point<float> (std::cos (dotAngle), std::sin (dotAngle)) * (radius * 1.18f);
        g.setColour (juce::Colour (0xffa7a39c).withAlpha (0.45f));
        g.fillEllipse (point.x - 1.25f, point.y - 1.25f, 2.5f, 2.5f);
    }
    g.setColour (juce::Colour (0xff3d3d3d));
    g.fillEllipse (dial);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (dial.reduced (0.5f), 0.8f);
    const auto markerStart = centre + juce::Point<float> (std::cos (angle), std::sin (angle)) * (radius * 0.30f);
    const auto markerEnd = centre + juce::Point<float> (std::cos (angle), std::sin (angle)) * (radius * 0.75f);
    g.setColour (accent);
    g.drawLine ({ markerStart, markerEnd }, juce::jmax (1.5f, radius * 0.055f));
}

EffectPatchMenu::EffectPatchMenu (FrameworksLookAndFeel& newLookAndFeel, std::function<void (int)> onEffectChosen, int active)
    : lookAndFeel (newLookAndFeel), chooseEffect (std::move (onEffectChosen)), activeEffect (active)
{
    setSize (260, 126);
    for (int effect = 0; effect < (int) effectButtons.size(); ++effect)
    {
        auto& button = effectButtons[(size_t) effect];
        button.setLookAndFeel (&lookAndFeel);
        button.setName ("effectPickerItem");
        button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff171717));
        const int effectID = effect == 0 ? 5 : 6;
        button.onClick = [this, effectID]
        {
            if (chooseEffect) chooseEffect (effectID);
        };
        addAndMakeVisible (button);
    }
    setActiveEffect (activeEffect);
}

void EffectPatchMenu::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    // The small centred arrow visually ties the open selector to its card.
    juce::Path pointer;
    const auto pointerX = (float) getWidth() * 0.50f;
    pointer.startNewSubPath (pointerX - 13.0f, 0.0f);
    pointer.lineTo (pointerX, -15.0f);
    pointer.lineTo (pointerX + 13.0f, 0.0f);
    g.setColour (juce::Colour (0xfff2f1ed));
    g.fillRoundedRectangle (bounds, 11.0f);
    g.fillPath (pointer);
    g.setColour (juce::Colour (0xff252525));
    g.drawRoundedRectangle (bounds.reduced (0.7f), 11.0f, 1.0f);
    g.strokePath (pointer, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colour (0xff202020));
    g.setFont (juce::Font ("Aniko", 10.0f, juce::Font::plain));
    g.drawText ("SELECT MODULE", 16, 8, 180, 22, juce::Justification::centredLeft);
}

void EffectPatchMenu::resized()
{
    auto list = getLocalBounds().reduced (12, 10).withTrimmedTop (24);
    const auto itemWidth = list.getWidth() / (int) effectButtons.size();
    for (auto& button : effectButtons)
        button.setBounds (list.removeFromLeft (itemWidth).reduced (4, 0));
}

void EffectPatchMenu::paintOverChildren (juce::Graphics& g)
{
    const std::array<juce::String, 2> labels { "EQ", "FLUTTER" };
    for (int effect = 0; effect < (int) effectButtons.size(); ++effect)
    {
        const auto bounds = effectButtons[(size_t) effect].getBounds().toFloat();
        const auto centre = juce::Point<float> (bounds.getCentreX(), bounds.getY() + 32.0f);
        const int effectID = effect == 0 ? 5 : 6;
        const auto selected = effectID == activeEffect;
        drawModuleIcon (g, effectID, centre, selected ? juce::Colours::white : juce::Colour (0xff202020), 0.82f);
        g.setFont (juce::Font ("Aniko", 9.0f, juce::Font::plain));
        g.drawText (labels[(size_t) effect], bounds.withY (bounds.getBottom() - 24.0f).withHeight (16.0f), juce::Justification::centred);
    }
}

void EffectPatchMenu::setActiveEffect (int effect)
{
    activeEffect = juce::jlimit (0, 6, effect);
    for (int index = 0; index < (int) effectButtons.size(); ++index)
    {
        auto& button = effectButtons[(size_t) index];
        const int effectID = index == 0 ? 5 : 6;
        button.setToggleState (effectID == activeEffect, juce::dontSendNotification);
        button.setColour (juce::TextButton::textColourOffId, effectID == activeEffect ? juce::Colours::white : juce::Colour (0xff202020));
    }
    repaint();
}

void FrameworksLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                                                    bool highlighted, bool down)
{
    // FX cards paint their own single border in the editor. This button is
    // only the hit target, so it must not add a second default JUCE outline.
    if (button.getName() == "fxSlotHitArea")
        return;

    auto bounds = button.getLocalBounds().toFloat().reduced (0.8f);
    if (button.getName() == "fxSlotToggle")
    {
        const auto stateColour = button.getToggleState() ? juce::Colour (0xff38b46a) : juce::Colour (0xffdf5b5b);
        g.setColour (juce::Colour (0xff202020));
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
        g.setColour (stateColour.withAlpha (highlighted ? 1.0f : 0.78f));
        g.drawRoundedRectangle (bounds, bounds.getHeight() * 0.5f, 1.2f);
        g.fillEllipse (bounds.getX() + 4.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        return;
    }
    if (button.getName() == "effectPickerItem")
    {
        const auto item = button.getLocalBounds().toFloat().reduced (0.7f);
        const auto active = button.getToggleState();
        g.setColour (active ? juce::Colour (0xffeb6c2a) : highlighted ? juce::Colour (0xffeceae4) : juce::Colour (0xfff8f7f3));
        g.fillRoundedRectangle (item, 8.0f);
        g.setColour (active ? juce::Colour (0xffeb6c2a) : juce::Colour (0xff77736d));
        g.drawRoundedRectangle (item, 8.0f, active ? 1.1f : 0.8f);
        return;
    }
    const auto isActive = button.getToggleState() || down;
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 2.5f), bounds.getHeight() * 0.5f);
    g.setGradientFill (juce::ColourGradient (isActive ? juce::Colour (0xff343434) : juce::Colour (0xff292929), bounds.getX(), bounds.getY(), juce::Colour (0xff191919), bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
    g.setColour (isActive || highlighted ? juce::Colour (0xffeb6c2a).withAlpha (0.88f) : juce::Colours::white.withAlpha (0.18f));
    g.drawRoundedRectangle (bounds, bounds.getHeight() * 0.5f, isActive ? 1.2f : 0.7f);
}

void FrameworksLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool isMouseOverButton, bool)
{
    if (button.getName() == "fxSlotHitArea" || button.getName() == "effectPickerItem")
        return;

    if (button.getName() != "textureIcon")
    {
        LookAndFeel_V4::drawButtonText (g, button, isMouseOverButton, false);
        return;
    }

    // A tiny OP-1-inspired modulation patch: a curve, terminals, and coloured control points.
    const auto bounds = button.getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();
    const auto orange = juce::Colour (0xffeb6c2a).withAlpha (isMouseOverButton ? 1.0f : 0.86f);
    const auto icon = juce::Rectangle<float> (centre.x - 16.0f, centre.y - 12.0f, 32.0f, 24.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    for (int x = 0; x < 4; ++x)
        g.drawVerticalLine ((int) icon.getX() + x * 10, icon.getY(), icon.getBottom());
    g.setColour (orange.withAlpha (0.28f));
    g.drawRoundedRectangle (icon, 4.0f, 0.8f);
    juce::Path patch;
    patch.startNewSubPath (icon.getX() + 3.0f, icon.getCentreY() + 5.0f);
    patch.cubicTo (icon.getX() + 8.0f, icon.getCentreY() - 9.0f,
                   icon.getX() + 16.0f, icon.getCentreY() + 9.0f,
                   icon.getRight() - 3.0f, icon.getCentreY() - 4.0f);
    g.setColour (orange);
    g.strokePath (patch, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const std::array<juce::Colour, 3> nodeColours { juce::Colour (0xffeb6c2a), juce::Colour (0xff7a8cff), juce::Colour (0xff42d78b) };
    const std::array<juce::Point<float>, 3> nodes { juce::Point<float> (icon.getX() + 7.0f, icon.getCentreY() - 3.0f), juce::Point<float> (icon.getCentreX(), icon.getCentreY() + 2.0f), juce::Point<float> (icon.getRight() - 7.0f, icon.getCentreY() - 3.0f) };
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        g.setColour (nodeColours[i].withAlpha (0.22f)); g.fillEllipse (nodes[i].x - 4.2f, nodes[i].y - 4.2f, 8.4f, 8.4f);
        g.setColour (nodeColours[i]); g.fillEllipse (nodes[i].x - 2.2f, nodes[i].y - 2.2f, 4.4f, 4.4f);
    }
}

void FrameworksLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image& cachedImage)
{
    // The custom picker paints its own complete surface; suppress JUCE's dark callout shell.
    cachedImage = {};
}

int FrameworksLookAndFeel::getCallOutBoxBorderSize (const juce::CallOutBox&) { return 0; }
float FrameworksLookAndFeel::getCallOutBoxCornerSize (const juce::CallOutBox&) { return 0.0f; }

juce::Font FrameworksLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    const auto height = button.getName() == "fxSlotToggle" ? 12.0f : (float) buttonHeight * 0.48f;
    return juce::Font ("Aniko", height, juce::Font::plain);
}

juce::Font FrameworksLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font ("Aniko", 14.0f, juce::Font::plain);
}

juce::Font FrameworksLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font ("Aniko", 12.0f, juce::Font::plain);
}

void FrameworksLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
    {
        const auto bounds = label.getLocalBounds().toFloat().reduced (1.0f, 1.0f);
        g.setColour (juce::Colour (0xffc2541f));
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (juce::Colour (0xfff2f1ed));
        g.setFont (juce::Font ("Aniko", juce::jmin (13.0f, bounds.getHeight() * 0.62f), juce::Font::plain));
        g.drawFittedText (label.getText(), label.getLocalBounds().reduced (5, 0), juce::Justification::centred, 1);
        return;
    }

    LookAndFeel_V4::drawLabel (g, label);
}

void FrameworksLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.8f);
    g.setColour (juce::Colours::black.withAlpha (0.10f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 2.0f), 5.0f);
    g.setColour (juce::Colour (0xff15191b));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (juce::Colour (0xffeb6c2a));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
    juce::Path arrow;
    const auto cx = (float) (buttonX + buttonW / 2), cy = (float) (buttonY + buttonH / 2);
    arrow.startNewSubPath (cx - 5.0f, cy - 2.0f); arrow.lineTo (cx + 5.0f, cy - 2.0f); arrow.lineTo (cx, cy + 3.5f); arrow.closeSubPath();
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

EnvelopePad::EnvelopePad (juce::AudioProcessorValueTreeState& state, OneShotSamplerAudioProcessor& processor)
    : parameters (state), audioProcessor (processor)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (30);
}

std::array<juce::Point<float>, 4> EnvelopePad::getHandles() const
{
    const auto area = getLocalBounds().toFloat().reduced (28.0f, 32.0f);
    const auto attack = parameters.getRawParameterValue ("attack")->load();
    const auto decay = parameters.getRawParameterValue ("decay")->load();
    const auto sustain = parameters.getRawParameterValue ("sustain")->load();
    const auto release = parameters.getRawParameterValue ("release")->load();
    const auto peakX = area.getX() + area.getWidth() * (0.08f + 0.26f * juce::jlimit (0.0f, 1.0f, attack / 3.0f));
    const auto decayEndX = juce::jmin (area.getRight() - 95.0f, peakX + area.getWidth() * (0.08f + 0.26f * juce::jlimit (0.0f, 1.0f, decay / 3.0f)));
    const auto releaseStartX = juce::jmax (decayEndX + 45.0f, area.getRight() - area.getWidth() * (0.10f + 0.30f * juce::jlimit (0.0f, 1.0f, release / 5.0f)));
    const auto sustainY = area.getBottom() - area.getHeight() * (0.12f + sustain * 0.70f);
    return { juce::Point<float> (peakX, area.getY()), juce::Point<float> (decayEndX, sustainY),
             juce::Point<float> (releaseStartX, sustainY), juce::Point<float> (area.getRight(), area.getBottom()) };
}

void EnvelopePad::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (28.0f, 32.0f);
    const auto handles = getHandles();
    g.setColour (juce::Colour (0xffe7e5df));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
    g.setColour (juce::Colour (0xff8e8b84).withAlpha (0.58f));
    g.drawRoundedRectangle (getLocalBounds().toFloat(), 10.0f, 0.9f);

    // Keep the Frameworks dot field, but let it sit behind the controls.
    g.setColour (juce::Colour (0xffaaa8a2).withAlpha (0.48f));
    for (int x = static_cast<int> (area.getX()); x < area.getRight(); x += 30)
        g.drawVerticalLine (x, area.getY(), area.getBottom());
    for (int y = static_cast<int> (area.getY()); y < area.getBottom(); y += 30)
        g.drawHorizontalLine (y, area.getX(), area.getRight());

    juce::Path envelope;
    envelope.startNewSubPath (area.getX(), area.getBottom());
    envelope.cubicTo (area.getX() + (handles[0].x - area.getX()) * 0.45f, area.getBottom(),
                      handles[0].x - 12.0f, handles[0].y + 8.0f, handles[0].x, handles[0].y);
    // Short peak hold, then a smooth decay: the familiar synth-envelope shape.
    envelope.lineTo (handles[0].x + 18.0f, handles[0].y);
    envelope.cubicTo (handles[0].x + 48.0f, handles[0].y, handles[1].x - 22.0f, handles[1].y, handles[1].x, handles[1].y);
    envelope.lineTo (handles[2]);
    envelope.cubicTo (handles[2].x + (handles[3].x - handles[2].x) * 0.42f, handles[2].y,
                      handles[3].x - 12.0f, handles[3].y - 10.0f, handles[3].x, handles[3].y);
    auto envelopeFill = envelope;
    envelopeFill.lineTo (area.getX(), area.getBottom());
    envelopeFill.closeSubPath();
    g.setColour (juce::Colour (0xffeb6c2a).withAlpha (0.09f));
    g.fillPath (envelopeFill);
    g.setColour (juce::Colour (0xffeb6c2a));
    g.strokePath (envelope, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (int i = 0; i < 3; ++i)
    {
        g.setColour (juce::Colour (0xff202020));
        g.fillEllipse (handles[(size_t) i].getX() - 7.0f, handles[(size_t) i].getY() - 7.0f, 14.0f, 14.0f);
        g.setColour (juce::Colour (0xffeb6c2a));
        g.fillEllipse (handles[(size_t) i].getX() - 4.0f, handles[(size_t) i].getY() - 4.0f, 8.0f, 8.0f);
    }

    g.setColour (juce::Colour (0xff171717));
    g.setFont (juce::Font ("Aniko", 11.0f, juce::Font::plain));
    // Leave a clear lane for the first envelope handle so it never sits on the title.
    g.drawText ("ENVELOPE  /  DRAG THE POINTS TO SHAPE", getLocalBounds().reduced (14).withTrimmedLeft (40).removeFromTop (18), juce::Justification::centredLeft);
}

void EnvelopePad::setParameter (const char* id, float value)
{
    if (auto* parameter = parameters.getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void EnvelopePad::mouseDown (const juce::MouseEvent& event)
{
    const auto handles = getHandles();
    draggedHandle = -1;
    auto closestDistance = 24.0f;
    for (int i = 0; i < 3; ++i)
        if (const auto distance = event.position.getDistanceFrom (handles[(size_t) i]); distance < closestDistance)
        {
            closestDistance = distance;
            draggedHandle = i;
        }
}

void EnvelopePad::mouseDrag (const juce::MouseEvent& event)
{
    if (draggedHandle < 0)
        return;
    const auto area = getLocalBounds().toFloat().reduced (28.0f, 32.0f);
    const auto handles = getHandles();
    const auto x = juce::jlimit (area.getX(), area.getRight(), event.position.x);
    const auto y = juce::jlimit (area.getY(), area.getBottom(), event.position.y);
    if (draggedHandle == 0)
        setParameter ("attack", juce::jlimit (0.001f, 3.0f, ((x - area.getX()) / area.getWidth() - 0.08f) / 0.26f * 3.0f));
    else if (draggedHandle == 1)
    {
        setParameter ("decay", juce::jlimit (0.001f, 3.0f, ((x - handles[0].x) / area.getWidth() - 0.08f) / 0.26f * 3.0f));
        setParameter ("sustain", juce::jlimit (0.0f, 1.0f, ((area.getBottom() - y) / area.getHeight() - 0.12f) / 0.70f));
    }
    else if (draggedHandle == 2)
        setParameter ("release", juce::jlimit (0.001f, 5.0f, ((area.getRight() - x) / area.getWidth() - 0.10f) / 0.30f * 5.0f));
}

void FxSlotButton::mouseDrag (const juce::MouseEvent& event)
{
    juce::TextButton::mouseDrag (event);
    if (event.getDistanceFromDragStart() > 6)
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! container->isDragAndDropActive())
                container->startDragging (juce::var (slot), this);
}

void EnvelopePad::timerCallback()
{
    // Keeps parameter-driven changes visibly smooth without a fake playback marker.
    repaint();
}

std::array<juce::Point<float>, 7> EqCurvePad::getHandles() const
{
    const auto area = getLocalBounds().reduced (12).toFloat();
    std::array<juce::Point<float>, 7> handles;
    for (int band = 0; band < 7; ++band)
    {
        const auto prefix = "eqBand" + juce::String (band + 1);
        const auto frequency = parameters.getRawParameterValue (prefix + "Freq")->load();
        const auto response = getResponseAtFrequency (frequency);
        handles[(size_t) band] = { area.getX() + area.getWidth() * positionFromFrequency (frequency),
                                    area.getCentreY() - juce::jlimit (-24.0f, 24.0f, response) / 24.0f * area.getHeight() * 0.40f };
    }
    return handles;
}

float EqCurvePad::getResponseAtFrequency (float frequency) const
{
    const auto sampleRate = juce::jmax (8000.0, audioProcessor.getSampleRate());
    const auto safeFrequency = juce::jlimit (eqMinFrequency, (float) sampleRate * 0.49f, frequency);
    float responseDb = 0.0f;
    for (int band = 0; band < 7; ++band)
    {
        const auto prefix = "eqBand" + juce::String (band + 1);
        const auto bandFrequency = parameters.getRawParameterValue (prefix + "Freq")->load();
        const auto q = parameters.getRawParameterValue (prefix + "Q")->load();
        const auto gain = parameters.getRawParameterValue (prefix + "Gain")->load();
        const auto type = (int) parameters.state.getProperty (prefix + "Type", band == 0 ? 4 : band == 6 ? 5 : 0);
        if (const auto coefficients = makeEqCoefficients (sampleRate, bandFrequency, q, gain, type))
            responseDb += juce::Decibels::gainToDecibels (juce::jmax (0.000001f, (float) coefficients->getMagnitudeForFrequency (safeFrequency, sampleRate)));
    }
    return juce::jlimit (-60.0f, 36.0f, responseDb);
}

void EqCurvePad::setBand (int band, float frequency, float gain)
{
    const auto prefix = "eqBand" + juce::String (band + 1);
    if (auto* parameter = parameters.getParameter (prefix + "Freq")) parameter->setValueNotifyingHost (parameter->convertTo0to1 (frequency));
    if (auto* parameter = parameters.getParameter (prefix + "Gain")) parameter->setValueNotifyingHost (parameter->convertTo0to1 (gain));
}

void EqCurvePad::mouseDown (const juce::MouseEvent& event)
{
    const auto handles = getHandles();
    draggedBand = 0;
    for (int band = 1; band < 7; ++band)
        if (event.position.getDistanceFrom (handles[(size_t) band]) < event.position.getDistanceFrom (handles[(size_t) draggedBand])) draggedBand = band;
    const auto distanceToHandle = event.position.getDistanceFrom (handles[(size_t) draggedBand]);

    // A click beside a band must never hijack and snap it. The pad only
    // captures deliberate grabs on a handle, with a tiny movement grace.
    if (distanceToHandle > 22.0f)
    {
        draggedBand = -1;
        return;
    }

    if (event.mods.isRightButtonDown())
    {
        juce::PopupMenu menu;
        menu.addItem (1, "BELL"); menu.addItem (2, "LOW PASS"); menu.addItem (3, "HIGH PASS / LOW CUT");
        menu.addItem (4, "NOTCH"); menu.addItem (5, "LOW SHELF"); menu.addItem (6, "HIGH SHELF");
        menu.addSeparator(); menu.addItem (20, "RESET THIS BAND");
        menu.addSeparator(); menu.addItem (30, "frwk_eq001"); menu.addItem (31, "frwk_eq002");
        menu.addItem (32, "frwk_eq003"); menu.addItem (33, "frwk_eq004");
        const auto property = "eqBand" + juce::String (draggedBand + 1) + "Type";
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, property, band = draggedBand] (int choice)
        {
            const auto set = [this] (const juce::String& id, float value) { if (auto* p = parameters.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value)); };
            if (choice >= 1 && choice <= 6) parameters.state.setProperty (property, choice - 1, nullptr);
            if (choice == 20) { const auto prefix = "eqBand" + juce::String (band + 1); set (prefix + "Gain", 0.0f); set (prefix + "Q", 0.8f); parameters.state.setProperty (property, 0, nullptr); }
            if (choice >= 30)
            {
                const std::array<float, 7> frequencies { 60, 170, 450, 1100, 2800, 7000, 14000 };
                const std::array<float, 7> warmth { 2.5f, 1.5f, 0.5f, -0.5f, -1.0f, 0.0f, 1.5f };
                const std::array<float, 7> air { -1.0f, -0.5f, 0.0f, 0.5f, 1.5f, 3.0f, 4.5f };
                for (int i = 0; i < 7; ++i) { const auto prefix = "eqBand" + juce::String (i + 1); set (prefix + "Freq", frequencies[(size_t) i]); set (prefix + "Gain", choice == 32 ? warmth[(size_t) i] : choice == 33 ? air[(size_t) i] : 0.0f); set (prefix + "Q", 0.8f); parameters.state.setProperty (prefix + "Type", 0, nullptr); }
                if (choice == 31) { parameters.state.setProperty ("eqBand1Type", 2, nullptr); set ("eqBand1Freq", 90.0f); }
            }
            repaint();
        });
        return;
    }
    dragStart = event.position;
    dragHasMoved = false;
}

void EqCurvePad::mouseDrag (const juce::MouseEvent& event)
{
    if (draggedBand < 0) return;
    if (! dragHasMoved)
    {
        if (event.position.getDistanceFrom (dragStart) < 2.5f)
            return;
        dragHasMoved = true;
    }
    const auto area = getLocalBounds().reduced (12).toFloat();
    const auto x = juce::jlimit (0.0f, 1.0f, (event.position.x - area.getX()) / area.getWidth());
    const auto frequency = frequencyFromPosition (x);
    const auto gain = juce::jlimit (-18.0f, 18.0f, (area.getCentreY() - event.position.y) / (area.getHeight() * 0.38f) * 18.0f);
    setBand (draggedBand, frequency, gain);
    repaint();
}

void EqCurvePad::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto handles = getHandles();
    int band = 0;
    for (int i = 1; i < 7; ++i)
        if (event.position.getDistanceFrom (handles[(size_t) i]) < event.position.getDistanceFrom (handles[(size_t) band])) band = i;
    if (event.position.getDistanceFrom (handles[(size_t) band]) > 26.0f)
        return;
    const auto prefix = "eqBand" + juce::String (band + 1);
    if (auto* parameter = parameters.getParameter (prefix + "Q"))
    {
        const auto current = parameter->convertFrom0to1 (parameter->getValue());
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (juce::jlimit (0.2f, 8.0f, current + wheel.deltaY * 0.55f)));
    }
    repaint();
}

void EqCurvePad::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().reduced (12).toFloat();
    g.setColour (juce::Colour (0xffdad7d0)); g.fillRoundedRectangle (area, 10.0f);
    g.setColour (juce::Colour (0xffbdbab3)); g.drawRoundedRectangle (area, 10.0f, 1.0f);
    const std::array<juce::String, 8> markers { "20", "60", "200", "600", "2k", "6k", "12k", "20k" };
    const std::array<float, 8> markerFrequencies { 20.0f, 60.0f, 200.0f, 600.0f, 2000.0f, 6000.0f, 12000.0f, 20000.0f };
    for (int i = 0; i < 8; ++i)
    {
        const auto x = area.getX() + area.getWidth() * positionFromFrequency (markerFrequencies[(size_t) i]);
        g.setColour (juce::Colour (0xffa9a69f).withAlpha (0.45f));
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        g.setColour (juce::Colour (0xff77736d)); g.setFont (juce::Font ("Aniko", 9.0f, juce::Font::plain));
        g.drawText (markers[(size_t) i], (int) x - 16, (int) area.getY() + 4, 32, 12, juce::Justification::centred);
    }
    g.setColour (juce::Colour (0xffa9a69f).withAlpha (0.4f));
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
    const auto handles = getHandles();
    const std::array<juce::Colour, 7> colours { juce::Colour (0xff38b46a), juce::Colour (0xff4e9bd7), juce::Colour (0xff8b62c9), juce::Colour (0xffeb6c2a), juce::Colour (0xffd95c8b), juce::Colour (0xff5d78cc), juce::Colour (0xff65a879) };
    // Use JUCE's exact IIR coefficient response, matching the audio DSP itself.
    juce::Path curve;
    for (int pixel = 0; pixel < (int) area.getWidth(); pixel += 2)
    {
        const auto x = area.getX() + (float) pixel;
        const auto frequency = frequencyFromPosition ((x - area.getX()) / area.getWidth());
        const auto response = getResponseAtFrequency (frequency);
        const auto y = area.getCentreY() - juce::jlimit (-24.0f, 24.0f, response) / 24.0f * area.getHeight() * 0.40f;
        if (pixel == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    g.setColour (juce::Colour (0xffeb6c2a)); g.strokePath (curve, juce::PathStrokeType (3.0f));
    g.setColour (juce::Colour (0xff77736d)); g.setFont (juce::Font ("Aniko", 10.0f, juce::Font::plain));
    g.drawText ("20 Hz", area.getX() + 4.0f, area.getBottom() - 15.0f, 50.0f, 12.0f, juce::Justification::left);
    g.drawText ("20 kHz", area.getRight() - 54.0f, area.getBottom() - 15.0f, 50.0f, 12.0f, juce::Justification::right);
    for (int band = 0; band < 7; ++band) { const auto point = handles[(size_t) band]; const auto prefix = "eqBand" + juce::String (band + 1); const auto width = parameters.getRawParameterValue (prefix + "Q")->load(); g.setColour (colours[(size_t) band].withAlpha (0.14f)); g.fillEllipse (point.x - (22.0f / width), point.y - 14.0f, 44.0f / width, 28.0f); g.setColour (juce::Colour (0xff252525)); g.fillEllipse (point.x - 9.0f, point.y - 9.0f, 18.0f, 18.0f); g.setColour (colours[(size_t) band]); g.fillEllipse (point.x - 4.0f, point.y - 4.0f, 8.0f, 8.0f); }
}

OneShotSamplerAudioProcessorEditor::OneShotSamplerAudioProcessorEditor (OneShotSamplerAudioProcessor& processor)
    : AudioProcessorEditor (&processor), audioProcessor (processor), envelopePad (processor.parameters, processor), eqCurvePad (processor.parameters, processor)
{
    int badgeDataSize = 0;
    if (const auto* badgeData = BinaryData::getNamedResource ("modulartop_png", badgeDataSize))
        modularBadge = juce::ImageFileFormat::loadFrom (badgeData, (size_t) badgeDataSize);
    if (const auto* markData = BinaryData::getNamedResource ("fmwnobgdarkgray_png", badgeDataSize))
        darkFrameworksMark = juce::ImageFileFormat::loadFrom (markData, (size_t) badgeDataSize);
    const auto setupControl = [this] (juce::Slider& slider, juce::Label& label, const juce::String& text)
    {
        slider.setLookAndFeel (&frameworksLookAndFeel);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 21);
        slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xfff2f1ed));
        // Keep readouts printed directly on the panel — no pill/badge behind
        // the value, so the controls stay light and instrument-like.
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xff202020));
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (slider);
        label.setText (text, juce::dontSendNotification);
        label.setLookAndFeel (&frameworksLookAndFeel);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colour (0xff292929));
        label.setFont (juce::Font ("Aniko", 11.0f, juce::Font::plain));
        addAndMakeVisible (label);
    };

    setupControl (outputGainSlider, outputGainLabel, "OUTPUT GAIN");
    outputGainSlider.setName ("outputGain");
    outputGainSlider.setTextValueSuffix (" dB");

    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.parameters, "outputGain", outputGainSlider);

    setupControl (attackSlider, attackLabel, "ATTACK");
    setupControl (decaySlider, decayLabel, "DECAY");
    setupControl (sustainSlider, sustainLabel, "SUSTAIN");
    setupControl (releaseSlider, releaseLabel, "RELEASE");
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "attack", attackSlider);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "decay", decaySlider);
    sustainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "sustain", sustainSlider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "release", releaseSlider);
    setupControl (delayTimeSlider, delayTimeLabel, "TIME");
    setupControl (delayFeedbackSlider, delayFeedbackLabel, "FEEDBACK");
    setupControl (delayMixSlider, delayMixLabel, "MIX");
    setupControl (reverbMixSlider, reverbMixLabel, "MIX");
    setupControl (reverbRoomSizeSlider, reverbRoomSizeLabel, "ROOM SIZE");
    setupControl (reverbDampingSlider, reverbDampingLabel, "DAMPING");
    setupControl (reverbWidthSlider, reverbWidthLabel, "WIDTH");
    setupControl (reverbFreezeSlider, reverbFreezeLabel, "FREEZE");
    delayTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "delayTime", delayTimeSlider);
    delayFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "delayFeedback", delayFeedbackSlider);
    delayMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "delayMix", delayMixSlider);
    reverbMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "reverbMix", reverbMixSlider);
    reverbRoomSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "reverbRoomSize", reverbRoomSizeSlider);
    reverbDampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "reverbDamping", reverbDampingSlider);
    reverbWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "reverbWidth", reverbWidthSlider);
    reverbFreezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "reverbFreeze", reverbFreezeSlider);

    setupControl (chorusRateSlider, chorusRateLabel, "RATE");
    setupControl (chorusDepthSlider, chorusDepthLabel, "DEPTH");
    setupControl (chorusDelaySlider, chorusDelayLabel, "DELAY");
    setupControl (chorusFeedbackSlider, chorusFeedbackLabel, "FEEDBACK");
    setupControl (chorusMixSlider, chorusMixLabel, "MIX");
    chorusRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "chorusRate", chorusRateSlider);
    chorusDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "chorusDepth", chorusDepthSlider);
    chorusDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "chorusDelay", chorusDelaySlider);
    chorusFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "chorusFeedback", chorusFeedbackSlider);
    chorusMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "chorusMix", chorusMixSlider);

    setupControl (phaserRateSlider, phaserRateLabel, "RATE");
    setupControl (phaserDepthSlider, phaserDepthLabel, "DEPTH");
    setupControl (phaserFeedbackSlider, phaserFeedbackLabel, "FEEDBACK");
    setupControl (phaserMixSlider, phaserMixLabel, "MIX");
    phaserRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "phaserRate", phaserRateSlider);
    phaserDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "phaserDepth", phaserDepthSlider);
    phaserFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "phaserFeedback", phaserFeedbackSlider);
    phaserMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "phaserMix", phaserMixSlider);

    sampleLabel.setJustificationType (juce::Justification::centred);
    sampleLabel.setColour (juce::Label::textColourId, juce::Colour (0xff171717));
    sampleLabel.setText (audioProcessor.getSampleName(), juce::dontSendNotification);
    addAndMakeVisible (sampleLabel);

    loadButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File {}, "*.wav;*.aif;*.aiff");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
                                  {
                                      const auto file = chooser.getResult();
                                      if (file.existsAsFile() && audioProcessor.loadSample (file))
                                      {
                                          sampleLabel.setText (audioProcessor.getSampleName(), juce::dontSendNotification);
                                          repaint();
                                      }
                                  });
    };
    addAndMakeVisible (loadButton);
    loadButton.setLookAndFeel (&frameworksLookAndFeel);
    loadButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffeb6c2a));
    addAndMakeVisible (envelopePad);

    for (auto* tab : { &samplerTab, &fxTab, &textureTab })
    {
        tab->setLookAndFeel (&frameworksLookAndFeel);
        tab->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff292929));
        tab->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffeb6c2a));
        addAndMakeVisible (*tab);
    }
    textureTab.setName ("textureIcon");
    textureTab.setButtonText ({});
    samplerTab.onClick = [this] { showFxPage (false); };
    fxTab.onClick = [this] { showFxPage (true); };
    textureTab.onClick = [this] { showTexturePage(); };
    const auto setupPreset = [this] (juce::ComboBox& box, std::initializer_list<juce::String> names)
    {
        box.setLookAndFeel (&frameworksLookAndFeel);
        int id = 1;
        for (const auto& name : names)
            box.addItem (name, id++);
        box.setSelectedId (1, juce::dontSendNotification);
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff15191b));
        box.setColour (juce::ComboBox::textColourId, juce::Colour (0xffeb6c2a));
        box.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xffeb6c2a));
        box.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xffeb6c2a));
        addAndMakeVisible (box);
    };
    frameworksLookAndFeel.setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff15191b));
    frameworksLookAndFeel.setColour (juce::PopupMenu::textColourId, juce::Colour (0xfff2f1ed));
    frameworksLookAndFeel.setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xffeb6c2a));
    frameworksLookAndFeel.setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff15191b));
    setupPreset (delayPreset, { "frwk_delay001", "frwk_delay002", "frwk_delay003", "frwk_delay004" });
    setupPreset (reverbPreset, { "frwk_reverb001", "frwk_reverb002", "frwk_reverb003", "frwk_reverb004" });
    setupPreset (chorusPreset, { "frwk_chorus001", "frwk_chorus002", "frwk_chorus003", "frwk_chorus004" });
    setupPreset (phaserPreset, { "frwk_phaser001", "frwk_phaser002", "frwk_phaser003", "frwk_phaser004" });

    const auto setParameter = [this] (const char* id, float value)
    {
        if (auto* parameter = audioProcessor.parameters.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };
    delayPreset.onChange = [setParameter, this]
    {
        // Tight, dotted, bloom and long-tail: expressive without washing out the source.
        const float values[][3] = { { 0.125f, 0.24f, 0.16f }, { 0.375f, 0.43f, 0.27f }, { 0.75f, 0.58f, 0.34f }, { 1.50f, 0.68f, 0.31f } };
        const auto& v = values[juce::jlimit (0, 3, delayPreset.getSelectedId() - 1)];
        setParameter ("delayTime", v[0]); setParameter ("delayFeedback", v[1]); setParameter ("delayMix", v[2]);
    };
    reverbPreset.onChange = [setParameter, this]
    {
        // Room, bloom, frozen shimmer and wide hall.
        const float values[][5] = { { 0.20f, 0.64f, 0.72f, 0.0f, 0.16f }, { 0.56f, 0.37f, 0.94f, 0.0f, 0.29f }, { 0.82f, 0.18f, 1.0f, 1.0f, 0.38f }, { 0.92f, 0.30f, 1.0f, 0.0f, 0.42f } };
        const auto& v = values[juce::jlimit (0, 3, reverbPreset.getSelectedId() - 1)];
        setParameter ("reverbRoomSize", v[0]); setParameter ("reverbDamping", v[1]); setParameter ("reverbWidth", v[2]); setParameter ("reverbFreeze", v[3]); setParameter ("reverbMix", v[4]);
    };
    chorusPreset.onChange = [setParameter, this]
    {
        const float values[][5] = { { 0.33f, 0.28f, 6.5f, 0.02f, 0.20f }, { 0.72f, 0.46f, 11.0f, 0.08f, 0.28f }, { 0.16f, 0.66f, 17.0f, 0.14f, 0.36f }, { 1.35f, 0.74f, 21.0f, 0.22f, 0.42f } };
        const auto& v = values[juce::jlimit (0, 3, chorusPreset.getSelectedId() - 1)];
        setParameter ("chorusRate", v[0]); setParameter ("chorusDepth", v[1]); setParameter ("chorusDelay", v[2]); setParameter ("chorusFeedback", v[3]); setParameter ("chorusMix", v[4]);
    };
    phaserPreset.onChange = [setParameter, this]
    {
        const float values[][4] = { { 0.18f, 0.42f, 0.12f, 0.22f }, { 0.38f, 0.62f, 0.30f, 0.34f }, { 0.09f, 0.78f, 0.48f, 0.44f }, { 1.85f, 0.68f, 0.25f, 0.30f } };
        const auto& v = values[juce::jlimit (0, 3, phaserPreset.getSelectedId() - 1)];
        setParameter ("phaserRate", v[0]); setParameter ("phaserDepth", v[1]); setParameter ("phaserFeedback", v[2]); setParameter ("phaserMix", v[3]);
    };
    setupControl (flangerRateSlider, flangerRateLabel, "RATE");
    setupControl (flangerDepthSlider, flangerDepthLabel, "DEPTH");
    setupControl (flangerDelaySlider, flangerDelayLabel, "DELAY");
    setupControl (flangerFeedbackSlider, flangerFeedbackLabel, "FEEDBACK");
    setupControl (flangerMixSlider, flangerMixLabel, "MIX");
    flangerRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "flangerRate", flangerRateSlider);
    flangerDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "flangerDepth", flangerDepthSlider);
    flangerDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "flangerDelay", flangerDelaySlider);
    flangerFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "flangerFeedback", flangerFeedbackSlider);
    flangerMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "flangerMix", flangerMixSlider);
    setupPreset (flangerPreset, { "frwk_flanger001", "frwk_flanger002", "frwk_flanger003", "frwk_flanger004" });
    setupControl (flutterRateSlider, flutterRateLabel, "RATE");
    setupControl (flutterDepthSlider, flutterDepthLabel, "DEPTH");
    setupControl (flutterDelaySlider, flutterDelayLabel, "DELAY");
    setupControl (flutterMixSlider, flutterMixLabel, "MIX");
    flutterRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "flutterRate", flutterRateSlider);
    flutterDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "flutterDepth", flutterDepthSlider);
    flutterDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "flutterDelay", flutterDelaySlider);
    flutterMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "flutterMix", flutterMixSlider);
    setupPreset (flutterPreset, { "frwk_flutter001", "frwk_flutter002", "frwk_flutter003", "frwk_flutter004" });
    for (const auto& pair : { std::pair<juce::Slider*, juce::Label*> { &granularSizeSlider, &granularSizeLabel }, { &granularDensitySlider, &granularDensityLabel }, { &granularPitchSlider, &granularPitchLabel }, { &granularMixSlider, &granularMixLabel }, { &harmonyShiftSlider, &harmonyShiftLabel }, { &harmonyDetuneSlider, &harmonyDetuneLabel }, { &harmonyMixSlider, &harmonyMixLabel }, { &randomAmountSlider, &randomAmountLabel }, { &randomRateSlider, &randomRateLabel }, { &randomMixSlider, &randomMixLabel } }) setupControl (*pair.first, *pair.second, pair.first == &granularSizeSlider ? "SIZE" : pair.first == &granularDensitySlider ? "DENSITY" : pair.first == &granularPitchSlider ? "PITCH" : pair.first == &granularMixSlider || pair.first == &harmonyMixSlider || pair.first == &randomMixSlider ? "MIX" : pair.first == &harmonyShiftSlider ? "WIDTH" : pair.first == &harmonyDetuneSlider ? "MOTION" : pair.first == &randomAmountSlider ? "AMOUNT" : "RATE");
    for (auto* slider : { &granularSizeSlider, &granularDensitySlider, &granularPitchSlider, &granularMixSlider, &harmonyShiftSlider, &harmonyDetuneSlider, &harmonyMixSlider, &randomAmountSlider, &randomRateSlider, &randomMixSlider })
        slider->setLookAndFeel (&textureLookAndFeel);
    granularSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "granularSize", granularSizeSlider); granularDensityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "granularDensity", granularDensitySlider); granularPitchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "granularPitch", granularPitchSlider); granularMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "granularMix", granularMixSlider);
    harmonyShiftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "harmonyShift", harmonyShiftSlider); harmonyDetuneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "harmonyDetune", harmonyDetuneSlider); harmonyMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "harmonyMix", harmonyMixSlider);
    randomAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "randomAmount", randomAmountSlider); randomRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "randomRate", randomRateSlider); randomMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "randomMix", randomMixSlider);
    setupPreset (granularPreset, { "frwk_granular001", "frwk_granular002", "frwk_granular003", "frwk_granular004" }); setupPreset (harmonyPreset, { "frwk_orbit001", "frwk_orbit002", "frwk_orbit003", "frwk_orbit004" }); setupPreset (randomPreset, { "frwk_random001", "frwk_random002", "frwk_random003", "frwk_random004" });
    setupPreset (eqPreset, { "frwk_eq001", "frwk_eq002", "frwk_eq003", "frwk_eq004" });
    eqPreset.onChange = [this]
    {
        const auto set = [this] (const juce::String& id, float value) { if (auto* p = audioProcessor.parameters.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value)); };
        set ("eqEnabled", 1.0f);
        const std::array<float, 7> frequencies { 55, 140, 380, 1050, 2700, 6800, 14500 };
        const std::array<float, 7> warmth { 1.8f, 1.1f, 0.4f, -0.7f, -1.1f, 0.3f, 1.3f };
        const std::array<float, 7> air { -1.5f, -0.8f, -0.2f, 0.4f, 1.2f, 2.6f, 3.8f };
        const auto preset = eqPreset.getSelectedId();
        for (int i = 0; i < 7; ++i) { const auto prefix = "eqBand" + juce::String (i + 1); set (prefix + "Freq", frequencies[(size_t) i]); set (prefix + "Gain", preset == 3 ? warmth[(size_t) i] : preset == 4 ? air[(size_t) i] : 0.0f); set (prefix + "Q", i == 0 || i == 6 ? 0.65f : 0.9f); audioProcessor.parameters.state.setProperty (prefix + "Type", i == 0 ? 4 : i == 6 ? 5 : 0, nullptr); }
        if (preset == 2) { audioProcessor.parameters.state.setProperty ("eqBand1Type", 2, nullptr); set ("eqBand1Freq", 95.0f); set ("eqBand1Q", 0.72f); }
        eqCurvePad.repaint();
    };
    flangerPreset.onChange = [setParameter, this]
    {
        const float values[][5] = { { 0.20f, 0.38f, 2.8f, 0.18f, 0.20f }, { 0.11f, 0.52f, 5.5f, 0.32f, 0.28f }, { 0.07f, 0.72f, 9.0f, 0.48f, 0.36f }, { 0.55f, 0.62f, 4.0f, 0.14f, 0.25f } };
        const auto& v = values[juce::jlimit (0, 3, flangerPreset.getSelectedId() - 1)];
        setParameter ("flangerRate", v[0]); setParameter ("flangerDepth", v[1]); setParameter ("flangerDelay", v[2]); setParameter ("flangerFeedback", v[3]); setParameter ("flangerMix", v[4]);
    };
    flutterPreset.onChange = [setParameter, this]
    {
        const float values[][4] = { { 1.8f, 0.14f, 1.2f, 0.16f }, { 3.8f, 0.24f, 1.8f, 0.24f }, { 6.5f, 0.42f, 2.7f, 0.30f }, { 0.75f, 0.32f, 4.8f, 0.22f } };
        const auto& v = values[juce::jlimit (0, 3, flutterPreset.getSelectedId() - 1)];
        setParameter ("flutterRate", v[0]); setParameter ("flutterDepth", v[1]); setParameter ("flutterDelay", v[2]); setParameter ("flutterMix", v[3]);
    };
    granularPreset.onChange = [setParameter, this] { const float v[][4] { { 185, 5, 0, .24f }, { 95, 11, 5, .32f }, { 340, 2.5f, -12, .40f }, { 62, 17, 12, .30f } }; const auto& p = v[juce::jlimit (0, 3, granularPreset.getSelectedId() - 1)]; setParameter ("granularSize", p[0]); setParameter ("granularDensity", p[1]); setParameter ("granularPitch", p[2]); setParameter ("granularMix", p[3]); };
    harmonyPreset.onChange = [setParameter, this] { const float v[][3] { { -5, 5, .18f }, { 2, 11, .24f }, { 8, 17, .30f }, { 12, 28, .36f } }; const auto& p = v[juce::jlimit (0, 3, harmonyPreset.getSelectedId() - 1)]; setParameter ("harmonyShift", p[0]); setParameter ("harmonyDetune", p[1]); setParameter ("harmonyMix", p[2]); };
    randomPreset.onChange = [setParameter, this] { const float v[][3] { { .12f, 2, .12f }, { .30f, 5, .20f }, { .55f, 9, .28f }, { .82f, 15, .34f } }; const auto& p = v[juce::jlimit (0, 3, randomPreset.getSelectedId() - 1)]; setParameter ("randomAmount", p[0]); setParameter ("randomRate", p[1]); setParameter ("randomMix", p[2]); };

    for (auto* slot : { &fxSlotOne, &fxSlotTwo, &fxSlotThree, &fxSlotFour, &fxSlotFive, &fxSlotSix, &fxSlotSeven })
    {
        slot->setLookAndFeel (&frameworksLookAndFeel);
        slot->setName ("fxSlotHitArea");
        slot->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        slot->setColour (juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
        addAndMakeVisible (*slot);
    }
    // Five focused cards and one add marker form the visual rack. The full
    // seven-effect selector below gives every actual module equal access.
    const auto selectAndToggle = [this] (int effect)
    {
        selectEffect (effect);
        toggleEffect (effect);
    };
    fxSlotOne.onClick = [selectAndToggle] { selectAndToggle (0); };
    fxSlotTwo.onClick = [selectAndToggle] { selectAndToggle (1); };
    fxSlotThree.onClick = [selectAndToggle] { selectAndToggle (2); };
    fxSlotFour.onClick = [selectAndToggle] { selectAndToggle (3); };
    fxSlotFive.onClick = [selectAndToggle] { selectAndToggle (4); };
    fxSlotSix.onClick = [this] { if (effectPickerMenu != nullptr) effectPickerMenu->setVisible (! effectPickerMenu->isVisible()); };
    fxSlotSeven.onClick = [selectAndToggle] { selectAndToggle (6); };
    for (auto* toggle : { &fxSlotToggleOne, &fxSlotToggleTwo, &fxSlotToggleThree, &fxSlotToggleFour, &fxSlotToggleFive, &fxSlotToggleSix, &fxSlotToggleSeven })
    {
        toggle->setLookAndFeel (&frameworksLookAndFeel);
        toggle->setName ("fxSlotToggle");
        toggle->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffeb6c2a));
        addAndMakeVisible (*toggle);
    }
    fxSlotToggleOne.onClick = [this] { setSlotEnabled (0, ! fxSlotEnabled[0]); };
    fxSlotToggleTwo.onClick = [this] { setSlotEnabled (1, ! fxSlotEnabled[1]); };
    fxSlotToggleThree.onClick = [this] { setSlotEnabled (2, ! fxSlotEnabled[2]); };
    fxSlotToggleFour.onClick = [this] { setSlotEnabled (3, ! fxSlotEnabled[3]); };
    fxSlotToggleFive.onClick = [this] { setSlotEnabled (4, ! fxSlotEnabled[4]); };
    fxSlotToggleSix.onClick = [this] { setSlotEnabled (5, ! fxSlotEnabled[5]); };
    fxSlotToggleSeven.onClick = [this] { setSlotEnabled (6, ! fxSlotEnabled[6]); };

    const auto setupSwitch = [this] (juce::TextButton& button, const char* parameterID, std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& attachment)
    {
        button.setLookAndFeel (&frameworksLookAndFeel);
        button.setClickingTogglesState (true);
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff202020));
        button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff292929));
        button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffeb6c2a));
        button.setColour (juce::TextButton::textColourOnId, juce::Colour (0xffeb6c2a));
        // Attachments update the toggle state when a DAW restores a session.
        // Keep the visible ON/OFF text in sync for both clicks and restores.
        button.onStateChange = [&button] { button.setButtonText (button.getToggleState() ? "ON" : "OFF"); };
        addAndMakeVisible (button);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (audioProcessor.parameters, parameterID, button);
        button.setButtonText (button.getToggleState() ? "ON" : "OFF");
    };
    setupSwitch (delayEnabledButton, "delayEnabled", delayEnabledAttachment);
    setupSwitch (reverbEnabledButton, "reverbEnabled", reverbEnabledAttachment);
    setupSwitch (chorusEnabledButton, "chorusEnabled", chorusEnabledAttachment);
    setupSwitch (phaserEnabledButton, "phaserEnabled", phaserEnabledAttachment);
    setupSwitch (flangerEnabledButton, "flangerEnabled", flangerEnabledAttachment);
    setupSwitch (eqEnabledButton, "eqEnabled", eqEnabledAttachment);
    setupSwitch (flutterEnabledButton, "flutterEnabled", flutterEnabledAttachment);
    delayEnabledButton.onClick = [this] { setSlotEnabled (0, delayEnabledButton.getToggleState()); };
    reverbEnabledButton.onClick = [this] { setSlotEnabled (1, reverbEnabledButton.getToggleState()); };
    chorusEnabledButton.onClick = [this] { setSlotEnabled (2, chorusEnabledButton.getToggleState()); };
    phaserEnabledButton.onClick = [this] { setSlotEnabled (3, phaserEnabledButton.getToggleState()); };
    flangerEnabledButton.onClick = [this] { setSlotEnabled (4, flangerEnabledButton.getToggleState()); };
    eqEnabledButton.onClick = [this] { setSlotEnabled (5, eqEnabledButton.getToggleState()); };
    flutterEnabledButton.onClick = [this] { setSlotEnabled (6, flutterEnabledButton.getToggleState()); };
    setupSwitch (granularEnabledButton, "granularEnabled", granularEnabledAttachment); setupSwitch (harmonyEnabledButton, "harmonyEnabled", harmonyEnabledAttachment); setupSwitch (randomEnabledButton, "randomEnabled", randomEnabledAttachment);
    for (auto* tab : { &granularTextureTab, &harmonyTextureTab, &randomTextureTab })
    {
        tab->setLookAndFeel (&frameworksLookAndFeel);
        addAndMakeVisible (*tab);
    }
    granularTextureTab.onClick = [this] { textureEngine = 0; resized(); repaint(); };
    harmonyTextureTab.onClick = [this] { textureEngine = 1; resized(); repaint(); };
    randomTextureTab.onClick = [this] { textureEngine = 2; resized(); repaint(); };
    setupControl (eqLowSlider, eqLowLabel, "LOW");
    setupControl (eqMidSlider, eqMidLabel, "MID");
    setupControl (eqHighSlider, eqHighLabel, "HIGH");
    eqLowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "eqLow", eqLowSlider);
    eqMidAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "eqMid", eqMidSlider);
    eqHighAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "eqHigh", eqHighSlider);
    addAndMakeVisible (eqCurvePad);
    for (int slot = 0; slot < (int) fxChain.size(); ++slot)
    {
        fxChain[(size_t) slot] = audioProcessor.getFxSlotEffect (slot);
        fxSlotEnabled[(size_t) slot] = audioProcessor.getFxSlotEnabled (slot);
        // A saved session from an earlier rack is migrated in the processor.
        // Keep each fixed card mapped to its corresponding effect from here on.
        if (fxChain[(size_t) slot] != slot)
            audioProcessor.setFxSlotState (slot, slot, fxSlotEnabled[(size_t) slot]);
        fxChain[(size_t) slot] = slot;
    }
    effectPickerMenu = std::make_unique<EffectPatchMenu> (frameworksLookAndFeel, [this] (int effect)
    {
        selectEffect (effect);
        if (effectPickerMenu != nullptr)
        {
            effectPickerMenu->setActiveEffect (effect);
            effectPickerMenu->setVisible (false);
        }
    }, selectedFx);
    addAndMakeVisible (*effectPickerMenu);
    effectPickerMenu->setVisible (false);
    addMouseListener (this, true);
    refreshEffectEnableStates();
    showFxPage (false);
    setSize (1100, 800);
    setResizable (false, false);
    startTimerHz (30);
}

OneShotSamplerAudioProcessorEditor::~OneShotSamplerAudioProcessorEditor()
{
    removeMouseListener (this);
}

void OneShotSamplerAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    if (effectPickerMenu == nullptr || ! effectPickerMenu->isVisible())
        return;

    const auto clicked = event.getEventRelativeTo (this).getPosition();
    const auto insideMenu = effectPickerMenu->getBounds().contains (clicked);
    const auto insideAddCard = fxSlotSix.getBounds().contains (clicked);

    if (! insideMenu && ! insideAddCard)
    {
        effectPickerMenu->setVisible (false);
        repaint();
    }
}

void OneShotSamplerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xfff2f1ed));

    g.setColour (juce::Colour (0xffc8c7c2));
    for (int x = 24; x < getWidth(); x += 18)
        for (int y = 24; y < getHeight(); y += 18)
            g.fillEllipse (static_cast<float> (x), static_cast<float> (y), 2.0f, 2.0f);

    const auto header = juce::Rectangle<int> (22, 10, 700, 82);
    if (modularBadge.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImageWithin (modularBadge, header.getX(), header.getY(), header.getWidth(), header.getHeight(), juce::RectanglePlacement::centred);
    }

    auto content = getLocalBounds().withTrimmedTop (112).reduced (30, 0);
    g.setColour (juce::Colour (0xff1b1b1b));
    if (! showingFx && ! showingTexture)
    {
        auto samplerArea = content.withTrimmedBottom (24);
        const auto materialRail = samplerArea.removeFromRight (270);
        samplerArea.removeFromRight (12);
        const auto dropArea = samplerArea.removeFromTop (205);
        g.setColour (juce::Colours::black.withAlpha (0.13f));
        g.fillRoundedRectangle (materialRail.translated (0, 5).toFloat(), 14.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff59a52), (float) materialRail.getX(), (float) materialRail.getY(), juce::Colour (0xffeb6c2a), (float) materialRail.getRight(), (float) materialRail.getBottom(), false));
        g.fillRoundedRectangle (materialRail.toFloat(), 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        for (int y = materialRail.getY() + 22; y < materialRail.getBottom() - 22; y += 16)
            g.drawLine ((float) materialRail.getX() + 22.0f, (float) y, (float) materialRail.getRight() - 22.0f, (float) y, 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.11f));
        for (int dot = 0; dot < 170; ++dot)
        {
            const auto x = (float) materialRail.getX() + 16.0f + std::fmod ((float) dot * 37.0f, (float) materialRail.getWidth() - 32.0f);
            const auto y = (float) materialRail.getY() + 16.0f + std::fmod ((float) dot * 71.0f, (float) materialRail.getHeight() - 32.0f);
            g.fillEllipse (x, y, 1.2f, 1.2f);
        }
        g.setColour (juce::Colour (0xff222222)); g.setFont (juce::Font ("Aniko", 13.0f, juce::Font::plain));
        g.drawText ("SAMPLE IDENTITY", materialRail.reduced (24).withHeight (28), juce::Justification::centredLeft);
        g.setFont (juce::Font ("Aniko", 21.0f, juce::Font::plain));
        g.drawFittedText (audioProcessor.hasLoadedSample() ? audioProcessor.getSampleName().toUpperCase() : "NO SAMPLE", materialRail.reduced (24).withTrimmedTop (52).withHeight (62), juce::Justification::centredLeft, 2);
        const auto activeVoices = audioProcessor.getActiveVoiceCount();
        const auto voicePulse = 9.0f + (float) activeVoices * 2.0f + std::sin (fxVisualPhase * 3.0f) * (activeVoices > 0 ? 2.0f : 0.0f);
        g.setColour (juce::Colour (0xff38b46a).withAlpha (activeVoices > 0 ? 0.22f : 0.0f));
        g.fillEllipse ((float) materialRail.getX() + 29.5f - voicePulse * 0.5f, (float) materialRail.getY() + 152.5f - voicePulse * 0.5f, voicePulse, voicePulse);
        g.setColour (juce::Colour (0xff38b46a)); g.fillEllipse ((float) materialRail.getX() + 25.0f, (float) materialRail.getY() + 148.0f, 9.0f, 9.0f);
        g.setColour (juce::Colour (0xff222222)); g.setFont (juce::Font ("Aniko", 11.0f, juce::Font::plain));
        g.drawText ("MIDI IN / POLYPHONIC", materialRail.getX() + 43, materialRail.getY() + 142, materialRail.getWidth() - 60, 22, juce::Justification::centredLeft);
        const auto duration = audioProcessor.getSampleLengthSeconds();
        const auto channels = audioProcessor.getSampleChannels();
        const auto metadata = audioProcessor.hasLoadedSample()
                                ? juce::String (duration, 2) + " SEC / " + (channels > 1 ? "STEREO" : "MONO") + " / 8 VOICES"
                                : "READY / 8 VOICES";
        g.setFont (juce::Font ("Aniko", 10.0f, juce::Font::plain));
        g.drawText (metadata, materialRail.reduced (24).withTrimmedTop (164).withHeight (17), juce::Justification::centredLeft);
        g.drawText ("STEREO OUT", materialRail.reduced (24).withTrimmedTop (187).withHeight (17), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff222222).withAlpha (0.4f)); g.drawLine ((float) materialRail.getX() + 24.0f, (float) materialRail.getY() + 219.0f, (float) materialRail.getRight() - 24.0f, (float) materialRail.getY() + 219.0f, 1.0f);
        const auto railWave = materialRail.reduced (24, 0).withTrimmedTop (238).withHeight (94);
        g.setColour (juce::Colour (0xffb94f1d).withAlpha (0.58f)); g.fillRoundedRectangle (railWave.toFloat(), 10.0f);
        g.setColour (juce::Colours::white.withAlpha (0.30f)); g.drawRoundedRectangle (railWave.toFloat(), 10.0f, 1.0f);
        if (audioProcessor.hasLoadedSample())
        {
            const auto peaks = audioProcessor.getWaveformPeaks (juce::jmax (1, railWave.getWidth()));
            const auto centreY = (float) railWave.getCentreY();
            g.setColour (juce::Colour (0xfff2f1ed));
            for (size_t i = 0; i < peaks.size(); ++i)
            {
                const auto x = (float) railWave.getX() + (float) i * railWave.getWidth() / (float) peaks.size();
                const auto h = peaks[i] * railWave.getHeight() * 0.40f;
                g.drawVerticalLine ((int) x, centreY - h, centreY + h);
            }
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.50f)); g.setFont (juce::Font ("Aniko", 10.0f, juce::Font::plain));
            g.drawText ("WAVEFORM PREVIEW", railWave, juce::Justification::centred);
        }
        g.setColour (juce::Colour (0xff222222)); g.setFont (juce::Font ("Aniko", 11.0f, juce::Font::plain));
        g.drawText ("OUTPUT", materialRail.reduced (24).withTrimmedTop (355).withHeight (20), juce::Justification::centred);
        const auto meter = juce::Rectangle<int> (materialRail.getRight() - 43, materialRail.getY() + 392, 8, 98);
        const auto activeSegments = (int) std::ceil (audioProcessor.getOutputLevel() * 7.0f);
        for (int segment = 0; segment < 7; ++segment)
        {
            const auto segmentArea = meter.withTrimmedTop (segment * 14).withHeight (9);
            g.setColour (segment < activeSegments ? juce::Colour (0xff222222).withAlpha (0.86f) : juce::Colours::white.withAlpha (0.28f));
            g.fillRoundedRectangle (segmentArea.toFloat(), 3.0f);
        }
        g.setColour (juce::Colour (0xffe7e5df));
        g.fillRoundedRectangle (dropArea.toFloat(), 12.0f);
        const auto waveArea = dropArea.reduced (28, 28);
        g.setColour (juce::Colour (0xffcfcdc7));
        for (int x = waveArea.getX(); x < waveArea.getRight(); x += 30)
            g.drawVerticalLine (x, (float) waveArea.getY(), (float) waveArea.getBottom());
        for (int y = waveArea.getY(); y < waveArea.getBottom(); y += 30)
            g.drawHorizontalLine (y, (float) waveArea.getX(), (float) waveArea.getRight());
        g.setColour (juce::Colour (0xff8e8b84).withAlpha (0.58f));
        g.drawRoundedRectangle (dropArea.toFloat(), 12.0f, 0.9f);
        g.setColour (juce::Colour (0xff171717));
        if (! audioProcessor.hasLoadedSample())
        {
            g.setFont (juce::Font ("Aniko", 16.0f, juce::Font::plain));
            g.drawFittedText ("DRAG A WAV OR AIFF HERE", dropArea.reduced (20), juce::Justification::centred, 1);
        }
        else
        {
            const auto peaks = audioProcessor.getWaveformPeaks (juce::jmax (48, waveArea.getWidth() / 3));
            const auto centreY = (float) waveArea.getCentreY();
            const auto scale = waveArea.getHeight() * 0.46f;
            juce::Path waveform;
            waveform.startNewSubPath ((float) waveArea.getX(), centreY);
            for (size_t i = 0; i < peaks.size(); ++i)
            {
                const auto x = (float) waveArea.getX() + (float) i * waveArea.getWidth() / (float) (peaks.size() - 1);
                waveform.lineTo (x, centreY - peaks[i] * scale);
            }
            for (size_t i = peaks.size(); i-- > 0;)
            {
                const auto x = (float) waveArea.getX() + (float) i * waveArea.getWidth() / (float) (peaks.size() - 1);
                waveform.lineTo (x, centreY + peaks[i] * scale);
            }
            waveform.closeSubPath();
            g.setColour (juce::Colour (0xffeb6c2a));
            g.fillPath (waveform);
            g.setColour (juce::Colour (0xffb94f1d).withAlpha (0.72f));
            g.drawHorizontalLine ((int) centreY, (float) waveArea.getX(), (float) waveArea.getRight());
            const auto progressX = (float) waveArea.getX() + audioProcessor.getPlaybackProgress() * (float) waveArea.getWidth();
            g.setColour (juce::Colour (0xff222222).withAlpha (0.85f));
            g.drawLine (progressX, (float) waveArea.getY(), progressX, (float) waveArea.getBottom(), 1.5f);
            g.setColour (juce::Colour (0xffeb6c2a));
            g.fillEllipse ((float) waveArea.getX() - 4.0f, centreY - 4.0f, 8.0f, 8.0f);
            g.fillEllipse ((float) waveArea.getRight() - 4.0f, centreY - 4.0f, 8.0f, 8.0f);
        }

        auto samplerControls = samplerArea;
        samplerControls.removeFromTop (22 + 270 + 10);
        samplerControls = samplerControls.removeFromTop (125);
        const auto samplerControlWidth = samplerControls.getWidth() / 4;
        for (const auto& panel : { samplerControls.removeFromLeft (samplerControlWidth), samplerControls.removeFromLeft (samplerControlWidth),
                                   samplerControls.removeFromLeft (samplerControlWidth), samplerControls })
        {
            const auto panelBounds = panel.reduced (7, 3).toFloat();
            g.setColour (juce::Colour (0xffdad7d0).withAlpha (0.84f));
            g.fillRoundedRectangle (panelBounds, 11.0f);
            g.setColour (juce::Colour (0xffbdbab3).withAlpha (0.55f));
            g.drawRoundedRectangle (panelBounds, 11.0f, 1.0f);
        }
    }
    else if (showingTexture)
    {
        auto lab = content.reduced (0, 8);
        // Texture Lab shares the orange hardware faceplate used by the FX
        // rack, while retaining its own animated engine graphics.
        g.setColour (juce::Colour (0xffeb6c2a));
        g.fillRoundedRectangle (lab.toFloat(), 14.0f);
        g.setColour (juce::Colour (0xffb94f1d).withAlpha (0.42f));
        for (int x = lab.getX() + 18; x < lab.getRight() - 12; x += 18)
            for (int y = lab.getY() + 18; y < lab.getBottom() - 12; y += 18)
                g.fillEllipse ((float) x, (float) y, 1.5f, 1.5f);
        g.setColour (juce::Colour (0xfff2f1ed).withAlpha (0.82f));
        g.drawRoundedRectangle (lab.toFloat(), 14.0f, 1.0f);
        g.setColour (juce::Colour (0xfff2f1ed));
        g.setFont (juce::Font ("Aniko", 25.0f, juce::Font::plain));
        g.drawText ("TEXTURE LAB", lab.reduced (34).withHeight (48), juce::Justification::centredLeft);
        const auto activeTextureParameter = textureEngine == 0 ? "granularEnabled" : textureEngine == 1 ? "harmonyEnabled" : "randomEnabled";
        const bool textureOn = audioProcessor.parameters.getRawParameterValue (activeTextureParameter)->load() > 0.5f;
        const auto accent = textureEngine == 0 ? juce::Colour (0xffeb6c2a) : textureEngine == 1 ? juce::Colour (0xff7a8cff) : juce::Colour (0xff42d78b);
        const auto engineName = textureEngine == 0 ? "ENGINE 01  GRANULAR" : textureEngine == 1 ? "ENGINE 02  ORBIT" : "ENGINE 03  RANDOM";
        g.setColour (accent.withAlpha (textureOn ? 1.0f : 0.35f)); g.fillEllipse ((float) lab.getRight() - 218.0f, (float) lab.getY() + 30.0f, 8.0f, 8.0f);
        g.setFont (juce::Font ("Aniko", 11.0f, juce::Font::plain)); g.drawText (engineName, lab.getRight() - 202, lab.getY() + 22, 180, 22, juce::Justification::centredLeft);
        // Input -> engine -> stereo output: a quiet hardware-style status rail.
        const auto railY = lab.getY() + 71;
        g.setColour (juce::Colour (0xfff2f1ed).withAlpha (0.52f));
        g.drawLine ((float) lab.getX() + 34.0f, (float) railY, (float) lab.getRight() - 34.0f, (float) railY, 1.0f);
        const auto signalX = (float) lab.getX() + 68.0f + std::fmod (fxVisualPhase * 160.0f, (float) lab.getWidth() - 136.0f);
        g.setColour (juce::Colour (0xfff2f1ed).withAlpha (textureOn ? 0.95f : 0.30f));
        g.fillEllipse (signalX - 3.0f, (float) railY - 3.0f, 6.0f, 6.0f);
        g.setFont (juce::Font ("Aniko", 9.0f, juce::Font::plain));
        g.drawText ("IN", lab.getX() + 34, railY - 19, 36, 14, juce::Justification::left);
        g.drawText ("ENGINE", lab.getCentreX() - 32, railY - 19, 64, 14, juce::Justification::centred);
        g.drawText ("OUT", lab.getRight() - 70, railY - 19, 36, 14, juce::Justification::right);

        const auto visual = lab.reduced (48, 0).withTrimmedTop (126).withHeight (176);
        g.setColour (juce::Colour (0xffc2541f));
        g.fillRoundedRectangle (visual.toFloat(), 13.0f);
        g.setColour (juce::Colour (0xfff2f1ed).withAlpha (0.78f));
        g.drawRoundedRectangle (visual.toFloat().reduced (1.0f), 13.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.11f));
        for (int x = visual.getX() + 24; x < visual.getRight() - 24; x += 36) g.drawVerticalLine (x, (float) visual.getY() + 16, (float) visual.getBottom() - 16);
        for (int y = visual.getY() + 24; y < visual.getBottom() - 24; y += 32) g.drawHorizontalLine (y, (float) visual.getX() + 16, (float) visual.getRight() - 16);
        const auto phase = fxVisualPhase * juce::MathConstants<float>::twoPi;
        const auto visualAlpha = textureOn ? 1.0f : 0.34f;
        if (textureEngine == 0)
        {
            for (int grain = 0; grain < 46; ++grain)
            {
                const auto x = (float) visual.getX() + std::fmod ((float) grain * 47.0f + phase * 38.0f * (1 + grain % 4), (float) visual.getWidth());
                const auto y = (float) visual.getCentreY() + std::sin (phase * (1.0f + grain % 3) + grain * 0.8f) * visual.getHeight() * 0.28f;
                g.setColour (juce::Colour (0xfff2f1ed).withAlpha (visualAlpha * (0.22f + (grain % 5) * 0.10f)));
                g.drawLine (x - 5.0f, y, x + 5.0f, y, 1.1f);
            }
        }
        else if (textureEngine == 1)
        {
            for (int voice = 0; voice < 3; ++voice) { juce::Path wave; wave.startNewSubPath ((float) visual.getX(), (float) visual.getCentreY()); for (int x = visual.getX(); x < visual.getRight(); x += 3) wave.lineTo ((float) x, (float) visual.getCentreY() + std::sin (phase * (1.2f + voice * .28f) + x * (.014f + voice * .004f)) * (20.0f + voice * 13.0f)); g.setColour (juce::Colour (0xfff2f1ed).withAlpha (visualAlpha * (0.78f - voice * .18f))); g.strokePath (wave, juce::PathStrokeType (voice == 1 ? 1.8f : 1.0f)); }
        }
        else
        {
            juce::Path steps; steps.startNewSubPath ((float) visual.getX(), (float) visual.getCentreY());
            for (int x = visual.getX(); x < visual.getRight(); x += 16) { const auto y = (float) visual.getCentreY() + std::sin (phase * 4.0f + x * 0.07f) * visual.getHeight() * 0.30f; steps.lineTo ((float) x + 8.0f, y); steps.lineTo ((float) x + 16.0f, y); }
            g.setColour (juce::Colour (0xfff2f1ed).withAlpha (visualAlpha)); g.strokePath (steps, juce::PathStrokeType (1.8f));
        }
        const auto meterLevel = juce::jlimit (0, 10, (int) std::round (audioProcessor.getOutputLevel() * 10.0f));
        for (int i = 0; i < 10; ++i) { auto block = juce::Rectangle<float> ((float) visual.getRight() - 22.0f, (float) visual.getBottom() - 24.0f - i * 14.0f, 7.0f, 9.0f); g.setColour ((i < meterLevel ? accent : juce::Colour (0xff5a5a5a)).withAlpha (visualAlpha)); g.fillRoundedRectangle (block, 2.0f); }
        g.setColour (juce::Colour (0xfff2f1ed).withAlpha (visualAlpha)); g.setFont (juce::Font ("Aniko", 14.0f, juce::Font::plain));
        g.drawText (textureEngine == 0 ? "GRAIN CLOUD" : textureEngine == 1 ? "STEREO ORBIT" : "CONTROLLED CHAOS", visual.reduced (0, 12), juce::Justification::centredBottom);

        auto knobArea = lab.reduced (36, 0).withTrimmedTop (356).withHeight (132);
        g.setColour (juce::Colour (0xfff2f1ed).withAlpha (0.90f)); g.setFont (juce::Font ("Aniko", 10.0f, juce::Font::plain));
        g.drawText ("SCULPT PARAMETERS", knobArea.withY (knobArea.getY() - 22).withHeight (16), juce::Justification::centredLeft);
        const auto knobCount = textureEngine == 0 ? 4 : 3;
        const auto panelWidth = knobArea.getWidth() / knobCount;
        for (int panel = 0; panel < knobCount; ++panel)
        {
            auto bounds = knobArea.removeFromLeft (panelWidth).reduced (7, 2);
            g.setColour (juce::Colour (0xffc2541f));
            g.fillRoundedRectangle (bounds.toFloat(), 12.0f);
        }
    }
    else if (showingFx)
    {
        const std::array<juce::String, 7> names { "PING PONG DELAY", "REVERB", "CHORUS", "PHASER", "FLANGER", "EQ", "FLUTTER" };
        auto main = content.withTrimmedBottom (24);
        auto slots = main.removeFromTop (154);
        const std::array<int, 6> cardEffects { 0, 1, 2, 3, 4, -1 };
        const std::array<juce::String, 6> cardNames { "DELAY", "REVERB", "CHORUS", "PHASER", "FLANGER", "" };
        const auto slotWidth = (slots.getWidth() - 60) / 6;
        std::array<juce::Rectangle<int>, 6> cards;
        for (int i = 0; i < 6; ++i)
        {
            cards[(size_t) i] = slots.removeFromLeft (slotWidth);
            if (i < 5) slots.removeFromLeft (12);
            const auto effect = cardEffects[(size_t) i];
            const bool isAddCard = effect < 0;
            auto cardBounds = cards[(size_t) i].toFloat().reduced (1.0f);
            const auto isSelected = effect == selectedFx;
            const auto isOn = ! isAddCard && fxSlotEnabled[(size_t) effect];
            g.setColour (isOn ? juce::Colour (0xffeb6c2a) : juce::Colour (0xfff8f7f3));
            g.fillRoundedRectangle (cardBounds, 11.0f);
            g.setColour (isAddCard ? juce::Colour (0xffeb6c2a) : isOn ? juce::Colour (0xfff2f1ed) : juce::Colour (0xff77736d));
            g.drawRoundedRectangle (cardBounds, 11.0f, isOn ? 1.2f : 0.8f);
            const auto iconCentre = juce::Point<float> ((float) cards[(size_t) i].getCentreX(), (float) cards[(size_t) i].getY() + 57.0f);
            drawModuleIcon (g, effect, iconCentre,
                            isAddCard ? juce::Colour (0xffeb6c2a)
                                      : isOn ? juce::Colour (0xfff2f1ed) : juce::Colour (0xff343434));
            if (isAddCard)
                continue;
            g.setColour (isOn ? juce::Colour (0xfff2f1ed) : juce::Colour (0xff343434));
            g.setFont (juce::Font ("Aniko", 11.0f, juce::Font::plain));
            g.drawFittedText (cardNames[(size_t) i], cards[(size_t) i].reduced (6).withTrimmedTop (108), juce::Justification::centred, 2);
            if (isSelected) { g.setColour (isOn ? juce::Colour (0xfff2f1ed) : juce::Colour (0xffeb6c2a)); g.fillEllipse ((float) cards[(size_t) i].getRight() - 20.0f, (float) cards[(size_t) i].getY() + 10.0f, 8.0f, 8.0f); }
        }

        auto detail = main.withTrimmedTop (20);
        g.setColour (juce::Colour (0xffe7e5df));
        g.fillRoundedRectangle (detail.toFloat(), 16.0f);
        g.setColour (juce::Colour (0xffbdbab3));
        g.drawRoundedRectangle (detail.toFloat().reduced (0.8f), 16.0f, 1.0f);
        for (int x = detail.getX() + 18; x < detail.getRight() - 12; x += 18)
            for (int y = detail.getY() + 18; y < detail.getBottom() - 12; y += 18)
            {
                g.setColour (juce::Colour (0xff9e9a92).withAlpha (0.32f));
                g.fillEllipse ((float) x, (float) y, 1.5f, 1.5f);
            }
        g.setColour (juce::Colour (0xff202020));
        g.setFont (juce::Font ("Aniko", 21.0f, juce::Font::plain));
        g.drawText (names[(size_t) selectedFx] + "  /  SPACE ENGINE", detail.getX() + 30, detail.getY() + 26, 380, 32, juce::Justification::centredLeft);
        g.setColour (fxSlotEnabled[(size_t) selectedFx] ? juce::Colour (0xff38b46a) : juce::Colour (0xffdf5b5b));
        g.fillEllipse ((float) detail.getX() + 32.0f, (float) detail.getY() + 80.0f, 15.0f, 15.0f);
        if (selectedFx < 0)
        {
            if (darkFrameworksMark.isValid())
            {
                const auto markArea = detail.withSizeKeepingCentre (70, 70).translated (0, -42);
                g.drawImage (darkFrameworksMark, markArea.getX(), markArea.getY(), markArea.getWidth(), markArea.getHeight(),
                             0, 0, darkFrameworksMark.getWidth(), darkFrameworksMark.getHeight(), false);
            }
            g.setFont (juce::Font ("Aniko", 14.0f, juce::Font::plain));
            g.drawText ("SELECT A CARD TO EDIT  /  CLICK A FRAMEWORKS MARK TO ADD", detail.reduced (24).translated (0, 48), juce::Justification::centred);
            return;
        }
        if (selectedFx != 5)
        {
            const auto visual = detail.reduced (92, 0).withTrimmedTop (76).withHeight (188);
            const auto audioLevel = juce::jlimit (0.0f, 1.0f, audioProcessor.getOutputLevel());
            const auto phase = fxVisualPhase * juce::MathConstants<float>::twoPi;
            const auto value = [this] (const char* id) { return audioProcessor.parameters.getRawParameterValue (id)->load(); };
            const auto amp = visual.getHeight() * (0.08f + audioLevel * 0.30f);
            g.setColour (juce::Colour (0xffc9c6be));
            g.fillRoundedRectangle (visual.toFloat(), 14.0f);
            g.setColour (juce::Colour (0xff202020).withAlpha (0.62f));
            g.drawRoundedRectangle (visual.toFloat().reduced (1.0f), 14.0f, 1.0f);
            auto innerVisual = visual.reduced (18);
            g.reduceClipRegion (innerVisual);

            // A quiet tile grid gives each motion display its own instrument
            // surface without competing with the white animation.
            g.setColour (juce::Colour (0xff99958d).withAlpha (0.38f));
            constexpr int tileSize = 22;
            for (int x = innerVisual.getX(); x <= innerVisual.getRight(); x += tileSize)
                g.drawVerticalLine (x, (float) innerVisual.getY(), (float) innerVisual.getBottom());
            for (int y = innerVisual.getY(); y <= innerVisual.getBottom(); y += tileSize)
                g.drawHorizontalLine (y, (float) innerVisual.getX(), (float) innerVisual.getRight());

            if (selectedFx == 0) // Delay: source pulse and fading left/right echo taps.
            {
                const auto centreX = (float) innerVisual.getCentreX();
                const auto delay = value ("delayTime");
                const auto feedback = value ("delayFeedback");
                g.setColour (juce::Colour (0xfff2f1ed));
                g.drawLine (centreX, (float) innerVisual.getY() + 12.0f, centreX, (float) innerVisual.getBottom() - 12.0f, 1.8f);
                for (int tap = 1; tap <= 4; ++tap)
                {
                    const auto distance = (float) tap * (38.0f + delay * 48.0f);
                    const auto radius = 12.0f * std::pow (feedback, (float) tap - 0.5f) * (0.5f + audioLevel);
                    g.setColour (juce::Colour (0xfff2f1ed).withAlpha (0.9f / (float) tap));
                    g.drawEllipse (centreX - distance - radius, (float) innerVisual.getCentreY() - radius, radius * 2.0f, radius * 2.0f, 1.2f);
                    g.drawEllipse (centreX + distance - radius, (float) innerVisual.getCentreY() - radius, radius * 2.0f, radius * 2.0f, 1.2f);
                }
            }
            else if (selectedFx == 1) // Reverb: orbital diffusion field.
            {
                const auto room = value ("reverbRoomSize");
                for (int ring = 0; ring < 7; ++ring)
                {
                    const auto travel = std::fmod (fxVisualPhase + ring * 0.16f, 1.0f);
                    const auto width = 28.0f + travel * innerVisual.getWidth() * (0.18f + room * 0.36f);
                    const auto height = 18.0f + travel * innerVisual.getHeight() * 0.34f;
                    g.setColour (juce::Colour (0xfff2f1ed).withAlpha ((1.0f - travel) * (0.18f + audioLevel * 0.52f)));
                    g.drawEllipse (juce::Rectangle<float> ((float) innerVisual.getCentreX() - width * 0.5f, (float) innerVisual.getCentreY() - height * 0.5f, width, height), 1.0f);
                }
            }
            else if (selectedFx == 2) // Chorus: two detuned voices.
            {
                const auto depth = value ("chorusDepth");
                for (int voice = 0; voice < 2; ++voice)
                {
                    juce::Path wave; wave.startNewSubPath ((float) innerVisual.getX(), (float) innerVisual.getCentreY());
                    for (int x = innerVisual.getX(); x < innerVisual.getRight(); x += 3) wave.lineTo ((float) x, (float) innerVisual.getCentreY() + std::sin (phase * (voice == 0 ? 0.85f : 1.15f) + x * 0.033f + voice * 1.8f) * amp * (0.6f + depth));
                    g.setColour (juce::Colour (0xfff2f1ed).withAlpha (voice == 0 ? 0.88f : 0.48f)); g.strokePath (wave, juce::PathStrokeType (1.7f));
                }
            }
            else if (selectedFx == 3) // Phaser: moving notches across a centre rail.
            {
                g.setColour (juce::Colour (0xfff2f1ed).withAlpha (0.35f)); g.drawHorizontalLine (innerVisual.getCentreY(), (float) innerVisual.getX(), (float) innerVisual.getRight());
                const auto depth = value ("phaserDepth");
                for (int notch = 0; notch < 5; ++notch) { const auto position = std::fmod (fxVisualPhase * (0.35f + depth) + notch * 0.21f, 1.0f); const auto x = innerVisual.getX() + position * innerVisual.getWidth(); const auto size = 8.0f + depth * 13.0f; g.setColour (juce::Colour (0xfff2f1ed).withAlpha (0.25f + audioLevel * 0.55f)); g.drawEllipse (x - size, innerVisual.getCentreY() - size, size * 2.0f, size * 2.0f, 1.1f); }
            }
            else if (selectedFx == 4) // Flanger: animated comb-filter teeth.
            {
                const auto depth = value ("flangerDepth");
                for (int tooth = 0; tooth < 16; ++tooth) { const auto x = innerVisual.getX() + tooth * innerVisual.getWidth() / 15.0f; const auto offset = std::sin (phase + tooth * 0.65f) * innerVisual.getHeight() * 0.22f * depth; g.setColour (juce::Colour (0xfff2f1ed).withAlpha (0.35f + 0.5f * audioLevel)); g.drawLine (x, (float) innerVisual.getCentreY() - offset - 15.0f, x, (float) innerVisual.getCentreY() - offset + 15.0f, 1.5f); }
            }
            else // Flutter: unstable tape-like pitch trail.
            {
                const auto depth = value ("flutterDepth");
                for (int trail = 0; trail < 3; ++trail) { juce::Path wave; wave.startNewSubPath ((float) innerVisual.getX(), (float) innerVisual.getCentreY()); for (int x = innerVisual.getX(); x < innerVisual.getRight(); x += 3) { const auto wobble = std::sin (phase * (2.0f + trail) + x * (0.018f + depth * 0.018f)) + 0.35f * std::sin (phase * 7.0f + x * 0.071f); wave.lineTo ((float) x, (float) innerVisual.getCentreY() + wobble * amp * (0.5f + depth)); } g.setColour (juce::Colour (0xfff2f1ed).withAlpha (0.8f - trail * 0.22f)); g.strokePath (wave, juce::PathStrokeType (trail == 0 ? 1.7f : 0.9f)); }
            }
        }

        const int knobCount = selectedFx == 0 || selectedFx == 8 || selectedFx == 9 ? 3 : selectedFx == 3 || selectedFx == 6 || selectedFx == 7 ? 4 : selectedFx == 5 ? 3 : 5;
        if (selectedFx != 5)
        {
            auto knobBackdrops = detail.reduced (34, 0).withTrimmedTop (302).withHeight (150);
            const auto knobWidth = knobBackdrops.getWidth() / knobCount;
            for (int i = 0; i < knobCount; ++i)
            {
                auto knobPanel = knobBackdrops.removeFromLeft (knobWidth).reduced (6, 4);
                g.setColour (juce::Colour (0xffd8d5ce));
                g.fillRoundedRectangle (knobPanel.toFloat(), 11.0f);
            }
        }
    }
}

void OneShotSamplerAudioProcessorEditor::resized()
{
    samplerTab.setBounds (getWidth() - 342, 24, 152, 46);
    fxTab.setBounds (getWidth() - 176, 24, 76, 46);
    textureTab.setBounds (getWidth() - 86, 24, 64, 46);
    auto content = getLocalBounds().withTrimmedTop (112).reduced (30, 0);
    const auto placeControl = [] (juce::Slider& slider, juce::Label& label, juce::Rectangle<int> area)
    {
        slider.setBounds (area.withTrimmedTop (18).withHeight (82));
        label.setBounds (area.withY (area.getY() + 102).withHeight (18));
    };
    if (! showingFx && ! showingTexture)
    {
        auto samplerArea = content.withTrimmedBottom (24);
        auto materialRail = samplerArea.removeFromRight (270);
        samplerArea.removeFromRight (12);
        auto dropArea = samplerArea.removeFromTop (205);
        sampleLabel.setBounds (0, 0, 0, 0);
        loadButton.setBounds (dropArea.withTrimmedTop (112).withSizeKeepingCentre (150, 34));
        samplerArea.removeFromTop (22);
        envelopePad.setBounds (samplerArea.removeFromTop (270));
        samplerArea.removeFromTop (10);
        auto controls = samplerArea.removeFromTop (125);
        const int controlWidth = controls.getWidth() / 4;
        placeControl (attackSlider, attackLabel, controls.removeFromLeft (controlWidth));
        placeControl (decaySlider, decayLabel, controls.removeFromLeft (controlWidth));
        placeControl (sustainSlider, sustainLabel, controls.removeFromLeft (controlWidth));
        placeControl (releaseSlider, releaseLabel, controls);
        placeControl (outputGainSlider, outputGainLabel, materialRail.reduced (24).withTrimmedTop (390).withHeight (130));
        return;
    }

    if (showingTexture)
    {
        auto detail = content.reduced (0, 8);
        const auto textureAccent = textureEngine == 0 ? juce::Colour (0xffeb6c2a) : textureEngine == 1 ? juce::Colour (0xff7a8cff) : juce::Colour (0xff42d78b);
        const auto placeTextureRow = [] (std::initializer_list<std::pair<juce::Slider*, juce::Label*>> controls, juce::Rectangle<int> area)
        {
            const auto width = area.getWidth() / (int) controls.size();
            for (const auto& control : controls) { auto cell = area.removeFromLeft (width).reduced (5, 0); control.first->setBounds (cell.withTrimmedTop (10).withHeight (76)); control.second->setBounds (cell.withTrimmedTop (91).withHeight (18)); }
        };
        const auto placeTextureGrid = [] (std::initializer_list<std::pair<juce::Slider*, juce::Label*>> controls, juce::Rectangle<int> area)
        {
            int index = 0;
            for (const auto& control : controls)
            {
                const auto column = index % 2, row = index / 2;
                auto cell = juce::Rectangle<int> (area.getX() + column * area.getWidth() / 2, area.getY() + row * 132, area.getWidth() / 2, 124).reduced (8, 0);
                control.first->setBounds (cell.withTrimmedTop (5).withHeight (76));
                control.second->setBounds (cell.withTrimmedTop (86).withHeight (18));
                ++index;
            }
        };
        auto engineTabs = detail.reduced (34, 0).withTrimmedTop (72);
        const auto engineTabWidth = 150;
        granularTextureTab.setBounds (engineTabs.removeFromLeft (engineTabWidth).withHeight (34)); engineTabs.removeFromLeft (10);
        harmonyTextureTab.setBounds (engineTabs.removeFromLeft (engineTabWidth).withHeight (34)); engineTabs.removeFromLeft (10);
        randomTextureTab.setBounds (engineTabs.removeFromLeft (engineTabWidth).withHeight (34));
        for (int index = 0; index < 3; ++index)
        {
            auto* tab = index == 0 ? &granularTextureTab : index == 1 ? &harmonyTextureTab : &randomTextureTab;
            const auto active = textureEngine == index;
            tab->setToggleState (active, juce::dontSendNotification);
            tab->setColour (juce::TextButton::textColourOffId, active ? juce::Colour (0xfff2f1ed) : juce::Colour (0xff202020));
            tab->setColour (juce::TextButton::textColourOnId, active ? juce::Colour (0xfff2f1ed) : juce::Colour (0xff202020));
        }
        for (auto* slider : { &granularSizeSlider, &granularDensitySlider, &granularPitchSlider, &granularMixSlider, &harmonyShiftSlider, &harmonyDetuneSlider, &harmonyMixSlider, &randomAmountSlider, &randomRateSlider, &randomMixSlider })
        {
            slider->setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xfff2f1ed));
            slider->setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xff202020));
            slider->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        }
        for (juce::Component* component : std::initializer_list<juce::Component*> { &granularSizeSlider, &granularSizeLabel, &granularDensitySlider, &granularDensityLabel, &granularPitchSlider, &granularPitchLabel, &granularMixSlider, &granularMixLabel, &granularPreset, &granularEnabledButton, &harmonyShiftSlider, &harmonyShiftLabel, &harmonyDetuneSlider, &harmonyDetuneLabel, &harmonyMixSlider, &harmonyMixLabel, &harmonyPreset, &harmonyEnabledButton, &randomAmountSlider, &randomAmountLabel, &randomRateSlider, &randomRateLabel, &randomMixSlider, &randomMixLabel, &randomPreset, &randomEnabledButton }) component->setVisible (false);
        const auto controlArea = detail.reduced (36, 0).withTrimmedTop (356).withHeight (120);
        if (textureEngine == 0)
        {
            granularPreset.setBounds (detail.getRight() - 260, detail.getY() + 72, 180, 30); granularEnabledButton.setBounds (detail.getRight() - 72, detail.getY() + 72, 48, 30);
            placeTextureRow ({ { &granularSizeSlider, &granularSizeLabel }, { &granularDensitySlider, &granularDensityLabel }, { &granularPitchSlider, &granularPitchLabel }, { &granularMixSlider, &granularMixLabel } }, controlArea);
            for (auto* c : { (juce::Component*) &granularSizeSlider, (juce::Component*) &granularSizeLabel, (juce::Component*) &granularDensitySlider, (juce::Component*) &granularDensityLabel, (juce::Component*) &granularPitchSlider, (juce::Component*) &granularPitchLabel, (juce::Component*) &granularMixSlider, (juce::Component*) &granularMixLabel, (juce::Component*) &granularPreset, (juce::Component*) &granularEnabledButton }) c->setVisible (true);
        }
        else if (textureEngine == 1)
        {
            harmonyPreset.setBounds (detail.getRight() - 260, detail.getY() + 72, 180, 30); harmonyEnabledButton.setBounds (detail.getRight() - 72, detail.getY() + 72, 48, 30);
            placeTextureRow ({ { &harmonyShiftSlider, &harmonyShiftLabel }, { &harmonyDetuneSlider, &harmonyDetuneLabel }, { &harmonyMixSlider, &harmonyMixLabel } }, controlArea);
            for (auto* c : { (juce::Component*) &harmonyShiftSlider, (juce::Component*) &harmonyShiftLabel, (juce::Component*) &harmonyDetuneSlider, (juce::Component*) &harmonyDetuneLabel, (juce::Component*) &harmonyMixSlider, (juce::Component*) &harmonyMixLabel, (juce::Component*) &harmonyPreset, (juce::Component*) &harmonyEnabledButton }) c->setVisible (true);
        }
        else
        {
            randomPreset.setBounds (detail.getRight() - 260, detail.getY() + 72, 180, 30); randomEnabledButton.setBounds (detail.getRight() - 72, detail.getY() + 72, 48, 30);
            placeTextureRow ({ { &randomAmountSlider, &randomAmountLabel }, { &randomRateSlider, &randomRateLabel }, { &randomMixSlider, &randomMixLabel } }, controlArea);
            for (auto* c : { (juce::Component*) &randomAmountSlider, (juce::Component*) &randomAmountLabel, (juce::Component*) &randomRateSlider, (juce::Component*) &randomRateLabel, (juce::Component*) &randomMixSlider, (juce::Component*) &randomMixLabel, (juce::Component*) &randomPreset, (juce::Component*) &randomEnabledButton }) c->setVisible (true);
        }
        for (auto* label : { &granularSizeLabel, &granularDensityLabel, &granularPitchLabel, &granularMixLabel,
                             &harmonyShiftLabel, &harmonyDetuneLabel, &harmonyMixLabel,
                             &randomAmountLabel, &randomRateLabel, &randomMixLabel })
            label->setColour (juce::Label::textColourId, juce::Colour (0xfff2f1ed));
        return;
    }

    auto main = content.withTrimmedBottom (24);
    auto slots = main.removeFromTop (150);
    const auto slotWidth = (slots.getWidth() - 60) / 6;
    std::array<juce::TextButton*, 6> slotButtons { &fxSlotOne, &fxSlotTwo, &fxSlotThree, &fxSlotFour, &fxSlotFive, &fxSlotSix };
    for (int i = 0; i < 6; ++i)
    {
        const auto bounds = slots.removeFromLeft (slotWidth);
        slotButtons[(size_t) i]->setBounds (bounds);
        if (i < 5) slots.removeFromLeft (12);
    }
    fxSlotSeven.setBounds (0, 0, 0, 0);
    for (auto* toggle : { &fxSlotToggleOne, &fxSlotToggleTwo, &fxSlotToggleThree, &fxSlotToggleFour, &fxSlotToggleFive, &fxSlotToggleSix, &fxSlotToggleSeven })
        toggle->setVisible (false);
    if (effectPickerMenu != nullptr)
        effectPickerMenu->setBounds (getWidth() / 2 - 130, content.getY() + 136, 260, 126);
    auto detail = main.withTrimmedTop (18);
    const auto placeDetail = [] (std::initializer_list<std::pair<juce::Slider*, juce::Label*>> controls, juce::Rectangle<int> area)
    {
        const auto width = area.getWidth() / (int) controls.size();
        for (const auto& control : controls)
        {
            auto cell = area.removeFromLeft (width);
            control.first->setBounds (cell.withTrimmedTop (4).withHeight (106));
            control.second->setBounds (cell.withTrimmedTop (120).withHeight (22));
        }
    };
    auto hide = [] (juce::Component& component) { component.setVisible (false); };
    for (juce::Component* component : std::initializer_list<juce::Component*> { &delayTimeSlider, &delayTimeLabel, &delayFeedbackSlider, &delayFeedbackLabel, &delayMixSlider, &delayMixLabel, &delayPreset, &delayEnabledButton,
        &reverbMixSlider, &reverbMixLabel, &reverbRoomSizeSlider, &reverbRoomSizeLabel, &reverbDampingSlider, &reverbDampingLabel, &reverbWidthSlider, &reverbWidthLabel, &reverbFreezeSlider, &reverbFreezeLabel, &reverbPreset, &reverbEnabledButton,
        &chorusRateSlider, &chorusRateLabel, &chorusDepthSlider, &chorusDepthLabel, &chorusDelaySlider, &chorusDelayLabel, &chorusFeedbackSlider, &chorusFeedbackLabel, &chorusMixSlider, &chorusMixLabel, &chorusPreset, &chorusEnabledButton,
        &phaserRateSlider, &phaserRateLabel, &phaserDepthSlider, &phaserDepthLabel, &phaserFeedbackSlider, &phaserFeedbackLabel, &phaserMixSlider, &phaserMixLabel, &phaserPreset, &phaserEnabledButton,
        &flangerRateSlider, &flangerRateLabel, &flangerDepthSlider, &flangerDepthLabel, &flangerDelaySlider, &flangerDelayLabel, &flangerFeedbackSlider, &flangerFeedbackLabel, &flangerMixSlider, &flangerMixLabel, &flangerPreset, &flangerEnabledButton, &flutterRateSlider, &flutterRateLabel, &flutterDepthSlider, &flutterDepthLabel, &flutterDelaySlider, &flutterDelayLabel, &flutterMixSlider, &flutterMixLabel, &flutterPreset, &flutterEnabledButton, &granularSizeSlider, &granularSizeLabel, &granularDensitySlider, &granularDensityLabel, &granularPitchSlider, &granularPitchLabel, &granularMixSlider, &granularMixLabel, &granularPreset, &granularEnabledButton, &harmonyShiftSlider, &harmonyShiftLabel, &harmonyDetuneSlider, &harmonyDetuneLabel, &harmonyMixSlider, &harmonyMixLabel, &harmonyPreset, &harmonyEnabledButton, &randomAmountSlider, &randomAmountLabel, &randomRateSlider, &randomRateLabel, &randomMixSlider, &randomMixLabel, &randomPreset, &randomEnabledButton, &eqLowSlider, &eqLowLabel, &eqMidSlider, &eqMidLabel, &eqHighSlider, &eqHighLabel, &eqEnabledButton, &eqCurvePad, &eqPreset }) hide (*component);
    const auto show = [] (juce::Component& component) { component.setVisible (true); };
    const auto controlArea = detail.reduced (112, 0).withTrimmedTop (286).withHeight (136);
    auto setHeader = [&detail, &show] (juce::ComboBox& preset, juce::TextButton& enabled)
    {
        preset.setBounds (detail.getRight() - 250, detail.getY() + 22, 210, 28);
        show (preset);
        enabled.setVisible (false);
    };
    if (selectedFx == 0) { setHeader (delayPreset, delayEnabledButton); placeDetail ({ { &delayTimeSlider, &delayTimeLabel }, { &delayFeedbackSlider, &delayFeedbackLabel }, { &delayMixSlider, &delayMixLabel } }, controlArea); for (auto* c : { (juce::Component*) &delayTimeSlider, (juce::Component*) &delayTimeLabel, (juce::Component*) &delayFeedbackSlider, (juce::Component*) &delayFeedbackLabel, (juce::Component*) &delayMixSlider, (juce::Component*) &delayMixLabel }) show (*c); }
    if (selectedFx == 1) { setHeader (reverbPreset, reverbEnabledButton); placeDetail ({ { &reverbRoomSizeSlider, &reverbRoomSizeLabel }, { &reverbDampingSlider, &reverbDampingLabel }, { &reverbWidthSlider, &reverbWidthLabel }, { &reverbFreezeSlider, &reverbFreezeLabel }, { &reverbMixSlider, &reverbMixLabel } }, controlArea); for (auto* c : { (juce::Component*) &reverbRoomSizeSlider, (juce::Component*) &reverbRoomSizeLabel, (juce::Component*) &reverbDampingSlider, (juce::Component*) &reverbDampingLabel, (juce::Component*) &reverbWidthSlider, (juce::Component*) &reverbWidthLabel, (juce::Component*) &reverbFreezeSlider, (juce::Component*) &reverbFreezeLabel, (juce::Component*) &reverbMixSlider, (juce::Component*) &reverbMixLabel }) show (*c); }
    if (selectedFx == 2) { setHeader (chorusPreset, chorusEnabledButton); placeDetail ({ { &chorusRateSlider, &chorusRateLabel }, { &chorusDepthSlider, &chorusDepthLabel }, { &chorusDelaySlider, &chorusDelayLabel }, { &chorusFeedbackSlider, &chorusFeedbackLabel }, { &chorusMixSlider, &chorusMixLabel } }, controlArea); for (auto* c : { (juce::Component*) &chorusRateSlider, (juce::Component*) &chorusRateLabel, (juce::Component*) &chorusDepthSlider, (juce::Component*) &chorusDepthLabel, (juce::Component*) &chorusDelaySlider, (juce::Component*) &chorusDelayLabel, (juce::Component*) &chorusFeedbackSlider, (juce::Component*) &chorusFeedbackLabel, (juce::Component*) &chorusMixSlider, (juce::Component*) &chorusMixLabel }) show (*c); }
    if (selectedFx == 3) { setHeader (phaserPreset, phaserEnabledButton); placeDetail ({ { &phaserRateSlider, &phaserRateLabel }, { &phaserDepthSlider, &phaserDepthLabel }, { &phaserFeedbackSlider, &phaserFeedbackLabel }, { &phaserMixSlider, &phaserMixLabel } }, controlArea); for (auto* c : { (juce::Component*) &phaserRateSlider, (juce::Component*) &phaserRateLabel, (juce::Component*) &phaserDepthSlider, (juce::Component*) &phaserDepthLabel, (juce::Component*) &phaserFeedbackSlider, (juce::Component*) &phaserFeedbackLabel, (juce::Component*) &phaserMixSlider, (juce::Component*) &phaserMixLabel }) show (*c); }
    if (selectedFx == 4) { setHeader (flangerPreset, flangerEnabledButton); placeDetail ({ { &flangerRateSlider, &flangerRateLabel }, { &flangerDepthSlider, &flangerDepthLabel }, { &flangerDelaySlider, &flangerDelayLabel }, { &flangerFeedbackSlider, &flangerFeedbackLabel }, { &flangerMixSlider, &flangerMixLabel } }, controlArea); for (auto* c : { (juce::Component*) &flangerRateSlider, (juce::Component*) &flangerRateLabel, (juce::Component*) &flangerDepthSlider, (juce::Component*) &flangerDepthLabel, (juce::Component*) &flangerDelaySlider, (juce::Component*) &flangerDelayLabel, (juce::Component*) &flangerFeedbackSlider, (juce::Component*) &flangerFeedbackLabel, (juce::Component*) &flangerMixSlider, (juce::Component*) &flangerMixLabel }) show (*c); }
    if (selectedFx == 5) { eqPreset.setBounds (detail.getRight() - 250, detail.getY() + 22, 210, 28); eqCurvePad.setBounds (detail.reduced (50).withTrimmedTop (78).withTrimmedBottom (34)); show (eqPreset); show (eqCurvePad); }
    if (selectedFx == 6) { setHeader (flutterPreset, flutterEnabledButton); placeDetail ({ { &flutterRateSlider, &flutterRateLabel }, { &flutterDepthSlider, &flutterDepthLabel }, { &flutterDelaySlider, &flutterDelayLabel }, { &flutterMixSlider, &flutterMixLabel } }, controlArea); for (auto* c : { (juce::Component*) &flutterRateSlider, (juce::Component*) &flutterRateLabel, (juce::Component*) &flutterDepthSlider, (juce::Component*) &flutterDepthLabel, (juce::Component*) &flutterDelaySlider, (juce::Component*) &flutterDelayLabel, (juce::Component*) &flutterMixSlider, (juce::Component*) &flutterMixLabel }) show (*c); }
    if (selectedFx == 7) { setHeader (granularPreset, granularEnabledButton); placeDetail ({ { &granularSizeSlider, &granularSizeLabel }, { &granularDensitySlider, &granularDensityLabel }, { &granularPitchSlider, &granularPitchLabel }, { &granularMixSlider, &granularMixLabel } }, controlArea); for (auto* c : { (juce::Component*) &granularSizeSlider, (juce::Component*) &granularSizeLabel, (juce::Component*) &granularDensitySlider, (juce::Component*) &granularDensityLabel, (juce::Component*) &granularPitchSlider, (juce::Component*) &granularPitchLabel, (juce::Component*) &granularMixSlider, (juce::Component*) &granularMixLabel }) show (*c); }
    if (selectedFx == 8) { setHeader (harmonyPreset, harmonyEnabledButton); placeDetail ({ { &harmonyShiftSlider, &harmonyShiftLabel }, { &harmonyDetuneSlider, &harmonyDetuneLabel }, { &harmonyMixSlider, &harmonyMixLabel } }, controlArea); for (auto* c : { (juce::Component*) &harmonyShiftSlider, (juce::Component*) &harmonyShiftLabel, (juce::Component*) &harmonyDetuneSlider, (juce::Component*) &harmonyDetuneLabel, (juce::Component*) &harmonyMixSlider, (juce::Component*) &harmonyMixLabel }) show (*c); }
    if (selectedFx == 9) { setHeader (randomPreset, randomEnabledButton); placeDetail ({ { &randomAmountSlider, &randomAmountLabel }, { &randomRateSlider, &randomRateLabel }, { &randomMixSlider, &randomMixLabel } }, controlArea); for (auto* c : { (juce::Component*) &randomAmountSlider, (juce::Component*) &randomAmountLabel, (juce::Component*) &randomRateSlider, (juce::Component*) &randomRateLabel, (juce::Component*) &randomMixSlider, (juce::Component*) &randomMixLabel }) show (*c); }

    for (auto* label : { &delayTimeLabel, &delayFeedbackLabel, &delayMixLabel, &reverbRoomSizeLabel, &reverbDampingLabel, &reverbWidthLabel, &reverbFreezeLabel, &reverbMixLabel, &chorusRateLabel, &chorusDepthLabel, &chorusDelayLabel, &chorusFeedbackLabel, &chorusMixLabel, &phaserRateLabel, &phaserDepthLabel, &phaserFeedbackLabel, &phaserMixLabel, &flangerRateLabel, &flangerDepthLabel, &flangerDelayLabel, &flangerFeedbackLabel, &flangerMixLabel, &flutterRateLabel, &flutterDepthLabel, &flutterDelayLabel, &flutterMixLabel })
        label->setColour (juce::Label::textColourId, juce::Colour (0xfff2f1ed));
}

void OneShotSamplerAudioProcessorEditor::showFxPage (bool shouldShowFx)
{
    // The picker only opens from the add card. It is never part of the FX
    // page's resting layout, so it cannot cover the editor after a tab change.
    if (effectPickerMenu != nullptr)
        effectPickerMenu->setVisible (false);

    showingFx = shouldShowFx;
    showingTexture = false;
    granularTextureTab.setVisible (false); harmonyTextureTab.setVisible (false); randomTextureTab.setVisible (false);
    samplerTab.setColour (juce::TextButton::buttonColourId, shouldShowFx ? juce::Colour (0xff292929) : juce::Colour (0xffeb6c2a));
    samplerTab.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffeb6c2a));
    fxTab.setColour (juce::TextButton::buttonColourId, shouldShowFx ? juce::Colour (0xffeb6c2a) : juce::Colour (0xff292929));
    fxTab.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffeb6c2a));

    sampleLabel.setVisible (false);
    for (juce::Component* component : std::initializer_list<juce::Component*> { &loadButton, &envelopePad, &outputGainSlider, &outputGainLabel,
                                                                                   &attackSlider, &attackLabel, &decaySlider, &decayLabel, &sustainSlider, &sustainLabel, &releaseSlider, &releaseLabel, &eqCurvePad })
        component->setVisible (! shouldShowFx);
    for (juce::Component* component : std::initializer_list<juce::Component*> { &delayTimeSlider, &delayTimeLabel, &delayFeedbackSlider, &delayFeedbackLabel,
                                                                                   &delayMixSlider, &delayMixLabel, &reverbMixSlider, &reverbMixLabel, &reverbRoomSizeSlider, &reverbRoomSizeLabel,
                                                                                   &reverbDampingSlider, &reverbDampingLabel, &reverbWidthSlider, &reverbWidthLabel, &reverbFreezeSlider, &reverbFreezeLabel, &chorusRateSlider, &chorusRateLabel,
                                                                                   &chorusDepthSlider, &chorusDepthLabel, &chorusDelaySlider, &chorusDelayLabel, &chorusFeedbackSlider, &chorusFeedbackLabel,
                                                                                   &chorusMixSlider, &chorusMixLabel, &phaserRateSlider, &phaserRateLabel, &phaserDepthSlider, &phaserDepthLabel,
                                                                                   &phaserFeedbackSlider, &phaserFeedbackLabel, &phaserMixSlider, &phaserMixLabel, &delayPreset, &reverbPreset, &chorusPreset, &phaserPreset, &eqPreset })
        component->setVisible (false);
    for (juce::Component* component : std::initializer_list<juce::Component*> { &flangerRateSlider, &flangerRateLabel, &flangerDepthSlider, &flangerDepthLabel,
                                                                                   &flangerDelaySlider, &flangerDelayLabel, &flangerFeedbackSlider, &flangerFeedbackLabel,
                                                                                   &flangerMixSlider, &flangerMixLabel, &flangerPreset, &flutterRateSlider, &flutterRateLabel, &flutterDepthSlider, &flutterDepthLabel, &flutterDelaySlider, &flutterDelayLabel, &flutterMixSlider, &flutterMixLabel, &flutterPreset, &eqLowSlider, &eqLowLabel, &eqMidSlider, &eqMidLabel, &eqHighSlider, &eqHighLabel })
        component->setVisible (false);
    for (juce::Component* component : std::initializer_list<juce::Component*> { &delayEnabledButton, &reverbEnabledButton, &chorusEnabledButton, &phaserEnabledButton, &flangerEnabledButton, &eqEnabledButton, &flutterEnabledButton })
        component->setVisible (false);
    for (juce::Component* component : std::initializer_list<juce::Component*> { &granularSizeSlider, &granularSizeLabel, &granularDensitySlider, &granularDensityLabel, &granularPitchSlider, &granularPitchLabel, &granularMixSlider, &granularMixLabel, &granularPreset, &granularEnabledButton, &harmonyShiftSlider, &harmonyShiftLabel, &harmonyDetuneSlider, &harmonyDetuneLabel, &harmonyMixSlider, &harmonyMixLabel, &harmonyPreset, &harmonyEnabledButton, &randomAmountSlider, &randomAmountLabel, &randomRateSlider, &randomRateLabel, &randomMixSlider, &randomMixLabel, &randomPreset, &randomEnabledButton })
        component->setVisible (false);
    for (juce::Component* slot : std::array<juce::Component*, 14> { &fxSlotOne, &fxSlotTwo, &fxSlotThree, &fxSlotFour, &fxSlotFive, &fxSlotSix, &fxSlotSeven,
                        &fxSlotToggleOne, &fxSlotToggleTwo, &fxSlotToggleThree, &fxSlotToggleFour, &fxSlotToggleFive, &fxSlotToggleSix, &fxSlotToggleSeven })
        slot->setVisible (shouldShowFx);
    for (juce::Label* label : std::initializer_list<juce::Label*> { &delayTimeLabel, &delayFeedbackLabel, &delayMixLabel, &reverbMixLabel,
                                                                       &reverbRoomSizeLabel, &reverbDampingLabel, &reverbWidthLabel, &reverbFreezeLabel,
                                                                       &chorusRateLabel, &chorusDepthLabel, &chorusDelayLabel, &chorusFeedbackLabel, &chorusMixLabel,
                                                                       &phaserRateLabel, &phaserDepthLabel, &phaserFeedbackLabel, &phaserMixLabel,
                                                                       &flangerRateLabel, &flangerDepthLabel, &flangerDelayLabel, &flangerFeedbackLabel, &flangerMixLabel, &eqLowLabel, &eqMidLabel, &eqHighLabel })
        label->setColour (juce::Label::textColourId, juce::Colour (0xff171717));
    resized();
    // The EQ editor is a custom component, so keep an explicit final guard
    // when returning to Sampler. This prevents it surviving a tab repaint.
    if (! shouldShowFx)
    {
        eqCurvePad.setVisible (false);
        eqPreset.setVisible (false);
    }
    repaint();
}

void OneShotSamplerAudioProcessorEditor::showTexturePage()
{
    // Reuse the sampler reset path first so nothing from the rack can leak
    // into the dedicated Texture Lab surface.
    showFxPage (false);
    showingTexture = true;
    samplerTab.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff292929));
    fxTab.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff292929));
    textureTab.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffeb6c2a));
    for (juce::Component* component : std::initializer_list<juce::Component*> { &loadButton, &envelopePad, &outputGainSlider, &outputGainLabel, &attackSlider, &attackLabel, &decaySlider, &decayLabel, &sustainSlider, &sustainLabel, &releaseSlider, &releaseLabel })
        component->setVisible (false);
    for (juce::Component* component : std::initializer_list<juce::Component*> { &granularSizeSlider, &granularSizeLabel, &granularDensitySlider, &granularDensityLabel, &granularPitchSlider, &granularPitchLabel, &granularMixSlider, &granularMixLabel, &granularPreset, &granularEnabledButton, &harmonyShiftSlider, &harmonyShiftLabel, &harmonyDetuneSlider, &harmonyDetuneLabel, &harmonyMixSlider, &harmonyMixLabel, &harmonyPreset, &harmonyEnabledButton, &randomAmountSlider, &randomAmountLabel, &randomRateSlider, &randomRateLabel, &randomMixSlider, &randomMixLabel, &randomPreset, &randomEnabledButton, &granularTextureTab, &harmonyTextureTab, &randomTextureTab })
        component->setVisible (true);
    for (auto* label : { &granularSizeLabel, &granularDensityLabel, &granularPitchLabel, &granularMixLabel, &harmonyShiftLabel, &harmonyDetuneLabel, &harmonyMixLabel, &randomAmountLabel, &randomRateLabel, &randomMixLabel })
        label->setColour (juce::Label::textColourId, juce::Colour (0xff202020));
    resized();
    repaint();
}

void OneShotSamplerAudioProcessorEditor::chooseEffectForSlot (int slot)
{
    // Legacy entry point retained for old UI calls. The rack has no picker;
    // selecting a fixed card only opens that card's editor.
    selectEffect (juce::jlimit (0, 6, slot));
}

void OneShotSamplerAudioProcessorEditor::selectEffect (int effect)
{
    if (effect < 0 || effect > 6)
        return;
    selectedFx = effect;
    if (effectPickerMenu != nullptr)
        effectPickerMenu->setActiveEffect (effect);
    resized();
    repaint();
}

void OneShotSamplerAudioProcessorEditor::toggleEffect (int effect)
{
    switch (effect)
    {
        case 0: delayEnabledButton.triggerClick(); break;
        case 1: reverbEnabledButton.triggerClick(); break;
        case 2: chorusEnabledButton.triggerClick(); break;
        case 3: phaserEnabledButton.triggerClick(); break;
        case 4: flangerEnabledButton.triggerClick(); break;
        case 5: eqEnabledButton.triggerClick(); break;
        case 6: flutterEnabledButton.triggerClick(); break;
        case 7: granularEnabledButton.triggerClick(); break;
        case 8: harmonyEnabledButton.triggerClick(); break;
        case 9: randomEnabledButton.triggerClick(); break;
        default: break;
    }
    resized();
    repaint();
}

void OneShotSamplerAudioProcessorEditor::setSlotEnabled (int slot, bool shouldBeEnabled)
{
    if (slot < 0 || slot >= (int) fxSlotEnabled.size() || fxChain[(size_t) slot] < 0)
        return;
    fxSlotEnabled[(size_t) slot] = shouldBeEnabled;
    audioProcessor.setFxSlotState (slot, fxChain[(size_t) slot], shouldBeEnabled);
    refreshEffectEnableStates();
    resized();
    repaint();
}

void OneShotSamplerAudioProcessorEditor::refreshEffectEnableStates()
{
    // The rack owns only these seven FX. Texture Lab has its own independent
    // engine switches and must never be changed when the rack UI reopens.
    const std::array<const char*, 7> enabledParameters { "delayEnabled", "reverbEnabled", "chorusEnabled", "phaserEnabled", "flangerEnabled", "eqEnabled", "flutterEnabled" };
    for (int effect = 0; effect < 7; ++effect)
    {
        bool shouldBeEnabled = false;
        for (int slot = 0; slot < (int) fxChain.size(); ++slot)
            shouldBeEnabled = shouldBeEnabled || (fxChain[(size_t) slot] == effect && fxSlotEnabled[(size_t) slot]);
        if (auto* parameter = audioProcessor.parameters.getParameter (enabledParameters[(size_t) effect]))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (shouldBeEnabled ? 1.0f : 0.0f));
    }
}

void OneShotSamplerAudioProcessorEditor::removeEffectFromSlot (int slot)
{
    if (slot < 0 || slot >= (int) fxChain.size() || fxChain[(size_t) slot] < 0)
        return;

    const auto effect = fxChain[(size_t) slot];
    juce::TextButton* toggle = effect == 0 ? &delayEnabledButton
        : effect == 1 ? &reverbEnabledButton
        : effect == 2 ? &chorusEnabledButton
        : effect == 3 ? &phaserEnabledButton
                      : effect == 4 ? &flangerEnabledButton : effect == 5 ? &eqEnabledButton : effect == 6 ? &flutterEnabledButton : effect == 7 ? &granularEnabledButton : effect == 8 ? &harmonyEnabledButton : &randomEnabledButton;
    if (toggle->getToggleState())
        toggle->triggerClick();

    fxChain[(size_t) slot] = -1;
    fxSlotEnabled[(size_t) slot] = false;
    audioProcessor.setFxSlotState (slot, -1, false);
    refreshEffectEnableStates();
    selectedFx = -1;
    resized();
    repaint();
}

void OneShotSamplerAudioProcessorEditor::timerCallback()
{
    fxVisualPhase = std::fmod (fxVisualPhase + 0.018f, 1.0f);
    // The sampler page also has live meters, a voice pulse and a playhead.
    repaint();
}

bool OneShotSamplerAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& file : files)
        if (file.endsWithIgnoreCase (".wav") || file.endsWithIgnoreCase (".aif") || file.endsWithIgnoreCase (".aiff"))
            return true;

    return false;
}

bool OneShotSamplerAudioProcessorEditor::isInterestedInDragSource (const SourceDetails& details)
{
    juce::ignoreUnused (details);
    return false;
}

void OneShotSamplerAudioProcessorEditor::itemDropped (const SourceDetails& details)
{
    juce::ignoreUnused (details);
}

void OneShotSamplerAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty())
        return;

    if (audioProcessor.loadSample (juce::File (files[0])))
    {
        sampleLabel.setText (audioProcessor.getSampleName(), juce::dontSendNotification);
        repaint();
    }
}
