/*
  ------------------------------------------------------------------------------
    Theme.h

    ModalDish colour scheme, after the Mango / Spread / AmbiRR2 pattern: dark
    diagonal gradient backdrop, one identity accent, one accent per control
    section, and the FxmeTools knob styling helpers (dark disc, accent on
    arc / outline / pointer).

    Also centralises the boundary-condition colour code shared by the shape
    canvas, the plate view overlay and the legend.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
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
    // Near-white, cool-tinted to match the backdrop. Control names, combo
    // text and button captions all come from here, and they are read against
    // panel and control bodies dark enough that a mid grey disappears into
    // them.
    inline const juce::Colour text      { 0xfff0f3f8 };
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

    // Control-group accents. The right column is organised into groups and
    // these name them. Kept separate from the semantic accents above because
    // a group colour says where a control *sits*, not what it does — the same
    // knob would keep its meaning in a differently coloured panel.
    //
    // Red and green are deliberately not the coral and lime of bcColour
    // below: those two are drawn on the plate itself a few hundred pixels
    // away, and two reds that nearly match read as a mistake. Cyan does
    // coincide with SimplySupported, which is the one collision left.
    inline const juce::Colour dynGroup    { 0xff4cc9f0 };   // cyan
    inline const juce::Colour freqGroup   { 0xffe8c341 };   // yellow
    inline const juce::Colour hammerGroup { 0xffe04a5a };   // red
    inline const juce::Colour ioGroup     { 0xff4fc98a };   // green
    inline const juce::Colour transGroup  { 0xff8fb8e8 };   // light blue

    // Plate display: canvas background and the diverging contour map ends.
    inline const juce::Colour plateBg   { 0xff141a24 };
    inline const juce::Colour plateGrid { 0xff4a5670 };
    inline const juce::Colour fieldNeg  { 0xff4cc9f0 };
    inline const juce::Colour fieldPos  { 0xffe0784a };

    /** The identity line under the top bar, and the ramp behind it: the same
        three colours the plate view paints a mode with, blue through grey to
        orange. Sampling the plate's own palette is the point — the line is a
        small copy of what the big picture does.

        The glow is a stack of ever-taller, ever-fainter copies of the ramp.
        On a dark backdrop that reads as light spilling off the line rather
        than as a drawn halo, and it costs a handful of gradient fills, so it
        needs no blur buffer. */
    inline void paintIdentityLine (juce::Graphics& g, juce::Rectangle<float> line,
                                   float glowHeight = 16.0f)
    {
        auto fillRamp = [&g] (juce::Rectangle<float> b, float alpha)
        {
            juce::ColourGradient grad (fieldNeg.withMultipliedAlpha (alpha), b.getTopLeft(),
                                       fieldPos.withMultipliedAlpha (alpha), b.getTopRight(),
                                       false);
            grad.addColour (0.5, plateGrid.withMultipliedAlpha (alpha));
            g.setGradientFill (grad);
            g.fillRect (b);
        };

        // Halo first, widest and faintest, so the crisp line lands on top of
        // it; the layers accumulate towards the centre, which is the falloff.
        constexpr int layers = 5;
        for (int i = layers; i >= 1; --i)
        {
            const float t = (float) i / (float) layers;   // 1 = outermost
            fillRamp (line.expanded (0.0f, glowHeight * t), 0.10f * (1.0f - t) + 0.025f);
        }

        fillRamp (line, 1.0f);
    }

    /** Colour code of the four boundary-condition types, indexed by
        (int) fxme::acoustics::BoundaryCondition — stiffest first. Each
        condition keeps the colour it has always had; only the order moved. */
    inline juce::Colour bcColour (int bc) noexcept
    {
        static const juce::Colour colours[4] = {
            juce::Colour (0xffe0784a),   // Clamped         — coral
            juce::Colour (0xff4cc9f0),   // SimplySupported — cyan
            juce::Colour (0xff9ac93c),   // Sliding         — lime
            juce::Colour (0xff8a93a6),   // Free            — grey
        };
        return colours[juce::jlimit (0, 3, bc)];
    }

    inline const char* bcName (int bc) noexcept
    {
        static const char* names[4] = { "Clamp", "Support", "Slide", "Free" };
        return names[juce::jlimit (0, 3, bc)];
    }

    // Two shapes, one accent vocabulary. accentControl sets the colour ids
    // both read; styleKnob adds the rotary geometry, styleBox the number
    // box's own name colour. The panels use boxes — they pair up two to a row
    // in the space one knob needed — and styleKnob is kept for anywhere a
    // rotary is still the right shape.
    inline void accentControl (fxme::FxmeSlider& s, const juce::String& name, juce::Colour a)
    {
        s.setName (name);
        s.setShowLabel (true);
        s.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xff2b2b2b));
        s.setColour (juce::Slider::rotarySliderOutlineColourId, a.darker (1.6f));
        s.setColour (juce::Slider::trackColourId,               a);
        s.setColour (juce::Slider::thumbColourId,               a.brighter (0.4f));
    }

    inline void styleKnob (fxme::FxmeSlider& s, const juce::String& name, juce::Colour a)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        accentControl (s, name, a);
    }

    // FxmeTools number box: the same colour ids a knob uses, so this is a
    // type change rather than a re-theming — except that the box draws its
    // own name, in textBoxTextColourId, which a knob never reads. The slider
    // style is deliberately left alone: the box sets RotaryVerticalDrag in
    // its constructor, and a box this small is unusable with the horizontal
    // mapping a knob wants.
    inline void styleBox (fxme::FxmeNumberBox& s, const juce::String& name, juce::Colour a)
    {
        accentControl (s, name, a);
        s.setColour (juce::Slider::textBoxTextColourId, text);
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
