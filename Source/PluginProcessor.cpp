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

    // Deterministic uniform in [0,1) (splitmix-style), so a given plate
    // always regenerates the same statistical tail.
    double tailRand (std::uint64_t& s)
    {
        s += 0x9e3779b97f4a7c15ull;
        std::uint64_t k = s;
        k = (k ^ (k >> 30)) * 0xbf58476d1ce4e5b9ull;
        k = (k ^ (k >> 27)) * 0x94d049bb133111ebull;
        k ^= k >> 31;
        return (double) (k & 0xffffffffull) / 4294967296.0;
    }

    /** Appends the statistical mode tail (Berry random-wave model) above
        the FEM modes, up to `targetTotal` modes in all:
          - frequencies continue at the Weyl spacing measured on the top
            half of the FEM spectrum (plate modal density is constant in
            omega), with +-40% jittered gaps;
          - tension sensitivity g = sqrt(lambda), the exact high-frequency
            asymptote (for a wavenumber kappa: lambda = kappa^4,
            g = kappa^2);
          - shapes are random plane waves with |k| = lambda^(1/4),
            mass-normalised through amp = sqrt(2/area).
        The tail gives the cascade a dense receiving continuum up to (and
        beyond) Nyquist without solving for hundreds of FEM modes; the
        identity-defining low modes stay exact. */
    void appendStatisticalTail (fem::ModalModel& m, int targetTotal)
    {
        auto& lam = m.modes.lambda;
        auto& g = m.modes.tensionG;
        const int nFem = m.numFemModes();
        if (m.mesh == nullptr || nFem < 8 || (int) lam.size() >= targetTotal)
            return;

        // Weyl spacing in omega from the top half of the FEM spectrum.
        const size_t iA = lam.size() / 2;
        const size_t iB = lam.size() - 1;
        const double dOmega = (std::sqrt (lam[iB]) - std::sqrt (lam[iA]))
                              / (double) juce::jmax ((size_t) 1, iB - iA);
        if (! (dOmega > 0.0))
            return;

        double area = 0.0;
        for (const auto& t : m.mesh->triangles)
        {
            const auto& a = m.mesh->vertices[(size_t) t[0]];
            const auto& b = m.mesh->vertices[(size_t) t[1]];
            const auto& c = m.mesh->vertices[(size_t) t[2]];
            area += 0.5 * std::abs ((b.x - a.x) * (c.y - a.y)
                                  - (c.x - a.x) * (b.y - a.y));
        }
        const float amp = (float) std::sqrt (2.0 / juce::jmax (area, 1.0e-6));

        std::uint64_t seed = 0x5eedULL + (std::uint64_t) nFem;
        double omega = std::sqrt (lam[iB]);
        while ((int) lam.size() < targetTotal)
        {
            omega += dOmega * (0.6 + 0.8 * tailRand (seed));   // jittered gap
            lam.push_back (omega * omega);
            g.push_back (omega);                               // g = sqrt(lambda)

            fem::ModalModel::TailWave w;
            const double kappa = std::sqrt (omega);            // lambda^(1/4)
            const double theta = juce::MathConstants<double>::twoPi * tailRand (seed);
            w.kx = (float) (kappa * std::cos (theta));
            w.ky = (float) (kappa * std::sin (theta));
            w.phase = (float) (juce::MathConstants<double>::twoPi * tailRand (seed));
            w.amp = amp;
            m.tailWaves.push_back (w);
        }
    }
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
    pGlide    = apvts.getRawParameterValue (fem::id::glide);
    pNonlin   = apvts.getRawParameterValue (fem::id::nonlin);
    pCascade  = apvts.getRawParameterValue (fem::id::cascade);
    pCascAmp     = apvts.getRawParameterValue (fem::id::cascAmp);
    pCascDrive   = apvts.getRawParameterValue (fem::id::cascDrive);
    pCascAttack  = apvts.getRawParameterValue (fem::id::cascAttack);
    pCascRelease = apvts.getRawParameterValue (fem::id::cascRelease);
    pCascOverlap = apvts.getRawParameterValue (fem::id::cascOverlap);
    pCascWindow  = apvts.getRawParameterValue (fem::id::cascWindow);
    pCascDeplete = apvts.getRawParameterValue (fem::id::cascDeplete);
    pNumModes = apvts.getRawParameterValue (fem::id::numModes);
    for (int i = 0; i < fem::maxPickups; ++i)
    {
        pPickupX[i]     = apvts.getRawParameterValue (fem::id::pickupX[i]);
        pPickupY[i]     = apvts.getRawParameterValue (fem::id::pickupY[i]);
        pPickupLevel[i] = apvts.getRawParameterValue (fem::id::pickupLevel[i]);
        pPickupPan[i]   = apvts.getRawParameterValue (fem::id::pickupPan[i]);
        pPickupOn[i]    = apvts.getRawParameterValue (fem::id::pickupOn[i]);
    }
    for (int i = 0; i < fem::maxSources; ++i)
    {
        pSourceX[i]      = apvts.getRawParameterValue (fem::id::sourceX[i]);
        pSourceY[i]      = apvts.getRawParameterValue (fem::id::sourceY[i]);
        pSourceHammer[i] = apvts.getRawParameterValue (fem::id::sourceHammer[i]);
        pSourceForce[i]  = apvts.getRawParameterValue (fem::id::sourceForce[i]);
        pSourceNote[i]   = apvts.getRawParameterValue (fem::id::sourceNote[i]);
        pSourceSpread[i] = apvts.getRawParameterValue (fem::id::sourceSpread[i]);
        pSourceSend[i]   = apvts.getRawParameterValue (fem::id::sourceSend[i]);
        pSourcePan[i]    = apvts.getRawParameterValue (fem::id::sourcePan[i]);
        pSourceOn[i]     = apvts.getRawParameterValue (fem::id::sourceOn[i]);
    }
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
        juce::ParameterID (fem::id::viscDamp, 1), "Viscous", logRange (1.0e-6f, 0.1f), 1.0e-4f));
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::matDamp, 1), "Material", logRange (1.0e-6f, 0.1f), 7.0e-6f));

    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::hammerMs, 1), "Hammer", logRange (0.1f, 50.0f), 3.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // Hammer force amplitude. With the nonlinearities engaged the absolute
    // level matters (it drives the dynamic tension and the cascade), so this
    // goes well past unity — to 20. The skew stays at 0.4, which already
    // spreads the quiet end generously enough that doubling the top only
    // moves the 1.0 default from 40% to 30% of the travel.
    //
    // Nothing downstream is unbounded in it: the linear bank is linear in the
    // force, the Berger driver saturates at gammaCap well below Force 5, and
    // the cascade ladder is a strict DAG whose carriers the tanh holds to
    // +/-1. What a hard hit buys past that point is the gating and the
    // depletion, not more level from the nonlinearity.
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::force, 1), "Force",
        juce::NormalisableRange<float> (0.0f, 20.0f, 0.0f, 0.4f), 1.0f));

    // Portamento between played notes. Currently inert and deliberately kept:
    // notes stopped setting the pitch when they became per-source triggers,
    // so nothing reads it and it has no GUI control. The glide machinery in
    // PlateSynth is intact, so this comes back as a one-line change if notes
    // are ever given the tuning again.
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::glide, 1), "Glide",
        juce::NormalisableRange<float> (0.0f, 2000.0f, 0.0f, 0.35f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // Geometric nonlinearity (see PlateSynth.h): Berger dynamic tension and
    // cubic mode-cascade feedback, both 0 = linear model.
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::nonlin, 1), "Nonlinear",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f, 0.5f), 0.0f));
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::cascade, 1), "Cascade",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f, 0.5f), 0.0f));

    // Cascade tuning set (see PlateSynth.h); defaults are the voiced values.
    // Amp is deliberately capped at 1.5: higher injection gains combined
    // with a hot Drive get loud enough to be unpleasant.
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::cascAmp, 1), "Casc Amp",
        juce::NormalisableRange<float> (0.0f, 2.5f, 0.0f, 0.7f), 1.1f));
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::cascDrive, 1), "Casc Drive", logRange (0.25f, 20.0f), 16.0f));
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::cascAttack, 1), "Casc Att", logRange (1.0f, 200.0f), 30.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms/band")));
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::cascRelease, 1), "Casc Rel", logRange (20.0f, 4000.0f), 2000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::cascOverlap, 1), "Casc Ovl",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f, 0.5f), 0.1f));
    p.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID (fem::id::cascWindow, 1), "Casc Win", 1, 7, 4));
    p.push_back (std::make_unique<FloatParam> (
        juce::ParameterID (fem::id::cascDeplete, 1), "Casc Depl",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f, 0.5f), 0.07f));

    // Default high enough that the statistical tail (above the FEM modes)
    // takes part out of the box.
    p.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID (fem::id::numModes, 1), "Modes", 1, fem::maxModes, 192));

    // Pickups. Only the first is on by default, centred and at unity, so the
    // plate out of the box is the single mono listening point it always was.
    // The others start spread across the plate and across the image, so that
    // switching one on is immediately audible as a position rather than
    // needing three knob moves first.
    {
        struct Defaults { float x, y, pan; bool on; };
        static constexpr Defaults defaults[fem::maxPickups] = {
            { 0.50f, 0.47f,  0.0f, true  },
            { 0.30f, 0.50f, -0.7f, false },
            { 0.70f, 0.50f,  0.7f, false },
            { 0.50f, 0.72f,  0.0f, false },
        };

        for (int i = 0; i < fem::maxPickups; ++i)
        {
            const auto label = juce::String (i + 1);
            p.push_back (std::make_unique<FloatParam> (
                juce::ParameterID (fem::id::pickupX[i], 1), "Pickup " + label + " X",
                juce::NormalisableRange<float> (0.0f, 1.0f, 1.0e-3f), defaults[i].x));
            p.push_back (std::make_unique<FloatParam> (
                juce::ParameterID (fem::id::pickupY[i], 1), "Pickup " + label + " Y",
                juce::NormalisableRange<float> (0.0f, 1.0f, 1.0e-3f), defaults[i].y));
            p.push_back (std::make_unique<FloatParam> (
                juce::ParameterID (fem::id::pickupLevel[i], 1), "Pickup " + label + " Level",
                juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
                juce::AudioParameterFloatAttributes().withLabel ("dB")));
            p.push_back (std::make_unique<FloatParam> (
                juce::ParameterID (fem::id::pickupPan[i], 1), "Pickup " + label + " Pan",
                juce::NormalisableRange<float> (-1.0f, 1.0f, 1.0e-2f), defaults[i].pan));
            p.push_back (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID (fem::id::pickupOn[i], 1), "Pickup " + label + " On",
                defaults[i].on));
        }
    }

    // Sources. Only 'a' is on, centred, at full send — which is exactly the
    // single injection point the external input used to have, so effect mode
    // behaves as before until more are switched on. Notes start unmapped
    // (-1), so MIDI keeps its pre-source behaviour until a source claims a
    // note.
    {
        for (int i = 0; i < fem::maxSources; ++i)
        {
            // Upper case in the host's parameter list, lower case on the
            // plate (fem::sourceLabel) — same source, read in two places.
            const auto prefix = "Source "
                              + juce::String::charToString ((juce::juce_wchar) ('A' + i))
                              + " ";
            const bool first = (i == 0);

            p.push_back (std::make_unique<FloatParam> (
                juce::ParameterID (fem::id::sourceX[i], 1), prefix + "X",
                juce::NormalisableRange<float> (0.0f, 1.0f, 1.0e-3f), 0.5f));
            p.push_back (std::make_unique<FloatParam> (
                juce::ParameterID (fem::id::sourceY[i], 1), prefix + "Y",
                juce::NormalisableRange<float> (0.0f, 1.0f, 1.0e-3f), 0.47f));
            p.push_back (std::make_unique<FloatParam> (
                juce::ParameterID (fem::id::sourceHammer[i], 1), prefix + "Hammer",
                logRange (0.1f, 50.0f), 3.0f,
                juce::AudioParameterFloatAttributes().withLabel ("ms")));
            p.push_back (std::make_unique<FloatParam> (
                juce::ParameterID (fem::id::sourceForce[i], 1), prefix + "Force",
                juce::NormalisableRange<float> (0.0f, 20.0f, 0.0f, 0.4f), 1.0f));
            // -1 reads as "off" rather than as a note: a source with no note
            // is the default, and a host automation lane should say so.
            p.push_back (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID (fem::id::sourceNote[i], 1), prefix + "Note",
                -1, 127, -1, juce::AudioParameterIntAttributes()
                    .withStringFromValueFunction ([] (int v, int)
                    {
                        return v < 0 ? juce::String ("Off")
                                     : juce::MidiMessage::getMidiNoteName (v, true, true, 4);
                    })));
            // Standard deviation per axis, in plate coordinates. A quarter of
            // the plate is already a wild scatter, so the range stops there.
            p.push_back (std::make_unique<FloatParam> (
                juce::ParameterID (fem::id::sourceSpread[i], 1), prefix + "Spread",
                juce::NormalisableRange<float> (0.0f, 0.25f, 1.0e-3f, 0.6f), 0.0f));
            p.push_back (std::make_unique<FloatParam> (
                juce::ParameterID (fem::id::sourceSend[i], 1), prefix + "Send",
                juce::NormalisableRange<float> (0.0f, 2.0f, 1.0e-3f, 0.5f),
                first ? 1.0f : 0.0f));
            p.push_back (std::make_unique<FloatParam> (
                juce::ParameterID (fem::id::sourcePan[i], 1), prefix + "In Bal",
                juce::NormalisableRange<float> (-1.0f, 1.0f, 1.0e-2f), 0.0f));
            p.push_back (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID (fem::id::sourceOn[i], 1), prefix + "On", first));
        }
    }

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
    outMeterL.prepare (sampleRate);
    outMeterR.prepare (sampleRate);
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

void FemPlateAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
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
    params.glideMs  = pGlide->load();
    params.nonlin   = pNonlin->load();
    params.cascade  = pCascade->load();
    params.cascAmp       = pCascAmp->load();
    params.cascDrive     = pCascDrive->load();
    params.cascAttackMs  = pCascAttack->load();
    params.cascReleaseMs = pCascRelease->load();
    params.cascOverlap   = pCascOverlap->load();
    params.cascWindow    = (int) pCascWindow->load();
    params.cascDeplete   = pCascDeplete->load();
    for (int i = 0; i < fem::maxPickups; ++i)
    {
        auto& pk = params.pickups[i];
        pk.x     = pPickupX[i]->load();
        pk.y     = pPickupY[i]->load();
        pk.level = juce::Decibels::decibelsToGain (pPickupLevel[i]->load(), -60.0f);
        pk.pan   = pPickupPan[i]->load();
        pk.on    = pPickupOn[i]->load() > 0.5f;
    }
    for (int i = 0; i < fem::maxSources; ++i)
    {
        auto& src = params.sources[i];
        src.x        = pSourceX[i]->load();
        src.y        = pSourceY[i]->load();
        src.hammerMs = pSourceHammer[i]->load();
        src.force    = pSourceForce[i]->load();
        src.note     = (int) pSourceNote[i]->load();
        src.spread   = pSourceSpread[i]->load();
        src.send     = pSourceSend[i]->load();
        src.pan      = pSourcePan[i]->load();
        src.on       = pSourceOn[i]->load() > 0.5f;
    }
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
    float* out0 = numOuts > 0 ? buffer.getWritePointer (0) : nullptr;
    float* out1 = numOuts > 1 ? buffer.getWritePointer (1) : nullptr;

    // Notes are consumed in step with the render loop, so a note lands on its
    // own sample rather than at the block boundary (audible on short blocks
    // with a fast glide). Note-offs are ignored: a struck plate rings out.
    auto midiEvent = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();

    for (int i = 0; i < numSamples; ++i)
    {
        for (; midiEvent != midiEnd && (*midiEvent).samplePosition <= i; ++midiEvent)
        {
            const auto msg = (*midiEvent).getMessage();
            if (msg.isNoteOn())
            {
                // Sources claim notes; a note nothing claims falls back to
                // the old behaviour, a hit at the last struck point with the
                // global Hammer and Force. That keeps the plugin playable
                // from a keyboard before any source has been mapped, which
                // is how it ships.
                const int note = msg.getNoteNumber();
                const float velocity = msg.getFloatVelocity();

                const int armed = midiLearnArmed.load (std::memory_order_acquire);
                if (armed >= 0)
                {
                    // Learning: capture and swallow. Firing the source as well
                    // would make every mapping start with a hit nobody asked
                    // for, which is exactly the note being used to teach.
                    midiLearnSource.store (armed, std::memory_order_relaxed);
                    midiLearnNote.store (note, std::memory_order_release);
                    midiLearnArmed.store (-1, std::memory_order_release);
                }
                else
                {
                    if (synth.strikeSourcesForNote (note, velocity) == 0)
                        synth.noteOn (velocity);
                }
            }
        }

        const float inL = in0 != nullptr ? in0[i] : 0.0f;
        const float inR = in1 != nullptr ? in1[i] : inL;

        float outL = 0.0f, outR = 0.0f;
        synth.processSample (inGain * inL, inGain * inR, outL, outR);
        outL *= outGain;
        outR *= outGain;

        if (out1 != nullptr)
        {
            out0[i] = outL;
            out1[i] = outR;
        }
        else if (out0 != nullptr)
        {
            // Folding down rather than dropping a side: on a mono bus a
            // hard-panned pickup would otherwise be inaudible.
            out0[i] = 0.5f * (outL + outR);
        }
    }

    // Metering reads what actually leaves, Out Gain included: the meters are
    // there to show clipping headroom, which a pre-gain reading would miss.
    if (out0 != nullptr)
        outMeterL.process (out0, numSamples);
    outMeterR.process (out1 != nullptr ? out1 : (out0 != nullptr ? out0 : nullptr),
                       out0 != nullptr ? numSamples : 0);
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
    const double h = 1.0 / juce::jlimit (6, 48, shapeData.meshDensity);
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
    // Solve as many FEM modes as reasonable in one go; fem::femModeCount says
    // how many the mesh can resolve, and the editor's footprint readout uses
    // the same rule so the two never disagree.
    opt.numModes = fem::femModeCount (displayMesh->numVertices() + displayMesh->numEdges());
    opt.tension = pTension->load();
    opt.progress = [this] (float pr) { computeProgress.store (pr); };

    computeProgress.store (0.0f);
    pendingModel = std::make_unique<fem::ModalModel>();
    pendingModel->mesh = mesh;

    std::vector<fxme::BackgroundTaskRunner::Job> jobs;
    jobs.push_back ([this, mesh, bc, opt]
    {
        // The assembled matrices are sparse but the shifted operator is still
        // factorised densely, so the working set is dominated by n^2 doubles
        // and reaches hundreds of megabytes at the top of the Grid range (see
        // fem::solverBytesEstimate). Let a failed allocation come back as "no
        // modes" rather than as a terminate() on the worker thread; the
        // completion handler below already treats an invalid result as a
        // failed computation.
        try
        {
            pendingModel->modes = fxme::acoustics::computePlateModes (*mesh, bc, opt);
        }
        catch (const std::bad_alloc&)
        {
            pendingModel->modes = {};
        }
    });

    runner.runJobs (std::move (jobs),
                    [] (float) {},
                    [this]
                    {
                        computeProgress.store (-1.0f);
                        if (pendingModel != nullptr && pendingModel->modes.valid())
                        {
                            appendStatisticalTail (*pendingModel, fem::maxModes);
                            publishModel (std::move (pendingModel));
                        }
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

namespace
{
    template <typename T>
    juce::var packVector (const T* data, size_t count)
    {
        return juce::var (juce::MemoryBlock (data, count * sizeof (T)));
    }

    template <typename T>
    bool unpackVector (const juce::var& v, std::vector<T>& out, size_t expectedCount)
    {
        const auto* block = v.getBinaryData();
        if (block == nullptr || block->getSize() != expectedCount * sizeof (T))
            return false;
        out.resize (expectedCount);
        std::memcpy (out.data(), block->getData(), block->getSize());
        return true;
    }
}

/*  The computed modal data (eigenvalues, tension coefficients, FEM shapes,
    statistical tail) is cached inside the plugin state, so loading a session
    or preset publishes the model immediately instead of re-running the
    eigensolver. The mesh itself is NOT serialised: generateMesh is a pure
    deterministic function of the outline and density, so it is rebuilt on
    load and only the vertex count is stored as a consistency check. */
juce::ValueTree FemPlateAudioProcessor::modesToTree() const
{
    if (currentModel == nullptr || currentModel->mesh == nullptr
         || ! currentModel->modes.valid())
        return {};

    const auto& m = *currentModel;
    const int numTotal = m.numModes();
    const int numFem = m.numFemModes();
    const int nv = m.mesh->numVertices();

    juce::ValueTree t ("MODES");
    t.setProperty ("tensionRef", m.modes.tensionRef, nullptr);
    t.setProperty ("numVertices", nv, nullptr);
    t.setProperty ("numFem", numFem, nullptr);
    t.setProperty ("numTotal", numTotal, nullptr);
    t.setProperty ("lambda", packVector (m.modes.lambda.data(), (size_t) numTotal), nullptr);
    t.setProperty ("tensionG", packVector (m.modes.tensionG.data(), (size_t) numTotal), nullptr);

    std::vector<float> shapes ((size_t) numFem * (size_t) nv);
    for (int k = 0; k < numFem; ++k)
        std::memcpy (shapes.data() + (size_t) k * (size_t) nv,
                     m.modes.shapes[(size_t) k].data(), (size_t) nv * sizeof (float));
    t.setProperty ("shapes", packVector (shapes.data(), shapes.size()), nullptr);

    std::vector<float> tail;
    for (const auto& w : m.tailWaves)
    {
        tail.push_back (w.kx);
        tail.push_back (w.ky);
        tail.push_back (w.phase);
        tail.push_back (w.amp);
    }
    t.setProperty ("tail", packVector (tail.data(), tail.size()), nullptr);
    return t;
}

bool FemPlateAudioProcessor::modesFromTree (const juce::ValueTree& t)
{
    if (! t.hasType ("MODES") || displayMesh == nullptr)
        return false;

    const int nv = (int) t.getProperty ("numVertices", -1);
    const int numFem = (int) t.getProperty ("numFem", 0);
    const int numTotal = (int) t.getProperty ("numTotal", 0);
    if (nv != displayMesh->numVertices() || numFem < 1 || numTotal < numFem)
        return false;

    auto model = std::make_unique<fem::ModalModel>();
    model->mesh = displayMesh;
    model->modes.tensionRef = (double) t.getProperty ("tensionRef", 0.0);

    std::vector<float> shapes, tail;
    if (! unpackVector (t["lambda"], model->modes.lambda, (size_t) numTotal)
         || ! unpackVector (t["tensionG"], model->modes.tensionG, (size_t) numTotal)
         || ! unpackVector (t["shapes"], shapes, (size_t) numFem * (size_t) nv)
         || ! unpackVector (t["tail"], tail, (size_t) (numTotal - numFem) * 4))
        return false;

    model->modes.shapes.resize ((size_t) numFem);
    for (int k = 0; k < numFem; ++k)
    {
        auto& s = model->modes.shapes[(size_t) k];
        s.resize ((size_t) nv);
        std::memcpy (s.data(), shapes.data() + (size_t) k * (size_t) nv,
                     (size_t) nv * sizeof (float));
    }
    for (int j = 0; j < numTotal - numFem; ++j)
        model->tailWaves.push_back ({ tail[(size_t) j * 4],     tail[(size_t) j * 4 + 1],
                                      tail[(size_t) j * 4 + 2], tail[(size_t) j * 4 + 3] });

    publishModel (std::move (model));
    sendChangeMessage();
    return true;
}

void FemPlateAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("FemPlateState");
    root.setProperty ("version", stateVersion, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (shapeToTree(), nullptr);
    if (auto modes = modesToTree(); modes.isValid())
        root.appendChild (modes, nullptr);

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
    pendingModesTree = root.getChildWithName ("MODES").createCopy();

    // Rebuild mesh + modes on the message thread.
    triggerAsyncUpdate();
}

void FemPlateAudioProcessor::handleAsyncUpdate()
{
    invalidateShape();
    if (buildMesh())
    {
        // Prefer the modal cache saved with the state; fall back to a fresh
        // background computation when it is missing or inconsistent.
        if (! modesFromTree (pendingModesTree))
            computeModes();
    }
    pendingModesTree = {};
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
