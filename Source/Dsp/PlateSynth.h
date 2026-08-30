/*
  ------------------------------------------------------------------------------
    PlateSynth.h

    Modal synthesis of the FEM plate: one resonant band-pass biquad per mode
    (the MechanOdd resonator approach). Given the modal model (frequencies,
    tension coefficients, mass-normalised shapes) the signal fed to filter k
    for a hit at point P is  phi_k(P) * force * hammer(t), and the output is
    read at the output point O through  phi_k(O) * filter_k(...). An external
    input signal is injected the same way at the last hit point, which makes
    the plugin usable as an effect.

    Frequency / damping laws (see doc/technical.tex and
    FxmeTools/acoustics/PlateModes.h):

        omega_k(T)^2 = lambda_k + (T - Tref) g_k          (scaled units)
        f_k          = f1 * omega_k(T_eff) / omega_1(T_knob)
        zeta_k       = zetaV / nu_k + zetaM * nu_k

    The frequency map is normalised by the fundamental at the *static* knob
    tension: turning the Tension knob keeps mode 1 at f1 (it reshapes the
    overtone ratios, i.e. the timbre), while the *dynamic* Berger tension
    below shifts the whole spectrum, fundamental included — that absolute
    shift is the audible hardening glide. zetaV is the viscous-damping knob
    (a term in d w/dt: relative damping falls with frequency) and zetaM the
    material-damping knob (a term in Delta^2 dw/dt: damping grows with
    frequency); both equal the damping ratio of mode 1.

    Geometric nonlinearity (Berger / von Karman, simplified)
    --------------------------------------------------------
    * Dynamic tension ("nonlin" knob). Berger's approximation replaces the
      von Karman membrane coupling by a spatially uniform tension that grows
      with the *global* stretching of the plate, parameterised as a
      *relative* stiffening gamma of mode 1:

          T_dyn = gamma * omega_1^2(T_knob) / g_1 ,
          gamma = nlGain * nonlin * < int |grad w|^2 dA >

      so mode 1 glides up by sqrt(1 + gamma) on a hard hit and relaxes as
      the ring decays (hardening), and every other mode follows its own
      tension sensitivity g_k. gamma is dimensionless and plate-independent.
      T_dyn drives the ordinary first-order retune law at a decimated rate,
      slewed and throttled (only rewrite coefficients above ~2 cents).

      The stretching integral needs no field reconstruction: the modes are
      mass-orthonormal, so it collapses onto the modal coordinates,

          int |grad w|^2 dA = sum_kl q_k q_l phi_k^T G phi_l ~ sum_k g_k q_k^2

      (the cross terms oscillate at omega_k +- omega_l and average out in
      the envelope) — one multiply-add per mode and per sample. What the
      band-pass bank holds is not q_k though: a 0 dB-peak band-pass fed the
      true modal force is exactly y_k = 2 zeta_k omega_k qdot_k, i.e. a
      *velocity*, normalised. Hence q_k = y_k / (2 zeta_k omega_k^2) and

          nlWeight_k = (g_k / g_1) (zetaRef / zeta_k)^2 / nu_k^4 ,
          driver     = < sum_k nlWeight_k y_k^2 >

      with the leftover constant folded into nlGain. The missing
      1/(4 zeta^2 omega^4) spans ten decades across the bank: leave it out
      and a sum of the filter outputs underestimates the stretching by
      orders of magnitude. Being an integral over the plate, the driver is
      the same wherever one listens — the glide belongs to the plate, not
      to the pickup — and it weights the modes as the stretching really
      does (contributions scale as 1/nu_k, so the lows dominate and the
      cascade shimmer does not drag the glide up). Peak gamma is damping-
      independent by construction too: after an impulse y_k ~ 2 zeta_k
      omega_k, so zeta_k cancels in nlWeight_k y_k^2 and only the *decay*
      of the glide follows the damping knobs.

    * Mode cascade ("cascade" amount, shaped by "Casc Drive"): a windowed
      multi-band ladder with
      transfer inertia. The bank is split into numCascadeBands frequency
      bands; band b is pumped by a tanh-bounded cubic of the output of the
      *cascWindow bands directly below it only* — summing all lower bands
      would let the loud strike band drive the top almost directly, making
      the whole spectrum light up at once. With the window, energy genuinely
      climbs rung by rung (a cubic only reaches 3x its source band), like
      the real von Karman cascade. Each band's injection gain additionally
      passes through an attack/release envelope whose attack time grows
      with the band's height on the ladder (~cascadeAttackMsPerBand x b),
      so the brightening is a progressive glow rather than an instant
      flash, and releases over ~cascadeReleaseMs so the pumping tails off
      smoothly. The transfer graph remains a strict DAG (band b only ever
      receives from below), so stability is unconditional with no gain
      restriction, unlike a full-output feedback (which needs a small-gain
      bound that renders it inaudible). Each target mode's injection is
      weighted by sqrt(zeta_k / zetaRef) — the ladder's own calibration,
      deliberately not the output gain rule, see cascadeInjectionGain in
      the .cpp — and target bandwidths are floored — proportionally to the
      cascade amount and the Overlap parameter — at a fraction of the local
      mode spacing: enough overlap for the receiving comb to catch the
      broadband products, low enough that the pumped modes keep a natural
      ring once the pumping stops. The amount scaling that floor is not a
      detail: it is what makes a cascade of zero leave the plate's own
      damping alone, rather than quietly widening every target mode while
      nothing is being pumped.

      The amount acts directly: the drive below carries no damping term, so
      there is nothing for a damping compensation to correct.

      The source signal is the plate's *motion* over the whole plate, and
      deliberately not its audible output: two things sit between the two,
      and the ladder's cubic would raise both to the third power.

      The first is the output gain. The bank models plate *acceleration*:
      c_k = compZetaRef / zeta_k exactly undoes the band-pass's own factor
      2 zeta omega, leaving 2 compZetaRef omega_k times the modal velocity,
      so the audible signal carries a frequency tilt omega_k that the
      plate's motion does not. Weighting the source by velScale_k removes
      both that tilt and the zeta (y_k is proportional to zeta_k omega_k,
      velScale_k to its inverse), leaving the modal velocity, which is what
      the plate actually does. The
      *frequency* balance the ladder needs then has to be supplied on
      purpose: physical modal amplitudes fall steeply with frequency, so a
      raw velocity sum starves the upper rungs (band 0 outweighs band 1 by
      ~900x). Each band's source is therefore normalised by sum(1/nu_k)
      over its own modes — a constant fixed by the mode indices alone, with
      no damping in it. Every rung is then driven by a band-mean modal
      velocity.

      The second is the pickup. A mode nodal at x_o contributes nothing to
      the audible signal however hard the plate is moving there, and the
      cubic coupling of a real plate does not know where you put the
      microphone. The weight is the constant 1/sqrt(A), what a typical
      phi_k is worth on a mass-normalised plate (int phi^2 dA = 1), so the
      drive keeps its level on any geometry while depending on no listening
      position. The *injection* does carry phi_l(x_h): the hit point is
      where the plate deflects most, so it is the one position the
      nonlinearity has a physical claim to, and it is what keeps the
      cascade's character tied to how the plate is played.

      Depletion ("Deplete" parameter): the transfer is otherwise purely
      additive, so the low modes would ring on untouched while the shimmer
      floats on top — an audible disconnect. While band b's gates draw
      from a source band, that band's filter states receive an extra
      per-sample exponential decay proportional to the mean gate activity
      of the bands it feeds: energy audibly *leaves* the lows as the
      shimmer blooms, gluing the two. Purely dissipative, so it cannot
      affect stability.

    Played notes
    ------------
    A note does two things, and they are two calls, not one. glideToNote()
    aims the base frequency f1 at the note (mode 1 lands on it, the rest of
    the spectrum follows the ratios of the plate); noteOn() strikes at the
    last hit point with velocity/127 scaled by Force. The processor gates
    each on its own MIDI channel, so a note can tune, strike, do both, or do
    neither — and eight sources mapped to their own notes never end up
    fighting over the tuning.

    The pitch travels in log2, so a glide takes the Glide time whatever the
    interval, and it is stepped on the same 32-sample grid as the Berger
    tension, sharing its ~2-cent retune throttle. The step is sized from the
    Glide time, the sample rate and the update period together, so the glide
    lasts the same wall-clock time at any rate. The Freq knob is not glided:
    it sets the pitch directly, and so does the first note of the session.

    Everything here runs on the audio thread and is allocation-free; the
    model pointer is published by the processor (see PluginProcessor).

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

#include "ModalModel.h"

#include <FxmeTools/dsp/Biquad.h>
#include <FxmeTools/util/Random.h>

namespace fem
{

class PlateSynth
{
public:
    // Half-sine force pulses that can be in contact at once. Not voices:
    // there is one filter bank, and every active pulse is summed into its
    // drive, so a strike adds to whatever is already ringing rather than
    // starting a plate of its own. What this sizes is simultaneous contact,
    // which lasts a hammer time (milliseconds), not sustain — and reuse is
    // round-robin, so overrunning it truncates a pulse still in contact,
    // never a tail. Eight sources on their own MIDI notes can overlap, and
    // mouse hits land on top of them; eight slots was tight for that. Each
    // slot carries a full mode-weight vector, so the raise costs 32 KB.
    static constexpr int maxStrikes = 16;

    /** One listening point. `level` is a linear gain and `pan` runs -1 (left)
        to +1 (right), equal-power with the centre at unity. A pickup that is
        off contributes nothing and costs nothing — not even the mode-shape
        evaluation at its position.

        Every pickup defaults to *off*: the caller says which listening points
        exist, and a Params with none on is silent rather than guessing. The
        position default is the plate's long-standing output point, so
        switching the first one on reproduces the mono output this replaced. */
    struct Pickup
    {
        float x = 0.5f, y = 0.47f;
        float level = 1.0f;
        float pan = 0.0f;
        bool  on = false;
    };

    /** One striking / injection point.

        `force` and `hammerMs` are the *minimum* ends of their mappings (see
        below); `hammerMs` is the contact time, and `spread` the standard deviation — per axis, in plate
        coordinates — of the random offset applied to each hit, so 0 always
        strikes exactly on the point. `send` and `pan` are this point's share
        of the plugin input: `pan` is an L/R balance of the incoming stereo
        pair, -1 taking the left channel alone and 0 their mean, which is the
        mono sum the single injection point used to receive.

        `note` is the MIDI note that triggers it, or -1 for none. */
    struct Source
    {
        // (x, y) is where a velocity-0 hit lands; (velX, velY) is how far
        // full velocity moves it, so a strike lands at (x + v*velX, ...).
        //
        // An offset rather than a second absolute point, so that zero — the
        // default — means "velocity does not move this source" whatever x and
        // y are. A second absolute point would default to somewhere of its
        // own, and any caller that set x without also setting it would get a
        // segment it never asked for. The plugin's parameters *are* two
        // absolute points ('a' and 'A' on the plate); the processor subtracts
        // them on the way in.
        //
        // Only strikes read it: the input send is injected at (x, y) alone,
        // a continuous signal having no velocity to place it by.
        float x = 0.5f, y = 0.47f;
        float velX = 0.0f, velY = 0.0f;
        // Each of the three modulated quantities is a pair plus a controller
        // (fem::ctlOff / ctlVelocity / ctlCcBase + n). The value used for a
        // strike is min + shaped(control) * (max - min); min above max is
        // allowed and simply inverts the mapping. Off holds the min, which is
        // why Force ships with min 0, max 1 and Velocity selected: that is
        // exactly "velocity scales force", the behaviour the hammer had
        // before any of this existed.
        float hammerMs = 3.0f, hammerMsMax = 3.0f;
        float force = 0.0f, forceMax = 1.0f;
        int posCtl = fem::ctlVelocity;
        int hammerCtl = fem::ctlOff;
        int forceCtl = fem::ctlVelocity;
        int velCurve = fem::velCurveLinear;
        float spread = 0.0f;
        float send = 0.0f;
        float pan = 0.0f;
        int   note = -1;
        bool  on = false;
    };

    struct Params
    {
        float f1        = 110.0f;   // Hz
        float tension   = 0.0f;     // T (scaled units)
        float viscDamp  = 1.0e-4f;  // zeta of mode 1, viscous term
        float matDamp   = 7.0e-6f;  // zeta of mode 1, material term
        float hammerMs  = 3.0f;     // half-sine shock duration
        float force     = 1.0f;     // hammer force amplitude
        float nonlin    = 0.0f;
        float cascade   = 0.0f;     // 0..1 upward-cascade amount, 0 = off     // 0..1 dynamic-tension (Berger) amount
        float glideMs   = 0.1f;     // portamento time between played notes (ms)

        // Which pickup the point panel's meter follows, or -1 for none. Not
        // a plugin parameter — it follows an open window, not a saved
        // setting — but it rides in here so that it reaches the audio thread
        // by the one path everything else uses.
        int meteredPickup = -1;

        // Cascade tuning set (see the class comment; defaults = voiced values).
        float cascDrive     = 8.0f;    // tanh knee B
        float cascAttackMs  = 30.0f;   // gate attack per band rung
        float cascReleaseMs = 2000.0f; // gate release
        float cascOverlap   = 0.1f;    // target bandwidth floor, x local spacing
        int   cascWindow    = 4;       // source window, bands below each rung
        float cascDeplete   = 0.07f;   // source-band energy loss while pumping

        Pickup pickups[fem::maxPickups];
        Source sources[fem::maxSources];
        int   numModes  = 32;       // active modes (<= model modes, <= fem::maxModes)
    };

    void prepare (double sampleRate);
    void reset();

    /** Number of modes the bank is actually sounding: the bank is ordered by
        frequency and everything above the audible band is muted, so the
        per-sample loop stops there. At a high Freq setting that can be a
        small fraction of the Modes knob — the knob buys spectrum, and the
        spectrum runs out at Nyquist. */
    int getLiveModeCount() const noexcept { return liveModes; }

    /** Audio thread, once per block: applies a possibly-new model and the
        current parameters, retuning the filter bank only when something
        relevant changed. */
    void update (const ModalModel* model, const Params& params);

    /** Audio thread: hit the plate at (x, y) (plate coordinates) with
        normalised velocity 0..1, using the global Hammer and Force settings.
        This is the hit a click on bare plate produces. Also moves the point
        the cascade ladder injects at. */
    void strike (float x, float y, float velocity);

    /** Audio thread: fire source `s` (0..fem::maxSources-1) at normalised
        velocity 0..1, which scales that source's own Force. The hit lands on
        the source's point, displaced by a random offset when its Spread is
        non-zero. Does nothing for a source that is off or out of range. */
    void strikeSource (int s, float velocity) noexcept;

    /** Audio thread: sources whose MIDI note is `note` and which are on, in
        index order. Returns how many fired, so the caller can decide what an
        unmapped note should do. */
    int strikeSourcesForNote (int note, float velocity) noexcept;

    /** Audio thread: a MIDI note strikes the plate at the last hit point with
        `velocity` (0..1, i.e. MIDI velocity / 127, which the Force parameter
        then scales). Pitch is not involved: see glideToNote.

        Striking and tuning are deliberately two calls rather than one. Eight
        sources can each be mapped to their own note, and a trigger that also
        moved the whole plate's tuning would make them fight over it; keeping
        them apart is what lets the processor route notes by channel, so one
        channel can play the plate and another can tune it. */
    void noteOn (float velocity) noexcept;

    /** Audio thread: aim the base frequency at `note` (mode 1 lands on it and
        the rest of the spectrum follows the plate's own ratios), gliding
        there over the Glide time. Does not strike.

        The travel is in log2, so a glide takes the Glide time whatever the
        interval, and it is stepped on the same decimated grid as the Berger
        tension, sharing its ~2-cent retune throttle. The first note of the
        session lands directly, as the Freq knob does: there is nothing to
        glide from. */
    void glideToNote (int note) noexcept;

    /** Audio thread: latest value of MIDI CC `cc`, 0..127, as a 0..1 float.
        Held here rather than in Params because a controller move is an event
        arriving between blocks, not a parameter the host automates. */
    void setControllerValue (int cc, float value) noexcept
    {
        if (cc >= 0 && cc < 128)
            ccValue[(size_t) cc] = juce::jlimit (0.0f, 1.0f, value);
    }

    //==========================================================================
    // Where the plate was actually struck, published for display.
    //
    // Only the synth knows this. A source scatters its hits around its point
    // when Spread is up, and one MIDI note can fire several sources at once,
    // so the position the caller asked for is not the position that was hit —
    // and for a note there was no position in the call at all.
    //
    // Lock-free, one producer (audio) and one consumer (the editor's timer):
    // the reader takes the count, then reads that many entries back. A reader
    // more than hitRingSize behind loses the oldest hits, which for a piece of
    // decoration is the right way to fail.

    static constexpr int hitRingSize = 16;

    /** `amplitude` is what the hammer actually delivered — velocity times the
        Force that applied, the source's own or the global one. Velocity alone
        would not do: two sources at the same velocity but Force 1 and Force 10
        hit the plate ten times as hard, and the display should say so. */
    struct HitPoint { float x = 0.0f, y = 0.0f, amplitude = 0.0f; };

    /** Hits since construction, only ever increasing. */
    int getHitCount() const noexcept { return hitCount.load (std::memory_order_acquire); }

    /** Hit number `index`, valid while index >= getHitCount() - hitRingSize. */
    HitPoint getHit (int index) const noexcept
    {
        const int slot = ((index % hitRingSize) + hitRingSize) % hitRingSize;
        return { hitX[slot].load (std::memory_order_relaxed),
                 hitY[slot].load (std::memory_order_relaxed),
                 hitAmp[slot].load (std::memory_order_relaxed) };
    }

    /** Base frequency the bank is currently sounding at (Hz), for display. */
    float getBaseFrequency() const noexcept { return (float) std::exp2 (f1Log); }

    /** The metered pickup's mono contribution to the last processSample:
        its own Level applied, its Pan not, so it reads what that point
        hears rather than where the point sits in the image. Zero unless
        Params::meteredPickup names a pickup that is switched on. */
    float getMeteredSample() const noexcept { return meterOut; }

    /** Audio thread: one stereo input sample in, one stereo plate sample out.

        The output is a stereo pair because the pickups pan; the input is a
        pair because a source takes its own L/R balance of it (see Params).
        Both collapse into per-mode weight vectors, so the loop cost does not
        grow with the number of pickups or sources. */
    void processSample (float inL, float inR, float& outL, float& outR) noexcept;

    /** Current relative stiffening of mode 1 (0 = linear), for display. */
    float getNonlinearGamma() const noexcept { return (float) gamma; }

    /** What a published modal snapshot holds — one power of the frequency
        ratio apart, which is exactly how much weight the picture gives the
        high modes. */
    enum class Field
    {
        displacement,   ///< q_k     ~ y_k / (zeta_k nu_k^2)
        velocity        ///< qdot_k  ~ y_k / (zeta_k nu_k)
    };

    /** Any thread: copies the latest modal snapshot into dest[0..n-1] and
        returns n (<= maxCount). Summed against the mode shapes this is the
        plate's field, w = sum_k q_k phi_k (or its time derivative).

        The 0 dB-peak band-pass output is y_k = 2 zeta_k omega_k qdot_k, so
        the bank holds *velocities*: Field::velocity is that, exactly, scaled
        per mode (velScale below) and phase-true. Field::displacement divides
        by omega_k once more (dispScale), which also turns each mode a
        quarter period ahead of its true displacement — not observable at
        frame rates, where the low modes are aliased many times over anyway.
        Both drop the constant common to every mode, which a display
        normalises away.

        Sampled every nlUpdatePeriod samples and published per mode with
        relaxed atomics: readers can straddle two sampling instants, which
        is invisible in a picture and keeps the audio thread lock-free. */
    int copyModalField (Field which, float* dest, int maxCount) const noexcept;

private:
    void retune();                    // full: mode count, weights, then bank
    void retuneBank();                // coefficients/amps at tension + T_dyn(gamma)
    void computeOutputWeights();      // phi_k at each pickup -> phiPickup
    void updatePickupMix() noexcept;  // phiPickup + levels/pans -> outAmpL/R
    void updateMeterWeights() noexcept;  // phiPickup + level -> meterAmp (mono)
    void updateTailSpread() noexcept; // band-resolution stereo spread of the tail
    /** The one place a hammer slot is filled: position, contact time and
        amplitude, whether the hit came from a click, a source or a note. */
    /** Launch one half-sine pulse. Takes the finished amplitude rather than
        a velocity and a force: a source resolves those through its own
        min/max mapping first, and only the global path still multiplies the
        two together. */
    void fireHammer (float x, float y, float amplitude, float hammerMs) noexcept;
    void computeHitWeights (float x, float y);   // phi_k(last hit) -> hitWeights
    void computeSourceShapes();                  // phi_k at each source -> phiSource
    void updateSourceMix() noexcept;             // phiSource + sends/pans -> inWL/R
    /** A hit position for source `s` at `velocity`: the point interpolated
        along its 'a'..'A' segment, or a random draw around that point that is
        still inside the plate. */
    void randomSourcePoint (int s, float velocity, float& x, float& y) noexcept;

    /** The 0..1 modulation a source's controller selection currently reads.
        Off is 0, so an unmapped quantity sits at its min; velocity is shaped
        by the source's curve, a CC is taken as the player's hardware left it. */
    float controlAmount (const Source& src, int control, float velocity) const noexcept;

    float ccValue[128] {};
    void updateCascadeWeights() noexcept;    // injection weights of the target modes
    void updateCascadeEnvelopes();           // attack/release coefficients from params
    void updateDynamicTension() noexcept;    // decimated Berger feedback step
    void updateGlide() noexcept;             // decimated portamento step

    struct Hammer
    {
        float weights[fem::maxModes] {};
        float phase = 0.0f;       // 0..1 over the shock
        float phaseInc = 0.0f;
        float amplitude = 0.0f;   // velocity * force at strike time
        bool active = false;
    };

    double fs = 44100.0;
    const ModalModel* model = nullptr;
    Params current;
    bool dirty = true;

    int activeModes = 0;
    int liveModes = 0;                   // modes actually in band, always <= activeModes
    // One allocation-free RBJ band-pass per mode (constant 0 dB peak; see
    // the compensation note at the top of the .cpp for why that shape).
    // fxme::Biquad exposes its state, which the cascade's depletion needs:
    // it scales z1 and z2 directly to bleed energy out of the source bands.
    fxme::Biquad filters[fem::maxModes];
    // Mode shapes sampled at each pickup, and the two vectors they collapse
    // into once levels and pans are applied. The per-sample loop only ever
    // touches the collapsed pair.
    fxme::Random rng { 0x5eed1234u };    // hit-position jitter; audio thread only

    // Published hit positions (see getHitCount). Deliberately not cleared by
    // reset(): the count is what the editor counts from, and restarting it
    // would replay old hits as new ones.
    std::atomic<float> hitX[hitRingSize] {};
    std::atomic<float> hitY[hitRingSize] {};
    std::atomic<float> hitAmp[hitRingSize] {};
    std::atomic<int> hitCount { 0 };

    float phiPickup[fem::maxPickups][fem::maxModes] {};
    float pickupGainL[fem::maxPickups] {};
    float pickupGainR[fem::maxPickups] {};
    float outAmpL[fem::maxModes] {};
    float outAmpR[fem::maxModes] {};
    // Mono weights of the one pickup being metered: its Level applied, its
    // Pan not. `metering` hoists the test out of the per-mode loop, and is
    // false whenever no panel is open or the metered pickup is switched off.
    float meterAmp[fem::maxModes] {};
    float meterOut = 0.0f;
    bool  metering = false;
    float hitWeights[fem::maxModes] {};  // phi_k(last hit), drives the cascade

    // Mode shapes at each source, and the two vectors the input sends
    // collapse into. As with the pickups, the per-sample loop only ever
    // touches the collapsed pair.
    float phiSource[fem::maxSources][fem::maxModes] {};
    float inWL[fem::maxModes] {};
    float inWR[fem::maxModes] {};
    float compensation[fem::maxModes] {};// output gain per mode (zetaRef / zeta)
    float cascInjW[fem::maxModes] {};    // cascade injection gain per mode
    float nlWeight[fem::maxModes] {};    // g_k q_k^2 per unit y_k^2 (Berger driver)
    float dispScale[fem::maxModes] {};   // q_k per unit y_k (display field)
    float velScale[fem::maxModes] {};    // qdot_k per unit y_k (display field)
    float cascSrcW[fem::maxModes] {};    // cascade source weight (band-normalised)
    float srcAmp = 1.0f;                 // 1/sqrt(plate area): typical |phi_k|
    float lastHitX = 0.5f, lastHitY = 0.5f;
    Hammer hammers[maxStrikes];
    int nextHammer = 0;

    // Base-frequency glide. The bank is tuned at exp2(f1Log); played notes
    // move f1Log towards f1LogTarget by glideStep per decimated update.
    // glideToNote sizes the step from the Glide time, the sample rate and the
    // update period together, so the glide lasts the same wall-clock time at
    // any rate — a step picked per update would not (see depleteTauSec).
    // (log2 domain, so a glide takes the same time whatever the interval),
    // and the bank is rewritten when it has drifted ~2 cents from the pitch
    // it currently sounds at — the same throttle the Berger glide uses.
    double f1Log = 0.0, f1LogTarget = 0.0;
    double f1LogTuned = -1.0e9;
    double glideStep = 0.0;
    bool gliding = false;
    bool notePlayed = false;

    // Field snapshots for the GUI (see copyModalField).
    std::atomic<float> fieldQ[fem::maxModes];
    std::atomic<float> fieldV[fem::maxModes];
    std::atomic<int> fieldCount { 0 };

    // Nonlinear state (Berger dynamic tension + windowed cubic cascade).
    static constexpr int nlUpdatePeriod = 32;   // samples between gamma steps
    static constexpr int numCascadeBands = 8;

    // Stereo spread of the statistical tail (see updateTailSpread). Sixteen
    // bands over however many tail modes the mode count leaves above the FEM
    // ones, and a pan clamp that stops a band whose signed sums happen to
    // nearly cancel in one channel from snapping hard to the other.
    static constexpr int numTailBands = 16;
    static constexpr float tailPanLimit = 0.8f;
    float envCoef = 0.001f;              // stretching follower coefficient
    // Per-step coefficients derived from durations in prepare(). They are not
    // constants because a constant here would be a time that changed with the
    // sample rate; see depleteTauSec and gammaSlewTauSec in the .cpp.
    float depletePerSample = 3.0e-4f;    // extra decay of a depleted source band
    double gammaSlew = 0.2;              // Berger stiffening slew, per decimated tick
    float envStretch = 0.0f;             // smoothed sum_k nlWeight_k y_k^2
    double gamma = 0.0;                  // slewed relative stiffening of mode 1
    double appliedGamma = 0.0;           // value the bank is currently tuned at
    int nlCountdown = nlUpdatePeriod;
    int cascadeSplit = 0;                // first mode of band 1 (targets start here)
    float cascEff = 0.0f;                // the cascade amount, as the DSP sees it
    int bandStart[numCascadeBands + 1] {};        // mode-index range of each band
    float cascadeW[fem::maxModes] {};    // injection weights (bands >= 1 only)
    float prevBandOut[numCascadeBands] {};        // per-band output, last sample
    float gateEnv[numCascadeBands] {};   // per-band injection envelope (0..1)
    float attackCoef[numCascadeBands] {};// per-band attack one-pole coefficient
    float releaseCoef = 0.001f;
};

} // namespace fem
