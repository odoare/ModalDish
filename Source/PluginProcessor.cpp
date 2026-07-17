/*
  ------------------------------------------------------------------------------
    PluginProcessor.cpp — see PluginProcessor.h.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <limits>

namespace
{
    constexpr int stateVersion = 1;
}

//==============================================================================
FemPlateAudioProcessor::FemPlateAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createLayout())
{
    pF1       = apvts.getRawParameterValue (fem::id::f1);
    pTension  = apvts.getRawParameterValue (fem::id::tension);
    pViscDamp = apvts.getRawParameterValue (fem::id::viscDamp);
    pMatDamp  = apvts.getRawParameterValue (fem::id::matDamp);
    pHammer   = apvts.getRawParameterValue (fem::id::hammerMs);
    pForce    = apvts.getRawParameterValue (fem::id::force);
    pNonlin   = apvts.getRawParameterValue (fem::id::nonlin);
    pCascade  = apvts.getRawParameterValue (fem::id::cascade);
    pNumModes = apvts.getRawParameterValue (fem::id::numModes);
    pOutX     = apvts.getRawParameterValue (fem::id::outX);
    pOutY     = apvts.getRawParameterValue (fem::id::outY);
    pInGain   = apvts.getRawParameterValue (fem::id::inGain);
    pOutGain  = apvts.getRawParameterValue (fem::id::outGain);

    shapeData = makeDefaultShape();
    buildMesh();
}

FemPlateAudioProcessor::~FemPlateAudioProcessor()
{
    runner.cancelAndWait();
}

juce::AudioProcessorValueTreeState::ParameterLayout FemPlateAudioProcessor::createLayout()
{
    using FloatParam = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto logRange = [] (float lo, float hi) {
        return juce::NormalisableRange<float> (
            lo, hi,
            [] (float start, float end, float t)
                { return start * std::pow (end / start, t); },
            [] (float start, float end, float v)
                { return std::log (v / start) / std::log (end / start); });
    };

    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::f1, 1), "Base Freq", logRange (20.0f, 2000.0f), 110.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    // Tension-to-flexural-stiffness: 0 = pure plate, large = membrane-like.
    // The eigenproblem is solved at the tension current when Compute runs;
    // knob moves around it retune the bank at audio rate (first-order law).
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::tension, 1), "Tension",
        juce::NormalisableRange<float> (0.0f, 500.0f, 0.0f, 0.4f), 0.0f));

    // Both damping knobs are the damping ratio of mode 1 (dimensionless,
    // scaled equations): viscous ~ dw/dt, material ~ Delta^2 dw/dt.
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::viscDamp, 1), "Viscous", logRange (1.0e-6f, 0.1f), 1.0e-3f));
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::matDamp, 1), "Material", logRange (1.0e-6f, 0.1f), 1.0e-4f));

    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::hammerMs, 1), "Hammer", logRange (0.1f, 50.0f), 3.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // Hammer force amplitude. With the nonlinearities engaged the absolute
    // level matters (it drives the dynamic tension and the cascade), so this
    // goes well past unity.
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::force, 1), "Force",
        juce::NormalisableRange<float> (0.0f, 4.0f, 0.0f, 0.5f), 1.0f));

    // Geometric nonlinearity (see PlateSynth.h): Berger dynamic tension and
    // cubic mode-cascade feedback, both 0 = linear model.
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::nonlin, 1), "Nonlinear",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f, 0.5f), 0.0f));
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::cascade, 1), "Cascade",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f, 0.5f), 0.0f));

    p.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID (fem::id::numModes, 1), "Modes", 1, fem::maxModes, 32));

    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::outX, 1), "Out X",
        juce::NormalisableRange<float> (0.0f, 1.0f, 1.0e-3f), 0.5f));
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::outY, 1), "Out Y",
        juce::NormalisableRange<float> (0.0f, 1.0f, 1.0e-3f), 0.47f));

    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::inGain, 1), "In Gain",
        juce::NormalisableRange<float> (0.0f, 2.0f, 1.0e-3f, 0.5f), 0.0f));
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::outGain, 1), "Out Gain",
        juce::NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return { p.begin(), p.end() };
}

FemPlateAudioProcessor::ShapeData FemPlateAudioProcessor::makeDefaultShape()
{
    ShapeData s;
    // Slightly elliptic default plate, four simply supported segments.
    constexpr int npts = 96;
    for (int i = 0; i < npts; ++i)
    {
        const double a = juce::MathConstants<double>::twoPi * i / npts;
        s.outline.push_back ({ 0.5 + 0.42 * std::cos (a), 0.5 + 0.35 * std::sin (a) });
    }
    s.segStarts = { 0.0, 0.25, 0.5, 0.75 };
    s.segBcs = { 1, 1, 1, 1 };   // SimplySupported
    s.meshDensity = 16;
    return s;
}

//==============================================================================
void FemPlateAudioProcessor::prepareToPlay (double sampleRate, int)
{
    juce::FloatVectorOperations::disableDenormalisedNumberSupport();
    synth.prepare (sampleRate);
}

void FemPlateAudioProcessor::releaseResources()
{
    // No processBlock can run concurrently with or after this call, so
    // superseded models can be reclaimed regardless of acknowledgement.
    reclaimModels (true);
}

bool FemPlateAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()
        || in == juce::AudioChannelSet::disabled();
}

void FemPlateAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto* model = publishedModel.load (std::memory_order_acquire);
    audioSeenGeneration.store (model != nullptr ? model->generation : 0,
                               std::memory_order_release);

    fem::PlateSynth::Params params;
    params.f1       = pF1->load();
    params.tension  = pTension->load();
    params.viscDamp = pViscDamp->load();
    params.matDamp  = pMatDamp->load();
    params.hammerMs = pHammer->load();
    params.force    = pForce->load();
    params.nonlin   = pNonlin->load();
    params.cascade  = pCascade->load();
    params.outX     = pOutX->load();
    params.outY     = pOutY->load();
    params.numModes = (int) pNumModes->load();
    synth.update (model, params);

    const int sc = strikeCounter.load (std::memory_order_acquire);
    if (sc != lastStrikeSeen)
    {
        lastStrikeSeen = sc;
        synth.strike (strikeX.load(), strikeY.load(), strikeVel.load());
    }

    const float inGain = pInGain->load();
    const float outGain = juce::Decibels::decibelsToGain (pOutGain->load());

    const int numSamples = buffer.getNumSamples();
    const int numIns = getTotalNumInputChannels();
    const int numOuts = getTotalNumOutputChannels();

    for (int ch = numOuts; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const float* in0 = numIns > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* in1 = numIns > 1 ? buffer.getReadPointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        float input = 0.0f;
        if (in0 != nullptr)
            input = in1 != nullptr ? 0.5f * (in0[i] + in1[i]) : in0[i];

        const float y = outGain * synth.processSample (inGain * input);
        for (int ch = 0; ch < numOuts; ++ch)
            buffer.getWritePointer (ch)[i] = y;
    }
}

//==============================================================================
void FemPlateAudioProcessor::invalidateShape()
{
    displayMesh = nullptr;
    invalidateBoundary();
}

void FemPlateAudioProcessor::invalidateBoundary()
{
    // Mark the modes stale for the UI, but keep the published model sounding
    // until a new computation replaces it: edits shouldn't cut the audio.
    currentModel = nullptr;
    sendChangeMessage();
}

bool FemPlateAudioProcessor::buildMesh()
{
    const double h = 1.0 / juce::jlimit (6, 40, shapeData.meshDensity);
    auto mesh = std::make_shared<fxme::acoustics::FemMesh> (
        fxme::acoustics::generateMesh (shapeData.outline, h));
    if (mesh->empty())
        return false;
    displayMesh = std::move (mesh);
    sendChangeMessage();
    return true;
}

void FemPlateAudioProcessor::computeModes()
{
    if (runner.isRunning())
        return;
    if (displayMesh == nullptr && ! buildMesh())
        return;

    // Snapshots for the job (it must not touch the processor's live state).
    auto mesh = displayMesh;
    fxme::acoustics::BoundarySpec bc;
    bc.segmentStart = shapeData.segStarts;
    for (int b : shapeData.segBcs)
        bc.segmentBc.push_back ((fxme::acoustics::BoundaryCondition) juce::jlimit (0, 3, b));

    fxme::acoustics::ModalOptions opt;
    // Solve the full bank once (the Modes knob then selects without
    // recomputing), but never ask for more modes than the mesh can resolve:
    // the top discrete modes need several DOFs per half-wave to be physical,
    // ~6 DOFs per mode in practice. A finer Grid setting unlocks more modes.
    opt.numModes = juce::jmin (fem::maxModes,
                               juce::jmax (8, (displayMesh->numVertices()
                                               + displayMesh->numEdges()) / 6));
    opt.tension = pTension->load();
    opt.progress = [this] (float pr) { computeProgress.store (pr); };

    computeProgress.store (0.0f);
    pendingModel = std::make_unique<fem::ModalModel>();
    pendingModel->mesh = mesh;

    std::vector<fxme::BackgroundTaskRunner::Job> jobs;
    jobs.push_back ([this, mesh, bc, opt]
    {
        pendingModel->modes = fxme::acoustics::computePlateModes (*mesh, bc, opt);
    });

    runner.runJobs (std::move (jobs),
                    [] (float) {},
                    [this]
                    {
                        computeProgress.store (-1.0f);
                        if (pendingModel != nullptr && pendingModel->modes.valid())
                            publishModel (std::move (pendingModel));
                        else
                            pendingModel = nullptr;
                        sendChangeMessage();
                    });
    sendChangeMessage();
}

void FemPlateAudioProcessor::publishModel (std::unique_ptr<fem::ModalModel> model)
{
    model->generation = nextGeneration++;
    currentModel = model.get();
    modelStore.push_back (std::move (model));
    publishedModel.store (currentModel, std::memory_order_release);
    reclaimModels (false);
}

void FemPlateAudioProcessor::reclaimModels (bool audioStopped)
{
    // A superseded model can be freed once the audio thread acknowledged a
    // newer generation (or the audio is known to be stopped). The published
    // model itself is always kept.
    const auto seen = audioStopped ? std::numeric_limits<std::uint64_t>::max()
                                   : audioSeenGeneration.load (std::memory_order_acquire);
    const auto* keep = publishedModel.load (std::memory_order_acquire);
    modelStore.erase (
        std::remove_if (modelStore.begin(), modelStore.end(),
                        [&] (const std::unique_ptr<fem::ModalModel>& m)
                        {
                            return m.get() != keep && m.get() != currentModel
                                && m->generation < seen;
                        }),
        modelStore.end());
}

void FemPlateAudioProcessor::requestStrike (float x, float y, float velocity)
{
    strikeX.store (x);
    strikeY.store (y);
    strikeVel.store (velocity);
    strikeCounter.fetch_add (1, std::memory_order_release);
}

//==============================================================================
juce::ValueTree FemPlateAudioProcessor::shapeToTree() const
{
    juce::ValueTree t ("SHAPE");

    juce::StringArray pts;
    for (const auto& p : shapeData.outline)
        pts.add (juce::String (p.x, 6) + "," + juce::String (p.y, 6));
    t.setProperty ("outline", pts.joinIntoString (";"), nullptr);

    juce::StringArray starts, bcs;
    for (double s : shapeData.segStarts)
        starts.add (juce::String (s, 6));
    for (int b : shapeData.segBcs)
        bcs.add (juce::String (b));
    t.setProperty ("segStarts", starts.joinIntoString (";"), nullptr);
    t.setProperty ("segBcs", bcs.joinIntoString (";"), nullptr);
    t.setProperty ("meshDensity", shapeData.meshDensity, nullptr);
    return t;
}

void FemPlateAudioProcessor::shapeFromTree (const juce::ValueTree& t)
{
    if (! t.hasType ("SHAPE"))
        return;

    ShapeData s;
    for (const auto& pt : juce::StringArray::fromTokens (t["outline"].toString(), ";", ""))
    {
        const auto xy = juce::StringArray::fromTokens (pt, ",", "");
        if (xy.size() == 2)
            s.outline.push_back ({ xy[0].getDoubleValue(), xy[1].getDoubleValue() });
    }
    for (const auto& st : juce::StringArray::fromTokens (t["segStarts"].toString(), ";", ""))
        s.segStarts.push_back (st.getDoubleValue());
    for (const auto& b : juce::StringArray::fromTokens (t["segBcs"].toString(), ";", ""))
        s.segBcs.push_back (b.getIntValue());
    s.meshDensity = (int) t.getProperty ("meshDensity", 16);

    if (s.outline.size() >= 3 && s.segStarts.size() == s.segBcs.size()
         && ! s.segStarts.empty())
        shapeData = std::move (s);
}

void FemPlateAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("FemPlateState");
    root.setProperty ("version", stateVersion, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (shapeToTree(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void FemPlateAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("FemPlateState"))
        return;

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);
    shapeFromTree (root.getChildWithName ("SHAPE"));

    // Rebuild mesh + modes on the message thread.
    triggerAsyncUpdate();
}

void FemPlateAudioProcessor::handleAsyncUpdate()
{
    invalidateShape();
    if (buildMesh())
        computeModes();
}

//==============================================================================
juce::AudioProcessorEditor* FemPlateAudioProcessor::createEditor()
{
    return new FemPlateAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FemPlateAudioProcessor();
}
