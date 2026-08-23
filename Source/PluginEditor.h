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

#include "PluginProcessor.h"
#include "Theme.h"
#include "Components/ShapeCanvas.h"

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
    void refreshStatus();
    void setOutputPosition (double x, double y);
    void drawPlateOverlay (juce::Graphics& g, fxme::acoustics::FemViewComponent& v);

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

    // Mode display selector (0 = none / live plate).
    fxme::FxmeSlider modeViewKnob;

    juce::Rectangle<int> geomPanel, modalPanel, cascPanel, ioPanel, legendArea;

    // "Advanced" toggle (parked in the top bar): shows the cascade tuning
    // column and widens the window.
    juce::TextButton advancedButton { "Advanced" };
    bool advancedVisible = false;

    int displayedModeCount = -1;         // status refresh bookkeeping
    juce::uint32 lastStrikeMs = 0;       // strike marker fade
    juce::Point<float> lastStrikePos;
    int lastMidiStrikeSeen = 0;          // MIDI notes already marked

    // Declared last: fixes keyboard focus for all TextEditors under the editor
    // (including FxmeSlider's right-click value entry).
    fxme::TextEntryFocusFixer textEntryFixer { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FemPlateAudioProcessorEditor)
};
