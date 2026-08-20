/*
  ------------------------------------------------------------------------------
    PluginProcessor.h

    FemPlate — physical model of a plate / membrane of arbitrary shape.
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

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

#include "ParamIDs.h"
#include "Dsp/ModalModel.h"
#include "Dsp/PlateSynth.h"

//==============================================================================
class FemPlateAudioProcessor : public juce::AudioProcessor,
                               public juce::ChangeBroadcaster,
                               private juce::AsyncUpdater
{
public:
    FemPlateAudioProcessor();
    ~FemPlateAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                      { return false; }
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

private:
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

    // Cached raw parameter pointers (APVTS owns the atomics).
    std::atomic<float>* pF1 = nullptr;
    std::atomic<float>* pTension = nullptr;
    std::atomic<float>* pViscDamp = nullptr;
    std::atomic<float>* pMatDamp = nullptr;
    std::atomic<float>* pHammer = nullptr;
    std::atomic<float>* pForce = nullptr;
    std::atomic<float>* pNonlin = nullptr;
    std::atomic<float>* pCascade = nullptr;
    std::atomic<float>* pCascAmp = nullptr;
    std::atomic<float>* pCascDrive = nullptr;
    std::atomic<float>* pCascAttack = nullptr;
    std::atomic<float>* pCascRelease = nullptr;
    std::atomic<float>* pCascOverlap = nullptr;
    std::atomic<float>* pCascWindow = nullptr;
    std::atomic<float>* pCascDeplete = nullptr;
    std::atomic<float>* pNumModes = nullptr;
    std::atomic<float>* pOutX = nullptr;
    std::atomic<float>* pOutY = nullptr;
    std::atomic<float>* pInGain = nullptr;
    std::atomic<float>* pOutGain = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FemPlateAudioProcessor)
};
