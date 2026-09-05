/*
  ------------------------------------------------------------------------------
    Tooltips.h

    What every control says when the pointer rests on it, in one place.

    Separate from the editor because these are prose, not layout: keeping them
    together makes them readable as a set, keeps the editor's constructor about
    wiring, and makes them diffable against the parameter tables in README.md,
    which say the same things at greater length. When a control changes, both
    have to change; a table and a tooltip that disagree is worse than either
    one alone.

    Two sentences at most. A tooltip is read standing up, mid-take, by someone
    who wants to know whether to turn this knob or a different one: it says
    what the control does, and where it is useful it says what for. Anything
    longer belongs in the README, and anything that needs a diagram belongs in
    the technical paper.

    No line breaks in the text. JUCE measures a tooltip with balanced line
    lengths inside a fixed maximum width and sizes the window from that, so a
    hard break here only fights the layout it is given. The breaks below are
    in the C++ literals, not in the strings.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

namespace fem::tip
{
    // --- Modal design ---------------------------------------------------------
    inline constexpr const char* aspect =
        "Width to height of the Ellipse and Rectangle buttons on the "
        "sketch. Keeps following the knob until you edit the shape it "
        "made.";
    inline constexpr const char* points =
        "How many segments the plate's edge is cut into. Each segment "
        "carries its own boundary condition.";
    inline constexpr const char* grid =
        "Mesh density, in elements across the plate. Finer is more "
        "accurate, not more modes, and costs computing time.";
    inline constexpr const char* modes =
        "Size of the filter bank. Past what the mesh can resolve, the "
        "rest is the statistical tail.";
    inline constexpr const char* modeView =
        "Which mode shape the plate view draws. 0 is the bare mesh.";

    // --- Dynamics -------------------------------------------------------------
    inline constexpr const char* tension =
        "Tension against flexural stiffness: 0 is a pure plate, large "
        "is membrane-like. Reshapes the ratios, keeping mode 1 at "
        "Frequency.";
    inline constexpr const char* nonlinear =
        "Dynamic tension. The whole spectrum glides up on a hard hit "
        "and relaxes as the plate rings out.";
    inline constexpr const char* viscous =
        "Damping through the plate's velocity, as the damping ratio of "
        "mode 1. High modes decay relatively slower than with Material.";
    inline constexpr const char* material =
        "Damping through internal friction, as the damping ratio of "
        "mode 1. High modes decay relatively faster than with Viscous.";
    inline constexpr const char* cascade =
        "Amount of upward energy transfer, low bands pumping higher "
        "ones. At 0 it is off in full, Overlap's bandwidth floor "
        "included.";
    inline constexpr const char* deplete =
        "How much energy the pumping bands give up in return.";

    // --- Frequency control ----------------------------------------------------
    inline constexpr const char* frequency =
        "Value of first eigenfrequency. Every other mode follows the"
        "ratios the calculated geometry gives it.";
    inline constexpr const char* glide =
        "Portamento time between MIDI-tuned notes."
    inline constexpr const char* srcChan =
        "The MIDI channel that triggers sources. Omni accepts any of "
        "them.";
    inline constexpr const char* freqChan =
        "The MIDI channel that tunes the plate. Off leaves the pitch to "
        "the Frequency knob alone.";

    // --- Hammer control -------------------------------------------------------
    inline constexpr const char* duration =
        "Length of the mouse hammer's half-sine shock. Short and hard "
        "reads as a stick, long and soft as a mallet.";
    inline constexpr const char* force =
        "How hard the mouse hammer strikes. High values are where the "
        "nonlinear behaviour is triggered.";
    inline constexpr const char* srcDur =
        "Trim over all eight sources' hammer durations. Multiplies "
        "whatever each source's own mapping resolved to rather than "
        "replacing it.";
    inline constexpr const char* srcForce =
        "Trim over all eight sources' forces. Multiplies whatever each "
        "source's own mapping resolved to rather than replacing it.";

    // --- Cascade (the Advanced column) ---------------------------------------
    inline constexpr const char* cascDrive =
        "The tanh knee: what the carrier is made of. Not gradual, and "
        "where its useful window sits depends on how hard you play.";
    inline constexpr const char* cascWindow =
        "How many bands below each band are allowed to pump it.";
    inline constexpr const char* cascAttack =
        "Gate attack, per band rung: the higher the band, the slower it "
        "opens.";
    inline constexpr const char* cascRelease =
        "Gate release: how the pumping tails off.";
    inline constexpr const char* cascOverlap =
        "Bandwidth floor on the receiving modes. Scaled by Cascade, so "
        "it switches off with it.";

    // --- IO -------------------------------------------------------------------
    inline constexpr const char* inGain =
        "How much of the plugin input reaches the sources. At 0 the "
        "plate is an instrument, played by strikes alone.";
    inline constexpr const char* outGain =
        "Master level, taken before the output highpass.";

    // --- Pickup panel ---------------------------------------------------------
    inline constexpr const char* pickupX =
        "Where on the plate this pickup listens, across.";
    inline constexpr const char* pickupY =
        "Where on the plate this pickup listens, up.";
    inline constexpr const char* pickupLevel =
        "This pickup's contribution to the mix.";
    inline constexpr const char* pickupPan =
        "Equal-power pan, at unity in the centre.";

    // --- Source panel ---------------------------------------------------------
    // The three mappings read the same way, so their tooltips do too: an end,
    // the other end, and what moves between them.
    inline constexpr const char* sourceX =
        "Where this source strikes at zero control, across.";
    inline constexpr const char* sourceY =
        "Where this source strikes at zero control, up.";
    inline constexpr const char* sourceX2 =
        "Where it strikes at full control, across. Sits on top of X1 "
        "until you pull the two apart.";
    inline constexpr const char* sourceY2 =
        "Where it strikes at full control, up. Sits on top of Y1 until "
        "you pull the two apart.";
    inline constexpr const char* spread =
        "Standard deviation per axis of a random offset on every hit, "
        "around wherever the mapping placed it.";
    inline constexpr const char* posCtl =
        "What moves the strike point between X1/Y1 and X2/Y2.";
    inline constexpr const char* hamMin =
        "This source's hammer duration at zero control.";
    inline constexpr const char* hamMax =
        "Its hammer duration at full control. Below the minimum is "
        "allowed, and simply inverts the mapping.";
    inline constexpr const char* hamCtl =
        "What moves the hammer duration between Ham Min and Ham Max.";
    inline constexpr const char* frcMin =
        "This source's force at zero control.";
    inline constexpr const char* frcMax =
        "Its force at full control. Below the minimum is allowed, and "
        "simply inverts the mapping.";
    inline constexpr const char* frcCtl =
        "What moves the force between Frc Min and Frc Max.";
    inline constexpr const char* curve =
        "Shape applied to velocity before it drives any mapping. Only "
        "Vel is curved; a CC is taken as the hardware left it.";
    inline constexpr const char* inVol =
        "This source's share of the plugin input. The input is injected "
        "at X1/Y1 only, having no velocity to place it by.";
    inline constexpr const char* inBal =
        "Which side of the incoming stereo pair this source takes.";
    inline constexpr const char* note =
        "The MIDI note this source answers to, on the Src Chan channel.";
}
