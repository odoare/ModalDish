/*
  ------------------------------------------------------------------------------
    PointToggle.h

    One switch for one point on the plate: the pickup or source marker itself,
    drawn at button size and clickable.

    It is deliberately not a styled TextButton. The rows hold eight switches
    across a 150 px column, and at that width JUCE's default button text
    layout leaves about four pixels for the label; more to the point, a switch
    that is a copy of the marker it controls needs no legend to say which is
    which. Filled ring and white label when on, faint ring when off, exactly as
    drawPlateOverlay draws it on the plate.

    A juce::Button subclass rather than a Component wrapping one, so that an
    AudioProcessorValueTreeState::ButtonAttachment binds to it directly and the
    host, the plate's alt-click and the marker's own On button all stay in step
    without anything here knowing about them.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class PointToggle : public juce::Button
{
public:
    PointToggle (const juce::String& label, juce::Colour accent)
        : juce::Button (label), accentColour (accent)
    {
        setClickingTogglesState (true);
        // The plate wants the keys: 1..8 and a..h place a point under the
        // mouse, and a switch that stole focus would swallow them.
        setMouseClickGrabsKeyboardFocus (false);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto centre = b.getCentre();
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 1.0f;
        if (r <= 1.0f)
            return;

        const auto ring = juce::Rectangle<float> (centre.x - r, centre.y - r, 2 * r, 2 * r);
        const auto c = (highlighted || down) ? accentColour.brighter (0.3f) : accentColour;
        const bool on = getToggleState();

        if (on)
        {
            g.setColour (c.withAlpha (0.30f));
            g.fillEllipse (ring);
            g.setColour (c);
            g.drawEllipse (ring, 2.0f);
        }
        else
        {
            g.setColour (c.withAlpha (highlighted ? 0.55f : 0.30f));
            g.drawEllipse (ring, 1.0f);
        }

        g.setFont (juce::Font (juce::FontOptions (11.0f)).boldened());
        g.setColour (on ? juce::Colours::white : c.withAlpha (0.45f));
        g.drawText (getButtonText(), b, juce::Justification::centred);
    }

private:
    juce::Colour accentColour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PointToggle)
};
