/*
  ------------------------------------------------------------------------------
    PluginEditor.h

    FemPlate editor: FX-Mechanics top bar; on the left the plate area — the
    shape canvas (draw / standard shapes / rotate / boundary editing) and the
    FEM view (grid + modal filled contours, click to hit the plate), switched
    by two view buttons — with the grid / compute controls underneath; on the
    right the geometry, modal-parameter and I/O knob panels.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <vector>

#include "PluginProcessor.h"
#include "Theme.h"
#include "Components/ShapeCanvas.h"
#include "Components/PlatePointPanel.h"
#include "Components/PointToggle.h"

//==============================================================================
class FemPlateAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::ChangeListener
{
public:
    explicit FemPlateAudioProcessorEditor (FemPlateAudioProcessor&);
    ~FemPlateAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /** The plugin has two modes rather than two views.

        Modal design shows the shape editor with its mesh, and the geometry
        and Modes controls — everything that decides what the plate *is*.
        Perform hides all of that and shows the controls that play it.

        The split is not cosmetic: Modes reallocates and retunes the whole
        filter bank, and raising it mid-performance can produce a very loud
        transient. Putting it behind a mode change is the point. */
    void setDesignMode (bool design);
    void updateWindowSize();
    void setAdvancedVisible (bool shouldShow);
    void syncShapeToProcessor (bool geometryChanged);
    void refreshMeshAndField();
    void refreshField();
    void refreshLiveField();             // live plate field, from the timer
    bool showingLiveField() const;
    /** True when the view selector is on one of the three-dimensional
        entries, in which case plateView3D is on screen instead of plateView. */
    bool showing3D() const;
    /** Whichever plate view the selector currently calls for. */
    juce::Component& activePlateView();
    /** Shows whichever plate view the mode and selector call for. */
    void updatePlateViewVisibility();
    fem::PlateSynth::Field selectedQuantity() const;
    void refreshStatus();
    void setOutputPosition (double x, double y);
    void drawPlateOverlay (juce::Graphics& g, fxme::acoustics::FemViewComponent& v);

    //==========================================================================
    // Plate markers: the pickups and sources drawn on the plate, and the
    // gestures that act on them.

    /** One marker's identity and state, read from the parameters. The two
        kinds share a struct because everything that acts on them — drawing,
        hit-testing, alt-click, the popup — treats them the same way and
        differs only in which parameter ids it reaches for. */
    struct PlateMarker
    {
        bool isPickup = true;
        int index = 0;
        float x = 0.0f, y = 0.0f;
        bool on = false;

        juce::String label() const;
        juce::Colour colour() const;
    };

    /** Every marker, pickups first. Reads the current parameter values. */
    std::vector<PlateMarker> plateMarkers() const;

    /** The marker under a plate-space point, or -1. `markers` must be the
        result of plateMarkers(); the search is in screen space so that the
        hit radius is a constant number of pixels rather than of plate. */
    int markerAt (const std::vector<PlateMarker>& markers, juce::Point<float> screenPos) const;

    void setMarkerPosition (const PlateMarker& m, double x, double y);
    void setMarkerOn (const PlateMarker& m, bool shouldBeOn);
    void showMarkerPanel (const PlateMarker& m);
    bool keyPressed (const juce::KeyPress& key) override;

    FemPlateAudioProcessor& processor;

    fxme::FxmeLookAndFeel lnf;

    fxme::TopBar topBar { "FemPlate", "finite-element plate physical model",
                          JucePlugin_VersionString,
                          juce::ImageCache::getFromMemory (BinaryData::logo686_png,
                                                           BinaryData::logo686_pngSize) };

    // Left: the two stacked views + the action strip.
    fem::ShapeCanvas canvas;
    fxme::acoustics::FemViewComponent plateView;
    // The same mesh and field as a deformed surface. A separate component
    // rather than a mode of the flat one: they share no drawing at all, and
    // only one is ever on screen.
    fxme::acoustics::FemView3DComponent plateView3D;
    juce::TextButton designButton { "Modal design" }, performButton { "Perform" };
    juce::TextButton computeButton { "Compute" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::Label statusLabel;

    // Right column — modal design (shape, mesh, bank size).
    juce::ComboBox toolBox;
    fxme::FxmeNumberBox aspectKnob, pointsKnob, densityKnob, modesKnob;
    std::unique_ptr<SliderAttachment> modesAtt;

    // Right column — dynamics.
    fxme::FxmeNumberBox f1Knob, tensionKnob, hammerKnob, forceKnob,
                        nonlinKnob, cascadeKnob, viscKnob, matKnob, deplKnob,
                        glideKnob, srcChanKnob, freqChanKnob;
    std::unique_ptr<SliderAttachment> f1Att, tensionAtt, hammerAtt,
                                      forceAtt, nonlinAtt, cascadeAtt, viscAtt,
                                      glideAtt, srcChanAtt, freqChanAtt,
                                      matAtt, deplAtt;

    // Right column — cascade tuning (Advanced). Deplete moved out to the
    // Dynamics panel: it is a voicing control, not a tuning constant.
    fxme::FxmeNumberBox cascDriveKnob, cascAttKnob, cascRelKnob,
                        cascOverKnob, cascWinKnob;
    std::unique_ptr<SliderAttachment> cascDriveAtt, cascAttAtt, cascRelAtt,
                                      cascOverAtt, cascWinAtt;

    // Right column — output. The pickup positions used to live here; they are
    // on the plate now, where they belong, and the room went to metering.
    fxme::FxmeNumberBox inGainKnob, outGainKnob;
    std::unique_ptr<SliderAttachment> inGainAtt, outGainAtt;
    fxme::VuMeterComponent outMeter[2];

    // Right column — every point on the plate as an on/off switch: pickups
    // 1..8 on the upper row, sources a..h on the lower, labelled and coloured
    // to match the markers themselves. Attached to the same parameters as the
    // On button in a marker's own panel, so the two agree without either
    // knowing the other exists, and so do alt-click on the plate and the host.
    std::unique_ptr<PointToggle> pickupToggle[fem::maxPickups];
    std::unique_ptr<PointToggle> sourceToggle[fem::maxSources];
    std::unique_ptr<ButtonAttachment> pickupToggleAtt[fem::maxPickups];
    std::unique_ptr<ButtonAttachment> sourceToggleAtt[fem::maxSources];

    // Plate view contents: mode shapes (picked by the knob, 0 = bare grid)
    // or the live field, displacement or velocity.
    juce::ComboBox viewBox;
    fxme::FxmeNumberBox modeViewKnob;
    std::vector<float> modalBuf, fieldBuf;   // scratch for the live field
    float fieldRef = 0.0f;                   // held amplitude reference

    juce::Rectangle<int> designPanel, dynPanel, freqPanel, excitePanel,
                         cascPanel, ioPanel, pointsPanel;

    /** Whether an unmapped note on the sources channel hits the last touched
        point. A parameter-bound toggle rather than a knob, in the group whose
        channel controls decide which notes reach it at all. */
    std::unique_ptr<fxme::FxmeButton> unmappedHitButton;
    juce::Rectangle<int> meterArea;

    // "Advanced" toggle, in the Dynamics panel next to what it extends:
    // shows the cascade tuning column and widens the window.
    juce::TextButton advancedButton { "A >" };
    bool advancedVisible = false;
    bool designMode = true;

    int displayedModeCount = -1;         // status refresh bookkeeping
    /** A fading ring on a point that was struck. Held in *plate* coordinates
        rather than screen ones, so a flash stays where it landed if the view
        is resized mid-fade. Several can be alive at once: one note can fire
        several sources, and a source with Spread scatters its hits — showing
        only the most recent would hide exactly what those controls do. */
    struct HitFlash
    {
        float x = 0.0f, y = 0.0f;
        float amplitude = 0.0f;
        juce::uint32 startMs = 0;
    };

    static constexpr juce::uint32 hitFlashMs = 700;

    std::vector<HitFlash> hitFlashes;
    int lastHitSeen = 0;                 // hits already turned into flashes

    // Declared last: fixes keyboard focus for all TextEditors under the editor
    // (including FxmeSlider's right-click value entry).
    fxme::TextEntryFocusFixer textEntryFixer { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FemPlateAudioProcessorEditor)
};
