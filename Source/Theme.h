/*
  ------------------------------------------------------------------------------
    Theme.h

    FemPlate colour scheme, after the Mango / Spread / AmbiRR2 pattern: dark
    diagonal gradient backdrop, one identity accent, one accent per control
    section, and the FxmeTools knob styling helpers (dark disc, accent on
    arc / outline / pointer).

    Also centralises the boundary-condition colour code shared by the shape
    canvas, the plate view overlay and the legend.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

namespace fem::theme
{
    inline void paintBackground (juce::Graphics& g, juce::Rectangle<float> b)
    {
        // Deep blue-slate, the global accent's tint.
        const auto base = juce::Colour::fromFloatRGBA (0.12f, 0.17f, 0.24f, 1.0f);
        juce::ColourGradient grad (base.darker().darker().darker(), b.getBottomLeft(),
                                   base, b.getTopRight(), false);
        g.setGradientFill (grad);
        g.fillRect (b);
    }

    inline const juce::Colour panel     { 0xff1d2430 };
    inline const juce::Colour panelLine { 0xff35415a };
    inline const juce::Colour text      { 0xffd8dce4 };
    inline const juce::Colour dimText   { 0xff97a1b4 };
    inline const juce::Colour topBarBg  { 0xff10141c };

    inline const juce::Colour accent      { 0xff4cc9f0 };   // cyan identity
    inline const juce::Colour geomAccent  { 0xff9ac93c };   // lime  — geometry section
    inline const juce::Colour modesAccent { 0xffe0784a };   // coral — modal parameters
    inline const juce::Colour ioAccent    { 0xffd96cd0 };   // orchid — in/out section
    inline const juce::Colour cascAccent  { 0xffd9b13a };   // amber — cascade tuning

    // Plate markers. Pickups borrow the in/out orchid; sources get a colour of
    // their own rather than sharing one, because the two sit on the same
    // picture and telling them apart at a glance is the whole job of the
    // marker. Deliberately not coral or lime: those already mean a boundary
    // condition a few pixels away on the outline.
    inline const juce::Colour pickupAccent { 0xffd96cd0 };  // orchid, = ioAccent
    inline const juce::Colour sourceAccent { 0xff5ad1a5 };  // teal

    // Plate display: canvas background and the diverging contour map ends.
    inline const juce::Colour plateBg   { 0xff141a24 };
    inline const juce::Colour plateGrid { 0xff4a5670 };
    inline const juce::Colour fieldNeg  { 0xff4cc9f0 };
    inline const juce::Colour fieldPos  { 0xffe0784a };

    /** Colour code of the four boundary-condition types, indexed by
        (int) fxme::acoustics::BoundaryCondition. */
    inline juce::Colour bcColour (int bc) noexcept
    {
        static const juce::Colour colours[4] = {
            juce::Colour (0xff8a93a6),   // Free            — grey
            juce::Colour (0xff4cc9f0),   // SimplySupported — cyan
            juce::Colour (0xffe0784a),   // Clamped         — coral
            juce::Colour (0xff9ac93c),   // Sliding         — lime
        };
        return colours[juce::jlimit (0, 3, bc)];
    }

    inline const char* bcName (int bc) noexcept
    {
        static const char* names[4] = { "Free", "Support", "Clamp", "Slide" };
        return names[juce::jlimit (0, 3, bc)];
    }

    // FxmeTools rotary knob: dark disc, one accent per control on the value
    // arc / outline / pointer; FxmeLookAndFeel draws the value read-out inside
    // the knob and the label (the slider's name) just below it.
    inline void styleKnob (fxme::FxmeSlider& s, const juce::String& name, juce::Colour a)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setName (name);
        s.setShowLabel (true);
        s.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xff2b2b2b));
        s.setColour (juce::Slider::rotarySliderOutlineColourId, a.darker (1.6f));
        s.setColour (juce::Slider::trackColourId,               a);
        s.setColour (juce::Slider::thumbColourId,               a.brighter (0.4f));
    }

    inline void styleCombo (juce::ComboBox& c, juce::Colour a)
    {
        c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2b2b2b));
        c.setColour (juce::ComboBox::outlineColourId,    a.darker());
        c.setColour (juce::ComboBox::arrowColourId,      a.brighter (0.3f));
        c.setColour (juce::ComboBox::textColourId,       text);
    }

    inline void styleButton (juce::TextButton& b, juce::Colour a)
    {
        b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff2b2b2b));
        b.setColour (juce::TextButton::buttonOnColourId, a.darker (0.4f));
        b.setColour (juce::TextButton::textColourOffId,  text);
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        b.setColour (juce::ComboBox::outlineColourId,    a.darker());
    }
}
