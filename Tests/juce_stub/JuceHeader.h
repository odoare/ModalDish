/*
  ------------------------------------------------------------------------------
    Tests/juce_stub/JuceHeader.h

    Stand-in for the plugin's generated JuceHeader.h, so that the modal
    synthesis (Source/Dsp/PlateSynth.cpp) can be rendered offline by a console
    test with no JUCE, no plugin host and no audio device.

    PlateSynth's entire JUCE surface is the handful of numeric helpers below
    plus fxme::BiquadCoeffs, which lives in the JUCE-free core. That is
    deliberate (see FxmeTools' core/shell split): if this stub ever stops
    being enough to compile it, the DSP has grown a real JUCE dependency and
    belongs on the other side of the line — so a build failure here is a
    finding, not an inconvenience.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include <FxmeTools/dsp/Biquad.h>

namespace juce
{
    template <class T> constexpr T jmax (T a, T b)          { return a > b ? a : b; }
    template <class T> constexpr T jmin (T a, T b)          { return a < b ? a : b; }
    template <class T> constexpr T jlimit (T lo, T hi, T v) { return v < lo ? lo : (v > hi ? hi : v); }

    template <class T> struct MathConstants
    {
        static constexpr T pi    = (T) 3.14159265358979323846;
        static constexpr T twoPi = (T) 6.28318530717958647692;
    };

    template <class T> bool approximatelyEqual (T a, T b) { return a == b; }
}
