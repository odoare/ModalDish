/*
  ------------------------------------------------------------------------------
    PluginEditor.cpp — see PluginEditor.h.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "PluginEditor.h"

//==============================================================================
FemPlateAudioProcessorEditor::FemPlateAudioProcessorEditor (FemPlateAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);
    lnf.setAccentColour (fem::theme::accent);   // tints the combo drop-downs
    addAndMakeVisible (topBar);
    topBar.setBackgroundColour (fem::theme::topBarBg);
    topBar.setAccentColour (fem::theme::accent);

    // Advanced toggle: shows the cascade tuning column, widening the window.
    fem::theme::styleButton (advancedButton, fem::theme::cascAccent);
    advancedButton.setClickingTogglesState (true);
    advancedButton.setMouseClickGrabsKeyboardFocus (false);
    advancedButton.onClick = [this] { setAdvancedVisible (advancedButton.getToggleState()); };
    topBar.setRightControls (nullptr, 0, &advancedButton, 86);

    // --- left: views ---------------------------------------------------------
    addAndMakeVisible (canvas);
    addChildComponent (plateView);   // hidden until a mesh exists

    canvas.bcColour = [] (int bc) { return fem::theme::bcColour (bc); };
    canvas.setShape (processor.shape().outline,
                     processor.shape().segStarts,
                     processor.shape().segBcs);
    canvas.onShapeChanged = [this] { syncShapeToProcessor (true); };
    canvas.onBoundaryChanged = [this] { syncShapeToProcessor (false); };

    plateView.setColours (fem::theme::plateBg, fem::theme::plateGrid,
                          fem::theme::fieldNeg, fem::theme::fieldPos);
    plateView.onPlateClick = [this] (double x, double y, const juce::MouseEvent& e)
    {
        if (e.mods.isRightButtonDown() || e.mods.isCtrlDown())
            setOutputPosition (x, y);
        else
        {
            processor.requestStrike ((float) x, (float) y, 1.0f);
            lastStrikeMs = juce::Time::getMillisecondCounter();
            lastStrikePos = plateView.plateToScreen (x, y);
            plateView.repaint();
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
    for (auto* b : { &shapeViewButton, &plateViewButton, &gridButton, &computeButton })
    {
        fem::theme::styleButton (*b, fem::theme::accent);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    shapeViewButton.setClickingTogglesState (false);
    plateViewButton.setClickingTogglesState (false);
    shapeViewButton.onClick = [this] { showShapeView (true); };
    plateViewButton.onClick = [this] { showShapeView (false); };
    gridButton.onClick = [this]
    {
        if (processor.buildMesh())
            showShapeView (false);
    };
    computeButton.onClick = [this]
    {
        processor.computeModes();
    };

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
    viewBox.setSelectedId (1, juce::dontSendNotification);
    viewBox.onChange = [this]
    {
        // The Mode knob picks a shape to display; it means nothing while the
        // view is showing the plate's live motion.
        modeViewKnob.setEnabled (! showingLiveField());
        fieldRef = 0.0f;
        refreshField();
        refreshStatus();
    };
    addAndMakeVisible (viewBox);

    fem::theme::styleKnob (modeViewKnob, "Mode", fem::theme::accent);
    modeViewKnob.setRange (0, fem::maxModes, 1);
    modeViewKnob.setValue (0, juce::dontSendNotification);
    modeViewKnob.onValueChange = [this] { refreshField(); refreshStatus(); };
    addAndMakeVisible (modeViewKnob);

    // --- right: geometry panel -------------------------------------------------
    fem::theme::styleCombo (toolBox, fem::theme::geomAccent);
    toolBox.addItem ("Draw shape", 1);
    toolBox.addItem ("Ellipse", 2);
    toolBox.addItem ("Rectangle", 3);
    toolBox.addItem ("Rotate", 4);
    toolBox.addItem ("Edit boundary", 5);
    toolBox.setSelectedId (5, juce::dontSendNotification);
    toolBox.onChange = [this]
    {
        canvas.setTool ((fem::ShapeCanvas::Tool) (toolBox.getSelectedId() - 1));
        showShapeView (true);
    };
    addAndMakeVisible (toolBox);

    fem::theme::styleKnob (aspectKnob, "Aspect", fem::theme::geomAccent);
    aspectKnob.setRange (0.25, 4.0, 0.01);
    aspectKnob.setSkewFactorFromMidPoint (1.0);
    aspectKnob.setValue (1.2, juce::dontSendNotification);
    aspectKnob.onValueChange = [this] { canvas.setAspect (aspectKnob.getValue()); };
    addAndMakeVisible (aspectKnob);

    fem::theme::styleKnob (pointsKnob, "Points", fem::theme::geomAccent);
    pointsKnob.setRange (1, 12, 1);
    pointsKnob.setValue (canvas.numBorderPoints(), juce::dontSendNotification);
    pointsKnob.onValueChange = [this]
    {
        canvas.setNumBorderPoints ((int) pointsKnob.getValue());
    };
    addAndMakeVisible (pointsKnob);

    fem::theme::styleKnob (densityKnob, "Grid", fem::theme::geomAccent);
    densityKnob.setRange (8, 48, 1);
    densityKnob.setValue (processor.shape().meshDensity, juce::dontSendNotification);
    densityKnob.onValueChange = [this]
    {
        processor.shape().meshDensity = (int) densityKnob.getValue();
        processor.invalidateShape();
        processor.buildMesh();
    };
    addAndMakeVisible (densityKnob);

    // --- right: modal parameters -----------------------------------------------
    auto& apvts = processor.apvts;
    auto addModal = [&] (fxme::FxmeSlider& s, const char* text, const char* id,
                         std::unique_ptr<SliderAttachment>& att)
    {
        fem::theme::styleKnob (s, text, fem::theme::modesAccent);
        addAndMakeVisible (s);
        att = std::make_unique<SliderAttachment> (apvts, id, s);
    };
    addModal (f1Knob, "Freq", fem::id::f1, f1Att);
    addModal (glideKnob, "Glide", fem::id::glide, glideAtt);
    addModal (tensionKnob, "Tension", fem::id::tension, tensionAtt);
    addModal (hammerKnob, "Hammer", fem::id::hammerMs, hammerAtt);
    addModal (forceKnob, "Force", fem::id::force, forceAtt);
    addModal (nonlinKnob, "Nonlinear", fem::id::nonlin, nonlinAtt);
    addModal (cascadeKnob, "Cascade", fem::id::cascade, cascadeAtt);
    addModal (viscKnob, "Viscous", fem::id::viscDamp, viscAtt);
    addModal (matKnob, "Material", fem::id::matDamp, matAtt);
    addModal (modesKnob, "Modes", fem::id::numModes, modesAtt);

    // --- right: cascade tuning ---------------------------------------------------
    auto addCasc = [&] (fxme::FxmeSlider& s, const char* text, const char* id,
                        std::unique_ptr<SliderAttachment>& att)
    {
        fem::theme::styleKnob (s, text, fem::theme::cascAccent);
        addAndMakeVisible (s);
        att = std::make_unique<SliderAttachment> (apvts, id, s);
    };
    addCasc (cascAmpKnob, "Amp", fem::id::cascAmp, cascAmpAtt);
    addCasc (cascDriveKnob, "Drive", fem::id::cascDrive, cascDriveAtt);
    addCasc (cascWinKnob, "Window", fem::id::cascWindow, cascWinAtt);
    addCasc (cascAttKnob, "Attack", fem::id::cascAttack, cascAttAtt);
    addCasc (cascRelKnob, "Release", fem::id::cascRelease, cascRelAtt);
    addCasc (cascOverKnob, "Overlap", fem::id::cascOverlap, cascOverAtt);
    addCasc (cascDeplKnob, "Deplete", fem::id::cascDeplete, cascDeplAtt);

    // --- right: I/O --------------------------------------------------------------
    auto addIo = [&] (fxme::FxmeSlider& s, const char* text, const char* id,
                      std::unique_ptr<SliderAttachment>& att)
    {
        fem::theme::styleKnob (s, text, fem::theme::ioAccent);
        addAndMakeVisible (s);
        att = std::make_unique<SliderAttachment> (apvts, id, s);
    };
    addIo (outXKnob, "Out X", fem::id::outX, outXAtt);
    addIo (outYKnob, "Out Y", fem::id::outY, outYAtt);
    addIo (inGainKnob, "In", fem::id::inGain, inGainAtt);
    addIo (outGainKnob, "Out", fem::id::outGain, outGainAtt);

    processor.addChangeListener (this);
    refreshMeshAndField();
    refreshStatus();
    // Reopening on an already-computed plate lands on the playable view.
    showShapeView (processor.getCurrentModel() == nullptr);
    startTimerHz (30);

    setAdvancedVisible (false);   // also sets the window size
}

FemPlateAudioProcessorEditor::~FemPlateAudioProcessorEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void FemPlateAudioProcessorEditor::setAdvancedVisible (bool shouldShow)
{
    advancedVisible = shouldShow;
    advancedButton.setToggleState (shouldShow, juce::dontSendNotification);
    for (auto* k : { &cascAmpKnob, &cascDriveKnob, &cascWinKnob,
                     &cascAttKnob, &cascRelKnob, &cascOverKnob, &cascDeplKnob })
        k->setVisible (shouldShow);
    setSize (shouldShow ? 1412 : 1142, 730);   // triggers resized()
    repaint();
}

void FemPlateAudioProcessorEditor::showShapeView (bool showShape)
{
    canvas.setVisible (showShape);
    plateView.setVisible (! showShape);
    shapeViewButton.setToggleState (showShape, juce::dontSendNotification);
    plateViewButton.setToggleState (! showShape, juce::dontSendNotification);
}

void FemPlateAudioProcessorEditor::syncShapeToProcessor (bool geometryChanged)
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

void FemPlateAudioProcessorEditor::refreshMeshAndField()
{
    plateView.setMesh (processor.getDisplayMesh());
    refreshField();
}

bool FemPlateAudioProcessorEditor::showingLiveField() const
{
    return viewBox.getSelectedId() >= 2;
}

fem::PlateSynth::Field FemPlateAudioProcessorEditor::selectedQuantity() const
{
    return viewBox.getSelectedId() == 3 ? fem::PlateSynth::Field::velocity
                                        : fem::PlateSynth::Field::displacement;
}

void FemPlateAudioProcessorEditor::refreshField()
{
    if (showingLiveField())
    {
        refreshLiveField();
        return;
    }

    // A mode shape is a still picture with no natural scale: let the view
    // normalise it on its own maximum.
    plateView.setFieldScale (0.0f);

    const auto* model = processor.getCurrentModel();
    const int k = (int) modeViewKnob.getValue();
    // Only FEM modes have a mesh shape; tail modes show the bare grid.
    if (model != nullptr && k >= 1 && k <= model->numFemModes())
        plateView.setField (model->modes.shapes[(size_t) (k - 1)]);
    else
        plateView.setField ({});
}

void FemPlateAudioProcessorEditor::refreshLiveField()
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
    plateView.setFieldScale (fieldRef);
    plateView.setField (fieldBuf);
}

void FemPlateAudioProcessorEditor::refreshStatus()
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
                text << (selectedQuantity() == fem::PlateSynth::Field::velocity
                            ? " - showing plate velocity"
                            : " - showing plate displacement");
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

void FemPlateAudioProcessorEditor::setOutputPosition (double x, double y)
{
    auto setParam = [this] (const char* id, float value)
    {
        if (auto* param = processor.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    setParam (fem::id::outX, (float) juce::jlimit (0.0, 1.0, x));
    setParam (fem::id::outY, (float) juce::jlimit (0.0, 1.0, y));
    plateView.repaint();
}

void FemPlateAudioProcessorEditor::drawPlateOverlay (juce::Graphics& g,
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

    // Output point marker.
    const float ox = processor.apvts.getRawParameterValue (fem::id::outX)->load();
    const float oy = processor.apvts.getRawParameterValue (fem::id::outY)->load();
    const auto op = v.plateToScreen (ox, oy);
    g.setColour (fem::theme::ioAccent);
    g.drawEllipse (op.x - 6.0f, op.y - 6.0f, 12.0f, 12.0f, 2.0f);
    g.drawLine (op.x - 9.0f, op.y, op.x + 9.0f, op.y, 1.0f);
    g.drawLine (op.x, op.y - 9.0f, op.x, op.y + 9.0f, 1.0f);

    // Fading marker on the last hit point.
    const auto elapsed = juce::Time::getMillisecondCounter() - lastStrikeMs;
    if (lastStrikeMs != 0 && elapsed < 700)
    {
        const float a = 1.0f - (float) elapsed / 700.0f;
        g.setColour (juce::Colours::white.withAlpha (a));
        const float r = 5.0f + 14.0f * (1.0f - a);
        g.drawEllipse (lastStrikePos.x - r, lastStrikePos.y - r, 2 * r, 2 * r, 2.0f);
    }
}

//==============================================================================
void FemPlateAudioProcessorEditor::timerCallback()
{
    const bool computing = processor.isComputing();
    computeButton.setEnabled (! computing);
    progressBar.setVisible (computing);
    if (computing)
    {
        progressValue = (double) juce::jmax (0.0f, processor.getComputeProgress());
        refreshStatus();
    }
    if (showingLiveField() && plateView.isVisible())
        refreshLiveField();

    // A MIDI note strikes at the last clicked point (or the plate centre if
    // it has never been clicked): mark it exactly like a click.
    const int midiStrikes = processor.getMidiStrikeCount();
    if (midiStrikes != lastMidiStrikeSeen)
    {
        if (lastStrikeMs == 0)
            lastStrikePos = plateView.plateToScreen (0.5, 0.5);
        lastMidiStrikeSeen = midiStrikes;
        lastStrikeMs = juce::Time::getMillisecondCounter();
    }

    if (lastStrikeMs != 0
         && juce::Time::getMillisecondCounter() - lastStrikeMs < 750
         && plateView.isVisible())
        plateView.repaint();
}

void FemPlateAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshMeshAndField();
    refreshStatus();
    // A finished computation switches to the plate view, ready to play.
    if (processor.getCurrentModel() != nullptr && ! processor.isComputing())
        showShapeView (false);
}

//==============================================================================
void FemPlateAudioProcessorEditor::paint (juce::Graphics& g)
{
    fem::theme::paintBackground (g, getLocalBounds().toFloat());

    auto drawPanel = [&g] (juce::Rectangle<int> r, const juce::String& title, juce::Colour accent)
    {
        if (r.isEmpty())
            return;
        g.setColour (fem::theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (fem::theme::panelLine);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        g.setColour (accent);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (title, r.removeFromTop (22).reduced (10, 2),
                    juce::Justification::centredLeft);
    };

    drawPanel (geomPanel, "GEOMETRY", fem::theme::geomAccent);
    drawPanel (modalPanel, "MODES", fem::theme::modesAccent);
    drawPanel (cascPanel, "CASCADE", fem::theme::cascAccent);
    drawPanel (ioPanel, "OUTPUT", fem::theme::ioAccent);

    // Boundary-condition legend.
    if (! legendArea.isEmpty())
    {
        auto r = legendArea;
        const int itemW = r.getWidth() / 4;
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        for (int bc = 0; bc < 4; ++bc)
        {
            auto cell = r.removeFromLeft (itemW);
            const auto swatch = cell.removeFromLeft (14).withSizeKeepingCentre (10, 10);
            g.setColour (fem::theme::bcColour (bc));
            g.fillRect (swatch);
            g.setColour (fem::theme::dimText);
            g.drawText (fem::theme::bcName (bc), cell.reduced (3, 0),
                        juce::Justification::centredLeft);
        }
    }
}

void FemPlateAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    topBar.setBounds (area.removeFromTop (54));
    area.reduce (10, 10);

    // Column A = geometry + modes + output; column B (Advanced only) =
    // cascade tuning.
    juce::Rectangle<int> colB;
    if (advancedVisible)
    {
        colB = area.removeFromRight (270);
        area.removeFromRight (10);
    }
    auto colA = area.removeFromRight (270);
    area.removeFromRight (10);

    geomPanel = colA.removeFromTop (196);
    {
        auto r = geomPanel.reduced (10);
        r.removeFromTop (22);
        toolBox.setBounds (r.removeFromTop (26));
        r.removeFromTop (6);
        legendArea = r.removeFromBottom (18);
        auto knobs = r;
        const int w = knobs.getWidth() / 3;
        aspectKnob.setBounds (knobs.removeFromLeft (w).reduced (2));
        pointsKnob.setBounds (knobs.removeFromLeft (w).reduced (2));
        densityKnob.setBounds (knobs.reduced (2));
    }
    colA.removeFromTop (10);

    modalPanel = colA.removeFromTop (310);
    {
        auto r = modalPanel.reduced (10);
        r.removeFromTop (22);
        const int rowH = r.getHeight() / 3;
        const int w = r.getWidth() / 3;

        // Four across on the excitation row (the I/O panel's width), three on
        // the others: Glide belongs next to the frequency it glides.
        auto row1 = r.removeFromTop (rowH);          // excitation
        const int w4 = r.getWidth() / 4;
        f1Knob.setBounds (row1.removeFromLeft (w4).reduced (2));
        glideKnob.setBounds (row1.removeFromLeft (w4).reduced (2));
        hammerKnob.setBounds (row1.removeFromLeft (w4).reduced (2));
        forceKnob.setBounds (row1.reduced (2));

        auto row2 = r.removeFromTop (rowH);          // tension / nonlinearity
        tensionKnob.setBounds (row2.removeFromLeft (w).reduced (2));
        nonlinKnob.setBounds (row2.removeFromLeft (w).reduced (2));
        cascadeKnob.setBounds (row2.reduced (2));

        auto row3 = r;                               // damping / bank size
        viscKnob.setBounds (row3.removeFromLeft (w).reduced (2));
        matKnob.setBounds (row3.removeFromLeft (w).reduced (2));
        modesKnob.setBounds (row3.reduced (2));
    }

    colA.removeFromTop (10);
    ioPanel = colA.removeFromTop (130);
    {
        auto r = ioPanel.reduced (10);
        r.removeFromTop (22);
        const int w = r.getWidth() / 4;
        outXKnob.setBounds (r.removeFromLeft (w).reduced (2));
        outYKnob.setBounds (r.removeFromLeft (w).reduced (2));
        inGainKnob.setBounds (r.removeFromLeft (w).reduced (2));
        outGainKnob.setBounds (r.reduced (2));
    }

    if (advancedVisible)
    {
        cascPanel = colB.removeFromTop (330);
        auto r = cascPanel.reduced (10);
        r.removeFromTop (22);
        const int rowH = r.getHeight() / 3;
        const int w = r.getWidth() / 3;

        auto row1 = r.removeFromTop (rowH);
        cascAmpKnob.setBounds (row1.removeFromLeft (w).reduced (2));
        cascDriveKnob.setBounds (row1.removeFromLeft (w).reduced (2));
        cascWinKnob.setBounds (row1.reduced (2));

        auto row2 = r.removeFromTop (rowH);
        cascAttKnob.setBounds (row2.removeFromLeft (w).reduced (2));
        cascRelKnob.setBounds (row2.removeFromLeft (w).reduced (2));
        cascOverKnob.setBounds (row2.reduced (2));

        auto row3 = r;
        cascDeplKnob.setBounds (row3.removeFromLeft (w).reduced (2));
    }
    else
    {
        cascPanel = {};
    }

    // Left: action strip at the bottom, views above.
    auto strip = area.removeFromBottom (64);
    strip.removeFromTop (8);
    shapeViewButton.setBounds (strip.removeFromLeft (58).reduced (0, 14));
    strip.removeFromLeft (4);
    plateViewButton.setBounds (strip.removeFromLeft (58).reduced (0, 14));
    strip.removeFromLeft (12);
    gridButton.setBounds (strip.removeFromLeft (52).reduced (0, 14));
    strip.removeFromLeft (4);
    computeButton.setBounds (strip.removeFromLeft (76).reduced (0, 14));
    strip.removeFromLeft (10);
    viewBox.setBounds (strip.removeFromLeft (116).reduced (0, 18));
    strip.removeFromLeft (6);
    modeViewKnob.setBounds (strip.removeFromLeft (64));
    strip.removeFromLeft (10);
    progressBar.setBounds (strip.removeFromLeft (110).reduced (0, 20));
    strip.removeFromLeft (8);
    statusLabel.setBounds (strip);

    canvas.setBounds (area);
    plateView.setBounds (area);
}
