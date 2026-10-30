#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
// Flat line design: one blue field and one off-white ink.
const juce::Colour modularBlue (0xff006eff);
const juce::Colour modularInk (0xffe6e6e6);

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
                                               float position, float startAngle, float endAngle, juce::Slider&)
{
    // Line knob: one hairline circle and one pointer. Nothing is filled or shaded.
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (8.0f);
    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto dial = bounds.withSizeKeepingCentre (diameter, diameter);
    const auto centre = dial.getCentre();
    const auto radius = diameter * 0.5f;
    const auto angle = startAngle + position * (endAngle - startAngle);
    const auto direction = juce::Point<float> (std::cos (angle), std::sin (angle));
    g.setColour (modularInk);
    g.drawEllipse (dial.reduced (0.5f), 1.0f);
    g.drawLine ({ centre + direction * (radius * 0.30f), centre + direction * (radius * 0.80f) }, 1.2f);
}

void TextureLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float position, float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (8.0f);
    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto dial = bounds.withSizeKeepingCentre (diameter, diameter);
    const auto centre = dial.getCentre();
    const auto radius = diameter * 0.5f;
    const auto angle = startAngle + position * (endAngle - startAngle);
    const auto direction = juce::Point<float> (std::cos (angle), std::sin (angle));
    g.setColour (modularInk);
    g.drawEllipse (dial.reduced (0.5f), 1.0f);
    g.drawLine ({ centre + direction * (radius * 0.30f), centre + direction * (radius * 0.80f) }, 1.2f);
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
    g.setColour (modularBlue);
    g.fillRoundedRectangle (bounds, 11.0f);
    g.fillPath (pointer);
    g.setColour (modularInk);
    g.drawRoundedRectangle (bounds.reduced (0.7f), 11.0f, 1.0f);
    g.strokePath (pointer, juce::PathStrokeType (1.0f));
    g.setFont (juce::Font ("Helvetica", 10.0f, juce::Font::plain));
    g.drawText ("Select module", 16, 8, 180, 22, juce::Justification::centredLeft);
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
        drawModuleIcon (g, effectID, centre, selected ? modularBlue : modularInk, 0.82f);
        g.setFont (juce::Font ("Helvetica", 9.0f, juce::Font::plain));
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

void FrameworksLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                    bool highlighted, bool down)
{
    // FX cards paint their own single border in the editor. This button is
    // only the hit target, so it must not add a second default JUCE outline.
    if (button.getName() == "fxSlotHitArea")
        return;

    const auto bounds = button.getLocalBounds().toFloat().reduced (0.8f);
    const auto active = button.getToggleState();

    if (button.getName() == "fxSlotToggle")
    {
        g.setColour (modularInk.withAlpha (highlighted ? 1.0f : 0.80f));
        g.drawRoundedRectangle (bounds, bounds.getHeight() * 0.5f, 1.0f);
        if (active)
            g.fillEllipse (bounds.getX() + 4.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        else
            g.drawEllipse (bounds.getX() + 4.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f, 1.0f);
        return;
    }
    if (button.getName() == "effectPickerItem")
    {
        const auto item = button.getLocalBounds().toFloat().reduced (0.7f);
        if (active)
        {
            g.setColour (modularInk);
            g.fillRoundedRectangle (item, 8.0f);
            return;
        }
        if (highlighted)
        {
            g.setColour (modularInk.withAlpha (0.14f));
            g.fillRoundedRectangle (item, 8.0f);
        }
        g.setColour (modularInk);
        g.drawRoundedRectangle (item, 8.0f, 1.0f);
        return;
    }
    if (button.getName() == "performanceScene")
    {
        const auto scene = button.getLocalBounds().toFloat().reduced (0.6f);
        if (active)
        {
            g.setColour (modularInk);
            g.fillRoundedRectangle (scene, 4.0f);
            return;
        }
        if (highlighted)
        {
            g.setColour (modularInk.withAlpha (0.14f));
            g.fillRoundedRectangle (scene, 4.0f);
        }
        g.setColour (modularInk);
        g.drawRoundedRectangle (scene, 4.0f, 1.0f);
        return;
    }

    // Everything else is a hairline pill.
    const auto radius = bounds.getHeight() * 0.5f;
    if (active || down || highlighted)
    {
        g.setColour (modularInk.withAlpha (down ? 0.28f : active ? 0.16f : 0.12f));
        g.fillRoundedRectangle (bounds, radius);
    }
    g.setColour (modularInk);
    g.drawRoundedRectangle (bounds, radius, 1.0f);
}

void FrameworksLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool isMouseOverButton, bool)
{
    if (button.getName() == "fxSlotHitArea" || button.getName() == "effectPickerItem" || button.getName() == "textureIcon")
        return;

    if (button.getName() == "performanceScene")
    {
        g.setColour (button.getToggleState() ? modularBlue : modularInk);
        g.setFont (juce::Font ("Helvetica", 10.0f, juce::Font::bold));
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
        return;
    }

    LookAndFeel_V4::drawButtonText (g, button, isMouseOverButton, false);
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
    return juce::Font ("Helvetica", height, juce::Font::plain);
}

juce::Font FrameworksLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font ("Helvetica", 14.0f, juce::Font::plain);
}

juce::Font FrameworksLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font ("Helvetica", 12.0f, juce::Font::plain);
}

void FrameworksLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
    {
        // Slider readouts are printed directly on the surface. This keeps
        // ADSR and master controls consistent with the new engine cards.
        g.setColour (label.findColour (juce::Label::textColourId));
        g.setFont (juce::Font ("Helvetica", juce::jmin (12.0f, label.getHeight() * 0.60f), juce::Font::plain));
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
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
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

juce::Rectangle<float> EnvelopePad::getGraphArea() const
{
    // This is deliberately a fixed internal lane. The live meter has a
    // reserved strip at the right, so neither the release point nor a handle
    // can collide with it at the card's locked Beta 2 size.
    return getLocalBounds().toFloat().reduced (18.0f, 12.0f)
                           .withTrimmedTop (31.0f)
                           .withTrimmedBottom (4.0f)
                           .withTrimmedRight (25.0f);
}

std::array<juce::Point<float>, 4> EnvelopePad::getHandles() const
{
    const auto area = getGraphArea();
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
    const auto bounds = getLocalBounds().toFloat();
    const auto area = getGraphArea();
    const auto handles = getHandles();
    const auto ink = modularInk;
    g.setColour (ink);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    // Compact header: the component owns this lane, so the curve and its
    // handles can never run through its text.
    g.setFont (juce::Font ("Helvetica", 10.0f, juce::Font::plain));
    g.drawText ("Shape / envelope", bounds.reduced (14.0f).withHeight (20.0f), juce::Justification::centredLeft);
    g.setFont (juce::Font ("Helvetica", 9.0f, juce::Font::plain));
    g.drawText ("Edit", bounds.reduced (14.0f).withHeight (20.0f), juce::Justification::centredRight);

    g.setColour (ink.withAlpha (0.22f));
    for (int x = static_cast<int> (area.getX()); x < area.getRight(); x += 24)
        g.drawVerticalLine (x, area.getY(), area.getBottom());
    for (int y = static_cast<int> (area.getY()); y < area.getBottom(); y += 24)
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
    g.setColour (ink);
    g.strokePath (envelope, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (int i = 0; i < 3; ++i)
    {
        // Fine technical handles: visually minimal, but their 24 px hit area
        // in mouseDown keeps them easy to grab.
        g.setColour (modularBlue);
        g.fillEllipse (handles[(size_t) i].getX() - 3.7f, handles[(size_t) i].getY() - 3.7f, 7.4f, 7.4f);
        g.setColour (ink);
        g.drawEllipse (handles[(size_t) i].getX() - 3.7f, handles[(size_t) i].getY() - 3.7f, 7.4f, 7.4f, 1.2f);
    }

    // A real envelope follower - this is not a decorative animation. It uses
    // the audio-thread envelope peak already exposed by the processor.
    const auto level = juce::jlimit (0.0f, 1.0f, audioProcessor.getEnvelopeLevel());
    const auto meter = juce::Rectangle<float> (area.getRight() + 9.0f, area.getY() + 3.0f, 7.0f, area.getHeight() - 6.0f);
    const auto segments = 9;
    const auto lit = (int) std::round (level * (float) segments);
    for (int segment = 0; segment < segments; ++segment)
    {
        const auto segmentHeight = (meter.getHeight() - (segments - 1) * 3.0f) / (float) segments;
        const auto y = meter.getBottom() - (segment + 1) * segmentHeight - segment * 3.0f;
        const auto cell = juce::Rectangle<float> (meter.getX(), y, meter.getWidth(), segmentHeight);
        if (segment < lit)
        {
            g.setColour (ink);
            g.fillRoundedRectangle (cell, 2.0f);
        }
        else
        {
            g.setColour (ink.withAlpha (0.35f));
            g.drawRoundedRectangle (cell.reduced (0.5f), 2.0f, 1.0f);
        }
    }
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
    const auto area = getGraphArea();
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

void FxSlotButton::mouseDown (const juce::MouseEvent& event)
{
    juce::TextButton::mouseDown (event);
    // Card state changes on press, rather than on the later mouse-up event.
    // This makes a quick tap as dependable as a long click.
    if (onPress)
        onPress();
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
    const auto ink = modularInk;
    g.setColour (ink);
    g.drawRoundedRectangle (area, 4.0f, 1.0f);
    const std::array<juce::String, 8> markers { "20", "60", "200", "600", "2k", "6k", "12k", "20k" };
    const std::array<float, 8> markerFrequencies { 20.0f, 60.0f, 200.0f, 600.0f, 2000.0f, 6000.0f, 12000.0f, 20000.0f };
    for (int i = 0; i < 8; ++i)
    {
        const auto x = area.getX() + area.getWidth() * positionFromFrequency (markerFrequencies[(size_t) i]);
        g.setColour (ink.withAlpha (0.22f));
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        g.setColour (ink.withAlpha (0.85f)); g.setFont (juce::Font ("Helvetica", 9.0f, juce::Font::plain));
        g.drawText (markers[(size_t) i], (int) x - 16, (int) area.getY() + 4, 32, 12, juce::Justification::centred);
    }
    g.setColour (ink.withAlpha (0.30f));
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
    const auto handles = getHandles();
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
    g.setColour (ink); g.strokePath (curve, juce::PathStrokeType (1.4f));
    g.setColour (ink.withAlpha (0.85f)); g.setFont (juce::Font ("Helvetica", 10.0f, juce::Font::plain));
    g.drawText ("20 Hz", area.getX() + 4.0f, area.getBottom() - 15.0f, 50.0f, 12.0f, juce::Justification::left);
    g.drawText ("20 kHz", area.getRight() - 54.0f, area.getBottom() - 15.0f, 50.0f, 12.0f, juce::Justification::right);
    for (int band = 0; band < 7; ++band)
    {
        const auto point = handles[(size_t) band];
        const auto prefix = "eqBand" + juce::String (band + 1);
        const auto width = parameters.getRawParameterValue (prefix + "Q")->load();
        g.setColour (ink.withAlpha (0.10f)); g.fillEllipse (point.x - (22.0f / width), point.y - 14.0f, 44.0f / width, 28.0f);
        g.setColour (modularBlue); g.fillEllipse (point.x - 9.0f, point.y - 9.0f, 18.0f, 18.0f);
        g.setColour (ink); g.drawEllipse (point.x - 9.0f, point.y - 9.0f, 18.0f, 18.0f, 1.0f);
        g.fillEllipse (point.x - 2.5f, point.y - 2.5f, 5.0f, 5.0f);
    }
}

juce::Colour PresetSelector::accentForEffect (int effect)
{
    // The line design has a single ink colour; modules are told apart by name.
    juce::ignoreUnused (effect);
    return modularInk;
}

juce::String PresetSelector::labelForEffect (int effect)
{
    switch (effect)
    {
        case 5: return "EQ preset";
        case 7: return "Granular preset";
        case 2: return "Chorus preset";
        case 9: return "Random preset";
        default: return "Reverb preset";
    }
}

juce::String PresetSelector::presetNameFor (int effect, int preset)
{
    static const std::array<juce::String, 4> eq { "Spectrum", "Low cut", "Warmth", "Air" };
    static const std::array<juce::String, 4> reverb { "Space", "Chamber", "Halo", "Drift" };
    static const std::array<juce::String, 4> granular { "Grain", "Shards", "Cloud", "Dust" };
    static const std::array<juce::String, 4> chorus { "Detune", "Wide", "VHS", "Drift" };
    static const std::array<juce::String, 4> random { "Chance", "Flux", "Tilt", "Ripple" };
    const auto index = (size_t) juce::jlimit (0, 3, preset);
    switch (effect) { case 5: return eq[index]; case 7: return granular[index]; case 2: return chorus[index]; case 9: return random[index]; default: return reverb[index]; }
}

juce::String PresetSelector::presetIdFor (int effect, int preset)
{
    juce::String prefix = "verb";
    switch (effect) { case 5: prefix = "eq"; break; case 7: prefix = "gran"; break; case 2: prefix = "chor"; break; case 9: prefix = "rand"; break; default: break; }
    return "frwk_" + prefix + "00" + juce::String (juce::jlimit (0, 3, preset) + 1);
}

juce::String PresetSelector::presetMetaFor (int effect, int preset)
{
    static const std::array<juce::String, 4> eq { "7-band neutral curve", "high-pass 95 Hz", "analogue warmth", "high-shelf lift" };
    static const std::array<juce::String, 4> reverb { "room / 2.4s decay", "chamber / 1.3s decay", "frozen bloom / wide", "hall / 4.2s decay" };
    static const std::array<juce::String, 4> granular { "185 ms / 5 grains", "95 ms / pitch scatter", "340 ms / cloud mode", "62 ms / dense field" };
    static const std::array<juce::String, 4> chorus { "subtle dual voice", "wide stereo motion", "slow cassette drift", "deep ensemble motion" };
    static const std::array<juce::String, 4> random { "controlled variance", "tempo-linked flux", "tilted probability", "slow evolving chance" };
    const auto index = (size_t) juce::jlimit (0, 3, preset);
    switch (effect) { case 5: return eq[index]; case 7: return granular[index]; case 2: return chorus[index]; case 9: return random[index]; default: return reverb[index]; }
}

int PresetSelector::effectForFilterIndex (int index)
{
    static constexpr std::array<int, 5> effects { 1, 7, 5, 2, 9 };
    return effects[(size_t) juce::jlimit (0, 4, index)];
}

int PresetSelector::filterIndexForEffect (int effect)
{
    for (int index = 0; index < 5; ++index)
        if (effectForFilterIndex (index) == effect)
            return index;
    return 0;
}

juce::Rectangle<int> PresetSelector::pillBounds() const
{
    // The pill leaves room on the right for the previous / next circles.
    const auto height = closedBounds.getHeight();
    return getLocalBounds().withHeight (height).withTrimmedRight (2 * height + 20);
}

juce::Rectangle<int> PresetSelector::stepBounds (int index) const
{
    const auto size = closedBounds.getHeight();
    return { index == 0 ? getWidth() - 2 * size - 10 : getWidth() - size, 0, size, size };
}

juce::Rectangle<int> PresetSelector::menuBounds() const
{
    const auto pill = pillBounds();
    return { 0, pill.getBottom() + 8, getWidth(), juce::jmax (0, getHeight() - pill.getHeight() - 8) };
}

juce::Rectangle<int> PresetSelector::filterBounds (int index) const
{
    auto row = menuBounds().reduced (14, 12).withHeight (28);
    const auto gap = 6;
    const auto width = (row.getWidth() - gap * 4) / 5;
    row.removeFromLeft ((width + gap) * juce::jlimit (0, 4, index));
    return row.withWidth (width);
}

juce::Rectangle<int> PresetSelector::presetRowBounds (int index) const
{
    auto list = menuBounds().reduced (14, 12).withTrimmedTop (42);
    constexpr int rowHeight = 39;
    return list.withTrimmedTop (juce::jlimit (0, 3, index) * (rowHeight + 3)).withHeight (rowHeight);
}

void PresetSelector::updateBounds()
{
    if (closedBounds.isEmpty())
        return;
    // 44 px header + 8 px separation + 234 px menu: enough vertical room for
    // the filter row, four complete preset rows, and the bottom inset.
    setBounds (closedBounds.withHeight (isOpen_ ? 286 : closedBounds.getHeight()));
    if (isOpen_)
        toFront (false);
}

void PresetSelector::setLayoutBounds (juce::Rectangle<int> bounds)
{
    closedBounds = bounds;
    updateBounds();
}

void PresetSelector::setModule (int effect, int preset)
{
    if (effect != 1 && effect != 2 && effect != 5 && effect != 7 && effect != 9)
        effect = 1;
    activeEffect = effect;
    if (! isOpen_)
        filteredEffect = effect;
    selectedPreset = juce::jlimit (0, 3, preset);
    repaint();
}

void PresetSelector::setOpen (bool shouldOpen)
{
    if (isOpen_ == shouldOpen)
        return;
    isOpen_ = shouldOpen;
    if (isOpen_)
        filteredEffect = activeEffect;
    updateBounds();
    repaint();
}

void PresetSelector::paint (juce::Graphics& g)
{
    // Helvetica is deliberately larger and rasterizes cleanly in FL Studio's
    // embedded plugin window at the fixed Beta 2 resolution.
    const auto menuFont = juce::String ("Helvetica");
    const auto ink = modularInk;
    const auto blue = modularBlue;
    const auto pill = pillBounds().toFloat();

    // One rounded pill: module context, preset name and id, counter, chevron.
    g.setColour (ink);
    g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);
    const auto left = (int) pill.getX() + 28;
    g.setFont (juce::Font (menuFont, 11.0f, juce::Font::plain));
    g.drawText (labelForEffect (activeEffect), left, (int) pill.getY(), 130, (int) pill.getHeight(), juce::Justification::centredLeft);
    g.setFont (juce::Font (menuFont, 18.0f, juce::Font::bold));
    g.drawText (presetNameFor (activeEffect, selectedPreset), left + 140, (int) pill.getY(), 120, (int) pill.getHeight(), juce::Justification::centredLeft);
    g.setFont (juce::Font (menuFont, 11.0f, juce::Font::plain));
    g.drawText (presetIdFor (activeEffect, selectedPreset), left + 270, (int) pill.getY(), 160, (int) pill.getHeight(), juce::Justification::centredLeft);

    const auto circleSize = pill.getHeight() - 12.0f;
    const auto circle = juce::Rectangle<float> (pill.getRight() - circleSize - 6.0f, pill.getY() + 6.0f, circleSize, circleSize);
    g.drawText ("0" + juce::String (selectedPreset + 1) + " / 04", (int) circle.getX() - 96, (int) pill.getY(), 84, (int) pill.getHeight(), juce::Justification::centredRight);
    g.drawEllipse (circle.reduced (0.5f), 1.0f);
    // Draw the disclosure marker ourselves so it stays crisp in every font.
    const auto chevronCentre = circle.getCentre();
    juce::Path chevron;
    const auto lift = isOpen_ ? 1.0f : -1.0f;
    chevron.startNewSubPath (chevronCentre.x - 5.0f, chevronCentre.y + lift * 2.5f);
    chevron.lineTo (chevronCentre.x, chevronCentre.y - lift * 2.5f);
    chevron.lineTo (chevronCentre.x + 5.0f, chevronCentre.y + lift * 2.5f);
    g.strokePath (chevron, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Previous / next preset circles.
    for (int step = 0; step < 2; ++step)
    {
        const auto stepArea = stepBounds (step).toFloat();
        const auto centre = stepArea.getCentre();
        const auto direction = step == 0 ? -1.0f : 1.0f;
        g.drawEllipse (stepArea.reduced (0.5f), 1.0f);
        juce::Path arrow;
        arrow.startNewSubPath (centre.x - direction * 2.5f, centre.y - 5.0f);
        arrow.lineTo (centre.x + direction * 2.5f, centre.y);
        arrow.lineTo (centre.x - direction * 2.5f, centre.y + 5.0f);
        g.strokePath (arrow, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (! isOpen_)
        return;

    const auto menu = menuBounds().toFloat();
    g.setColour (blue); g.fillRoundedRectangle (menu, 14.0f);
    g.setColour (ink); g.drawRoundedRectangle (menu.reduced (0.5f), 14.0f, 1.0f);

    for (int index = 0; index < 5; ++index)
    {
        const auto effect = effectForFilterIndex (index);
        const auto chip = filterBounds (index).toFloat();
        const auto selected = effect == filteredEffect;
        if (selected)
        {
            g.setColour (ink);
            g.fillRoundedRectangle (chip, chip.getHeight() * 0.5f);
        }
        else
        {
            g.setColour (ink);
            g.drawRoundedRectangle (chip.reduced (0.5f), chip.getHeight() * 0.5f, 1.0f);
        }
        g.setColour (selected ? blue : ink); g.setFont (juce::Font (menuFont, 10.5f, juce::Font::bold));
        g.drawFittedText (labelForEffect (effect).replace (" preset", ""), (int) chip.getX() + 6, (int) chip.getY(), (int) chip.getWidth() - 12, (int) chip.getHeight(), juce::Justification::centred, 1);
    }

    for (int index = 0; index < 4; ++index)
    {
        const auto row = presetRowBounds (index).toFloat();
        const auto selected = filteredEffect == activeEffect && index == selectedPreset;
        const auto hovered = index == hoveredPreset;
        if (hovered)
        {
            g.setColour (ink.withAlpha (0.12f));
            g.fillRoundedRectangle (row, 8.0f);
        }
        g.setColour (ink.withAlpha (selected ? 1.0f : 0.55f));
        g.drawRoundedRectangle (row.reduced (0.5f), 8.0f, selected ? 1.6f : 1.0f);
        g.setColour (ink); g.setFont (juce::Font (menuFont, 12.5f, juce::Font::bold));
        g.drawText (presetNameFor (filteredEffect, index), (int) row.getX() + 16, (int) row.getY() + 4, 140, 16, juce::Justification::centredLeft);
        g.setFont (juce::Font (menuFont, 10.5f, juce::Font::plain));
        g.drawFittedText (presetMetaFor (filteredEffect, index) + " / " + presetIdFor (filteredEffect, index), (int) row.getX() + 16, (int) row.getY() + 20, (int) row.getWidth() - 56, 14, juce::Justification::centredLeft, 1);
        if (selected)
        {
            const auto checkCentre = juce::Point<float> (row.getRight() - 21.0f, row.getCentreY());
            juce::Path check;
            check.startNewSubPath (checkCentre.x - 5.0f, checkCentre.y);
            check.lineTo (checkCentre.x - 1.2f, checkCentre.y + 4.0f);
            check.lineTo (checkCentre.x + 6.0f, checkCentre.y - 5.0f);
            g.strokePath (check, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
}

void PresetSelector::mouseDown (const juce::MouseEvent& event)
{
    const auto point = event.getPosition();
    if (pillBounds().contains (point))
    {
        setOpen (! isOpen_);
        return;
    }

    // Previous / next circles step through the four presets of the active module.
    for (int step = 0; step < 2; ++step)
        if (stepBounds (step).contains (point))
        {
            selectedPreset = (selectedPreset + (step == 0 ? 3 : 1)) % 4;
            if (onPresetSelected) onPresetSelected (activeEffect, selectedPreset);
            repaint();
            return;
        }

    if (! isOpen_ || ! menuBounds().contains (point))
        return;

    for (int index = 0; index < 5; ++index)
        if (filterBounds (index).contains (point))
        {
            filteredEffect = effectForFilterIndex (index);
            activeEffect = filteredEffect;
            if (onModuleSelected) onModuleSelected (activeEffect);
            repaint();
            return;
        }

    for (int index = 0; index < 4; ++index)
        if (presetRowBounds (index).contains (point))
        {
            activeEffect = filteredEffect;
            selectedPreset = index;
            if (onPresetSelected) onPresetSelected (activeEffect, selectedPreset);
            setOpen (false);
            return;
        }

    setOpen (false);
}

void PresetSelector::mouseMove (const juce::MouseEvent& event)
{
    int nextHovered = -1;
    if (isOpen_)
        for (int index = 0; index < 4; ++index)
            if (presetRowBounds (index).contains (event.getPosition()))
                nextHovered = index;

    if (nextHovered != hoveredPreset)
    {
        hoveredPreset = nextHovered;
        repaint();
    }
}

void PresetSelector::mouseExit (const juce::MouseEvent&)
{
    if (hoveredPreset != -1)
    {
        hoveredPreset = -1;
        repaint();
    }
}

PerformancePad::PerformancePad()
{
    for (auto& point : trail)
        point = { positionX, positionY };
    trailInitialised = true;
    startTimerHz (60);
}

void PerformancePad::pushTrailPoint()
{
    if (! trailInitialised)
    {
        for (auto& point : trail)
            point = { positionX, positionY };
        trailInitialised = true;
    }
    trail[(size_t) trailHead] = { positionX, positionY };
    trailHead = (trailHead + 1) % (int) trail.size();
}

void PerformancePad::setPosition (float newX, float newY, bool notify)
{
    targetX = juce::jlimit (0.0f, 1.0f, newX);
    targetY = juce::jlimit (0.0f, 1.0f, newY);
    if (notify)
    {
        positionX = targetX;
        positionY = targetY;
        pushTrailPoint();
        if (onPositionChanged)
            onPositionChanged (positionX, positionY);
    }
    repaint();
}

void PerformancePad::timerCallback()
{
    animationPhase += 0.045f;
    const auto dx = targetX - positionX;
    const auto dy = targetY - positionY;
    if (std::abs (dx) > 0.00025f || std::abs (dy) > 0.00025f)
    {
        positionX += dx * 0.16f;
        positionY += dy * 0.16f;
        pushTrailPoint();
    }
    else
    {
        positionX = targetX;
        positionY = targetY;
    }
    repaint();
}

void PerformancePad::updateFromPointer (juce::Point<float> point)
{
    const auto pad = getLocalBounds().toFloat().reduced (15.0f);
    if (pad.isEmpty())
        return;
    setPosition ((point.x - pad.getX()) / pad.getWidth(), 1.0f - (point.y - pad.getY()) / pad.getHeight(), true);
}

void PerformancePad::mouseDown (const juce::MouseEvent& event) { updateFromPointer (event.position); }
void PerformancePad::mouseDrag (const juce::MouseEvent& event) { updateFromPointer (event.position); }

void PerformancePad::paint (juce::Graphics& g)
{
    const auto blue = modularBlue;
    const auto white = modularInk;
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (white);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    const auto pad = bounds.reduced (15.0f);
    const auto point = juce::Point<float> (pad.getX() + positionX * pad.getWidth(), pad.getBottom() - positionY * pad.getHeight());
    const auto target = juce::Point<float> (pad.getX() + targetX * pad.getWidth(), pad.getBottom() - targetY * pad.getHeight());

    // A deliberately exaggerated gravity field: every grid line bows toward
    // the performance point instead of sitting motionless behind it.
    const auto distortion = [&point, &pad] (float x, float y)
    {
        const auto dx = (x - point.x) / pad.getWidth();
        const auto dy = (y - point.y) / pad.getHeight();
        return std::exp (-(dx * dx + dy * dy) * 28.0f);
    };
    g.setColour (white.withAlpha (0.26f));
    for (int division = 1; division < 7; ++division)
    {
        const auto baseY = pad.getY() + pad.getHeight() * (float) division / 7.0f;
        juce::Path horizontal;
        for (float x = pad.getX(); x <= pad.getRight(); x += 5.0f)
        {
            const auto bend = distortion (x, baseY) * (point.y - baseY) * 0.45f;
            if (x == pad.getX()) horizontal.startNewSubPath (x, baseY + bend);
            else horizontal.lineTo (x, baseY + bend);
        }
        g.strokePath (horizontal, juce::PathStrokeType (0.8f));

        const auto baseX = pad.getX() + pad.getWidth() * (float) division / 7.0f;
        juce::Path vertical;
        for (float y = pad.getY(); y <= pad.getBottom(); y += 5.0f)
        {
            const auto bend = distortion (baseX, y) * (point.x - baseX) * 0.45f;
            if (y == pad.getY()) vertical.startNewSubPath (baseX + bend, y);
            else vertical.lineTo (baseX + bend, y);
        }
        g.strokePath (vertical, juce::PathStrokeType (0.8f));
    }

    if (point.getDistanceFrom (target) > 1.5f)
    {
        g.setColour (white.withAlpha (0.42f));
        g.drawEllipse (target.x - 8.0f, target.y - 8.0f, 16.0f, 16.0f, 1.0f);
        g.drawLine (point.x, point.y, target.x, target.y, 0.8f);
    }

    const auto pulse = 0.5f + 0.5f * std::sin (animationPhase * 1.8f);
    for (int ring = 0; ring < 4; ++ring)
    {
        const auto size = 26.0f + (float) ring * 18.0f + pulse * 11.0f;
        g.setColour (white.withAlpha (0.36f - ring * 0.060f));
        g.drawEllipse (point.x - size * 0.5f, point.y - size * 0.5f, size, size, 1.1f);
    }
    g.setColour (white); g.fillEllipse (point.x - 6.0f, point.y - 6.0f, 12.0f, 12.0f);
    g.setColour (blue); g.fillEllipse (point.x - 2.0f, point.y - 2.0f, 4.0f, 4.0f);

    g.setColour (white.withAlpha (0.96f));
    g.setFont (juce::Font ("Helvetica", 10.0f, juce::Font::bold));
    g.drawText ("Performance map", (int) pad.getX(), (int) pad.getY() + 2, 150, 16, juce::Justification::centredLeft);
    g.setColour (white.withAlpha (0.72f));
    g.setFont (juce::Font ("Helvetica", 8.5f, juce::Font::plain));
    g.drawText ("Dry", (int) pad.getX(), (int) pad.getBottom() - 14, 36, 12, juce::Justification::centredLeft);
    g.drawText ("Wide", (int) pad.getRight() - 42, (int) pad.getBottom() - 14, 42, 12, juce::Justification::centredRight);
    g.drawText ("Grain", (int) pad.getX(), (int) pad.getY() + 18, 42, 12, juce::Justification::centredLeft);
    g.drawText ("Chaos", (int) pad.getRight() - 49, (int) pad.getY() + 18, 49, 12, juce::Justification::centredRight);
}

OneShotSamplerAudioProcessorEditor::OneShotSamplerAudioProcessorEditor (OneShotSamplerAudioProcessor& processor)
    : AudioProcessorEditor (&processor), audioProcessor (processor), envelopePad (processor.parameters, processor), eqCurvePad (processor.parameters, processor)
{
    int badgeDataSize = 0;
    if (const auto* badgeData = BinaryData::getNamedResource ("modulartop_png", badgeDataSize))
        modularBadge = juce::ImageFileFormat::loadFrom (badgeData, (size_t) badgeDataSize);
    if (const auto* markData = BinaryData::getNamedResource ("fmwnobgdarkgray_png", badgeDataSize))
        darkFrameworksMark = juce::ImageFileFormat::loadFrom (markData, (size_t) badgeDataSize);
    if (const auto* halftoneData = BinaryData::getNamedResource ("frameworkshalftone3000_png", badgeDataSize))
        frameworksHalftoneMark = juce::ImageFileFormat::loadFrom (halftoneData, (size_t) badgeDataSize);
    if (const auto* wordmarkData = BinaryData::getNamedResource ("modularwordmark_png", badgeDataSize))
        modularWordmark = juce::ImageFileFormat::loadFrom (wordmarkData, (size_t) badgeDataSize);
    const auto setupControl = [this] (juce::Slider& slider, juce::Label& label, const juce::String& text)
    {
        slider.setLookAndFeel (&frameworksLookAndFeel);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 21);
        slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xfff2f1ed));
        // Keep readouts printed directly on the panel - no pill/badge behind
        // the value, so the controls stay light and instrument-like.
        slider.setColour (juce::Slider::textBoxTextColourId, modularInk);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (slider);
        label.setText (text, juce::dontSendNotification);
        label.setLookAndFeel (&frameworksLookAndFeel);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, modularInk);
        label.setFont (juce::Font ("Helvetica", 11.0f, juce::Font::plain));
        addAndMakeVisible (label);
    };

    setupControl (outputGainSlider, outputGainLabel, "Output gain");
    outputGainSlider.setName ("outputGain");
    outputGainSlider.setTextValueSuffix (" dB");

    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.parameters, "outputGain", outputGainSlider);

    setupControl (attackSlider, attackLabel, "Attack");
    setupControl (decaySlider, decayLabel, "Decay");
    setupControl (sustainSlider, sustainLabel, "Sustain");
    setupControl (releaseSlider, releaseLabel, "Release");
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "attack", attackSlider);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "decay", decaySlider);
    sustainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "sustain", sustainSlider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "release", releaseSlider);
    setupControl (delayTimeSlider, delayTimeLabel, "Time");
    setupControl (delayFeedbackSlider, delayFeedbackLabel, "Feedback");
    setupControl (delayMixSlider, delayMixLabel, "Mix");
    setupControl (reverbMixSlider, reverbMixLabel, "Mix");
    setupControl (reverbRoomSizeSlider, reverbRoomSizeLabel, "Room size");
    setupControl (reverbDampingSlider, reverbDampingLabel, "Damping");
    setupControl (reverbWidthSlider, reverbWidthLabel, "Width");
    setupControl (reverbFreezeSlider, reverbFreezeLabel, "Freeze");
    delayTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "delayTime", delayTimeSlider);
    delayFeedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "delayFeedback", delayFeedbackSlider);
    delayMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "delayMix", delayMixSlider);
    reverbMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "reverbMix", reverbMixSlider);
    reverbRoomSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "reverbRoomSize", reverbRoomSizeSlider);
    reverbDampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "reverbDamping", reverbDampingSlider);
    reverbWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "reverbWidth", reverbWidthSlider);
    reverbFreezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "reverbFreeze", reverbFreezeSlider);

    setupControl (chorusRateSlider, chorusRateLabel, "Rate");
    setupControl (chorusDepthSlider, chorusDepthLabel, "Depth");
    setupControl (chorusDelaySlider, chorusDelayLabel, "Delay");
    setupControl (chorusFeedbackSlider, chorusFeedbackLabel, "Feedback");
    setupControl (chorusMixSlider, chorusMixLabel, "Mix");
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
    loadButton.setColour (juce::TextButton::textColourOffId, modularInk);
    addAndMakeVisible (envelopePad);

    addAndMakeVisible (performancePad);
    performancePad.onPositionChanged = [this] (float x, float y)
    {
        activePerformanceScene = -1;
        for (auto* button : { &cleanSceneButton, &wideSceneButton, &warpSceneButton, &chaosSceneButton })
            button->setToggleState (false, juce::dontSendNotification);
        setPerformancePosition (x, y);
    };
    performancePad.setPosition (0.32f, 0.42f, false);

    const std::array<juce::TextButton*, 4> sceneButtons { &cleanSceneButton, &wideSceneButton, &warpSceneButton, &chaosSceneButton };
    for (size_t index = 0; index < sceneButtons.size(); ++index)
    {
        auto& button = *sceneButtons[index];
        button.setName ("performanceScene");
        button.setLookAndFeel (&frameworksLookAndFeel);
        button.setClickingTogglesState (false);
        button.setToggleState (index == 0, juce::dontSendNotification);
        button.onClick = [this, index] { applyPerformanceScene ((int) index); };
        addAndMakeVisible (button);
    }

    for (auto* tab : { &samplerTab, &fxTab, &textureTab })
    {
        tab->setLookAndFeel (&frameworksLookAndFeel);
        tab->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff292929));
        tab->setColour (juce::TextButton::textColourOffId, modularInk);
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
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xfff1efe9));
        box.setColour (juce::ComboBox::textColourId, juce::Colour (0xff1c1c1a));
        box.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff1c1c1a));
        box.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff1c1c1a).withAlpha (0.18f));
        addAndMakeVisible (box);
    };
    frameworksLookAndFeel.setColour (juce::PopupMenu::backgroundColourId, modularBlue);
    frameworksLookAndFeel.setColour (juce::PopupMenu::textColourId, modularInk);
    frameworksLookAndFeel.setColour (juce::PopupMenu::highlightedBackgroundColourId, modularInk);
    frameworksLookAndFeel.setColour (juce::PopupMenu::highlightedTextColourId, modularBlue);
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

    presetSelector.onModuleSelected = [this] (int effect)
    {
        // Filtering the menu also focuses its matching module, but never
        // changes its enabled state. Preset browsing must be safe and silent.
        selectEffect (effect);
    };
    presetSelector.onPresetSelected = [this] (int effect, int preset)
    {
        juce::ComboBox* source = effect == 5 ? &eqPreset
                                : effect == 7 ? &granularPreset
                                : effect == 2 ? &chorusPreset
                                : effect == 9 ? &randomPreset : &reverbPreset;
        source->setSelectedId (preset + 1, juce::sendNotificationSync);
        selectEffect (effect);
    };
    addAndMakeVisible (presetSelector);

    for (auto* slot : { &fxSlotOne, &fxSlotTwo, &fxSlotThree, &fxSlotFour, &fxSlotFive, &fxSlotSix, &fxSlotSeven })
    {
        slot->setLookAndFeel (&frameworksLookAndFeel);
        slot->setName ("fxSlotHitArea");
        slot->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        slot->setColour (juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
        addAndMakeVisible (*slot);
    }
    // These transparent hit targets cover only the title/visual portion of a
    // Beta 2 card. They give every card a reliable click target without ever
    // sitting on top of its knobs.
    const auto activateCard = [this] (int effect, int card)
    {
        const auto wasSelected = selectedFx == effect;
        const auto wasEnabled = isEffectEnabled (effect);
        selectEffect (effect);
        setEffectEnabled (effect, ! wasEnabled || ! wasSelected);
        fxCardPulse[(size_t) card] = 1.0f;
    };
    fxSlotOne.onPress = [activateCard] { activateCard (1, 0); };
    fxSlotTwo.onPress = [activateCard] { activateCard (7, 1); };
    fxSlotThree.onPress = [activateCard] { activateCard (9, 2); };
    fxSlotFour.onPress = [activateCard] { activateCard (2, 3); };
    fxSlotFive.onPress = [activateCard] { activateCard (5, 4); };
    fxSlotSix.onClick = [this] { if (effectPickerMenu != nullptr) effectPickerMenu->setVisible (! effectPickerMenu->isVisible()); };
    fxSlotSeven.onClick = [this] { selectEffect (6); };
    for (auto* toggle : { &fxSlotToggleOne, &fxSlotToggleTwo, &fxSlotToggleThree, &fxSlotToggleFour, &fxSlotToggleFive, &fxSlotToggleSix, &fxSlotToggleSeven })
    {
        toggle->setLookAndFeel (&frameworksLookAndFeel);
        toggle->setName ("fxSlotToggle");
        toggle->setColour (juce::TextButton::textColourOffId, modularInk);
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
        button.setColour (juce::TextButton::textColourOffId, modularInk);
        button.setColour (juce::TextButton::textColourOnId, modularInk);
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
    setupControl (eqHighSlider, eqHighLabel, "AIR");
    // These are macro handles for the very same 7-band EQ rendered in the
    // Active Module panel - not a second three-band processor. They stay in
    // sync with direct curve edits through their shared parameter attachments.
    eqLowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "eqBand1Gain", eqLowSlider);
    eqMidAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "eqBand4Gain", eqMidSlider);
    eqHighAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.parameters, "eqBand7Gain", eqHighSlider);
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
    setSize (1640, 720);
    setResizable (false, false);
    // The board now runs a lightweight, cached 60 fps visual clock.
    startTimerHz (60);
}

OneShotSamplerAudioProcessorEditor::~OneShotSamplerAudioProcessorEditor()
{
    removeMouseListener (this);
}

void OneShotSamplerAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    const auto clicked = event.getEventRelativeTo (this).getPosition();

    // The selector is a self-contained popup. A click anywhere outside its
    // bounds closes it before the board handles the same click normally.
    if (presetSelector.isOpen() && ! presetSelector.getBounds().contains (clicked))
        presetSelector.setOpen (false);

    // This editor also hears child events. Let controls keep their normal
    // behaviour, but treat every unoccupied card surface as a card click.
    for (auto* component = event.originalComponent; component != nullptr; component = component->getParentComponent())
        if (dynamic_cast<PresetSelector*> (component) != nullptr
            || dynamic_cast<PerformancePad*> (component) != nullptr
            || dynamic_cast<juce::Slider*> (component) != nullptr
            || dynamic_cast<juce::ComboBox*> (component) != nullptr
            || dynamic_cast<juce::Button*> (component) != nullptr)
            return;

    const std::array<int, 5> tabEffects { 1, 7, 9, 2, 5 };
    for (size_t i = 0; i < beta2EffectTabBounds.size(); ++i)
        if (beta2EffectTabBounds[i].contains (clicked))
        {
            const auto effect = tabEffects[i];
            const auto wasSelected = selectedFx == effect;
            const auto wasEnabled = isEffectEnabled (effect);
            selectEffect (effect);
            // First click enables a dimmed card. A later click on its own
            // selected card turns it back off; selecting another card leaves
            // its audio state intact.
            setEffectEnabled (effect, ! wasEnabled || ! wasSelected);
            fxCardPulse[i] = 1.0f;
            return;
        }

    if (effectPickerMenu == nullptr || ! effectPickerMenu->isVisible())
        return;

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
    // Line design: a single blue field and a single off-white ink. Panels are
    // hairline outlines; nothing is filled, shaded or textured.
    const auto blue = modularBlue;
    const auto ink = modularInk;

    g.fillAll (blue);
    const auto drawPanel = [&g, ink] (juce::Rectangle<int> area, float radius = 6.0f)
    {
        g.setColour (ink);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), radius, 1.0f);
    };
    const auto drawGrid = [&g] (juce::Rectangle<int> area, juce::Colour colour, int step = 30)
    {
        g.setColour (colour);
        for (int x = area.getX(); x < area.getRight(); x += step) g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());
        for (int y = area.getY(); y < area.getBottom(); y += step) g.drawHorizontalLine (y, (float) area.getX(), (float) area.getRight());
    };
    const auto font = [] (float size) { return juce::Font ("Helvetica", size, juce::Font::plain); };

    auto board = getLocalBounds().reduced (18);
    auto fullHeader = board.removeFromTop (56);
    const auto header = fullHeader.removeFromLeft (700);
    auto footer = board.removeFromBottom (30);
    board.removeFromTop (3);
    board.removeFromBottom (3);

    drawPanel (beta2SourceBounds);
    drawPanel (beta2ModuleBounds);
    drawPanel (beta2SignalBounds);
    drawPanel (beta2EnvelopeBounds);
    drawPanel (beta2OutputBounds);
    drawPanel (beta2ChainBounds);
    drawPanel (footer);

    // Header: the official wordmark on the left, two pills for the live state.
    // The preset pill and its circles are drawn by the PresetSelector itself.
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (modularWordmark.isValid())
    {
        g.drawImage (modularWordmark, juce::Rectangle<float> ((float) header.getX() + 2.0f, (float) header.getY() + 13.0f, 180.0f, 30.0f),
                     juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
    }
    else
    {
        g.setColour (ink); g.setFont (font (24.0f));
        g.drawText ("modular", header.getX() + 2, header.getY() + 13, 180, 30, juce::Justification::centredLeft);
    }
    const auto pillY = (float) header.getCentreY() - 18.0f;
    const auto timePill = juce::Rectangle<float> ((float) header.getRight() - 86.0f, pillY, 86.0f, 36.0f);
    const auto flowPill = juce::Rectangle<float> (timePill.getX() - 10.0f - 122.0f, pillY, 122.0f, 36.0f);
    g.setColour (ink);
    g.drawRoundedRectangle (flowPill.reduced (0.5f), 18.0f, 1.0f);
    g.drawRoundedRectangle (timePill.reduced (0.5f), 18.0f, 1.0f);
    g.fillEllipse (flowPill.getX() + 18.0f, flowPill.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setFont (font (10.0f));
    g.drawText ("Signal flow", flowPill.withTrimmedLeft (32.0f), juce::Justification::centredLeft);
    g.drawText ("01 / C3", timePill, juce::Justification::centred);

    // Five engines, five outlined cards. Each module stays legible at rest and
    // becomes a proper, animated editor when it is selected.
    const std::array<juce::String, 5> tabNames { "Reverb", "Granular", "Random", "Chorus", "EQ" };
    const std::array<juce::String, 5> tabSubtitles { "Space", "Grain", "Chance", "Detune", "Spectrum" };
    const std::array<int, 5> tabEffects { 1, 7, 9, 2, 5 };
    const auto motion = fxVisualPhase * juce::MathConstants<float>::twoPi;
    const auto mousePosition = getMouseXYRelative();
    for (size_t i = 0; i < beta2EffectTabBounds.size(); ++i)
    {
        const auto tab = beta2EffectTabBounds[i];
        const auto enabled = isEffectEnabled (tabEffects[i]);
        const auto active = enabled && selectedFx == tabEffects[i];
        const auto hovered = tab.contains (mousePosition);
        const auto reveal = fxCardReveal[i];
        const auto pulse = fxCardPulse[i];
        const auto card = tab.toFloat();
        // Disabled cards rest quieter; enabled and hovered cards come to full ink.
        const auto lineAlpha = juce::jmin (1.0f, 0.55f + 0.45f * reveal + (hovered ? 0.20f : 0.0f));
        const auto cardInk = ink.withAlpha (lineAlpha);

        g.setColour (cardInk);
        g.drawRoundedRectangle (card.reduced (1.0f), 16.0f, (active ? 2.0f : 1.0f) + pulse * 1.2f);

        auto inner = tab.reduced (13);
        auto title = inner.removeFromTop (30);
        g.setColour (cardInk); g.setFont (font (active ? 13.0f : 12.0f));
        g.drawText (tabNames[i], title.reduced (9, 2).withTrimmedRight (64), juce::Justification::centredLeft);
        g.setFont (font (10.0f));
        g.drawText (tabSubtitles[i], title.reduced (9, 2).withTrimmedLeft (64), juce::Justification::centredRight);

        auto visual = inner.removeFromTop (active ? 91 : 72).reduced (7, 6);
        g.setColour (cardInk.withMultipliedAlpha (0.9f));
        g.drawRoundedRectangle (visual.toFloat().reduced (0.5f), 6.0f, 1.0f);
        g.setColour (cardInk);
        const auto visualCentre = visual.getCentre().toFloat();
        // All card loops are sine-based and phase-continuous. There is no
        // reset frame or coordinate wrap, so they can run indefinitely with
        // no visual "click" at the end of a cycle.
        const auto visualMotion = motion * (enabled ? (hovered ? 1.24f : 0.86f) : 0.22f);
        // Audio energy controls the scale of every engine's motion. The
        // underlying phase remains continuous, but the board visibly wakes
        // up when the instrument is actually producing sound.
        const auto signalResponse = 0.26f + visualSignalLevel * 0.74f;
        if (i == 0) // space rings
        {
            for (int ring = 1; ring < 5; ++ring)
            {
                const auto wobble = std::sin (visualMotion * 2.0f + (float) ring) * 3.0f * signalResponse;
                g.drawEllipse (visualCentre.x - ring * 10.0f - wobble, visualCentre.y - ring * 6.0f, ring * 20.0f + wobble * 2.0f, ring * 12.0f, 0.85f);
            }
        }
        else if (i == 1) // grain field
        {
            for (int dot = 0; dot < 22; ++dot)
            {
                const auto x = visualCentre.x + std::sin ((float) dot * 1.91f + visualMotion * (0.6f + (dot % 4) * 0.12f)) * (visual.getWidth() * 0.39f * signalResponse);
                const auto y = visualCentre.y + std::cos ((float) dot * 1.27f + visualMotion * (0.8f + (dot % 3) * 0.17f)) * ((dot % 3) * 5.0f + 4.0f) * signalResponse;
                g.fillEllipse (x, y, 2.5f, 2.5f);
            }
        }
        else if (i == 2) // controlled stochastic path
        {
            juce::Path p; p.startNewSubPath ((float) visual.getX(), visualCentre.y);
            for (int x = 0; x <= visual.getWidth(); x += 9)
                p.lineTo ((float) visual.getX() + x, visualCentre.y + std::sin ((float) x * .20f + visualMotion * 5.0f) * (7.0f + (x % 4) * 2.0f) * signalResponse);
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }
        else if (i == 3) // detuned lines
        {
            for (int line = -2; line <= 2; ++line)
            {
                juce::Path p; p.startNewSubPath ((float) visual.getX(), visualCentre.y + line * 6.0f);
                for (int x = 0; x < visual.getWidth(); x += 7)
                    p.lineTo ((float) visual.getX() + x, visualCentre.y + line * 6.0f + std::sin ((float) x * .10f + visualMotion * (2.0f + line * .15f)) * 4.0f * signalResponse);
                g.strokePath (p, juce::PathStrokeType (1.0f));
            }
        }
        else // spectrum columns
        {
            for (int bar = 0; bar < 11; ++bar)
            {
                const auto height = 6.0f + (1.0f + std::sin (visualMotion * 5.0f + bar * .8f)) * 11.0f * signalResponse;
                const auto x = (float) visual.getX() + 11.0f + bar * (float) (visual.getWidth() - 22) / 10.0f;
                g.drawLine (x, visualCentre.y + height * .5f, x, visualCentre.y - height * .5f, 1.0f);
            }
        }

        // One travelling point is the activation signal: it is clipped to the
        // visual area and finishes before the card settles into its loop.
        if (pulse > 0.01f)
        {
            const auto progress = 1.0f - pulse;
            const auto x = (float) visual.getX() + progress * (float) visual.getWidth();
            g.setColour (ink.withAlpha (pulse * 0.28f));
            g.drawLine ((float) visual.getX(), visualCentre.y, x, visualCentre.y, 1.0f);
            g.setColour (ink.withAlpha (pulse));
            g.fillEllipse (x - 3.0f, visualCentre.y - 3.0f, 6.0f, 6.0f);
        }

        if (active)
        {
            auto controlStrip = inner.reduced (1).withTrimmedTop (4);
            g.setColour (ink.withAlpha (0.55f));
            g.drawRoundedRectangle (controlStrip.toFloat().reduced (0.5f), 10.0f, 1.0f);
            g.setColour (ink); g.setFont (font (9.0f));
            g.drawText ("Live controls", controlStrip.removeFromTop (17).reduced (8, 0), juce::Justification::centredLeft);
        }
    }

    // Source / 01: the loaded sample as one thin line.
    auto sourceInner = beta2SourceBounds.reduced (20);
    g.setColour (ink); g.setFont (font (10.0f)); g.drawText ("Source / 01", sourceInner.removeFromTop (18), juce::Justification::centredLeft);
    auto sourceWave = sourceInner.withTrimmedTop (174).withTrimmedBottom (56);
    g.setColour (ink);
    g.setFont (font (30.0f));
    const auto sourceName = audioProcessor.hasLoadedSample() ? audioProcessor.getSampleName().upToFirstOccurrenceOf (".", false, false) : "Nocturne\nOne shot";
    g.drawFittedText (sourceName, sourceInner.withTrimmedTop (38).withHeight (62), juce::Justification::centredLeft, 2);
    g.setFont (font (10.0f)); g.setColour (ink.withAlpha (0.85f));
    const auto metadata = audioProcessor.hasLoadedSample()
        ? audioProcessor.getSampleName() + "  /  " + juce::String ((int) audioProcessor.getSampleRate()) + " Hz"
        : "Source ready  /  44.1 kHz  /  24 bit";
    g.drawText (metadata, sourceInner.withTrimmedTop (117).withHeight (16), juce::Justification::centredLeft);

    // Centre axis, then a single connected line that swings between the upper
    // and lower peak of each slice of the sample.
    g.setColour (ink.withAlpha (0.35f));
    g.drawHorizontalLine (sourceWave.getCentreY(), (float) sourceWave.getX(), (float) sourceWave.getRight());
    juce::Path trace;
    if (audioProcessor.hasLoadedSample())
    {
        const auto peaks = audioProcessor.getWaveformPeaks (juce::jmax (24, sourceWave.getWidth() / 3));
        const auto centreY = (float) sourceWave.getCentreY();
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const auto divisor = peaks.size() > 1 ? peaks.size() - 1 : (size_t) 1;
            const auto x = (float) sourceWave.getX() + (float) i * (float) sourceWave.getWidth() / (float) divisor;
            const auto height = peaks[i] * sourceWave.getHeight() * 0.44f;
            const auto y = centreY + (i % 2 == 0 ? -height : height);
            if (i == 0) trace.startNewSubPath (x, y); else trace.lineTo (x, y);
        }
    }
    else
    {
        constexpr int points = 64;
        for (int i = 0; i <= points; ++i)
        {
            const auto t = (float) i / (float) points;
            const auto amplitude = (float) sourceWave.getHeight() * 0.44f * std::exp (-3.2f * t);
            const auto x = (float) sourceWave.getX() + t * (float) sourceWave.getWidth();
            const auto y = (float) sourceWave.getCentreY() + (i % 2 == 0 ? -amplitude : amplitude);
            if (i == 0) trace.startNewSubPath (x, y); else trace.lineTo (x, y);
        }
    }
    g.setColour (ink);
    g.strokePath (trace, juce::PathStrokeType (1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

    // Signal Map: a wireframe oscilloscope - inspired by early digital audio
    // terminals, but driven by Modular's live output level.
    auto map = beta2SignalBounds.reduced (21);
    g.setColour (ink); g.setFont (font (10.0f)); g.drawText ("Signal map", map.removeFromTop (18), juce::Justification::centredLeft);
    g.fillEllipse ((float) beta2SignalBounds.getRight() - 59.0f, (float) beta2SignalBounds.getY() + 24.0f, 5.0f, 5.0f);
    g.drawText ("Live", beta2SignalBounds.getRight() - 50, beta2SignalBounds.getY() + 18, 30, 18, juce::Justification::centredRight);
    auto scope = map.reduced (1, 9).withTrimmedBottom (17);
    g.setColour (ink); g.drawRoundedRectangle (scope.toFloat().reduced (0.5f), 4.0f, 1.0f);
    auto scopeInner = scope.reduced (9, 18);
    g.saveState();
    g.reduceClipRegion (scopeInner);
    const auto scopeEnergy = 0.16f + visualSignalLevel * 0.84f;
    constexpr int scanCount = 24;
    constexpr int pointsPerScan = 48;
    for (int scan = 0; scan < scanCount; ++scan)
    {
        const auto depth = (float) scan / (float) (scanCount - 1);
        const auto perspective = 0.44f + depth * 0.56f;
        const auto originX = (float) scopeInner.getX() + (1.0f - perspective) * 28.0f;
        const auto width = (float) scopeInner.getWidth() * perspective;
        const auto baseY = (float) scopeInner.getBottom() - 4.0f - depth * ((float) scopeInner.getHeight() - 12.0f);
        const auto phaseOffset = motion * (1.8f + depth * 2.2f) - depth * 5.0f;
        juce::Path waveform;
        for (int pointIndex = 0; pointIndex < pointsPerScan; ++pointIndex)
        {
            const auto t = (float) pointIndex / (float) (pointsPerScan - 1);
            const auto envelope = std::sin (t * juce::MathConstants<float>::pi);
            const auto carrier = std::sin (t * 18.0f + phaseOffset)
                               + 0.46f * std::sin (t * 43.0f - phaseOffset * 1.7f)
                               + 0.18f * std::sin (t * 71.0f + phaseOffset * 2.4f);
            const auto amplitude = (4.0f + scopeEnergy * 18.0f) * perspective * envelope;
            const auto x = originX + t * width;
            const auto y = baseY - carrier * amplitude;
            if (pointIndex == 0) waveform.startNewSubPath (x, y); else waveform.lineTo (x, y);
        }
        g.setColour (ink.withAlpha (0.30f + depth * 0.70f));
        g.strokePath (waveform, juce::PathStrokeType (0.55f + depth * 0.45f));
    }
    g.restoreState();
    g.setColour (ink); g.setFont (font (8.0f));
    g.drawText ("Waveform display", scope.getX() + 9, scope.getY() + 4, 118, 11, juce::Justification::centredLeft);
    g.drawText ("Stereo / live", scope.getRight() - 86, scope.getY() + 4, 77, 11, juce::Justification::centredRight);
    g.setColour (ink.withAlpha (0.40f)); g.drawLine ((float) scope.getX() + 8.0f, (float) scope.getY() + 16.0f, (float) scope.getRight() - 8.0f, (float) scope.getY() + 16.0f, 0.7f);
    g.setColour (ink); g.setFont (font (10.0f)); g.drawText ("Audio vector / output monitor", beta2SignalBounds.reduced (20).withTrimmedTop (beta2SignalBounds.getHeight() - 43), juce::Justification::centredLeft);

    // Master output / limiter is a single final-stage block: level on the
    // left, a dedicated safety meter on the right.
    auto outputInner = beta2OutputBounds.reduced (20);
    g.setColour (ink); g.setFont (font (10.0f)); g.drawText ("Master output / limiter", outputInner.removeFromTop (18), juce::Justification::centredLeft);
    g.setFont (font (10.0f)); g.drawText ("Output gain", beta2OutputBounds.getX() + 22, beta2OutputBounds.getY() + 63, 116, 16, juce::Justification::centredLeft);
    g.setColour (ink.withAlpha (0.60f)); g.drawLine ((float) beta2OutputBounds.getX() + 20.0f, (float) beta2OutputBounds.getY() + 87.0f, (float) beta2OutputBounds.getX() + 143.0f, (float) beta2OutputBounds.getY() + 87.0f, 1.0f);

    const auto limiter = juce::Rectangle<int> (beta2OutputBounds.getX() + 164, beta2OutputBounds.getY() + 51, beta2OutputBounds.getWidth() - 184, 173);
    g.setColour (ink); g.drawRoundedRectangle (limiter.toFloat().reduced (0.5f), 6.0f, 1.0f);
    g.setFont (font (11.0f)); g.drawText ("Limiter", limiter.reduced (12, 8).withHeight (17), juce::Justification::centredLeft);
    g.fillEllipse ((float) limiter.getRight() - 24.0f, (float) limiter.getY() + 13.0f, 6.0f, 6.0f);
    g.setFont (font (9.0f)); g.drawText ("Safe", limiter.getRight() - 50, limiter.getY() + 8, 24, 16, juce::Justification::centredLeft);
    g.setColour (ink.withAlpha (0.40f)); g.drawLine ((float) limiter.getX() + 12.0f, (float) limiter.getY() + 37.0f, (float) limiter.getRight() - 12.0f, (float) limiter.getY() + 37.0f, 1.0f);
    const auto outputActivity = visualSignalLevel;
    const auto activeSegments = juce::jlimit (0, 8, (int) std::ceil (outputActivity * 8.0f));
    for (int segment = 0; segment < 8; ++segment)
    {
        const auto meter = juce::Rectangle<float> ((float) limiter.getX() + 14.0f + segment * 11.0f, (float) limiter.getY() + 58.0f, 7.0f, 38.0f);
        g.setColour (ink.withAlpha (segment < activeSegments ? 1.0f : 0.25f));
        g.fillRoundedRectangle (meter, 2.0f);
    }
    g.setColour (ink); g.setFont (font (10.0f)); g.drawText ("Ceiling  -0.3 dB", limiter.getX() + 12, limiter.getY() + 108, limiter.getWidth() - 24, 15, juce::Justification::centred);
    g.setFont (font (9.0f)); g.drawText ("True peak safe", limiter.getX() + 12, limiter.getY() + 130, limiter.getWidth() - 24, 15, juce::Justification::centred);

    // The active module gets a different compact visual for each engine.
    auto moduleInner = beta2ModuleBounds.reduced (16);
    const std::array<juce::String, 10> moduleNames { "Ping pong delay", "Reverb", "Chorus", "Phaser", "Flanger", "EQ", "Flutter", "Granular", "Orbit", "Random" };
    const auto moduleName = selectedFx >= 0 && selectedFx < (int) moduleNames.size() ? moduleNames[(size_t) selectedFx] : "REVERB";
    g.setColour (ink); g.setFont (font (10.0f));
    g.drawText ("Active module", moduleInner.removeFromTop (18), juce::Justification::centredLeft);
    g.drawText (moduleName, beta2ModuleBounds.getX() + 118, beta2ModuleBounds.getY() + 20, beta2ModuleBounds.getWidth() - 138, 18, juce::Justification::centredLeft);
    g.setFont (font (20.0f)); g.drawText (selectedFx == 5 ? "7-band EQ" : selectedFx == 7 ? "Grain field" : selectedFx == 9 ? "Chance field" : selectedFx == 2 ? "Detune field" : "Space", beta2ModuleBounds.getX() + 22, beta2ModuleBounds.getY() + 51, beta2ModuleBounds.getWidth() - 44, 26, juce::Justification::centredLeft);
    auto moduleVisual = juce::Rectangle<int> (beta2ModuleBounds.getX() + 20, beta2ModuleBounds.getY() + 84, beta2ModuleBounds.getWidth() - 40, beta2ModuleBounds.getHeight() - 104);
    g.setColour (ink); g.drawRoundedRectangle (moduleVisual.toFloat().reduced (0.5f), 4.0f, 1.0f);
    const auto visualCentre = moduleVisual.getCentre().toFloat();
    if (selectedFx == 7)
    {
        // Grain field: grains are outlined circles resting on a quiet line
        // lattice. The live grain carries a centre dot and a ring that breathes
        // with the output level.
        const auto field = moduleVisual.reduced (12).toFloat();
        constexpr int rows = 3;
        const auto rowSpacing = field.getHeight() / (float) rows;
        const float columns[5] = { 0.10f, 0.30f, 0.52f, 0.74f, 0.92f };
        g.setColour (ink.withAlpha (0.30f));
        for (int row = 0; row < rows; ++row)
            g.drawHorizontalLine ((int) (field.getY() + rowSpacing * ((float) row + 0.5f)), field.getX(), field.getRight());
        for (const auto column : columns)
            g.drawVerticalLine ((int) (field.getX() + field.getWidth() * column), field.getY(), field.getBottom());

        const int grainColumn[10] = { 0, 1, 3, 4, 0, 3, 4, 1, 2, 4 };
        const int grainRow[10]    = { 0, 0, 0, 0, 1, 1, 1, 2, 2, 2 };
        const float grainSize[10] = { 0.55f, 0.95f, 0.45f, 0.75f, 0.45f, 0.95f, 0.45f, 0.75f, 0.45f, 0.95f };
        const auto unit = rowSpacing * 0.46f;
        for (int grain = 0; grain < 10; ++grain)
        {
            const auto cx = field.getX() + field.getWidth() * columns[grainColumn[grain]];
            const auto cy = field.getY() + rowSpacing * ((float) grainRow[grain] + 0.5f);
            const auto r = unit * grainSize[grain];
            g.setColour (blue); g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
            g.setColour (ink); g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);
        }
        const auto liveX = field.getX() + field.getWidth() * columns[2];
        const auto liveY = field.getY() + rowSpacing * 1.5f;
        const auto liveR = unit * 1.15f;
        const auto ringR = liveR * 1.45f + visualSignalLevel * 4.0f;
        g.setColour (ink.withAlpha (0.45f)); g.drawEllipse (liveX - ringR, liveY - ringR, ringR * 2.0f, ringR * 2.0f, 1.0f);
        g.setColour (blue); g.fillEllipse (liveX - liveR, liveY - liveR, liveR * 2.0f, liveR * 2.0f);
        g.setColour (ink); g.drawEllipse (liveX - liveR, liveY - liveR, liveR * 2.0f, liveR * 2.0f, 1.0f);
        g.fillEllipse (liveX - 2.5f, liveY - 2.5f, 5.0f, 5.0f);
    }
    else
    {
        drawGrid (moduleVisual.reduced (12), ink.withAlpha (0.22f), 25);
        g.setColour (ink);
        if (selectedFx == 5)
        {
            juce::Path eq; eq.startNewSubPath ((float) moduleVisual.getX() + 8, visualCentre.y);
            for (int x = moduleVisual.getX() + 8; x < moduleVisual.getRight() - 8; x += 4)
                eq.lineTo ((float) x, visualCentre.y - std::sin ((float) x * .07f + motion * 0.75f) * 17.0f);
            g.strokePath (eq, juce::PathStrokeType (1.0f));
        }
        else
        {
            for (int r = 1; r < 5; ++r) g.drawEllipse (visualCentre.x - r * 19.0f, visualCentre.y - r * 11.0f, r * 38.0f, r * 22.0f, 0.9f);
            g.drawLine ((float) moduleVisual.getX() + 18.0f, visualCentre.y, (float) moduleVisual.getRight() - 18.0f, visualCentre.y, 0.9f);
        }
        g.fillEllipse (visualCentre.x - 3.0f, visualCentre.y - 3.0f, 6.0f, 6.0f);
    }

    // Performance Map: a playable macro surface replaces the dense routing
    // list. It controls space on X and texture/movement on Y.
    g.setColour (ink); g.setFont (font (12.0f)); g.drawText ("Performance", beta2ChainBounds.getX() + 20, beta2ChainBounds.getY() + 16, 126, 18, juce::Justification::centredLeft);
    g.setFont (font (10.0f)); g.drawText ("Morph / play", beta2ChainBounds.getRight() - 116, beta2ChainBounds.getY() + 17, 96, 16, juce::Justification::centredRight);
    g.drawText ("Drag the point / X space / Y texture", beta2ChainBounds.getX() + 20, beta2ChainBounds.getBottom() - 64, beta2ChainBounds.getWidth() - 40, 14, juce::Justification::centredLeft);

    // Fixed information rail makes this a board rather than a collection of tabs.
    g.setColour (ink); g.setFont (font (9.0f));
    g.drawText ("FRWK / Modular Beta 2", footer.getX() + 18, footer.getY(), 170, footer.getHeight(), juce::Justification::centredLeft);
    g.drawText ("Key C3", footer.getX() + 205, footer.getY(), 48, footer.getHeight(), juce::Justification::centred);
    g.drawText ("120 BPM", footer.getX() + 260, footer.getY(), 60, footer.getHeight(), juce::Justification::centred);
    g.setColour (ink.withAlpha (0.30f)); g.fillRect (footer.getX() + 326, footer.getY() + 13, 70, 3);
    g.setColour (ink); g.fillRect (footer.getX() + 326, footer.getY() + 13, 38, 3);
    g.drawText ("8 voices", footer.getX() + 410, footer.getY(), 70, footer.getHeight(), juce::Justification::centred);
    for (int i = 0; i < 6; ++i) { g.setColour (i < 4 ? ink : ink.withAlpha (0.30f)); g.fillRect (footer.getRight() - 18 - i * 6, footer.getY() + 10, 1, 10); }
}

void OneShotSamplerAudioProcessorEditor::rebuildBeta2DotLayers()
{
    // The line design is completely flat: no dotted pegboard layers.
    beta2CanvasDotLayer = {};
    beta2PanelDotLayer = {};
}

void OneShotSamplerAudioProcessorEditor::resized()
{
    // This is deliberately a fixed board.  There are no responsive layers
    // competing for the same pixels as there were in Beta 1.
    auto board = getLocalBounds().reduced (18);
    auto headerRow = board.removeFromTop (56);
    headerRow.removeFromLeft (700);
    headerRow.removeFromLeft (12);
    beta2PresetBounds = headerRow.reduced (12, 6);
    board.removeFromTop (3);
    board.removeFromBottom (33);
    const auto gap = 4;
    // Reserve the coloured engine bank before laying out the original board.
    // It used to be calculated after the board had already consumed all width.
    const auto engineWidth = 500;
    auto engineColumn = board.removeFromRight (engineWidth);
    board.removeFromRight (12);
    auto sourceColumn = board.removeFromLeft ((int) std::round (board.getWidth() * 0.42f));
    board.removeFromLeft (gap);
    auto signalColumn = board.removeFromLeft ((int) std::round (board.getWidth() * 0.45f));
    board.removeFromLeft (gap);
    auto outputColumn = board;
    beta2SourceBounds = sourceColumn.removeFromTop (390);
    sourceColumn.removeFromTop (gap);
    beta2ModuleBounds = sourceColumn;
    beta2SignalBounds = signalColumn.removeFromTop (255);
    signalColumn.removeFromTop (gap);
    beta2EnvelopeBounds = signalColumn;
    beta2OutputBounds = outputColumn.removeFromTop (255);
    outputColumn.removeFromTop (gap);
    beta2ChainBounds = outputColumn;

    // Dedicated engine tabs live to the right of the main board, matching the
    // reference colours and keeping one editor/preset context at a time.
    auto tabArea = engineColumn;
    auto topTabs = tabArea.removeFromTop (255);
    beta2EffectTabBounds[0] = topTabs.removeFromLeft ((topTabs.getWidth() - 10) / 2);
    topTabs.removeFromLeft (10);
    auto topRest = topTabs;
    beta2EffectTabBounds[1] = topRest;
    tabArea.removeFromTop (12);
    auto lower = tabArea;
    const auto third = (lower.getWidth() - 20) / 3;
    beta2EffectTabBounds[2] = lower.removeFromLeft (third);
    lower.removeFromLeft (10);
    beta2EffectTabBounds[3] = lower.removeFromLeft (third);
    lower.removeFromLeft (10);
    beta2EffectTabBounds[4] = lower;

    const auto placeControl = [] (juce::Slider& slider, juce::Label& label, juce::Rectangle<int> area)
    {
        slider.setBounds (area.withTrimmedTop (8).withHeight (64));
        label.setBounds (area.withTrimmedTop (74).withHeight (16));
        slider.setVisible (true); label.setVisible (true);
        label.setColour (juce::Label::textColourId, modularInk);
    };
    const auto hide = [] (juce::Component& component) { component.setVisible (false); component.setBounds (0, 0, 0, 0); };
    for (auto* component : std::initializer_list<juce::Component*> {
        &performancePad, &cleanSceneButton, &wideSceneButton, &warpSceneButton, &chaosSceneButton,
        &sampleLabel, &samplerTab, &fxTab, &textureTab, &granularTextureTab, &harmonyTextureTab, &randomTextureTab,
        &fxSlotOne, &fxSlotTwo, &fxSlotThree, &fxSlotFour, &fxSlotFive, &fxSlotSix, &fxSlotSeven,
        &fxSlotToggleOne, &fxSlotToggleTwo, &fxSlotToggleThree, &fxSlotToggleFour, &fxSlotToggleFive, &fxSlotToggleSix, &fxSlotToggleSeven,
        &delayEnabledButton, &reverbEnabledButton, &chorusEnabledButton, &phaserEnabledButton, &flangerEnabledButton, &eqEnabledButton, &flutterEnabledButton,
        &granularEnabledButton, &harmonyEnabledButton, &randomEnabledButton,
        &delayTimeSlider, &delayTimeLabel, &delayFeedbackSlider, &delayFeedbackLabel, &delayMixSlider, &delayMixLabel, &delayPreset,
        &reverbMixSlider, &reverbMixLabel, &reverbRoomSizeSlider, &reverbRoomSizeLabel, &reverbDampingSlider, &reverbDampingLabel, &reverbWidthSlider, &reverbWidthLabel, &reverbFreezeSlider, &reverbFreezeLabel, &reverbPreset,
        &chorusRateSlider, &chorusRateLabel, &chorusDepthSlider, &chorusDepthLabel, &chorusDelaySlider, &chorusDelayLabel, &chorusFeedbackSlider, &chorusFeedbackLabel, &chorusMixSlider, &chorusMixLabel, &chorusPreset,
        &phaserRateSlider, &phaserRateLabel, &phaserDepthSlider, &phaserDepthLabel, &phaserFeedbackSlider, &phaserFeedbackLabel, &phaserMixSlider, &phaserMixLabel, &phaserPreset,
        &flangerRateSlider, &flangerRateLabel, &flangerDepthSlider, &flangerDepthLabel, &flangerDelaySlider, &flangerDelayLabel, &flangerFeedbackSlider, &flangerFeedbackLabel, &flangerMixSlider, &flangerMixLabel, &flangerPreset,
        &flutterRateSlider, &flutterRateLabel, &flutterDepthSlider, &flutterDepthLabel, &flutterDelaySlider, &flutterDelayLabel, &flutterMixSlider, &flutterMixLabel, &flutterPreset,
        &granularSizeSlider, &granularSizeLabel, &granularDensitySlider, &granularDensityLabel, &granularPitchSlider, &granularPitchLabel, &granularMixSlider, &granularMixLabel, &granularPreset,
        &harmonyShiftSlider, &harmonyShiftLabel, &harmonyDetuneSlider, &harmonyDetuneLabel, &harmonyMixSlider, &harmonyMixLabel, &harmonyPreset,
        &randomAmountSlider, &randomAmountLabel, &randomRateSlider, &randomRateLabel, &randomMixSlider, &randomMixLabel, &randomPreset,
        &eqLowSlider, &eqLowLabel, &eqMidSlider, &eqMidLabel, &eqHighSlider, &eqHighLabel, &eqCurvePad, &eqPreset }) hide (*component);

    auto performanceArea = beta2ChainBounds.reduced (16);
    performancePad.setBounds (performanceArea.withTrimmedTop (31).withTrimmedBottom (58));
    performancePad.setVisible (true);
    auto scenes = performanceArea.removeFromBottom (34);
    const auto sceneGap = 6;
    const auto sceneWidth = (scenes.getWidth() - sceneGap * 3) / 4;
    const std::array<juce::TextButton*, 4> sceneButtons { &cleanSceneButton, &wideSceneButton, &warpSceneButton, &chaosSceneButton };
    for (size_t index = 0; index < sceneButtons.size(); ++index)
    {
        beta2SceneBounds[index] = scenes.removeFromLeft (sceneWidth);
        sceneButtons[index]->setBounds (beta2SceneBounds[index]);
        sceneButtons[index]->setVisible (true);
        if (index + 1 < sceneButtons.size()) scenes.removeFromLeft (sceneGap);
    }

    loadButton.setVisible (true);
    loadButton.setBounds (beta2SourceBounds.getRight() - 145, beta2SourceBounds.getBottom() - 51, 124, 32);
    outputGainSlider.setVisible (true); outputGainLabel.setVisible (false);
    outputGainSlider.setBounds (beta2OutputBounds.getX() + 22, beta2OutputBounds.getY() + 88, 104, 72);
    outputGainSlider.setColour (juce::Slider::textBoxTextColourId, modularInk);
    outputGainLabel.setBounds (0, 0, 0, 0);
    outputGainLabel.setColour (juce::Label::textColourId, modularInk);

    // Locked vertical lanes: curve above, four controls below. The ADSR card
    // never shares either lane with the footer, so values and parameter names
    // cannot escape when the host embeds the fixed-size editor.
    auto envelopeInner = beta2EnvelopeBounds.reduced (14);
    const auto controlLaneHeight = 104;
    envelopePad.setVisible (true);
    envelopePad.setBounds (envelopeInner.withTrimmedBottom (controlLaneHeight + 10));
    auto envelopeControls = envelopeInner.removeFromBottom (controlLaneHeight);
    const auto controlGap = 6;
    const auto envelopeWidth = (envelopeControls.getWidth() - controlGap * 3) / 4;
    const auto placeEnvelopeControl = [] (juce::Slider& slider, juce::Label& label, juce::Rectangle<int> area)
    {
        slider.setBounds (area.withTrimmedTop (1).withHeight (62));
        label.setBounds (area.withTrimmedTop (70).withHeight (15));
        slider.setVisible (true); label.setVisible (true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 18);
        label.setColour (juce::Label::textColourId, modularInk);
    };
    placeEnvelopeControl (attackSlider, attackLabel, envelopeControls.removeFromLeft (envelopeWidth));
    envelopeControls.removeFromLeft (controlGap);
    placeEnvelopeControl (decaySlider, decayLabel, envelopeControls.removeFromLeft (envelopeWidth));
    envelopeControls.removeFromLeft (controlGap);
    placeEnvelopeControl (sustainSlider, sustainLabel, envelopeControls.removeFromLeft (envelopeWidth));
    envelopeControls.removeFromLeft (controlGap);
    placeEnvelopeControl (releaseSlider, releaseLabel, envelopeControls);

    auto moduleControls = juce::Rectangle<int> (beta2ModuleBounds.getX() + 181, beta2ModuleBounds.getY() + 76, beta2ModuleBounds.getWidth() - 195, beta2ModuleBounds.getHeight() - 90);
    const auto placeModuleControls = [&placeControl] (std::initializer_list<std::pair<juce::Slider*, juce::Label*>> controls, juce::Rectangle<int> area)
    {
        const auto width = area.getWidth() / (int) controls.size();
        for (const auto& control : controls) placeControl (*control.first, *control.second, area.removeFromLeft (width));
    };
    const auto placeEngineControl = [] (juce::Slider& slider, juce::Label& label, juce::Rectangle<int> area, juce::Colour labelColour, juce::Colour dialColour)
    {
        slider.setBounds (area.withTrimmedTop (2).withHeight (52));
        label.setBounds (area.withTrimmedTop (56).withHeight (13));
        slider.setVisible (true); label.setVisible (true);
        slider.setName ("engineDial");
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour (juce::Slider::rotarySliderFillColourId, dialColour);
        slider.setColour (juce::Slider::thumbColourId, labelColour);
        label.setColour (juce::Label::textColourId, labelColour);
        label.setFont (juce::Font ("Helvetica", 8.5f, juce::Font::plain));
    };
    const auto placeEngineRow = [&placeEngineControl] (std::initializer_list<std::pair<juce::Slider*, juce::Label*>> controls, juce::Rectangle<int> area, juce::Colour labelColour, juce::Colour dialColour)
    {
        const auto width = area.getWidth() / (int) controls.size();
        for (const auto& control : controls)
            placeEngineControl (*control.first, *control.second, area.removeFromLeft (width), labelColour, dialColour);
    };
    const auto placeEngineGrid = [&placeEngineControl] (std::initializer_list<std::pair<juce::Slider*, juce::Label*>> controls, juce::Rectangle<int> area, juce::Colour labelColour, juce::Colour dialColour)
    {
        const auto cellWidth = area.getWidth() / 2;
        const auto cellHeight = area.getHeight() / 2;
        int index = 0;
        for (const auto& control : controls)
        {
            placeEngineControl (*control.first, *control.second,
                                { area.getX() + (index % 2) * cellWidth, area.getY() + (index / 2) * cellHeight, cellWidth, cellHeight }, labelColour, dialColour);
            ++index;
        }
    };
    const auto engineControls = [] (juce::Rectangle<int> tab) { return tab.reduced (14).withTrimmedTop (143).withTrimmedBottom (10); };
    const auto light = modularInk;
    const auto ink = juce::Colour (0xff1c1c1a);
    const auto reverbDial = modularInk;
    const auto granularDial = modularInk;
    const auto randomDial = modularInk;
    const auto chorusDial = modularInk;
    const auto eqDial = modularInk;

    reverbRoomSizeLabel.setText ("Room", juce::dontSendNotification);
    reverbDampingLabel.setText ("Tone", juce::dontSendNotification);
    reverbWidthLabel.setText ("Width", juce::dontSendNotification);
    granularSizeLabel.setText ("Size", juce::dontSendNotification);
    granularDensityLabel.setText ("Density", juce::dontSendNotification);
    granularPitchLabel.setText ("Pitch", juce::dontSendNotification);
    randomAmountLabel.setText ("Amount", juce::dontSendNotification);
    chorusDelayLabel.setText ("Time", juce::dontSendNotification);
    eqLowLabel.setText ("Low", juce::dontSendNotification);
    eqMidLabel.setText ("Mid", juce::dontSendNotification);
    eqHighLabel.setText ("High", juce::dontSendNotification);

    placeEngineRow ({ { &reverbRoomSizeSlider, &reverbRoomSizeLabel }, { &reverbDampingSlider, &reverbDampingLabel }, { &reverbWidthSlider, &reverbWidthLabel }, { &reverbMixSlider, &reverbMixLabel } }, engineControls (beta2EffectTabBounds[0]), light, reverbDial);
    placeEngineRow ({ { &granularSizeSlider, &granularSizeLabel }, { &granularDensitySlider, &granularDensityLabel }, { &granularPitchSlider, &granularPitchLabel }, { &granularMixSlider, &granularMixLabel } }, engineControls (beta2EffectTabBounds[1]), light, granularDial);
    placeEngineRow ({ { &randomAmountSlider, &randomAmountLabel }, { &randomRateSlider, &randomRateLabel }, { &randomMixSlider, &randomMixLabel } }, engineControls (beta2EffectTabBounds[2]), light, randomDial);
    placeEngineGrid ({ { &chorusRateSlider, &chorusRateLabel }, { &chorusDepthSlider, &chorusDepthLabel }, { &chorusDelaySlider, &chorusDelayLabel }, { &chorusMixSlider, &chorusMixLabel } }, engineControls (beta2EffectTabBounds[3]), light, chorusDial);
    placeEngineRow ({ { &eqLowSlider, &eqLowLabel }, { &eqMidSlider, &eqMidLabel }, { &eqHighSlider, &eqHighLabel } }, engineControls (beta2EffectTabBounds[4]), light, eqDial);

    // Give every part of each card the same immediate activation behaviour.
    // Sending the transparent target behind children keeps every knob and its
    // label completely interactive above it.
    const std::array<juce::TextButton*, 5> cardHitTargets { &fxSlotOne, &fxSlotTwo, &fxSlotThree, &fxSlotFour, &fxSlotFive };
    for (size_t i = 0; i < cardHitTargets.size(); ++i)
    {
        cardHitTargets[i]->setBounds (beta2EffectTabBounds[i].reduced (3));
        cardHitTargets[i]->setVisible (true);
        cardHitTargets[i]->toBack();
    }
    // The header field owns its full pill and menu surface. The old ComboBoxes
    // remain state carriers only; they are never drawn in the Beta 2 board.
    const auto showHeaderPreset = [this] (int effect, juce::ComboBox& preset)
    {
        presetSelector.setVisible (true);
        presetSelector.setLayoutBounds (beta2PresetBounds);
        presetSelector.setModule (effect, juce::jmax (0, preset.getSelectedId() - 1));
    };
    if (selectedFx == 5)
    {
        eqCurvePad.setVisible (true); showHeaderPreset (5, eqPreset);
        eqCurvePad.setBounds (moduleControls.reduced (2).withTrimmedTop (5));
    }
    else if (selectedFx == 7)
    {
        showHeaderPreset (7, granularPreset);
    }
    else if (selectedFx == 9)
    {
        showHeaderPreset (9, randomPreset);
    }
    else if (selectedFx == 2)
    {
        showHeaderPreset (2, chorusPreset);
    }
    else
    {
        showHeaderPreset (1, reverbPreset);
    }

    rebuildBeta2DotLayers();
}
void OneShotSamplerAudioProcessorEditor::showFxPage (bool shouldShowFx)
{
    // Beta 2 is one board.  The legacy header buttons now simply return to
    // this surface, so reopening an editor can never expose an old layer.
    juce::ignoreUnused (shouldShowFx);
    showingFx = false;
    showingTexture = false;
    if (effectPickerMenu != nullptr)
        effectPickerMenu->setVisible (false);
    resized();
    repaint();
    return;

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
    showFxPage (false);
    return;

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

void OneShotSamplerAudioProcessorEditor::setPerformancePosition (float x, float y)
{
    const auto set = [this] (const char* id, float value)
    {
        if (auto* parameter = audioProcessor.parameters.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    const auto space = juce::jlimit (0.0f, 1.0f, x);
    const auto texture = juce::jlimit (0.0f, 1.0f, y);
    // X opens space and stereo movement. Y adds grain, instability and motion.
    set ("reverbMix", 0.03f + space * 0.47f);
    set ("reverbRoomSize", 0.25f + space * 0.60f);
    set ("chorusDepth", 0.08f + space * 0.78f);
    set ("chorusMix", space * 0.42f);
    set ("granularMix", texture * 0.52f);
    set ("randomAmount", texture * space * 0.68f);
    set ("flutterDepth", texture * 0.58f);
    repaint();
}

void OneShotSamplerAudioProcessorEditor::applyPerformanceScene (int scene)
{
    const auto index = juce::jlimit (0, 3, scene);
    const std::array<juce::Point<float>, 4> positions {
        juce::Point<float> (0.10f, 0.08f), // Clean
        juce::Point<float> (0.72f, 0.18f), // Wide
        juce::Point<float> (0.48f, 0.76f), // Warp
        juce::Point<float> (0.88f, 0.92f)  // Chaos
    };

    const auto point = positions[(size_t) index];
    performancePad.setPosition (point.x, point.y, false);
    setPerformancePosition (point.x, point.y);
    activePerformanceScene = index;
    const std::array<juce::TextButton*, 4> sceneButtons { &cleanSceneButton, &wideSceneButton, &warpSceneButton, &chaosSceneButton };
    for (int i = 0; i < 4; ++i)
        sceneButtons[(size_t) i]->setToggleState (i == activePerformanceScene, juce::dontSendNotification);

    const auto set = [this] (const char* id, float value)
    {
        if (auto* parameter = audioProcessor.parameters.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    // Each scene is a complete musical starting point, not just a different
    // point on the same macro curve. Exact values make recalling a scene
    // repeatable in a session and give every button a clear identity.
    switch (index)
    {
        case 0: // CLEAN: direct, dry, ready to shape from scratch.
            set ("reverbMix", 0.0f); set ("chorusMix", 0.0f);
            set ("granularMix", 0.0f); set ("randomMix", 0.0f);
            set ("flutterDepth", 0.0f);
            break;
        case 1: // WIDE: stable stereo dimension with no texture breakup.
            set ("reverbMix", 0.27f); set ("reverbRoomSize", 0.70f);
            set ("reverbDamping", 0.36f); set ("reverbWidth", 1.0f);
            set ("chorusRate", 0.32f); set ("chorusDepth", 0.31f);
            set ("chorusDelay", 12.0f); set ("chorusFeedback", 0.05f); set ("chorusMix", 0.30f);
            set ("granularMix", 0.0f); set ("randomMix", 0.0f); set ("flutterDepth", 0.0f);
            break;
        case 2: // WARP: slow grain movement and an enveloping space.
            set ("reverbMix", 0.38f); set ("reverbRoomSize", 0.82f);
            set ("reverbDamping", 0.56f); set ("reverbWidth", 1.0f);
            set ("chorusRate", 0.54f); set ("chorusDepth", 0.47f);
            set ("chorusDelay", 16.0f); set ("chorusFeedback", 0.16f); set ("chorusMix", 0.34f);
            set ("granularSize", 230.0f); set ("granularDensity", 5.5f);
            set ("granularPitch", -5.0f); set ("granularMix", 0.35f);
            set ("randomMix", 0.0f); set ("flutterDepth", 0.16f);
            break;
        default: // CHAOS: dense, unstable texture without unsafe gain.
            set ("reverbMix", 0.47f); set ("reverbRoomSize", 0.91f);
            set ("reverbDamping", 0.24f); set ("reverbWidth", 1.0f);
            set ("chorusRate", 1.70f); set ("chorusDepth", 0.68f);
            set ("chorusDelay", 19.0f); set ("chorusFeedback", 0.28f); set ("chorusMix", 0.42f);
            set ("granularSize", 72.0f); set ("granularDensity", 16.0f);
            set ("granularPitch", 7.0f); set ("granularMix", 0.48f);
            set ("randomAmount", 0.78f); set ("randomRate", 11.0f); set ("randomMix", 0.31f);
            set ("flutterRate", 4.2f); set ("flutterDepth", 0.43f); set ("flutterMix", 0.18f);
            break;
    }

    setEffectEnabled (1, index >= 1); // Reverb
    setEffectEnabled (2, index >= 1); // Chorus
    setEffectEnabled (7, index >= 2); // Granular
    setEffectEnabled (9, index >= 3); // Random
    setEffectEnabled (6, index >= 3); // Flutter
}

void OneShotSamplerAudioProcessorEditor::selectEffect (int effect)
{
    if (effect < 0 || effect > 9)
        return;
    selectedFx = effect;
    if (effectPickerMenu != nullptr)
        effectPickerMenu->setActiveEffect (effect);
    resized();
    repaint();
}

void OneShotSamplerAudioProcessorEditor::toggleEffect (int effect)
{
    setEffectEnabled (effect, ! isEffectEnabled (effect));
}

bool OneShotSamplerAudioProcessorEditor::isEffectEnabled (int effect) const
{
    if (effect >= 0 && effect < (int) fxSlotEnabled.size())
        return fxSlotEnabled[(size_t) effect];

    if (effect == 7)
        return audioProcessor.parameters.getRawParameterValue ("granularEnabled")->load() > 0.5f;
    if (effect == 8)
        return audioProcessor.parameters.getRawParameterValue ("harmonyEnabled")->load() > 0.5f;
    if (effect == 9)
        return audioProcessor.parameters.getRawParameterValue ("randomEnabled")->load() > 0.5f;

    return false;
}

void OneShotSamplerAudioProcessorEditor::setEffectEnabled (int effect, bool shouldBeEnabled)
{
    if (effect >= 0 && effect < (int) fxSlotEnabled.size())
    {
        setSlotEnabled (effect, shouldBeEnabled);
        return;
    }

    const char* parameterID = effect == 7 ? "granularEnabled"
                            : effect == 8 ? "harmonyEnabled"
                            : effect == 9 ? "randomEnabled" : nullptr;
    if (parameterID != nullptr)
        if (auto* parameter = audioProcessor.parameters.getParameter (parameterID))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (shouldBeEnabled ? 1.0f : 0.0f));

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
    // Keep phase unbounded: trigonometric functions are periodic, and never
    // wrapping this value avoids the single-frame discontinuity a modulo reset
    // can introduce in complex visuals.
    // Preserve the same visual speed after moving from 30 to 60 fps.
    fxVisualPhase += 0.00225f;
    const auto meterTarget = juce::jlimit (0.0f, 1.0f, audioProcessor.getOutputLevel());
    // Fast attack, slow release: visual meters breathe with the audio rather
    // than flickering between host audio blocks.
    visualSignalLevel += (meterTarget - visualSignalLevel) * (meterTarget > visualSignalLevel ? 0.34f : 0.075f);
    const std::array<int, 5> tabEffects { 1, 7, 9, 2, 5 };
    for (size_t i = 0; i < tabEffects.size(); ++i)
    {
        const auto target = isEffectEnabled (tabEffects[i]) ? 1.0f : 0.0f;
        fxCardReveal[i] += (target - fxCardReveal[i]) * 0.18f;
        fxCardPulse[i] = juce::jmax (0.0f, fxCardPulse[i] - 0.06f);
    }
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
