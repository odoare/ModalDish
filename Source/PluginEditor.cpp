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
    addAndMakeVisible (topBar);
    topBar.setBackgroundColour (fem::theme::topBarBg);
    topBar.setAccentColour (fem::theme::accent);

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
    densityKnob.setRange (8, 32, 1);
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

    setSize (1290, 730);
}

FemPlateAudioProcessorEditor::~FemPlateAudioProcessorEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
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

void FemPlateAudioProcessorEditor::refreshField()
{
    const auto* model = processor.getCurrentModel();
    const int k = (int) modeViewKnob.getValue();
    // Only FEM modes have a mesh shape; tail modes show the bare grid.
    if (model != nullptr && k >= 1 && k <= model->numFemModes())
        plateView.setField (model->modes.shapes[(size_t) (k - 1)]);
    else
        plateView.setField ({});
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
        if (model != nullptr)
        {
            text << " - " << model->numModes() << " modes";
            const int k = (int) modeViewKnob.getValue();
            if (k >= 1 && k <= model->numModes())
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

    // Two right-hand columns: A = geometry + modes, B = cascade + output.
    auto colB = area.removeFromRight (270);
    area.removeFromRight (10);
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

        auto row1 = r.removeFromTop (rowH);          // excitation
        f1Knob.setBounds (row1.removeFromLeft (w).reduced (2));
        hammerKnob.setBounds (row1.removeFromLeft (w).reduced (2));
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

    cascPanel = colB.removeFromTop (330);
    {
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
    colB.removeFromTop (10);

    ioPanel = colB.removeFromTop (130);
    {
        auto r = ioPanel.reduced (10);
        r.removeFromTop (22);
        const int w = r.getWidth() / 4;
        outXKnob.setBounds (r.removeFromLeft (w).reduced (2));
        outYKnob.setBounds (r.removeFromLeft (w).reduced (2));
        inGainKnob.setBounds (r.removeFromLeft (w).reduced (2));
        outGainKnob.setBounds (r.reduced (2));
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
    modeViewKnob.setBounds (strip.removeFromLeft (64));
    strip.removeFromLeft (10);
    progressBar.setBounds (strip.removeFromLeft (110).reduced (0, 20));
    strip.removeFromLeft (8);
    statusLabel.setBounds (strip);

    canvas.setBounds (area);
    plateView.setBounds (area);
}
