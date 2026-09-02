/*
  ------------------------------------------------------------------------------
    PluginEditor.h

    ModalDish editor: FX-Mechanics top bar; on the left the plate area — the
    shape canvas (draw / standard shapes / rotate / boundary editing) and the
    FEM view (grid + modal filled contours, click to hit the plate), switched
    by two view buttons — with the grid / compute controls underneath; on the
    right the geometry, modal-parameter and I/O knob panels.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "Theme.h"
#include "Components/ShapeCanvas.h"
#include "Components/PlatePointPanel.h"
#include "Components/PointToggle.h"
#include "ShapeFile.h"

#include <FxmeTools/components/InfoButton.h>
#include <FxmeTools/components/PresetBarComponent.h>
#include <FxmeTools/components/PresetComponent.h>
#include <FxmeTools/components/SplashOverlay.h>

//==============================================================================
class ModalDishAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::ChangeListener
{
public:
    explicit ModalDishAudioProcessorEditor (ModalDishAudioProcessor&);
    ~ModalDishAudioProcessorEditor() override;

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
    /** Copies the window's own state onto the processor, which outlives it,
        so that closing and reopening the editor is not a reset. Called from
        the destructor; see ModalDishAudioProcessor::EditorState. */
    void saveEditorState();
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

        /** A source's velocity endpoint ('A') rather than its base ('a').
            Everything that acts on a marker still treats it the same way and
            only reaches for a different pair of parameter ids, so this needs
            no special case in the drag, the popup or the on/off. */
        bool velocityEnd = false;

        juce::String label() const;
        juce::Colour colour() const;
    };

    /** Every marker, pickups first. Reads the current parameter values. */
    std::vector<PlateMarker> plateMarkers() const;

    /** Whether a source's two endpoints are close enough to be one point. */
    static bool coincident (float x1, float y1, float x2, float y2);

    /** The marker under a plate-space point, or -1. `markers` must be the
        result of plateMarkers(); the search is in screen space so that the
        hit radius is a constant number of pixels rather than of plate. */
    int markerAt (const std::vector<PlateMarker>& markers, juce::Point<float> screenPos) const;

    void setMarkerPosition (const PlateMarker& m, double x, double y);
    void setMarkerOn (const PlateMarker& m, bool shouldBeOn);
    void showMarkerPanel (const PlateMarker& m);
    bool keyPressed (const juce::KeyPress& key) override;

    ModalDishAudioProcessor& processor;

    fxme::FxmeLookAndFeel lnf;

    /** The identity line under the header: a component of its own rather than
        a paint() call, because half the glow falls *inside* the top bar, and
        only a sibling stacked above it can bleed over the header. */
    struct GlowLine : juce::Component
    {
        static constexpr int kGlow   = 14;
        static constexpr int kHeight = 2 * kGlow + 2;

        GlowLine() { setInterceptsMouseClicks (false, false); }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();
            fem::theme::paintIdentityLine (g, { 0.0f, b.getCentreY() - 1.0f,
                                                b.getWidth(), 2.0f }, (float) kGlow);
        }
    };

    /** The preset browser's toggle: a small triangle that points down when
        the panel is closed and up when it is open, so the control says which
        way the panel will move rather than merely that it exists. Drawn here
        rather than with juce::ArrowButton because that one bakes its
        direction in at construction and cannot be flipped. */
    struct TriangleButton : juce::Button
    {
        TriangleButton() : juce::Button ("presets") {}

        juce::Colour accent { juce::Colours::white };
        bool pointsUp = false;

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto b = getLocalBounds().toFloat().reduced (3.0f);
            const float w = juce::jmin (b.getWidth(), 11.0f);
            const float h = juce::jmin (b.getHeight(), 7.0f);
            const auto c = b.getCentre();

            juce::Path t;
            if (pointsUp)
            {
                t.addTriangle (c.x - w * 0.5f, c.y + h * 0.5f,
                               c.x + w * 0.5f, c.y + h * 0.5f,
                               c.x,            c.y - h * 0.5f);
            }
            else
            {
                t.addTriangle (c.x - w * 0.5f, c.y - h * 0.5f,
                               c.x + w * 0.5f, c.y - h * 0.5f,
                               c.x,            c.y + h * 0.5f);
            }

            g.setColour ((highlighted || down) ? accent.brighter (0.4f) : accent);
            g.fillPath (t);
        }
    };

    /** Opaque backdrop for the preset browser, which paints no background of
        its own and would otherwise show the plate through it. */
    struct PresetOverlay : juce::Component
    {
        explicit PresetOverlay (fxme::PresetManager& manager) : browser (manager)
        {
            addAndMakeVisible (browser);
        }
        void paint (juce::Graphics& g) override { g.fillAll (fem::theme::panel); }
        void resized() override { browser.setBounds (getLocalBounds().reduced (10)); }

        fxme::PresetComponent browser;
    };

    void setPresetPanelVisible (bool shouldBeVisible);

    GlowLine glowLine;
    fxme::InfoButton infoButton;
    fxme::SplashOverlay splash;
    fxme::PresetBarComponent presetBar { processor.getPresetManager() };
    TriangleButton presetsButton;
    PresetOverlay presetOverlay { processor.getPresetManager() };

    fxme::TopBar topBar { "ModalDish", "finite-element plate physical model",
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
    juce::TextButton loadShapeButton { "Load" };
    juce::TextButton saveShapeButton { "Save" };
    // Shape generators, laid out over the sketch beside the boundary-condition
    // key rather than in the panel: they are not tools and not settings, they
    // are two ways of starting a shape, and what they replace is on the canvas.
    juce::TextButton ellipseButton { "Ellipse" }, rectangleButton { "Rectangle" };
    fxme::FxmeNumberBox aspectKnob, pointsKnob, densityKnob, modesKnob;

    // Held as a member because the chooser runs asynchronously and must
    // outlive the call that launches it.
    std::unique_ptr<juce::FileChooser> shapeChooser;
    void loadShapeFile();
    void saveShapeFile();
    std::unique_ptr<SliderAttachment> modesAtt;

    // Right column — dynamics.
    fxme::FxmeNumberBox f1Knob, tensionKnob, hammerKnob, forceKnob,
                        nonlinKnob, cascadeKnob, viscKnob, matKnob, deplKnob,
                        glideKnob, srcChanKnob, freqChanKnob,
                        srcDurScaleKnob, srcForceScaleKnob;
    std::unique_ptr<SliderAttachment> f1Att, tensionAtt, hammerAtt,
                                      forceAtt, nonlinAtt, cascadeAtt, viscAtt,
                                      glideAtt, srcChanAtt, freqChanAtt,
                                      matAtt, deplAtt,
                                      srcDurScaleAtt, srcForceScaleAtt;

    // Right column — cascade tuning (Advanced). Deplete moved out to the
    // Dynamics panel: it is a voicing control, not a tuning constant.
    fxme::FxmeNumberBox cascDriveKnob, cascAttKnob, cascRelKnob,
                        cascOverKnob, cascWinKnob;
    std::unique_ptr<SliderAttachment> cascDriveAtt, cascAttAtt, cascRelAtt,
                                      cascOverAtt, cascWinAtt;

    /** Cascade injection point: off is the hit point, on is the fixed
        per-mode weighting. A button rather than a sixth number box, because
        it chooses between two behaviours rather than setting an amount. */
    std::unique_ptr<fxme::FxmeButton> cascModalInjButton;

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
    /** A grid rebuild is owed, from the Grid knob. The canvas keeps its own
        flag for an outline being dragged; both are drained by the timer. */
    bool meshDirty = false;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModalDishAudioProcessorEditor)
};
