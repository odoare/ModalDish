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

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void showShapeView (bool showShape);
    void setAdvancedVisible (bool shouldShow);
    void syncShapeToProcessor (bool geometryChanged);
    void refreshMeshAndField();
    void refreshField();
    void refreshLiveField();             // live plate field, from the timer
    bool showingLiveField() const;
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
    juce::TextButton shapeViewButton { "Shape" }, plateViewButton { "Plate" };
    juce::TextButton gridButton { "Grid" }, computeButton { "Compute" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::Label statusLabel;

    // Right column — geometry.
    juce::ComboBox toolBox;
    fxme::FxmeSlider aspectKnob, pointsKnob, densityKnob;

    // Right column — modal parameters.
    fxme::FxmeSlider f1Knob, glideKnob, tensionKnob, hammerKnob, forceKnob,
                     nonlinKnob, cascadeKnob, viscKnob, matKnob, modesKnob;
    std::unique_ptr<SliderAttachment> f1Att, glideAtt, tensionAtt, hammerAtt,
                                      forceAtt, nonlinAtt, cascadeAtt, viscAtt,
                                      matAtt, modesAtt;

    // Right column — cascade tuning.
    fxme::FxmeSlider cascAmpKnob, cascDriveKnob, cascAttKnob, cascRelKnob,
                     cascOverKnob, cascWinKnob, cascDeplKnob;
    std::unique_ptr<SliderAttachment> cascAmpAtt, cascDriveAtt, cascAttAtt,
                                      cascRelAtt, cascOverAtt, cascWinAtt, cascDeplAtt;

    // Right column — I/O.
    fxme::FxmeSlider outXKnob, outYKnob, inGainKnob, outGainKnob;
    std::unique_ptr<SliderAttachment> outXAtt, outYAtt, inGainAtt, outGainAtt;

    // Plate view contents: mode shapes (picked by the knob, 0 = bare grid)
    // or the live field, displacement or velocity.
    juce::ComboBox viewBox;
    fxme::FxmeSlider modeViewKnob;
    std::vector<float> modalBuf, fieldBuf;   // scratch for the live field
    float fieldRef = 0.0f;                   // held amplitude reference

    juce::Rectangle<int> geomPanel, modalPanel, cascPanel, ioPanel, legendArea;

    // "Advanced" toggle (parked in the top bar): shows the cascade tuning
    // column and widens the window.
    juce::TextButton advancedButton { "Advanced" };
    bool advancedVisible = false;

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
