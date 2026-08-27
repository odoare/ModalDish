/*
  ------------------------------------------------------------------------------
    PlatePointPanel.h

    The little panel that appears when a pickup or a source marker on the plate
    is clicked: every parameter of that one point, and nothing else.

    One class covers both because they differ only in which parameters they
    show. A pickup is a listening point (position, level, pan) with a meter of
    what it is hearing; a source is a striking and injection point (position,
    hammer, force, spread, send, balance) plus the MIDI note it answers to and
    a Learn button for it.

    It is meant to live inside a juce::CallOutBox launched from the editor, so
    it sizes itself and expects to be handed to the callout by value of its
    getLocalBounds(). Launching it with the editor as the callout's parent is
    what lets it inherit FxmeLookAndFeel — a callout parented to the desktop
    would be drawn in stock JUCE style instead.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "Theme.h"

#include <memory>
#include <optional>
#include <vector>

//==============================================================================
class PlatePointPanel : public juce::Component,
                        private juce::Timer
{
public:
    /** `index` is the pickup or source (both 0..7) this panel edits. */
    PlatePointPanel (FemPlateAudioProcessor& p, bool pickup, int index)
        : processor (p), isPickup (pickup), pointIndex (index),
          accent (pickup ? fem::theme::pickupAccent : fem::theme::sourceAccent)
    {
        title = isPickup ? "Pickup " + juce::String (index + 1)
                         : "Source " + juce::String::charToString (
                               (juce::juce_wchar) fem::sourceLabel (index));

        addKnob ("X", isPickup ? fem::id::pickupX[index] : fem::id::sourceX[index]);
        addKnob ("Y", isPickup ? fem::id::pickupY[index] : fem::id::sourceY[index]);

        if (isPickup)
        {
            addKnob ("Level", fem::id::pickupLevel[index]);
            addKnob ("Pan", fem::id::pickupPan[index], 0.0);

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
            addKnob ("Hammer", fem::id::sourceHammer[index]);
            addKnob ("Force", fem::id::sourceForce[index]);
            addKnob ("Spread", fem::id::sourceSpread[index]);
            addKnob ("Send", fem::id::sourceSend[index]);
            addKnob ("In Bal", fem::id::sourcePan[index], 0.0);
            addKnob ("Note", fem::id::sourceNote[index]);

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
            startTimerHz (10);
        }

        onButton = std::make_unique<fxme::FxmeButton> (
            processor.apvts,
            isPickup ? fem::id::pickupOn[index] : fem::id::sourceOn[index],
            "On", accent);
        addAndMakeVisible (*onButton);

        setSize (4 * knobW + 2 * pad,
                 headerH + rowH * (isPickup ? 1 : 2)
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

        for (int row = 0; row * 4 < (int) knobs.size(); ++row)
        {
            auto line = r.removeFromTop (rowH);
            for (int c = 0; c < 4; ++c)
            {
                const size_t k = (size_t) (row * 4 + c);
                if (k < knobs.size())
                    knobs[k]->setBounds (line.removeFromLeft (knobW).reduced (2));
            }
        }

        if (! isPickup)
            learnButton.setBounds (footer.removeFromRight (110).reduced (1, 2));
    }

private:
    void timerCallback() override
    {
        if (isPickup)
        {
            meter.setValue (processor.getPickupPeakDb());
            return;
        }

        // Polled rather than driven from onClick, so that the button also
        // clears when the audio thread captures a note, and when something
        // else disarms it.
        const bool armed = processor.midiLearnArmed.load (std::memory_order_acquire) == pointIndex;
        if (armed != learnButton.getToggleState())
            learnButton.setToggleState (armed, juce::dontSendNotification);
        learnButton.setButtonText (armed ? "listening..." : "MIDI learn");
    }

    void addKnob (const juce::String& label, const char* paramId,
                  std::optional<double> centre = {})
    {
        auto s = std::make_unique<fxme::FxmeNumberBox>();
        fem::theme::styleBox (*s, label, accent);
        if (centre.has_value())
            s->setCentralValue (*centre);          // bipolar: arc grows from centre
        addAndMakeVisible (*s);
        attachments.push_back (std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, *s));
        knobs.push_back (std::move (s));
    }

    static constexpr int pad = 8, headerH = 22, rowH = 42, footerH = 26, knobW = 62,
                         meterH = 14;

    FemPlateAudioProcessor& processor;
    const bool isPickup;
    const int pointIndex;
    const juce::Colour accent;
    juce::String title;

    std::vector<std::unique_ptr<fxme::FxmeNumberBox>> knobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::unique_ptr<fxme::FxmeButton> onButton;
    juce::TextButton learnButton { "MIDI learn" };
    fxme::VuMeterComponent meter;          // pickups only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlatePointPanel)
};
