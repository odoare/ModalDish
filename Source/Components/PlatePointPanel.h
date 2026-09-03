/*
  ------------------------------------------------------------------------------
    PlatePointPanel.h

    The little panel that appears when a pickup or a source marker on the plate
    is clicked: every parameter of that one point, and nothing else.

    One class covers both because they differ only in which parameters they
    show. A pickup is a listening point (position, level, pan) with a meter of
    what it is hearing; a source is a striking and injection point (a base
    position and a velocity endpoint, hammer, force, spread, send, balance)
    plus the MIDI note it answers to and a Learn button for it.

    It is meant to live inside a juce::CallOutBox launched from the editor, so
    it sizes itself and expects to be handed to the callout by value of its
    getLocalBounds(). Launching it with the editor as the callout's parent is
    what lets it inherit FxmeLookAndFeel — a callout parented to the desktop
    would be drawn in stock JUCE style instead.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "Theme.h"
#include "Tooltips.h"

#include <memory>
#include <optional>
#include <vector>

//==============================================================================
class PlatePointPanel : public juce::Component,
                        private juce::Timer
{
public:
    /** `index` is the pickup or source (both 0..7) this panel edits. */
    PlatePointPanel (ModalDishAudioProcessor& p, bool pickup, int index)
        : processor (p), isPickup (pickup), pointIndex (index),
          accent (pickup ? fem::theme::pickupAccent : fem::theme::sourceAccent),
          cols (pickup ? 4 : 3)
    {
        title = isPickup ? "Pickup " + juce::String (index + 1)
                         : "Source " + juce::String::charToString (
                               (juce::juce_wchar) fem::sourceLabel (index));

        addKnob (isPickup ? "X" : "X1",
                 isPickup ? fem::id::pickupX[index] : fem::id::sourceX[index],
                 isPickup ? fem::tip::pickupX : fem::tip::sourceX);
        addKnob (isPickup ? "Y" : "Y1",
                 isPickup ? fem::id::pickupY[index] : fem::id::sourceY[index],
                 isPickup ? fem::tip::pickupY : fem::tip::sourceY);
        if (! isPickup)
            addKnob ("Spread", fem::id::sourceSpread[index], fem::tip::spread);

        if (isPickup)
        {
            addKnob ("Level", fem::id::pickupLevel[index], fem::tip::pickupLevel);
            addKnob ("Pan", fem::id::pickupPan[index], fem::tip::pickupPan, 0.0);

            // Mono, and pre-pan on purpose: the question this answers is how
            // much this point is picking up, which Level scales and Pan does
            // not. Where that signal lands in the image is what Pan is for,
            // and the output meters already show the result of it.
            meter.setRange (-60.0f, 6.0f);
            meter.setZeroLevel (0.0f);
            meter.setHorizontal (true);
            meter.setMeterColor (accent);
            addAndMakeVisible (meter);

            // Metering costs a multiply-add per mode per sample, so the synth
            // only ever meters the pickup whose panel is open. Claiming it on
            // construction and releasing it below keeps that in step with the
            // window, whatever dismisses it.
            processor.meteredPickup.store (index, std::memory_order_release);
            startTimerHz (24);
        }
        else
        {
            // Three rows to a mapping: the two ends and what moves between
            // them. Reading across a row tells you the whole story of one
            // quantity, which is why the controller sits beside its pair
            // rather than in a block of its own.
            const auto ctlText = [] (double v)
            {
                const int i = (int) std::lround (v);
                if (i == fem::ctlOff)      return juce::String ("Off");
                if (i == fem::ctlVelocity) return juce::String ("Vel");
                return "CC " + juce::String (i - fem::ctlCcBase);
            };
            const auto curveText = [] (double v)
            {
                static const char* n[3] = { "Slow", "Linear", "Fast" };
                return juce::String (n[juce::jlimit (0, 2, (int) std::lround (v))]);
            };

            x2Box = addKnob ("X2", fem::id::sourceX2[index], fem::tip::sourceX2);
            y2Box = addKnob ("Y2", fem::id::sourceY2[index], fem::tip::sourceY2);
            addKnob ("Pos Ctl", fem::id::sourcePosCtl[index], fem::tip::posCtl, {}, ctlText);

            addKnob ("Ham Min", fem::id::sourceHammer[index], fem::tip::hamMin);
            hamMaxBox = addKnob ("Ham Max", fem::id::sourceHammerMax[index], fem::tip::hamMax);
            addKnob ("Ham Ctl", fem::id::sourceHammerCtl[index], fem::tip::hamCtl, {}, ctlText);

            addKnob ("Frc Min", fem::id::sourceForce[index], fem::tip::frcMin);
            frcMaxBox = addKnob ("Frc Max", fem::id::sourceForceMax[index], fem::tip::frcMax);
            addKnob ("Frc Ctl", fem::id::sourceForceCtl[index], fem::tip::frcCtl, {}, ctlText);

            curveBox = addKnob ("Curve", fem::id::sourceVelCurve[index], fem::tip::curve, {}, curveText);
            addKnob ("In Vol", fem::id::sourceSend[index], fem::tip::inVol);
            addKnob ("In Bal", fem::id::sourcePan[index], fem::tip::inBal, 0.0);

            // Note lives in the footer beside the Learn button that sets it,
            // rather than in the grid: the grid is the sound, this pair is
            // the wiring.
            noteBox = std::make_unique<fxme::FxmeNumberBox>();
            fem::theme::styleBox (*noteBox, "Note", accent, fem::tip::note);
            addAndMakeVisible (*noteBox);
            attachments.push_back (std::make_unique<
                juce::AudioProcessorValueTreeState::SliderAttachment> (
                    processor.apvts, fem::id::sourceNote[index], *noteBox));

            fem::theme::styleButton (learnButton, accent);
            learnButton.setClickingTogglesState (true);
            learnButton.setMouseClickGrabsKeyboardFocus (false);
            learnButton.onClick = [this]
            {
                // Arming is a toggle: clicking again gives up rather than
                // leaving the plugin quietly waiting for a note forever.
                processor.midiLearnArmed.store (learnButton.getToggleState() ? pointIndex : -1,
                                                std::memory_order_release);
            };
            addAndMakeVisible (learnButton);
            updateEnablement();      // open in the right state, not a tick later
            startTimerHz (10);
        }

        onButton = std::make_unique<fxme::FxmeButton> (
            processor.apvts,
            isPickup ? fem::id::pickupOn[index] : fem::id::sourceOn[index],
            "On", accent);
        addAndMakeVisible (*onButton);

        // Rows follow the knob count rather than the kind of point, so adding
        // a control does not silently overflow the panel it lives in. The
        // width is the same either way; only the number of columns differs,
        // so a source's three columns are wider than a pickup's four.
        const int rows = ((int) knobs.size() + cols - 1) / cols;
        setSize (4 * knobW + 2 * pad,
                 headerH + rowH * rows
                   + (isPickup ? meterH + 4 : footerH) + 2 * pad);
    }

    ~PlatePointPanel() override
    {
        // Closing the panel disarms: an armed Learn that outlived its window
        // would capture the next note played for no visible reason.
        if (! isPickup && processor.midiLearnArmed.load (std::memory_order_acquire) == pointIndex)
            processor.midiLearnArmed.store (-1, std::memory_order_release);

        // Likewise the meter: stop paying for it once nobody is looking. The
        // test matters because opening a second panel claims the slot before
        // this one is destroyed.
        if (isPickup && processor.meteredPickup.load (std::memory_order_acquire) == pointIndex)
            processor.meteredPickup.store (-1, std::memory_order_release);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (fem::theme::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (accent.withAlpha (0.55f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

        g.setColour (accent);
        g.setFont (juce::Font (juce::FontOptions (15.0f)).boldened());
        g.drawText (title, pad, pad, getWidth() - 2 * pad - 54, headerH,
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (pad);

        auto header = r.removeFromTop (headerH);
        onButton->setBounds (header.removeFromRight (54));

        juce::Rectangle<int> footer;
        if (isPickup)
        {
            auto bar = r.removeFromBottom (meterH);
            meter.setBounds (bar.reduced (2, 1));
            r.removeFromBottom (4);
        }
        else
        {
            footer = r.removeFromBottom (footerH);
        }

        // Cells share the row evenly rather than taking a fixed width, so
        // three columns fill the same panel four do.
        for (int row = 0; row * cols < (int) knobs.size(); ++row)
        {
            auto line = r.removeFromTop (rowH);
            const int cellW = line.getWidth() / cols;
            for (int c = 0; c < cols; ++c)
            {
                const size_t k = (size_t) (row * cols + c);
                const auto cell = line.removeFromLeft (c == cols - 1 ? line.getWidth() : cellW);
                if (k < knobs.size())
                    knobs[k]->setBounds (cell.reduced (2));
            }
        }

        if (! isPickup)
        {
            learnButton.setBounds (footer.removeFromRight (110).reduced (1, 2));
            footer.removeFromRight (6);
            if (noteBox != nullptr)
                noteBox->setBounds (footer.removeFromLeft (76).reduced (1, 0));
        }
    }

private:
    void timerCallback() override
    {
        if (isPickup)
        {
            meter.setValue (processor.getPickupPeakDb());
            return;
        }

        updateEnablement();

        // Polled rather than driven from onClick, so that the button also
        // clears when the audio thread captures a note, and when something
        // else disarms it.
        const bool armed = processor.midiLearnArmed.load (std::memory_order_acquire) == pointIndex;
        if (armed != learnButton.getToggleState())
            learnButton.setToggleState (armed, juce::dontSendNotification);
        learnButton.setButtonText (armed ? "listening..." : "MIDI learn");
    }

    /** Greys out the half of a mapping its controller is not using. A max end
        means nothing while its control is Off (the value sits at the min), and
        the velocity curve means nothing unless something actually reads
        velocity: a CC arrives already shaped by the player's hardware.

        Polled from the timer rather than driven from the controls' onChange,
        so a controller moved by host automation greys them too. */
    void updateEnablement()
    {
        const auto value = [this] (const char* id)
        { return processor.apvts.getRawParameterValue (id)->load(); };

        const int posCtl = (int) value (fem::id::sourcePosCtl[pointIndex]);
        const int hamCtl = (int) value (fem::id::sourceHammerCtl[pointIndex]);
        const int frcCtl = (int) value (fem::id::sourceForceCtl[pointIndex]);

        const bool readsVelocity = posCtl == fem::ctlVelocity
                                || hamCtl == fem::ctlVelocity
                                || frcCtl == fem::ctlVelocity;

        if (x2Box     != nullptr) x2Box    ->setEnabled (posCtl != fem::ctlOff);
        if (y2Box     != nullptr) y2Box    ->setEnabled (posCtl != fem::ctlOff);
        if (hamMaxBox != nullptr) hamMaxBox->setEnabled (hamCtl != fem::ctlOff);
        if (frcMaxBox != nullptr) frcMaxBox->setEnabled (frcCtl != fem::ctlOff);
        if (curveBox  != nullptr) curveBox ->setEnabled (readsVelocity);
    }

    /** Returns the box, so a caller that needs to grey it out later can keep a
        handle rather than indexing into `knobs` by position. */
    fxme::FxmeNumberBox* addKnob (const juce::String& label, const char* paramId,
                                  const char* tip,
                                  std::optional<double> centre = {},
                                  std::function<juce::String (double)> textFn = {})
    {
        auto s = std::make_unique<fxme::FxmeNumberBox>();
        fem::theme::styleBox (*s, label, accent, tip);
        if (centre.has_value())
            s->setCentralValue (*centre);          // bipolar: arc grows from centre
        if (textFn != nullptr)
            s->textFromValueFunction = std::move (textFn);
        addAndMakeVisible (*s);
        attachments.push_back (std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, *s));
        knobs.push_back (std::move (s));
        return knobs.back().get();
    }

    static constexpr int pad = 8, headerH = 22, rowH = 42, footerH = 40, knobW = 62,
                         meterH = 14;

    ModalDishAudioProcessor& processor;
    const bool isPickup;
    const int pointIndex;
    const juce::Colour accent;

    /** Knobs per row. A pickup's four are one row of position, level and pan;
        a source's fifteen are five rows of three, each row one quantity and
        what moves it. Declared here so the init list stays in declaration
        order. */
    const int cols;
    juce::String title;

    std::vector<std::unique_ptr<fxme::FxmeNumberBox>> knobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::unique_ptr<fxme::FxmeButton> onButton;
    std::unique_ptr<fxme::FxmeNumberBox> noteBox;   // sources only

    // Non-owning handles into `knobs`, for the ends and the curve a controller
    // can supersede. Null on a pickup panel, which has no mappings.
    fxme::FxmeNumberBox* x2Box = nullptr;
    fxme::FxmeNumberBox* y2Box = nullptr;
    fxme::FxmeNumberBox* hamMaxBox = nullptr;
    fxme::FxmeNumberBox* frcMaxBox = nullptr;
    fxme::FxmeNumberBox* curveBox = nullptr;
    juce::TextButton learnButton { "MIDI learn" };
    fxme::VuMeterComponent meter;          // pickups only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlatePointPanel)
};
