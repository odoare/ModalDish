/*
  ------------------------------------------------------------------------------
    PluginProcessor.h

    ModalDish — physical model of a plate / membrane of arbitrary shape.
    The user draws the shape, sets boundary conditions per border segment,
    a finite-element modal analysis runs in the background
    (FxmeTools/acoustics), and the resulting modes drive a resonant filter
    bank (PlateSynth) excited by mouse hits and/or the external audio input.

    Threading model
    ---------------
    * Shape / boundary data: message thread only (editor edits it, compute
      snapshots it).
    * Modal computation: one background job (fxme::BackgroundTaskRunner);
      progress in an atomic, completion callback on the message thread.
    * Model publication: completed ModalModels are owned by `modelStore`
      (message thread) and exposed through the atomic `publishedModel`
      pointer. The audio thread acknowledges the generation it uses in
      `audioSeenGeneration`; superseded models are deleted (message thread)
      only once acknowledged, so the audio thread never touches freed memory.
    * Strikes: tiny atomic mailbox (position, velocity, counter).
    * MIDI: read straight off the block's MidiBuffer on the audio thread; a
      note-on retunes the plate to the note and strikes it (PlateSynth).

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

#include "ParamIDs.h"
#include "Dsp/ModalModel.h"
#include "Dsp/PlateSynth.h"

#include <FxmeTools/dsp/Biquad.h>
#include <FxmeTools/presets/PresetManager.h>
#include <FxmeTools/dsp/VuMeter.h>

#include <atomic>
#include <vector>

//==============================================================================
class ModalDishAudioProcessor : public juce::AudioProcessor,
                               public juce::ChangeBroadcaster,
                               private juce::AsyncUpdater
{
public:
    ModalDishAudioProcessor();
    ~ModalDishAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                      { return true; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 30.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    fxme::PresetManager& getPresetManager() noexcept { return presetManager; }

    /** True the first time it is called on this processor, false ever after.
        The editor shows the splash on the first call. The flag lives here
        rather than on the editor because the editor is destroyed and rebuilt
        every time the window is closed and reopened, and a reopened window is
        not a new run. Deliberately not serialised: a reloaded session is. */
    bool claimSplash() noexcept
    {
        const bool first = ! splashClaimed;
        splashClaimed = true;
        return first;
    }

    /** MIDI learn, editor <-> audio thread.

        `midiLearnArmed` is the source index the editor is waiting to map, or
        -1. The audio thread captures the next note-on into `midiLearnNote` /
        `midiLearnSource`, disarms, and swallows that note rather than firing
        anything with it. The editor's timer applies the capture to the
        parameter, so nothing writes a parameter from the audio thread. */
    std::atomic<int> midiLearnArmed { -1 };
    std::atomic<int> midiLearnSource { -1 };
    std::atomic<int> midiLearnNote { -1 };


    /** The user-editable geometry (message thread only). */
    struct ShapeData
    {
        std::vector<fxme::acoustics::Point2> outline;   // closed, plate coords ~[0,1]^2
        std::vector<double> segStarts;                  // sorted arc params in [0,1)
        std::vector<int> segBcs;                        // BoundaryCondition per segment
        int meshDensity = 16;                           // target element size = 1/density
    };

    ShapeData& shape() noexcept                    { return shapeData; }
    const ShapeData& shape() const noexcept        { return shapeData; }

    /** Message thread: the outline changed — drop mesh and modes. */
    void invalidateShape();
    /** Message thread: only boundary segments / BCs changed — modes stale. */
    void invalidateBoundary();

    /** Message thread: (re)build the preview mesh from the current shape.
        Fast (milliseconds). Returns true on success. */
    bool buildMesh();
    /** Message thread: start the background modal computation (builds the
        mesh first when needed). No-op while one is already running. */
    void computeModes();
    bool isComputing() const                       { return runner.isRunning(); }
    /** -1 when idle, else 0..1. */
    float getComputeProgress() const               { return computeProgress.load(); }

    /** Message thread: mesh being displayed (preview or last computed). */
    std::shared_ptr<const fxme::acoustics::FemMesh> getDisplayMesh() const { return displayMesh; }
    /** Message thread: last computed model, nullptr when stale/none. */
    const fem::ModalModel* getCurrentModel() const { return currentModel; }

    /** Any thread: hit the plate (plate coordinates, velocity 0..1). */
    void requestStrike (float x, float y, float velocity);

    /** Message thread: latest modal snapshot of the sounding plate (see
        PlateSynth::copyModalField). Summed against the mode shapes this is
        the plate's displacement or velocity field. */
    int copyModalField (fem::PlateSynth::Field which, float* dest, int maxCount) const noexcept
        { return synth.copyModalField (which, dest, maxCount); }

    /** Message thread: how many MIDI notes have struck the plate. The editor
        watches it so a played note flashes the same marker a click does. */
    /** Where the plate has actually been struck, for the editor's hit
        markers. Forwarded from the synth, which is the only thing that knows:
        a source scatters its hits, and one note can fire several sources.
        Replaces the old note counter, which could only say that *something*
        had been hit and left the editor to guess where. */
    /** Output level in dBFS, channel 0 = left. Post Out Gain, so what the
        meter shows is what leaves the plugin.

        This is a *peak* reading, held and then falling at `peakFallDbPerSec`.
        RMS is the wrong quantity for these meters: their job is clipping
        headroom, and a struck plate's 100 ms RMS sits 8 to 12 dB under its
        sample peak, so an RMS bar reads +3 while the output is really +13.
        The hold is computed per block on the audio thread rather than by the
        editor, so a transient cannot be missed by a GUI frame landing
        between blocks. */
    float getOutputPeakDb (int channel) const noexcept
    {
        return (channel == 0 ? peakHoldL : peakHoldR).load (std::memory_order_relaxed);
    }

    /** Message thread: which pickup the point panel's meter follows, or -1.
        A panel sets it when it opens and clears it when it closes, so at
        most one pickup is ever metered. */
    std::atomic<int> meteredPickup { -1 };

    /** Level in dBFS of the pickup named by `meteredPickup`: mono, its own
        Level applied and its Pan not, and pre Out Gain. Peak, held and
        falling, for the same reason as the output meters above. */
    float getPickupPeakDb() const noexcept
    {
        return peakHoldPickup.load (std::memory_order_relaxed);
    }

    int getHitCount() const noexcept              { return synth.getHitCount(); }
    fem::PlateSynth::HitPoint getHit (int i) const noexcept { return synth.getHit (i); }

private:
    fxme::PresetManager presetManager;
    bool splashClaimed = false;

    /** A preset carries the plate's geometry as well as its knobs, so the
        shape is folded into apvts.state on the way out and read back on the
        way in. The computed modes are deliberately *not* included: they are
        megabytes of mode shapes, and recomputing them from the geometry is
        what Compute already does. */
    void storeShapeInState();
    void loadShapeFromState();

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleAsyncUpdate() override;   // post-setState rebuild
    void publishModel (std::unique_ptr<fem::ModalModel> model);
    void reclaimModels (bool audioStopped);
    juce::ValueTree shapeToTree() const;
    void shapeFromTree (const juce::ValueTree& tree);
    juce::ValueTree modesToTree() const;             // serialise currentModel
    bool modesFromTree (const juce::ValueTree&);     // rebuild + publish model
    static ShapeData makeDefaultShape();

    // --- message-thread state -----------------------------------------------
    ShapeData shapeData;
    std::shared_ptr<const fxme::acoustics::FemMesh> displayMesh;
    const fem::ModalModel* currentModel = nullptr;   // owned by modelStore
    std::vector<std::unique_ptr<fem::ModalModel>> modelStore;
    std::uint64_t nextGeneration = 1;

    fxme::BackgroundTaskRunner runner { 1 };
    std::atomic<float> computeProgress { -1.0f };
    std::unique_ptr<fem::ModalModel> pendingModel;   // written by the job, read in onFinished
    juce::ValueTree pendingModesTree;                // modal cache from setStateInformation

    // --- audio-thread bridge --------------------------------------------------
    std::atomic<const fem::ModalModel*> publishedModel { nullptr };
    std::atomic<std::uint64_t> audioSeenGeneration { 0 };

    std::atomic<int> strikeCounter { 0 };
    std::atomic<float> strikeX { 0.5f }, strikeY { 0.5f }, strikeVel { 1.0f };
    int lastStrikeSeen = 0;

    fem::PlateSynth synth;
    // Subsonic protection on the output. Base Freq reaches down to 1 Hz now,
    // and although modes below 20 Hz are muted outright (PlateSynth::retune),
    // that is not the whole of the low end: a live mode just above 20 Hz is a
    // band-pass whose lower skirt falls at only 6 dB/oct, and the half-sine
    // hammer pulse carries DC. Second-order Butterworth, so there is a double
    // zero at DC — an offset is removed absolutely rather than attenuated —
    // and 12 dB/oct below, while 35 Hz and up is untouched to within 0.4 dB.
    static constexpr float outHighpassHz = 20.0f;
    static constexpr float outHighpassQ  = 0.70710678f;   // Butterworth
    fxme::Biquad outHpL, outHpR;

    fxme::VuMeter outMeterL, outMeterR;

    // Peak-hold ballistics, in dBFS. 20 dB/s is slow enough to read a
    // transient off the bar and fast enough that the bar follows a decaying
    // plate rather than sitting at the strike.
    static constexpr float peakFallDbPerSec = 20.0f;
    std::atomic<float> peakHoldL { -100.0f };
    std::atomic<float> peakHoldR { -100.0f };
    std::atomic<float> peakHoldPickup { -100.0f };

    /** Fold one block's peak into a held reading. Both are dBFS, and the
        floor at -100 keeps a silent hold from falling forever. */
    static void holdPeak (std::atomic<float>& held, float blockDb, float fallDb) noexcept
    {
        const float fallen = held.load (std::memory_order_relaxed) - fallDb;
        held.store (juce::jmax (blockDb, fallen), std::memory_order_relaxed);
    }

    // The pickup meter measures a signal that never reaches a bus, so it
    // needs a block of its own to measure; sized in prepareToPlay.
    fxme::VuMeter pickupMeter;
    std::vector<float> pickupMeterScratch;

    // Cached raw parameter pointers (APVTS owns the atomics).
    std::atomic<float>* pF1 = nullptr;
    std::atomic<float>* pTension = nullptr;
    std::atomic<float>* pViscDamp = nullptr;
    std::atomic<float>* pMatDamp = nullptr;
    std::atomic<float>* pHammer = nullptr;
    std::atomic<float>* pForce = nullptr;
    std::atomic<float>* pGlide = nullptr;
    std::atomic<float>* pNonlin = nullptr;
    std::atomic<float>* pSrcChannel { nullptr };
    std::atomic<float>* pFreqChannel { nullptr };
    std::atomic<float>* pUnmappedHit { nullptr };
    std::atomic<float>* pCascade { nullptr };
    std::atomic<float>* pCascDrive = nullptr;
    std::atomic<float>* pCascAttack = nullptr;
    std::atomic<float>* pCascRelease = nullptr;
    std::atomic<float>* pCascOverlap = nullptr;
    std::atomic<float>* pCascWindow = nullptr;
    std::atomic<float>* pCascDeplete = nullptr;
    std::atomic<float>* pNumModes = nullptr;
    std::atomic<float>* pPickupX[fem::maxPickups] {};
    std::atomic<float>* pPickupY[fem::maxPickups] {};
    std::atomic<float>* pPickupLevel[fem::maxPickups] {};
    std::atomic<float>* pPickupPan[fem::maxPickups] {};
    std::atomic<float>* pPickupOn[fem::maxPickups] {};
    std::atomic<float>* pSourceX[fem::maxSources] {};
    std::atomic<float>* pSourceY[fem::maxSources] {};
    std::atomic<float>* pSourceX2[fem::maxSources] {};
    std::atomic<float>* pSourceY2[fem::maxSources] {};
    std::atomic<float>* pSourceHammerMax[fem::maxSources] {};
    std::atomic<float>* pSourceForceMax[fem::maxSources] {};
    std::atomic<float>* pSourcePosCtl[fem::maxSources] {};
    std::atomic<float>* pSourceHammerCtl[fem::maxSources] {};
    std::atomic<float>* pSourceForceCtl[fem::maxSources] {};
    std::atomic<float>* pSourceVelCurve[fem::maxSources] {};
    std::atomic<float>* pSourceHammer[fem::maxSources] {};
    std::atomic<float>* pSourceForce[fem::maxSources] {};
    std::atomic<float>* pSourceNote[fem::maxSources] {};
    std::atomic<float>* pSourceSpread[fem::maxSources] {};
    std::atomic<float>* pSourceSend[fem::maxSources] {};
    std::atomic<float>* pSourcePan[fem::maxSources] {};
    std::atomic<float>* pSourceOn[fem::maxSources] {};
    std::atomic<float>* pInGain = nullptr;
    std::atomic<float>* pOutGain = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModalDishAudioProcessor)
};
