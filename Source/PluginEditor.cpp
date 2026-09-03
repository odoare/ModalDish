/*
  ------------------------------------------------------------------------------
    PluginEditor.cpp — see PluginEditor.h.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "PluginEditor.h"

//==============================================================================
ModalDishAudioProcessorEditor::ModalDishAudioProcessorEditor (ModalDishAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);
    // The plate takes 1..4 / a..h to place a pickup or a source under the
    // mouse, so the editor has to be able to hold the keyboard focus itself
    // — key presses bubble up from a focused child, but only if something in
    // the window has focus at all.
    setWantsKeyboardFocus (true);
    lnf.setAccentColour (fem::theme::accent);   // tints the combo drop-downs
    addAndMakeVisible (topBar);
    topBar.setBackgroundColour (fem::theme::topBarBg);
    topBar.setAccentColour (fem::theme::accent);

    // Above the header so the halo can bleed up over it.
    addAndMakeVisible (glowLine);

    // Hidden until shown; show() brings it to the front itself, so it does
    // not matter that later children are added above it.
    splash.setImage (juce::ImageCache::getFromMemory (BinaryData::splash_png,
                                                      BinaryData::splash_pngSize));
    addChildComponent (splash);

    // Centred in whatever the bar has left between the title blurb and the
    // preset controls; the bar gives the blurb priority and shrinks the
    // artwork into the gap, so it never collides with either.
    topBar.setDecoration (juce::ImageCache::getFromMemory (BinaryData::icon_png,
                                                           BinaryData::icon_pngSize));

    // Clicking either the company logo or the icon reopens the splash.
    topBar.onLogoClicked = [this] { splash.show(); };

    presetBar.setAccentColour (fem::theme::accent);
    presetsButton.setTooltip ("Browse presets");
    presetsButton.setMouseClickGrabsKeyboardFocus (false);
    presetsButton.accent = fem::theme::accent;
    presetsButton.onClick = [this]
    {
        setPresetPanelVisible (! presetOverlay.isVisible());
    };
    // The plugin is driven as much from the keyboard and the plate as from the
    // knobs, and none of that is discoverable from the panels. The README
    // carries the same list, but the README is not in front of the player.
    infoButton.setColours ({ fem::theme::accent,
                             fem::theme::text,
                             fem::theme::panel,
                             fem::theme::panelLine });
    infoButton.setInfo (
        "ModalDish shortcuts",
        "ON THE PLATE\n"
        "  click                  strike the plate there\n"
        "  click a marker         open that point's panel\n"
        "  alt-click a marker     switch that point off\n"
        "  right-click / ctrl-drag   move pickup 1\n"
        "\n"
        "PLACING POINTS (mouse over the plate)\n"
        "  1 - 8                  put that pickup under the cursor\n"
        "  a - h                  put that source under the cursor\n"
        "  A - H                  put that source's velocity endpoint there\n"
        "\n"
        "A source is a segment: it strikes at 'a' when its control reads 0 and\n"
        "at 'A' when it reads full. The two start together, so a source is a\n"
        "plain point until you place its capital.\n"
        "\n"
        "PLACE POINTS TOOL (modal design)\n"
        "  click                  add a vertex, or insert one into the\n"
        "                         nearest edge once the shape is closed\n"
        "  drag a vertex          move it\n"
        "  alt-click a vertex     delete it (three are always kept)\n"
        "\n"
        "SOURCE MAPPINGS\n"
        "Position, hammer and force are each a min/max pair plus a control\n"
        "(Off, Vel or a CC). The value is min + amount * (max - min), and min\n"
        "above max simply inverts it. Off holds the min, which is why a max\n"
        "greys out when its control is Off.");
    addAndMakeVisible (infoButton);

    // Tooltips. Every number box carries one (Tooltips.h); this decides
    // whether any of them can be seen. On by default, because the plugin has
    // a lot of controls whose names are necessarily short, and off is one
    // click away once they are learned.
    fem::theme::styleButton (tipsButton, fem::theme::accent);
    tipsButton.setClickingTogglesState (true);
    tipsButton.setMouseClickGrabsKeyboardFocus (false);
    tipsButton.setTooltip ("Control tooltips on or off");
    tipsButton.onClick = [this] { setTooltipsEnabled (tipsButton.getToggleState()); };
    setTooltipsEnabled (processor.editorState.tooltips);

    topBar.setRightControls ({ { &presetBar, 260 },
                               { &presetsButton, 22 },
                               { &tipsButton, 24 },
                               { &infoButton, 26 } });

    addChildComponent (presetOverlay);
    presetOverlay.browser.setAccentColour (fem::theme::accent);

    // Advanced toggle: shows the cascade tuning column, widening the window.
    // The Advanced toggle sits in the Dynamics panel, in the slot next to the
    // controls it extends, rather than in the top bar away from them.
    fem::theme::styleButton (advancedButton, fem::theme::dynGroup);
    advancedButton.setClickingTogglesState (true);
    advancedButton.setMouseClickGrabsKeyboardFocus (false);
    advancedButton.onClick = [this] { setAdvancedVisible (advancedButton.getToggleState()); };
    addAndMakeVisible (advancedButton);

    // --- left: views ---------------------------------------------------------
    addAndMakeVisible (canvas);
    addChildComponent (plateView);   // hidden until a mesh exists

    canvas.bcColour = [] (int bc) { return fem::theme::bcColour (bc); };
    canvas.bcName   = [] (int bc) { return juce::String (fem::theme::bcName (bc)); };
    canvas.setShape (processor.shape().outline,
                     processor.shape().segStarts,
                     processor.shape().segBcs);
    canvas.onShapeChanged = [this] { syncShapeToProcessor (true); };
    canvas.onBoundaryChanged = [this] { syncShapeToProcessor (false); };

    plateView.setColours (fem::theme::plateBg, fem::theme::plateGrid,
                          fem::theme::fieldNeg, fem::theme::fieldPos);

    addChildComponent (plateView3D);
    // Black ground and a white mesh, rather than the flat view's palette: the
    // 3D picture is read as a lit surface, and the higher contrast is what
    // makes the wireframe describe the relief instead of tinting it.
    plateView3D.setColours (juce::Colours::black, juce::Colours::white,
                            fem::theme::fieldNeg, fem::theme::fieldPos);
    plateView3D.paintOverlay = [this] (juce::Graphics& g,
                                       fxme::acoustics::FemView3DComponent& v)
    {
        // Only the status hint: the markers and the hit flashes are tied to
        // plate positions, and a screen point in the deformed view is not one.
        juce::ignoreUnused (v);
        g.setColour (fem::theme::dimText);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("drag to rotate - wheel to zoom",
                    getLocalBounds().reduced (8).removeFromBottom (16),
                    juce::Justification::centredLeft);
    };
    plateView.onPlateClick = [this] (double x, double y, const juce::MouseEvent& e)
    {
        // A marker under the cursor claims the click: alt turns it off, a
        // plain click opens its panel. Everywhere else the plate behaves as
        // it always has — a click is a hit.
        const auto markers = plateMarkers();
        const int hit = markerAt (markers, e.position);
        if (hit >= 0)
        {
            if (e.mods.isAltDown())
                setMarkerOn (markers[(size_t) hit], false);
            else
                showMarkerPanel (markers[(size_t) hit]);
            return;
        }

        if (e.mods.isRightButtonDown() || e.mods.isCtrlDown())
            setOutputPosition (x, y);
        else
        {
            // No marker is drawn from here: the flash follows the hit the
            // synth actually made, which arrives back through the hit ring
            // one block later. That is what makes a scattered source show
            // its scatter instead of the point it was aimed at.
            processor.requestStrike ((float) x, (float) y, 1.0f);
        }
    };
    plateView.onPlateDrag = [this] (double x, double y, const juce::MouseEvent& e)
    {
        if (e.mods.isRightButtonDown() || e.mods.isCtrlDown())
            setOutputPosition (x, y);
    };
    plateView.paintOverlay = [this] (juce::Graphics& g, fxme::acoustics::FemViewComponent& v)
    {
        drawPlateOverlay (g, v);
    };

    // --- left: action strip ---------------------------------------------------
    for (auto* b : { &designButton, &performButton, &computeButton })
    {
        fem::theme::styleButton (*b, fem::theme::accent);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    designButton.setClickingTogglesState (false);
    performButton.setClickingTogglesState (false);
    designButton.onClick = [this] { setDesignMode (true); };
    // Leaving design without computing keeps the shape but goes on sounding
    // the last model that was computed, so the shape can be worked on across
    // several passes without the plate falling silent in between.
    performButton.onClick = [this] { setDesignMode (false); };
    computeButton.onClick = [this]
    {
        processor.computeModes();
    };

    // The Grid button is gone: the mesh is rebuilt on every geometry change
    // (syncShapeToProcessor) and drawn straight into the shape editor, so the
    // grid being designed is always on screen without asking for it.

    addChildComponent (progressBar);
    progressBar.setColour (juce::ProgressBar::foregroundColourId, fem::theme::accent);
    progressBar.setColour (juce::ProgressBar::backgroundColourId, fem::theme::panel);
    progressBar.setPercentageDisplay (false);

    statusLabel.setColour (juce::Label::textColourId, fem::theme::dimText);
    statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (statusLabel);

    fem::theme::styleCombo (viewBox, fem::theme::accent);
    viewBox.addItem ("Modes", 1);
    viewBox.addItem ("Displacement", 2);
    viewBox.addItem ("Velocity", 3);
    // Ids 4 and 5 are the same two quantities as a deformed surface, so the
    // odd ids stay "velocity" and selectedQuantity() needs no special case.
    viewBox.addItem ("Displacement 3D", 4);
    viewBox.addItem ("Velocity 3D", 5);
    viewBox.setSelectedId (juce::jlimit (1, 5, processor.editorState.viewId),
                           juce::dontSendNotification);
    viewBox.onChange = [this]
    {
        // The Mode knob picks a shape to display; it means nothing while the
        // view is showing the plate's live motion.
        modeViewKnob.setEnabled (! showingLiveField());
        fieldRef = 0.0f;
        updatePlateViewVisibility();
        refreshField();
        refreshStatus();
    };
    addAndMakeVisible (viewBox);

    fem::theme::styleBox (modeViewKnob, "Mode", fem::theme::accent, fem::tip::modeView);
    modeViewKnob.setRange (0, fem::maxModes, 1);
    modeViewKnob.setValue (processor.editorState.displayMode, juce::dontSendNotification);
    // Normally set by viewBox.onChange, which a restored selection does not
    // fire: the Mode knob means nothing while the view shows live motion.
    modeViewKnob.setEnabled (! showingLiveField());
    modeViewKnob.onValueChange = [this] { refreshField(); refreshStatus(); };
    addAndMakeVisible (modeViewKnob);

    // --- right: geometry panel -------------------------------------------------
    fem::theme::styleCombo (toolBox, fem::theme::geomAccent);
    // Ids are positions in ShapeCanvas::Tool (id - 1), so Place points keeps
    // id 4 wherever it sits in the list; it reads next to Draw shape because
    // the two are the same job by different means. Every entry here is a
    // lasting mode of the mouse: making an ellipse or a rectangle is a single
    // act, so it is a button on the canvas rather than a mode to be left.
    toolBox.addItem ("Draw shape", 1);
    toolBox.addItem ("Place points", 4);
    toolBox.addItem ("Rotate", 2);
    toolBox.addItem ("Edit boundary", 3);
    toolBox.setSelectedId (juce::jlimit (1, 4, processor.editorState.toolId),
                           juce::dontSendNotification);
    toolBox.onChange = [this]
    {
        canvas.setTool ((fem::ShapeCanvas::Tool) (toolBox.getSelectedId() - 1));
        setDesignMode (true);
    };
    addAndMakeVisible (toolBox);

    for (auto* b : { &loadShapeButton, &saveShapeButton })
    {
        fem::theme::styleButton (*b, fem::theme::geomAccent);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    loadShapeButton.onClick = [this] { loadShapeFile(); };
    saveShapeButton.onClick = [this] { saveShapeFile(); };

    // Added after the canvas, so they sit on top of it. Neither one changes
    // the tool: the shape appears under whatever the mouse is already doing,
    // ready to have points placed in it, its boundary re-cut, a fresh outline
    // drawn over it or the modes computed from it.
    for (auto* b : { &ellipseButton, &rectangleButton })
    {
        fem::theme::styleButton (*b, fem::theme::geomAccent);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    ellipseButton.onClick   = [this] { canvas.generateStandardShape (false); };
    rectangleButton.onClick = [this] { canvas.generateStandardShape (true); };

    fem::theme::styleBox (aspectKnob, "Aspect", fem::theme::geomAccent, fem::tip::aspect);
    aspectKnob.setRange (0.25, 4.0, 0.01);
    aspectKnob.setSkewFactorFromMidPoint (1.0);
    aspectKnob.setValue (processor.editorState.aspect, juce::dontSendNotification);
    aspectKnob.onValueChange = [this] { canvas.setAspect (aspectKnob.getValue()); };
    addAndMakeVisible (aspectKnob);

    // Both restored together, and quietly: the shape on screen when a window
    // reopens is the one that was left there, not a fresh one.
    canvas.restoreToolAndAspect (
        (fem::ShapeCanvas::Tool) (toolBox.getSelectedId() - 1),
        processor.editorState.aspect);

    fem::theme::styleBox (pointsKnob, "Points", fem::theme::geomAccent, fem::tip::points);
    pointsKnob.setRange (1, 12, 1);
    pointsKnob.setValue (canvas.numBorderPoints(), juce::dontSendNotification);
    pointsKnob.onValueChange = [this]
    {
        canvas.setNumBorderPoints ((int) pointsKnob.getValue());
    };
    addAndMakeVisible (pointsKnob);

    fem::theme::styleBox (densityKnob, "Grid", fem::theme::geomAccent, fem::tip::grid);
    densityKnob.setRange (8, 48, 1);
    densityKnob.setValue (processor.shape().meshDensity, juce::dontSendNotification);
    densityKnob.onValueChange = [this]
    {
        // The density is taken now so a deferred rebuild uses the value the
        // knob ended on; the rebuild itself waits for the timer, because a
        // drag from 8 to 48 walks through every density in between and the
        // ones near the top are milliseconds each.
        processor.shape().meshDensity = (int) densityKnob.getValue();
        meshDirty = true;
    };
    addAndMakeVisible (densityKnob);

    // --- right: the control groups ---------------------------------------------
    // Every box takes the colour of the group it sits in, so the panel it
    // belongs to is readable without reading its label.
    auto& apvts = processor.apvts;
    auto addBox = [&] (fxme::FxmeNumberBox& s, const char* text, const char* id,
                       std::unique_ptr<SliderAttachment>& att, juce::Colour accent,
                       const char* tip)
    {
        fem::theme::styleBox (s, text, accent, tip);
        addAndMakeVisible (s);
        att = std::make_unique<SliderAttachment> (apvts, id, s);
    };

    // Group 1, Dynamics.
    addBox (tensionKnob, "Tension", fem::id::tension, tensionAtt, fem::theme::dynGroup, fem::tip::tension);
    addBox (nonlinKnob, "Nonlinear", fem::id::nonlin, nonlinAtt, fem::theme::dynGroup, fem::tip::nonlinear);
    addBox (viscKnob, "Viscous", fem::id::viscDamp, viscAtt, fem::theme::dynGroup, fem::tip::viscous);
    addBox (matKnob, "Material", fem::id::matDamp, matAtt, fem::theme::dynGroup, fem::tip::material);
    addBox (cascadeKnob, "Cascade", fem::id::cascade, cascadeAtt, fem::theme::dynGroup, fem::tip::cascade);
    addBox (deplKnob, "Deplete", fem::id::cascDeplete, deplAtt, fem::theme::dynGroup, fem::tip::deplete);

    // Group 2, Frequency control. Integer boxes rather than combos for the
    // channels so the row matches the rest of the panel; the zero of each
    // reads as a word, because "0" means two different things here and
    // neither of them is a channel number.
    addBox (f1Knob, "Frequency", fem::id::f1, f1Att, fem::theme::freqGroup, fem::tip::frequency);
    addBox (glideKnob, "Glide", fem::id::glide, glideAtt, fem::theme::freqGroup, fem::tip::glide);
    addBox (srcChanKnob, "Src Chan", fem::id::srcChannel, srcChanAtt, fem::theme::freqGroup, fem::tip::srcChan);
    addBox (freqChanKnob, "Freq Chan", fem::id::freqChannel, freqChanAtt, fem::theme::freqGroup, fem::tip::freqChan);
    srcChanKnob.textFromValueFunction = [] (double v)
        { return v < 0.5 ? juce::String ("Omni") : juce::String ((int) v); };
    freqChanKnob.textFromValueFunction = [] (double v)
        { return v < 0.5 ? juce::String ("Off") : juce::String ((int) v); };
    srcChanKnob.updateText();
    freqChanKnob.updateText();

    unmappedHitButton = std::make_unique<fxme::FxmeButton> (
        apvts, fem::id::unmappedHit, "Unmapped note hits", fem::theme::freqGroup);
    addAndMakeVisible (*unmappedHitButton);

    // Group 3, Hammer control.
    addBox (hammerKnob, "Duration", fem::id::hammerMs, hammerAtt, fem::theme::hammerGroup, fem::tip::duration);
    addBox (forceKnob, "Force", fem::id::force, forceAtt, fem::theme::hammerGroup, fem::tip::force);

    // Trims over the eight sources, under the global hammer they sit beside.
    // Shown as a multiplier rather than a bare number: "Src Dur 2.5" could be
    // milliseconds, and "x2.50" cannot.
    addBox (srcDurScaleKnob, "Src Dur", fem::id::srcHammerScale,
            srcDurScaleAtt, fem::theme::hammerGroup, fem::tip::srcDur);
    addBox (srcForceScaleKnob, "Src Force", fem::id::srcForceScale,
            srcForceScaleAtt, fem::theme::hammerGroup, fem::tip::srcForce);
    for (auto* k : { &srcDurScaleKnob, &srcForceScaleKnob })
    {
        k->textFromValueFunction = [] (double v)
            { return "x" + juce::String (v, v < 10.0 ? 2 : 1); };
        k->updateText();
    }

    // Modes belongs with the design, not the performance: it reallocates and
    // retunes the whole bank, and raising it while the plate is ringing can
    // produce a very loud transient. It is only reachable in design mode.
    fem::theme::styleBox (modesKnob, "Modes", fem::theme::geomAccent, fem::tip::modes);
    addAndMakeVisible (modesKnob);
    modesAtt = std::make_unique<SliderAttachment> (apvts, fem::id::numModes, modesKnob);

    // --- right: cascade tuning ---------------------------------------------------
    auto addCasc = [&] (fxme::FxmeNumberBox& s, const char* text, const char* id,
                        std::unique_ptr<SliderAttachment>& att, const char* tip)
    {
        fem::theme::styleBox (s, text, fem::theme::dynGroup, tip);
        addAndMakeVisible (s);
        att = std::make_unique<SliderAttachment> (apvts, id, s);
    };
    addCasc (cascDriveKnob, "Drive", fem::id::cascDrive, cascDriveAtt, fem::tip::cascDrive);
    addCasc (cascWinKnob, "Window", fem::id::cascWindow, cascWinAtt, fem::tip::cascWindow);
    addCasc (cascAttKnob, "Attack", fem::id::cascAttack, cascAttAtt, fem::tip::cascAttack);
    addCasc (cascRelKnob, "Release", fem::id::cascRelease, cascRelAtt, fem::tip::cascRelease);
    addCasc (cascOverKnob, "Overlap", fem::id::cascOverlap, cascOverAtt, fem::tip::cascOverlap);

    cascModalInjButton = std::make_unique<fxme::FxmeButton> (
        apvts, fem::id::cascModalInject, "Modal inject", fem::theme::dynGroup);
    addAndMakeVisible (*cascModalInjButton);

    // --- right: I/O --------------------------------------------------------------
    auto addIo = [&] (fxme::FxmeNumberBox& s, const char* text, const char* id,
                      std::unique_ptr<SliderAttachment>& att, const char* tip)
    {
        fem::theme::styleBox (s, text, fem::theme::ioGroup, tip);
        addAndMakeVisible (s);
        att = std::make_unique<SliderAttachment> (apvts, id, s);
    };
    addIo (inGainKnob, "In", fem::id::inGain, inGainAtt, fem::tip::inGain);
    addIo (outGainKnob, "Out", fem::id::outGain, outGainAtt, fem::tip::outGain);

    // Output metering, post Out Gain. The pickup positions used to occupy
    // this room; they live on the plate now, where a position belongs.
    for (auto& m : outMeter)
    {
        m.setRange (-60.0f, 6.0f);
        m.setZeroLevel (0.0f);
        m.setHorizontal (true);
        m.setMeterColor (fem::theme::ioGroup);
        addAndMakeVisible (m);
    }

    // --- right: the plate's points, as two rows of switches ----------------
    // The same digits and letters that label the markers on the plate and
    // that place them from the keyboard, so a row reads as a picture of what
    // is currently on. Nothing here knows about the marker panels: both ends
    // are attachments on one parameter, which is what keeps them in step.
    const auto addPointToggle = [this] (std::unique_ptr<PointToggle>& b,
                                        std::unique_ptr<ButtonAttachment>& att,
                                        const juce::String& label, const char* id,
                                        juce::Colour accent)
    {
        b = std::make_unique<PointToggle> (label, accent);
        addAndMakeVisible (*b);
        att = std::make_unique<ButtonAttachment> (processor.apvts, id, *b);
    };

    for (int i = 0; i < fem::maxPickups; ++i)
        addPointToggle (pickupToggle[i], pickupToggleAtt[i], juce::String (i + 1),
                        fem::id::pickupOn[i], fem::theme::pickupAccent);
    for (int i = 0; i < fem::maxSources; ++i)
        addPointToggle (sourceToggle[i], sourceToggleAtt[i],
                        juce::String::charToString ((juce::juce_wchar) fem::sourceLabel (i)),
                        fem::id::sourceOn[i], fem::theme::sourceAccent);

    plateView3D.setOrientation (processor.editorState.azimuth,
                                processor.editorState.elevation);
    plateView3D.setZoom (processor.editorState.zoom);

    processor.addChangeListener (this);
    refreshMeshAndField();
    refreshStatus();
    // The first window of a run lands on the playable view if there is
    // already something to play; a reopened one goes back to whichever side
    // it was closed on, which is what makes closing the window a no-op.
    setDesignMode (processor.editorState.everOpened
                       ? processor.editorState.designMode
                       : processor.getCurrentModel() == nullptr);
    startTimerHz (30);

    setAdvancedVisible (processor.editorState.advanced);   // also sets the window size

    // First window of this run only: reopening the editor must not replay the
    // splash, which is why the flag lives on the processor.
    if (processor.claimSplash())
        splash.show();
}

ModalDishAudioProcessorEditor::~ModalDishAudioProcessorEditor()
{
    saveEditorState();
    processor.removeChangeListener (this);

    // Before the look-and-feel is detached and while this is still a valid
    // parent: the tooltip window is a child that draws through both.
    tooltipWindow = nullptr;
    setLookAndFeel (nullptr);
}

void ModalDishAudioProcessorEditor::saveEditorState()
{
    // Written once, here, rather than at each control: the destructor runs
    // whenever the window closes, it is the only moment that matters, and the
    // 3D camera has no change callback to hang a write on anyway.
    auto& st = processor.editorState;
    st.designMode  = designMode;
    st.advanced    = advancedVisible;
    st.viewId      = viewBox.getSelectedId();
    st.toolId      = toolBox.getSelectedId();
    st.displayMode = (int) modeViewKnob.getValue();
    st.aspect      = aspectKnob.getValue();
    st.azimuth     = plateView3D.projection().getAzimuth();
    st.elevation   = plateView3D.projection().getElevation();
    st.zoom        = plateView3D.getZoom();
    st.tooltips    = tipsButton.getToggleState();
    st.everOpened  = true;

    // The two overlays are not restored. A preset browser or a marker panel
    // that reappeared over the plate on its own would be a window doing
    // something nobody asked it to, where a view selector coming back is a
    // window remembering.
}

//==============================================================================
void ModalDishAudioProcessorEditor::setAdvancedVisible (bool shouldShow)
{
    advancedVisible = shouldShow;
    advancedButton.setToggleState (shouldShow, juce::dontSendNotification);
    // The arrow points the way the column will move, which is the only part
    // of a disclosure control anyone reads.
    advancedButton.setButtonText (shouldShow ? "A <" : "A >");
    for (auto* k : { &cascDriveKnob, &cascWinKnob, &cascAttKnob,
                     &cascRelKnob, &cascOverKnob })
        k->setVisible (shouldShow && ! designMode);
    if (cascModalInjButton != nullptr)
        cascModalInjButton->setVisible (shouldShow && ! designMode);
    updateWindowSize();
    repaint();
}

void ModalDishAudioProcessorEditor::setDesignMode (bool design)
{
    designMode = design;
    canvas.setVisible (design);
    updatePlateViewVisibility();
    designButton.setToggleState (design, juce::dontSendNotification);
    performButton.setToggleState (! design, juce::dontSendNotification);

    // Design controls and performance controls are mutually exclusive: the
    // panel a control is not in is not merely greyed but absent, so there is
    // never a Modes knob on screen while the plate is being played.
    //
    // Typed arrays rather than braced lists: the elements are sliders, combo
    // boxes and buttons, and a braced list deduces one element type without
    // considering the derived-to-base conversion that would unify them.
    juce::Component* const designOnly[] = {
        &toolBox, &aspectKnob, &pointsKnob, &densityKnob, &modesKnob,
        &loadShapeButton, &saveShapeButton, &ellipseButton, &rectangleButton
    };
    for (auto* c : designOnly)
        c->setVisible (design);

    juce::Component* const performOnly[] = {
        &f1Knob, &tensionKnob, &hammerKnob, &forceKnob, &nonlinKnob, &cascadeKnob,
        &viscKnob, &matKnob, &deplKnob, &advancedButton, &inGainKnob, &outGainKnob,
        &glideKnob, &srcChanKnob, &freqChanKnob, unmappedHitButton.get(),
        &srcDurScaleKnob, &srcForceScaleKnob,
        &outMeter[0], &outMeter[1], &viewBox, &modeViewKnob
    };
    for (auto* c : performOnly)
        c->setVisible (! design);

    for (auto* k : { &cascDriveKnob, &cascWinKnob, &cascAttKnob,
                     &cascRelKnob, &cascOverKnob })
        k->setVisible (advancedVisible && ! design);
    if (cascModalInjButton != nullptr)
        cascModalInjButton->setVisible (advancedVisible && ! design);

    // The switches follow the plate: in modal design there are no markers on
    // screen for them to refer to.
    for (auto& b : pickupToggle) b->setVisible (! design);
    for (auto& b : sourceToggle) b->setVisible (! design);

    updateWindowSize();
    resized();
    repaint();
}

void ModalDishAudioProcessorEditor::updateWindowSize()
{
    // The cascade column only exists in Perform with Advanced up, so the
    // window must not stay wide when design mode hides it. It is narrower
    // than the main column because it is a single stack of number boxes.
    // 800 rather than 760 since the Hammer panel took a second row: the
    // perform column needs 710 px of panels and the window has to hold them,
    // or the Transducers row at the bottom is laid out past the edge and
    // simply is not there. The left side gets the extra height too, which the
    // plate view can only use well.
    setSize ((advancedVisible && ! designMode) ? 1142 : 1022, 800);
}

void ModalDishAudioProcessorEditor::setPresetPanelVisible (bool shouldBeVisible)
{
    presetOverlay.setVisible (shouldBeVisible);
    if (shouldBeVisible)
        presetOverlay.toFront (false);
    presetsButton.pointsUp = shouldBeVisible;   // points the way the panel will go
    presetsButton.repaint();
    resized();
}

void ModalDishAudioProcessorEditor::saveShapeFile()
{
    shapeChooser = std::make_unique<juce::FileChooser> (
        "Save this plate geometry", juce::File(), "*.json");

    const auto chooserFlags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    shapeChooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;                       // cancelled
        if (file.getFileExtension().isEmpty())
            file = file.withFileExtension ("json");

        // Saved from the canvas rather than from the processor's copy: the
        // canvas is what is on screen, and a gesture that has not settled yet
        // has reached it but not the processor.
        const bool ok = fem::shapefile::save (file, canvas.outline(),
                                              canvas.segmentStarts(),
                                              canvas.segmentBcs(),
                                              processor.shape().meshDensity,
                                              file.getFileNameWithoutExtension());
        if (! ok)
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Could not save " + file.getFileName(),
                "The file could not be written. Check the folder is writable.");
    });
}

void ModalDishAudioProcessorEditor::loadShapeFile()
{
    shapeChooser = std::make_unique<juce::FileChooser> (
        "Load a plate geometry", juce::File(), "*.json");

    const auto chooserFlags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    shapeChooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;                       // cancelled

        const auto result = fem::shapefile::load (file);
        if (! result.ok)
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Could not load " + file.getFileName(), result.error);
            return;
        }

        // Lands in the canvas as an ordinary editable shape: the points can be
        // dragged, the segments re-cut, the conditions cycled, and nothing is
        // computed until Compute is pressed. Loading a file is a starting
        // point, not a commitment.
        canvas.setShape (result.shape.outline, result.shape.segStarts,
                         result.shape.segBcs);

        if (result.shape.meshDensity > 0)
        {
            processor.shape().meshDensity = result.shape.meshDensity;
            densityKnob.setValue (result.shape.meshDensity, juce::dontSendNotification);
        }

        setDesignMode (true);
        toolBox.setSelectedId (3, juce::dontSendNotification);   // Edit boundary
        canvas.setTool (fem::ShapeCanvas::Tool::Boundary);
        syncShapeToProcessor (true);
    });
}

void ModalDishAudioProcessorEditor::setTooltipsEnabled (bool shouldBeEnabled)
{
    tipsButton.setToggleState (shouldBeEnabled, juce::dontSendNotification);

    // Creating and destroying the window, rather than leaving it in place with
    // a long delay: a TooltipWindow is what drives the whole mechanism, so
    // without one the controls keep their sentences and nothing displays them.
    if (shouldBeEnabled)
    {
        if (tooltipWindow == nullptr)
            tooltipWindow = std::make_unique<juce::TooltipWindow> (this);
    }
    else
    {
        tooltipWindow = nullptr;
    }
}

void ModalDishAudioProcessorEditor::syncShapeToProcessor (bool geometryChanged)
{
    auto& s = processor.shape();
    s.outline = canvas.outline();
    s.segStarts = canvas.segmentStarts();
    s.segBcs = canvas.segmentBcs();

    if (geometryChanged)
    {
        processor.invalidateShape();
        processor.buildMesh();   // instant feedback in the plate view
    }
    else
    {
        processor.invalidateBoundary();
    }
    pointsKnob.setValue (canvas.numBorderPoints(), juce::dontSendNotification);
    refreshStatus();
}

void ModalDishAudioProcessorEditor::refreshMeshAndField()
{
    // Every view of the mesh gets it here, and this is the only place any of
    // them does: a view left holding a null mesh draws nothing at all, with
    // no other symptom to go on.
    const auto mesh = processor.getDisplayMesh();
    plateView.setMesh (mesh);
    plateView3D.setMesh (mesh);
    // The shape editor draws the same mesh, in its own coordinates, so the
    // grid is visible while it is being designed.
    canvas.setMesh (mesh);
    refreshField();
}

bool ModalDishAudioProcessorEditor::showingLiveField() const
{
    return viewBox.getSelectedId() >= 2;
}

fem::PlateSynth::Field ModalDishAudioProcessorEditor::selectedQuantity() const
{
    const int id = viewBox.getSelectedId();
    return (id == 3 || id == 5) ? fem::PlateSynth::Field::velocity
                                : fem::PlateSynth::Field::displacement;
}

bool ModalDishAudioProcessorEditor::showing3D() const
{
    return viewBox.getSelectedId() >= 4;
}

juce::Component& ModalDishAudioProcessorEditor::activePlateView()
{
    return showing3D() ? (juce::Component&) plateView3D : (juce::Component&) plateView;
}

void ModalDishAudioProcessorEditor::updatePlateViewVisibility()
{
    const bool perform = ! designMode;
    plateView.setVisible (perform && ! showing3D());
    plateView3D.setVisible (perform && showing3D());
}

void ModalDishAudioProcessorEditor::refreshField()
{
    if (showingLiveField())
    {
        refreshLiveField();
        return;
    }

    // A mode shape is a still picture with no natural scale: let the view
    // normalise it on its own maximum.
    plateView.setFieldScale (0.0f);
    plateView3D.setFieldScale (0.0f);

    const auto* model = processor.getCurrentModel();
    const int k = (int) modeViewKnob.getValue();
    // Only FEM modes have a mesh shape; tail modes show the bare grid.
    if (model != nullptr && k >= 1 && k <= model->numFemModes())
    {
        plateView.setField (model->modes.shapes[(size_t) (k - 1)]);
        plateView3D.setField (model->modes.shapes[(size_t) (k - 1)]);
    }
    else
    {
        plateView.setField ({});
        plateView3D.setField ({});
    }
}

void ModalDishAudioProcessorEditor::refreshLiveField()
{
    // w(x) = sum_k q_k phi_k(x) (or its time derivative), evaluated at the
    // mesh vertices. No field reconstruction is needed on the audio thread:
    // it publishes the modal coordinates and the sum happens here, once per
    // frame. FEM modes only — the statistical tail has no mesh shape.
    const auto* model = processor.getCurrentModel();
    const int numFem = model != nullptr ? model->numFemModes() : 0;
    if (numFem < 1)
    {
        plateView.setField ({});
        plateView3D.setField ({});
        return;
    }

    const int numVerts = (int) model->modes.shapes[0].size();
    modalBuf.resize ((size_t) fem::maxModes);
    const int n = juce::jmin (numFem,
                              processor.copyModalField (selectedQuantity(),
                                                        modalBuf.data(),
                                                        fem::maxModes));

    fieldBuf.assign ((size_t) numVerts, 0.0f);
    for (int k = 0; k < n; ++k)
    {
        const float q = modalBuf[(size_t) k];
        if (std::abs (q) < 1.0e-20f)
            continue;
        const auto& shape = model->modes.shapes[(size_t) k];
        for (int v = 0; v < numVerts; ++v)
            fieldBuf[(size_t) v] += q * shape[(size_t) v];
    }

    float peak = 0.0f;
    for (float v : fieldBuf)
        peak = juce::jmax (peak, std::abs (v));

    // Below this the plate is at rest (a velocity-1 hit at Force 1 peaks
    // around 0.065 in these units, and 0.084 for the velocity field — close
    // enough for the two to share a scale): show the bare grid rather than a
    // magnified numerical floor.
    if (peak < 1.0e-6f)
    {
        fieldRef = 0.0f;
        plateView.setField ({});
        return;
    }

    // Amplitude reference, held across frames: it follows a rising field at
    // once and falls at about 1.5 dB/s, so a hard hit never clips for long
    // and the ring's decay is visible as the colours cool. The floor is what
    // makes the fade actually reach the background: plate rings are long
    // (seconds to a minute at light damping), far slower than any release
    // that still tracks a fresh hit, so without it the reference would stay
    // glued to the signal and the plate would look eternally full-scale.
    // 0.02 is about a third of that reference hit, so a normal strike starts
    // above it and everything quieter than it fades out proportionally.
    fieldRef = juce::jmax (juce::jmax (peak, 0.02f), fieldRef * 0.995f);
    if (showing3D())
    {
        plateView3D.setFieldScale (fieldRef);
        plateView3D.setField (fieldBuf);
    }
    else
    {
        plateView.setFieldScale (fieldRef);
        plateView.setField (fieldBuf);
    }
}

void ModalDishAudioProcessorEditor::refreshStatus()
{
    juce::String text;
    const auto mesh = processor.getDisplayMesh();
    const auto* model = processor.getCurrentModel();

    if (processor.isComputing())
        text = "computing modes...";
    else if (mesh == nullptr)
        text = "no grid - define a shape, then Grid / Compute";
    else
    {
        text << mesh->numVertices() << " nodes, " << mesh->numTriangles() << " elements";

        // Nothing in the solver is quadratic in the free DOFs (nodes + edges)
        // any more, so this no longer warns of anything alarming — it is a
        // size readout for the upper Grid settings, where a solve starts
        // taking long enough to be worth knowing about in advance. Time, not
        // memory, is what the top of the range costs now.
        const int n = mesh->numVertices() + mesh->numEdges();
        const double solverMb = fem::solverBytesEstimate (n) / 1048576.0;
        if (solverMb >= 48.0)
            text << " - solver " << (solverMb >= 1024.0
                                        ? juce::String (solverMb / 1024.0, 1) + " GB"
                                        : juce::String (juce::roundToInt (solverMb)) + " MB");
        if (model != nullptr)
        {
            text << " - " << model->numModes() << " modes";
            const int k = showingLiveField() ? 0 : (int) modeViewKnob.getValue();
            if (showingLiveField())
            {
                text << (selectedQuantity() == fem::PlateSynth::Field::velocity
                            ? " - showing plate velocity"
                            : " - showing plate displacement");
                if (showing3D())
                    text << " in 3D (drag to rotate, wheel to zoom)";
            }
            else if (k >= 1 && k <= model->numModes())
            {
                // f_k = f1 * sqrt(omega_k^2 / omega_1^2) at the current tension.
                const double dT = (double) processor.apvts.getRawParameterValue (fem::id::tension)->load()
                                  - model->modes.tensionRef;
                const auto& l = model->modes.lambda;
                const auto& g = model->modes.tensionG;
                const double w2k = juce::jmax (0.0, l[(size_t) (k - 1)] + dT * g[(size_t) (k - 1)]);
                const double w21 = juce::jmax (1e-12, l[0] + dT * g[0]);
                const double fk = processor.apvts.getRawParameterValue (fem::id::f1)->load()
                                  * std::sqrt (w2k / w21);
                text << " - mode " << k << ": " << juce::String (fk, 1) << " Hz";
                if (k > model->numFemModes())
                    text << " (statistical tail)";
            }
        }
        else
            text << " - modes not computed";
    }
    statusLabel.setText (text, juce::dontSendNotification);
}

void ModalDishAudioProcessorEditor::setOutputPosition (double x, double y)
{
    auto setParam = [this] (const char* id, float value)
    {
        if (auto* param = processor.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    setParam (fem::id::pickupX[0], (float) juce::jlimit (0.0, 1.0, x));
    setParam (fem::id::pickupY[0], (float) juce::jlimit (0.0, 1.0, y));
    plateView.repaint();
}

juce::String ModalDishAudioProcessorEditor::PlateMarker::label() const
{
    if (isPickup)
        return juce::String (index + 1);

    const auto c = (juce::juce_wchar) fem::sourceLabel (index);
    return juce::String::charToString (velocityEnd ? juce::CharacterFunctions::toUpperCase (c)
                                                   : c);
}

juce::Colour ModalDishAudioProcessorEditor::PlateMarker::colour() const
{
    return isPickup ? fem::theme::pickupAccent : fem::theme::sourceAccent;
}

bool ModalDishAudioProcessorEditor::coincident (float x1, float y1, float x2, float y2)
{
    // The position parameters step in thousandths, so anything under a couple
    // of steps apart is the same point as far as the player is concerned.
    constexpr float eps = 2.0e-3f;
    return std::abs (x1 - x2) < eps && std::abs (y1 - y2) < eps;
}

std::vector<ModalDishAudioProcessorEditor::PlateMarker>
ModalDishAudioProcessorEditor::plateMarkers() const
{
    const auto value = [this] (const char* id)
    {
        return processor.apvts.getRawParameterValue (id)->load();
    };

    std::vector<PlateMarker> out;
    out.reserve ((size_t) (fem::maxPickups + fem::maxSources));

    for (int i = 0; i < fem::maxPickups; ++i)
        out.push_back ({ true, i, value (fem::id::pickupX[i]), value (fem::id::pickupY[i]),
                         value (fem::id::pickupOn[i]) > 0.5f });
    for (int i = 0; i < fem::maxSources; ++i)
    {
        const bool on = value (fem::id::sourceOn[i]) > 0.5f;
        const float x = value (fem::id::sourceX[i]), y = value (fem::id::sourceY[i]);
        const float x2 = value (fem::id::sourceX2[i]), y2 = value (fem::id::sourceY2[i]);

        // Base first, so that when the two coincide a tie in markerAt goes to
        // it — and dragging the base carries the endpoint along (see
        // setMarkerPosition), which is what keeps a source a point until it
        // is deliberately pulled into a segment.
        out.push_back ({ false, i, x, y, on, false });

        // The endpoint is only a marker of its own once it is somewhere else:
        // stacked on the base it would be an invisible thing to catch drags.
        if (! coincident (x, y, x2, y2))
            out.push_back ({ false, i, x2, y2, on, true });
    }
    return out;
}

int ModalDishAudioProcessorEditor::markerAt (const std::vector<PlateMarker>& markers,
                                            juce::Point<float> screenPos) const
{
    // Nearest within the radius rather than the first one inside it: markers
    // can be dropped on top of each other, and the one whose centre is
    // closest is the one being pointed at. A disabled marker carries a small
    // penalty so that it loses to an enabled one nearly underneath it — it is
    // still reachable, just not in the way.
    constexpr float radius = 11.0f;
    constexpr float disabledPenalty = 3.0f;

    int best = -1;
    float bestDist = radius;

    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto p = plateView.plateToScreen (markers[i].x, markers[i].y);
        const float d = p.getDistanceFrom (screenPos)
                        + (markers[i].on ? 0.0f : disabledPenalty);
        if (d < bestDist)
        {
            best = (int) i;
            bestDist = d;
        }
    }
    return best;
}

void ModalDishAudioProcessorEditor::setMarkerPosition (const PlateMarker& m, double x, double y)
{
    const auto setParam = [this] (const char* id, float value)
    {
        if (auto* param = processor.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    const float nx = (float) juce::jlimit (0.0, 1.0, x);
    const float ny = (float) juce::jlimit (0.0, 1.0, y);

    if (m.isPickup)
    {
        setParam (fem::id::pickupX[m.index], nx);
        setParam (fem::id::pickupY[m.index], ny);
    }
    else if (m.velocityEnd)
    {
        setParam (fem::id::sourceX2[m.index], nx);
        setParam (fem::id::sourceY2[m.index], ny);
    }
    else
    {
        // Moving the base carries a coincident endpoint with it, so that
        // dragging a source that is still a point moves the whole thing
        // instead of quietly leaving 'A' behind and inventing a segment the
        // player never drew. Once they are apart they move independently.
        const auto value = [this] (const char* id)
        { return processor.apvts.getRawParameterValue (id)->load(); };

        const bool wasOnePoint = coincident (value (fem::id::sourceX[m.index]),
                                             value (fem::id::sourceY[m.index]),
                                             value (fem::id::sourceX2[m.index]),
                                             value (fem::id::sourceY2[m.index]));
        setParam (fem::id::sourceX[m.index], nx);
        setParam (fem::id::sourceY[m.index], ny);
        if (wasOnePoint)
        {
            setParam (fem::id::sourceX2[m.index], nx);
            setParam (fem::id::sourceY2[m.index], ny);
        }
    }
    plateView.repaint();
}

void ModalDishAudioProcessorEditor::setMarkerOn (const PlateMarker& m, bool shouldBeOn)
{
    const char* id = m.isPickup ? fem::id::pickupOn[m.index] : fem::id::sourceOn[m.index];
    if (auto* param = processor.apvts.getParameter (id))
        param->setValueNotifyingHost (shouldBeOn ? 1.0f : 0.0f);
    plateView.repaint();
}

void ModalDishAudioProcessorEditor::showMarkerPanel (const PlateMarker& m)
{
    auto panel = std::make_unique<PlatePointPanel> (processor, m.isPickup, m.index);
    const auto centre = plateView.plateToScreen (m.x, m.y);
    const auto area = juce::Rectangle<int> (0, 0, 1, 1)
                        .withCentre (getLocalPoint (&plateView, centre).roundToInt());

    // Parented to the editor rather than the desktop, so the callout inherits
    // FxmeLookAndFeel; a desktop callout would be drawn in stock JUCE style.
    juce::CallOutBox::launchAsynchronously (std::move (panel), area, this);
}

bool ModalDishAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // 1..4 place a pickup, a..h a source, at whatever the mouse is over.
    // Placing something also switches it on: the gesture that turns a marker
    // off is alt-click, so this is the one that has to turn it back on, and
    // positioning an invisible point would otherwise do nothing at all.
    if (! plateView.isVisible())
        return false;

    const auto local = plateView.getMouseXYRelative();
    if (! plateView.getLocalBounds().contains (local))
        return false;

    // A capital letter places the source's velocity endpoint instead of its
    // base — the gesture that pulls a source out into an 'a'..'A' segment,
    // and the only one that can, since the two start on top of each other.
    const auto typed = key.getTextCharacter();
    const auto ch = juce::CharacterFunctions::toLowerCase (typed);
    const bool capital = typed != ch;

    PlateMarker m;
    if (ch >= '1' && ch < (juce::juce_wchar) ('1' + fem::maxPickups))
        m = { true, (int) (ch - '1'), 0.0f, 0.0f, false, false };
    else if (ch >= 'a' && ch < (juce::juce_wchar) ('a' + fem::maxSources))
        m = { false, (int) (ch - 'a'), 0.0f, 0.0f, false, capital };
    else
        return false;

    const auto p = plateView.screenToPlate (local.toFloat());
    setMarkerPosition (m, p.x, p.y);
    setMarkerOn (m, true);
    return true;
}

void ModalDishAudioProcessorEditor::drawPlateOverlay (juce::Graphics& g,
                                                     fxme::acoustics::FemViewComponent& v)
{
    const auto mesh = v.mesh();
    if (mesh == nullptr)
        return;

    // Boundary edges coloured by their segment's condition.
    const auto& starts = processor.shape().segStarts;
    const auto& bcs = processor.shape().segBcs;
    if (! starts.empty() && starts.size() == bcs.size())
    {
        auto bcAt = [&] (double t) -> int
        {
            size_t seg = starts.size() - 1;
            for (size_t i = 0; i < starts.size(); ++i)
            {
                if (starts[i] > t)
                    break;
                seg = i;
            }
            if (t < starts[0])
                seg = starts.size() - 1;
            return bcs[seg];
        };

        for (int e = 0; e < mesh->numEdges(); ++e)
        {
            if (! mesh->isBoundaryEdge (e) || mesh->edgeParam[(size_t) e] < 0.0)
                continue;
            const auto& ed = mesh->edges[(size_t) e];
            const auto a = v.plateToScreen (mesh->vertices[(size_t) ed.v0].x,
                                            mesh->vertices[(size_t) ed.v0].y);
            const auto b = v.plateToScreen (mesh->vertices[(size_t) ed.v1].x,
                                            mesh->vertices[(size_t) ed.v1].y);
            g.setColour (fem::theme::bcColour (bcAt (mesh->edgeParam[(size_t) e])));
            g.drawLine (a.x, a.y, b.x, b.y, 2.5f);
        }
    }

    // Pickup and source markers. An enabled one is filled and labelled; a
    // disabled one is a faint ring, still visible so it can be found and
    // switched back on, but clearly not taking part.
    g.setFont (juce::Font (juce::FontOptions (11.0f)).boldened());

    // The velocity segments first, so the markers sit on top of their line
    // rather than under it. A source that is still a point has no segment
    // (plateMarkers only emits its endpoint once the two are apart), so this
    // draws nothing at all in the default case.
    for (int i = 0; i < fem::maxSources; ++i)
    {
        const auto value = [this] (const char* id)
        { return processor.apvts.getRawParameterValue (id)->load(); };

        const float x = value (fem::id::sourceX[i]),  y = value (fem::id::sourceY[i]);
        const float x2 = value (fem::id::sourceX2[i]), y2 = value (fem::id::sourceY2[i]);
        if (coincident (x, y, x2, y2))
            continue;

        const auto a = v.plateToScreen (x, y);
        const auto b = v.plateToScreen (x2, y2);
        const bool on = value (fem::id::sourceOn[i]) > 0.5f;
        g.setColour (fem::theme::sourceAccent.withAlpha (on ? 0.55f : 0.22f));
        g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
    }

    for (const auto& m : plateMarkers())
    {
        const auto p = v.plateToScreen (m.x, m.y);
        const auto c = m.colour();
        const float r = m.isPickup ? 7.0f : 6.0f;

        if (m.on)
        {
            g.setColour (c.withAlpha (0.30f));
            g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
            g.setColour (c);
            g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 2.0f);
        }
        else
        {
            g.setColour (c.withAlpha (0.30f));
            g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 1.0f);
        }

        g.setColour (m.on ? juce::Colours::white : c.withAlpha (0.45f));
        g.drawText (m.label(), juce::Rectangle<float> (p.x - 10.0f, p.y - 8.0f, 20.0f, 16.0f),
                    juce::Justification::centred);
    }

    // Fading rings on the points that were struck, sized by how hard.
    //
    // The square root is there because the amplitude range is wide — Force
    // spans 0 to 20 and velocity scales it further — and a linear mapping
    // would make everything below the hardest hit indistinguishable. It also
    // has the reading that the ring's area, not its radius, follows the hit.
    // The bounds keep the softest hit visible and the hardest from swallowing
    // the plate; only a full-velocity hit above Force ~7 reaches the top one.
    // Amplitude 1 — full velocity at the default Force — maps to exactly 1, so
    // the default patch's marker looks as it always did and the scaling only
    // shows up once hits differ from one another.
    const auto now = juce::Time::getMillisecondCounter();
    for (const auto& f : hitFlashes)
    {
        const auto elapsed = now - f.startMs;
        if (elapsed >= hitFlashMs)
            continue;

        const float a = 1.0f - (float) elapsed / (float) hitFlashMs;
        const float scale = juce::jlimit (0.35f, 2.6f, std::sqrt (juce::jmax (0.0f, f.amplitude)));
        const auto p = v.plateToScreen (f.x, f.y);

        g.setColour (juce::Colours::white.withAlpha (a * juce::jlimit (0.4f, 1.0f, scale)));
        const float r = (5.0f + 14.0f * (1.0f - a)) * scale;
        g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 2.0f);
    }
}

//==============================================================================
void ModalDishAudioProcessorEditor::timerCallback()
{
    // Everything that changes the grid mid-gesture is remeshed here rather
    // than where it happens: a dragged vertex, a rotation, a swept Grid knob.
    // One remesh runs from 0.4 ms at Grid 16 to 7.6 ms at Grid 48, and both
    // the mouse and a knob drag deliver values faster than the screen can
    // show them, so rebuilding per event would spend most of a fast gesture
    // on grids nobody sees. Polled at 30 Hz it is at most one remesh a frame,
    // and the grid follows the shape while it is being dragged.
    //
    // Both flags are read before the test, never inside it: a short-circuited
    // || would leave one of them set and the rebuild owing.
    const bool outlineMoved = canvas.takeGeometryDirty();
    const bool densityMoved = meshDirty;
    meshDirty = false;
    if (designMode && (outlineMoved || densityMoved))
        syncShapeToProcessor (true);

    // Glide only means something once a channel is tuning the plate: with
    // Freq Chan Off nothing ever calls glideToNote, so the time it would take
    // is not a setting the player can hear. Polled here rather than driven
    // from the knob, so host automation of Freq Chan greys it too.
    glideKnob.setEnabled (processor.apvts.getRawParameterValue (fem::id::freqChannel)->load() > 0.5f);

    const bool computing = processor.isComputing();
    computeButton.setEnabled (! computing);
    progressBar.setVisible (computing);
    if (computing)
    {
        progressValue = (double) juce::jmax (0.0f, processor.getComputeProgress());
        refreshStatus();
    }
    if (showingLiveField() && activePlateView().isVisible())
        refreshLiveField();

    for (int ch = 0; ch < 2; ++ch)
        outMeter[ch].setValue (processor.getOutputPeakDb (ch));

    // A MIDI learn capture is applied here rather than on the audio thread,
    // which must not touch a parameter. Consumed with exchange so a note is
    // applied once even if the timer and the audio thread race.
    const int learned = processor.midiLearnNote.exchange (-1, std::memory_order_acq_rel);
    if (learned >= 0)
    {
        const int src = processor.midiLearnSource.load (std::memory_order_relaxed);
        if (juce::isPositiveAndBelow (src, fem::maxSources))
            if (auto* param = processor.apvts.getParameter (fem::id::sourceNote[src]))
                param->setValueNotifyingHost (param->convertTo0to1 ((float) learned));
    }

    // Drain the synth's hit ring into flashes. Every hit comes through here,
    // whether it started as a click, a MIDI note or several sources firing on
    // one note, so the editor never has to guess a position.
    const auto now = juce::Time::getMillisecondCounter();
    const int hits = processor.getHitCount();
    if (hits != lastHitSeen)
    {
        // A reader further behind than the ring is deep has lost the oldest
        // hits; take what is still there rather than reading wrapped slots.
        for (int i = juce::jmax (lastHitSeen, hits - fem::PlateSynth::hitRingSize);
             i < hits; ++i)
        {
            const auto p = processor.getHit (i);
            hitFlashes.push_back ({ p.x, p.y, p.amplitude, now });
        }
        lastHitSeen = hits;
    }

    if (! hitFlashes.empty())
    {
        const auto expired = [now] (const HitFlash& f)
        {
            return now - f.startMs >= hitFlashMs;
        };
        hitFlashes.erase (std::remove_if (hitFlashes.begin(), hitFlashes.end(), expired),
                          hitFlashes.end());
        if (activePlateView().isVisible())
            activePlateView().repaint();
    }
}

void ModalDishAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshMeshAndField();
    refreshStatus();
    // A finished computation switches to Perform, ready to play.
    if (processor.getCurrentModel() != nullptr && ! processor.isComputing())
        setDesignMode (false);
}

//==============================================================================
void ModalDishAudioProcessorEditor::paint (juce::Graphics& g)
{
    fem::theme::paintBackground (g, getLocalBounds().toFloat());

    // reserveRight leaves room for a button sharing the title row (Advanced,
    // in Dynamics). Fitted rather than plain text: "FREQUENCY CONTROL" is
    // within a few pixels of the column width, and a title that silently
    // loses its last letters is worse than one a point smaller.
    auto drawPanel = [&g] (juce::Rectangle<int> r, const juce::String& title,
                           juce::Colour accent, int reserveRight = 0)
    {
        if (r.isEmpty())
            return;
        g.setColour (fem::theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (fem::theme::panelLine);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        g.setColour (accent);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawFittedText (title,
                          r.removeFromTop (22).reduced (10, 2).withTrimmedRight (reserveRight),
                          juce::Justification::centredLeft, 1);
    };

    drawPanel (designPanel, "MODAL DESIGN", fem::theme::geomAccent);
    drawPanel (dynPanel, "DYNAMICS", fem::theme::dynGroup, 42);
    drawPanel (cascPanel, "CASCADE", fem::theme::dynGroup);
    drawPanel (freqPanel, "FREQUENCY CONTROL", fem::theme::freqGroup);
    drawPanel (excitePanel, "HAMMER CONTROL", fem::theme::hammerGroup);
    drawPanel (ioPanel, "IO", fem::theme::ioGroup);
    drawPanel (pointsPanel, "TRANSDUCERS", fem::theme::transGroup);

    // Scale under the output meters, so a reading means something.
    if (! meterArea.isEmpty())
    {
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (fem::theme::dimText);
        for (const int db : { -60, -36, -24, -12, 0 })
        {
            const float t = juce::jmap ((float) db, -60.0f, 6.0f, 0.0f, 1.0f);
            const int x = meterArea.getX() + juce::roundToInt (t * meterArea.getWidth());
            g.drawText (juce::String (db),
                        juce::Rectangle<int> (x - 13, meterArea.getBottom() - 11, 26, 10),
                        juce::Justification::centred);
        }
    }

}

void ModalDishAudioProcessorEditor::resized()
{
    // Two columns of number boxes throughout. A box is legible at a third of
    // the height a knob needs for the same information, which is what makes
    // pairing them up worthwhile: every panel is shorter *and* wider-spaced
    // than the single column of knobs it replaces.
    constexpr int boxH = 38, gap = 6, titleH = 22, padding = 10;

    // Lays `controls` out two per row inside `r`, left to right, top to
    // bottom; a null entry leaves its cell empty.
    const auto twoColumns = [boxH, gap] (juce::Rectangle<int> r,
                                std::initializer_list<juce::Component*> controls)
    {
        const int colW = r.getWidth() / 2;
        int i = 0;
        juce::Rectangle<int> row;
        for (auto* c : controls)
        {
            if (i % 2 == 0)
            {
                if (i > 0)
                    r.removeFromTop (gap);
                row = r.removeFromTop (boxH);
            }
            auto cell = (i % 2 == 0) ? row.removeFromLeft (colW) : row;
            if (c != nullptr)
                c->setBounds (cell.reduced (2, 0));
            ++i;
        }
    };

    auto area = getLocalBounds();
    topBar.setBounds (area.removeFromTop (54));

    // Straddles the header's bottom edge so the glow bleeds into both.
    glowLine.setBounds (0, topBar.getBottom() - GlowLine::kHeight / 2,
                        getWidth(), GlowLine::kHeight);

    splash.setBounds (getLocalBounds());
    presetOverlay.setBounds (area);

    area.reduce (padding, padding);

    // Column B is the cascade tuning: one narrow column, so a single stack of
    // boxes fills it rather than being stretched across a wide panel.
    juce::Rectangle<int> colB;
    if (advancedVisible && ! designMode)
    {
        colB = area.removeFromRight (110);
        area.removeFromRight (10);
    }
    auto colA = area.removeFromRight (150);
    area.removeFromRight (10);

    designPanel = dynPanel = freqPanel = excitePanel = ioPanel = cascPanel = {};
    pointsPanel = {};
    meterArea = {};

    if (designMode)
    {
        // Tool selector across the full width, then the knobs that shape what
        // it draws, then the file row. Load and Save are not tools and not
        // shape parameters — they move a whole geometry in or out — so they
        // sit under both rather than competing with the selector for a row.
        const int fileH = 24;
        designPanel = colA.removeFromTop (2 * padding + titleH + 26 + gap
                                          + 2 * boxH + 2 * gap + fileH);
        auto r = designPanel.reduced (padding);
        r.removeFromTop (titleH);

        toolBox.setBounds (r.removeFromTop (26));
        r.removeFromTop (gap);

        twoColumns (r, { &aspectKnob, &pointsKnob, &densityKnob, &modesKnob });
        r.removeFromTop (2 * boxH + 2 * gap);

        auto fileRow = r.removeFromTop (fileH);
        const int half = (fileRow.getWidth() - gap) / 2;
        loadShapeButton.setBounds (fileRow.removeFromLeft (half));
        saveShapeButton.setBounds (fileRow.removeFromRight (half));
    }
    else
    {
        // Group 1: Dynamics. Advanced sits in the title row rather than in a
        // cell of its own: it opens a whole column, which is a different kind
        // of act from turning a knob, and a cell would have said otherwise.
        dynPanel = colA.removeFromTop (2 * padding + titleH + 3 * boxH + 2 * gap);
        {
            auto r = dynPanel.reduced (padding);
            auto header = r.removeFromTop (titleH);
            advancedButton.setBounds (header.removeFromRight (38).reduced (0, 2));
            twoColumns (r, { &tensionKnob,  &nonlinKnob,
                             &viscKnob,     &matKnob,
                             &cascadeKnob,  &deplKnob });
        }

        // Group 2: Frequency control.
        colA.removeFromTop (10);
        const int toggleH = 26;
        freqPanel = colA.removeFromTop (2 * padding + titleH + 2 * boxH + 2 * gap + toggleH);
        {
            auto r = freqPanel.reduced (padding);
            r.removeFromTop (titleH);
            twoColumns (r, { &f1Knob,      &glideKnob,
                             &srcChanKnob, &freqChanKnob });
            r.removeFromTop (2 * boxH + 2 * gap);
            unmappedHitButton->setBounds (r.removeFromTop (toggleH).reduced (2, 0));
        }

        // Group 3: Hammer control.
        colA.removeFromTop (10);
        // Two rows: the global hammer, then the trims that scale what the
        // eight sources resolved for themselves. Same column each time, so a
        // duration sits over a duration and a force over a force.
        excitePanel = colA.removeFromTop (2 * padding + titleH + 2 * boxH + gap);
        {
            auto r = excitePanel.reduced (padding);
            r.removeFromTop (titleH);
            twoColumns (r, { &hammerKnob,      &forceKnob,
                             &srcDurScaleKnob, &srcForceScaleKnob });
        }

        // Group 4: IO.
        colA.removeFromTop (10);
        const int meterH = 14, scaleH = 12;
        ioPanel = colA.removeFromTop (2 * padding + titleH + boxH + gap
                                      + 2 * meterH + 4 + scaleH);
        {
            auto r = ioPanel.reduced (padding);
            r.removeFromTop (titleH);
            twoColumns (r, { &inGainKnob, &outGainKnob });
            r.removeFromTop (boxH + gap);

            // Horizontal bars across the panel's full width, with the dB
            // scale drawn under them in paint().
            meterArea = r;
            outMeter[0].setBounds (r.removeFromTop (meterH).reduced (2, 1));
            r.removeFromTop (4);
            outMeter[1].setBounds (r.removeFromTop (meterH).reduced (2, 1));
        }

        // Group 5: Transducers. Every point on the plate as a switch, drawn
        // as the marker it controls (see PointToggle).
        colA.removeFromTop (10);
        const int rowH = 22;
        pointsPanel = colA.removeFromTop (2 * padding + titleH + 2 * rowH + gap);
        {
            auto r = pointsPanel.reduced (padding);
            r.removeFromTop (titleH);

            // Cut at exact fractions of the row rather than by a fixed button
            // width, so eight switches always fill it with no leftover pixels
            // collecting at one end.
            const auto layoutRow = [] (juce::Rectangle<int> row,
                                       std::unique_ptr<PointToggle>* buttons, int count)
            {
                for (int i = 0; i < count; ++i)
                {
                    const int x0 = row.getX() + row.getWidth() * i / count;
                    const int x1 = row.getX() + row.getWidth() * (i + 1) / count;
                    buttons[i]->setBounds (x0, row.getY(), x1 - x0, row.getHeight());
                }
            };
            layoutRow (r.removeFromTop (rowH), pickupToggle, fem::maxPickups);
            r.removeFromTop (gap);
            layoutRow (r.removeFromTop (rowH), sourceToggle, fem::maxSources);
        }
    }

    if (advancedVisible && ! designMode)
    {
        // Five boxes, then the injection switch under them: it is the odd
        // one out in this column, so it sits apart from the numbers rather
        // than in the middle of them.
        const int injectH = 26;
        cascPanel = colB.removeFromTop (2 * padding + titleH + 5 * boxH + 5 * gap + injectH);
        auto r = cascPanel.reduced (padding);
        r.removeFromTop (titleH);
        for (auto* k : { &cascDriveKnob, &cascWinKnob, &cascAttKnob,
                         &cascRelKnob, &cascOverKnob })
        {
            k->setBounds (r.removeFromTop (boxH));
            r.removeFromTop (gap);
        }
        if (cascModalInjButton != nullptr)
            cascModalInjButton->setBounds (r.removeFromTop (injectH).reduced (2, 0));
    }

    // Left: action strip at the bottom, views above. In design mode the
    // boundary-condition legend sits directly under the sketch it explains,
    // rather than across the panel on the far side of the window.
    auto strip = area.removeFromBottom (64);
    strip.removeFromTop (8);
    designButton.setBounds (strip.removeFromLeft (96).reduced (0, 14));
    strip.removeFromLeft (4);
    performButton.setBounds (strip.removeFromLeft (72).reduced (0, 14));
    strip.removeFromLeft (12);
    computeButton.setBounds (strip.removeFromLeft (76).reduced (0, 14));
    strip.removeFromLeft (10);
    if (! designMode)
    {
        viewBox.setBounds (strip.removeFromLeft (116).reduced (0, 18));
        strip.removeFromLeft (6);
        modeViewKnob.setBounds (strip.removeFromLeft (64).reduced (0, 12));
        strip.removeFromLeft (10);
    }
    progressBar.setBounds (strip.removeFromLeft (110).reduced (0, 20));
    strip.removeFromLeft (8);
    statusLabel.setBounds (strip);

    // The boundary-condition key is drawn by the sketch itself, in its own
    // bottom-right corner (ShapeCanvas::paint) — a child component paints
    // after its parent, so anything the editor drew there would be covered.

    canvas.setBounds (area);
    plateView.setBounds (area);
    plateView3D.setBounds (area);

    // The two generators go directly above the key, in the corner of the
    // sketch: they belong with the drawing rather than with the panel, and
    // that corner is the one part of the canvas a centred shape never reaches.
    // Laid out from the key's own bounds so the two cannot drift apart.
    if (designMode)
    {
        constexpr int bw = 76, bh = 24, bgap = 6;
        const auto key = canvas.legendBounds().translated (canvas.getX(), canvas.getY());
        auto row = juce::Rectangle<int> (key.getRight() - (2 * bw + bgap),
                                         key.getY() - bh - bgap,
                                         2 * bw + bgap, bh);
        ellipseButton.setBounds (row.removeFromLeft (bw));
        rectangleButton.setBounds (row.removeFromRight (bw));
    }
}


