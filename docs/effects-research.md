# What to build next, and what not to

A survey of current acoustic and audio-DSP techniques, judged against what
DreamDSP can actually run.

Sections 1 to 4 were written before any of it was implemented, so that the
arguments are on record and can be disagreed with independently of whether the
code turned out well. Section 5 records what was then built, what was declined,
and what is still open — with the numbers the implementation actually measured,
which in three places are not the numbers the sources predicted.

The order of this document is deliberate. The constraints come first, then the
measured budget, then the criteria — because almost every proposal in the second
half is killed or shaped by one of those three, and reading the proposals first
makes them all look equally attractive.

---

## 1. What DreamDSP is, for the purposes of this document

Two pieces: a Qt application, and `DreamDspApo.dll` loaded into `audiodg.exe`.
Everything below turns on which side a thing can live on.

**Inside `audiodg.exe`** — the audio callback:

- No allocation, no locks, no syscalls, no file I/O, no exceptions. A dropout
  here is not one application glitching; it is the machine losing its sound.
- 480–1056 frames per call, deinterleaved float32, 1–8 channels, 8–384 kHz.
- Ten milliseconds of wall clock per 480 frames at 48 kHz, shared with every
  other processing object on the endpoint and with the rest of the system.
- Latency is reported to Windows **once**, when the stream is built. A stage
  whose latency depends on a live control leaves that figure stale until the
  stream is rebuilt.
- The process runs as LOCAL SERVICE. What gets loaded into it is a security
  decision, not only an engineering one.

**Inside the Qt application** — no such limits. It can measure, optimise, do
FFTs of arbitrary size, read files, and take a second doing it.

**Between them**: a 1300-byte struct of bounded scalars, written to a file that
any user can edit, and separately a content-addressed blob for anything large
(impulse responses). Filter coefficients deliberately never cross this line: a
denominator cannot be validated by any cheap test — `r = 0.998` is both a 40 Hz
highpass at 192 kHz and a +48 dB resonator — so the parameter channel carries
only quantities that can be clamped exactly.

That last rule has already decided one feature: ViPER DDC import was declined
because a `.vdc` file *is* raw biquad coefficients. It will decide several more
below.

**Already declined, and the reasoning still applies**: VST hosting and EEL2
scripting, both on the grounds that arbitrary third-party code on a real-time
thread inside a system audio process is not an acceptable thing to ship.

### The sixteen stages that existed when this was written

Equalizer (32 bands, 18 filter types, double precision) · graphic EQ (31 ISO
bands, AutoEQ curve import) · loudness correction · dynamic bass · virtual bass
· exciter · tube · compressor · 3-band multiband · reverb · stereo widener ·
crossfeed · 8×8 channel matrix · convolution (any IR rate → any endpoint rate) ·
per-channel delay (sub-sample) · look-ahead limiter.

Order is the user's; the chain editor writes a permutation of the whole set.

*(Four more were added as a result of this document — transient shaper, soft
clipper, auto volume and night mode. Section 5 says why.)*

Infrastructure already in-tree and tested: radix-2 FFT with real-input
optimisation, Kaiser-windowed sinc resampler, uniform-partition overlap-save
convolver, RBJ biquad design with a stability check, 2× oversampler,
Linkwitz-Riley 4th-order crossovers, DC blocker, denormal guard.

---

## 2. The budget, measured

`dspharness --bench`, this machine, every stage switched on and doing real work
on real material. The unit is the fraction of one core at real time, because
that — not milliseconds — is what decides whether something fits.

| Stage | 48k stereo | 96k stereo | 96k 7.1 |
|---|---:|---:|---:|
| equalizer (32 bands) | 0.54 % | 1.29 % | **4.57 %** |
| graphic eq (31 bands) | 0.51 % | 1.19 % | **4.43 %** |
| virtual bass | 0.44 % | 0.86 % | 2.91 % |
| tube | 0.37 % | 0.69 % | 2.74 % |
| exciter | 0.29 % | 0.58 % | 2.09 % |
| multiband | 0.10 % | 0.20 % | 0.68 % |
| limiter | 0.11 % | 0.14 % | 0.25 % |
| compressor | 0.09 % | 0.23 % | 0.23 % |
| reverb | 0.10 % | 0.25 % | 0.21 % |
| dynamic bass | 0.04 % | 0.08 % | 0.32 % |
| loudness | 0.03 % | 0.05 % | 0.22 % |
| matrix | 0.01 % | 0.01 % | 0.16 % |
| crossfeed | 0.01 % | 0.04 % | 0.05 % |
| delay | 0.01 % | 0.02 % | 0.04 % |
| width | 0.00 % | 0.00 % | 0.01 % |
| **everything** | **2.58 %** | **4.88 %** | **18.77 %** |

Convolution is excluded because its cost is set by the impulse response, not by
the stage.

Three things follow, and they shape everything in section 4:

1. **There is room, but it is not unlimited.** Nineteen percent of a core in the
   worst realistic configuration is affordable. Another nineteen would not be.
2. **The two filter banks dominate.** At a fixed 96 kHz the equalizer costs
   3.54× more at eight channels than at two, against a 4× channel count — so it
   is scaling very nearly linearly and the only thing being amortised is loop
   overhead. Nothing is vectorised across channels, and channels are the one
   axis of a biquad cascade that *is* trivially parallel (the cascade itself is
   serial and cannot be). That is the single largest optimisation available in
   the codebase, and it is worth more than any new effect.
3. **The oversampled nonlinear stages are next**, and together cost more than
   everything that is not a filter bank. Any change to how they suppress
   aliasing is a change to 8 % of the worst-case budget.

---

## 3. How proposals are judged

In order. A proposal that fails an earlier test is not rescued by doing well on
a later one.

1. **Can it live inside the callback at all?** No allocation, no locks, bounded
   time. If not — can it live in the Qt application and send scalars? Several
   things below are excellent ideas that belong entirely on the control side.
2. **Can its parameters be bounded?** If the thing it needs to send is a filter,
   a network, or a program, the answer is no and the design has to change or the
   feature is declined.
3. **Is the effect real?** Consumer audio has a long tradition of tone controls
   with a story attached. Where there are listening tests, they are cited. Where
   there are not, that is stated rather than glossed.
4. **What does it cost**, against the table in section 2.
5. **Can it be verified?** Every stage in DreamDSP is checked against a
   closed-form or measured expectation — the equalizer's plotted curve against
   its own measured impulse response to 0.0000 dB, the limiter's threshold as an
   exact guarantee, 12,600 filter designs for stability. A stage whose
   correctness cannot be stated as a number is a stage that will quietly rot.

---

## 4. Findings

*All seven investigations returned. Findings are recorded as they
arrive; claims that could be checked against this codebase have been, and
where the research was wrong about our own code that is noted in place.*

### 4.1 Binaural rendering and spatial audio

**The constraint that decides the whole area, and that has to be written down
before anyone asks for it again: an APO never sees Atmos objects.** Windows
Sonic and Dolby Atmos for Headphones are *scene renderers* that sit upstream of
the endpoint's APO chain. They consume objects and emit a stereo bed. By the
time audio reaches DreamDSP the rendering has already happened. "DreamDSP does
Atmos" is not a priority question, it is an impossibility.

The same architecture decides the shape of what *is* possible:

- **Stereo → binaural works unconditionally**, on any endpoint, with no user
  configuration. This is where nearly all the value is.
- **7.1 → binaural requires the user to set the endpoint to 7.1 in Windows
  Sound first**, which is exactly how HeSuVi has always worked. The APO then
  receives eight channels, renders into 0/1, and must zero 2–7. The existing
  8×8 matrix is already that mechanism.

#### Why BRIR rather than bare HRTF

Anechoic HRTF convolution reliably fails to externalise — the image stays
inside the head, worst for frontal and rear sources. Room reflections are what
push it out. The useful finding for a program that must ship *someone else's*
data is that this barely depends on whose ears were measured: Leclerc et al.
report externalisation of 53.4 % with individualised BRIRs against 51.2 % with
non-individualised. Essentially nothing. Perception here is dominated by the
room, not by the pinna.

#### What it costs, and the one structural decision

Convolution counts are fixed and there is no clever reduction: stereo needs 4
(L→l, L→r, R→l, R→r), 5.1 needs 10, 7.1 needs 14.

The costs, using our own convolver's arithmetic at 48 kHz:

| Configuration | Filters | Taps | MFLOP/s | Memory |
|---|---:|---:|---:|---:|
| Stereo, anechoic HRIR | 4 | 512 | 12 | 33 KB |
| Stereo, short BRIR (85 ms) | 4 | 4096 | 22 | 197 KB |
| Stereo, full BRIR (1.37 s) | 4 | 65536 | 207 | 3.1 MB |
| 7.1, full BRIR | 14 | 65536 | 711 | 11 MB |
| **7.1 hybrid** (50 ms conv + FDN tail) | 14 | 2400 | **49** | 1.0 MB |

At 96 kHz the same *durations* roughly quadruple the rate: full 7.1 BRIR
becomes **2801 MFLOP/s**, the hybrid **151**. Against the budget in section 2 —
where the entire existing rack is 18.77 % of a core at 96 kHz 7.1 — the full
convolution is not shippable and the hybrid is nothing.

**The structural decision is therefore forced, and it is also the published
one.** Split the BRIR at 30–50 ms: convolve the direct sound and early
reflections per channel per ear, and feed the *sum* into the existing FDN reverb
tuned to the measured T60. Perceived location comes from the early part;
envelopment comes from the interaural coherence of the tail. Two separable
jobs, and Dolby's headphone-virtualisation patents (US 10382875 and family)
describe exactly this: a bank of short per-direction HRTF filters feeding a
feedback delay network. We already own both halves.

#### What this actually costs us in code — the agent was wrong

The research said this "reuses the convolver verbatim". It does not, and the
difference matters for scheduling.

`ConvKernel` holds `ConvRoute route[kConvMaxChannels]` — **one route per output
channel, naming one stored IR spectrum**, applied to that channel's own input.
That is per-channel convolution. True-stereo binaural is not that shape:

```
out_L = conv(in_L, IR_LL) + conv(in_R, IR_RL)
out_R = conv(in_L, IR_LR) + conv(in_R, IR_RR)
```

Every output is a *sum* over several inputs. `ConvRoute` has to become a list of
(input channel, spectrum, gain) rather than a single spectrum, and the
accumulation loop has to iterate it.

The good news is that the frequency-delay line already stores **input** spectra
per channel, so every operand is already present and in the right domain — the
change is to the accumulation, not to the transform or the delay line. It is a
bounded change to the most performance-sensitive and most carefully tested
component in the tree, which is a reason to do it deliberately and with the
existing convolution tests extended first, not a reason to avoid it.

#### Individualisation: do not build

Photo-based and anthropometric HRTF fitting is the most heavily marketed thing
in this area and the least supported. The individualisation advantage is real in
static anechoic localisation tasks, and it is largely erased by the two things
our renderer will always have:

- **Head movement** substantially reduces the measurable advantage of
  individualised over generic HRTFs — replicated across recent studies.
- **Room reverberation** reduces it to the 2.2-point difference above.

Worse for us: the standard objective metric (spectral distortion) correlates
imperfectly with localisation performance, so most published improvements
optimise a number that does not track what listeners hear. And no shipping
consumer product — not Apple's TrueDepth scan, not Sony's ear photos — has
published a listening test. Both ship personalisation *alongside* head tracking,
which has the larger and better-evidenced effect; users are very likely
attributing the tracker's benefit to their ear scan.

It also fails our own rule directly: we have no measurement rig, so we could not
verify it against anything.

**The individualisation that does have evidence is headphone compensation.** The
headphone-to-eardrum transfer function varies strongly above 4 kHz with fit, and
equalising it is a prerequisite for binaural plausibility rather than a nicety.
We already have that feature — the 31-band graphic EQ with AutoEQ import. This
is a UX job: wire the two together and say plainly that one requires the other.

Instead of fitting, ship *selectable* sets and let people A/B.

#### Data: SADIE II, and a licence trap

**SADIE II (University of York) is Apache 2.0** — redistributable, commercial
use permitted, KU100 and KEMAR dummy heads at 1° resolution, available at 44.1 /
48 / **96 kHz**. That is the default HRTF set.

**The ASH-IR-Dataset is CC BY-NC-SA 4.0 and ASH-Toolset is AGPL-3.0.** It is the
best-sounding free BRIR collection and it is the one users will point at. Link
to it; never bundle it, never link the toolset. Worth recording explicitly
because the pressure to ship it will be constant.

SOFA (AES69-2022) is NetCDF-4, which is HDF5 — but this is not the blocker it
looks like: `libmysofa` implements the needed HDF5 subset in ~5–6k lines of
BSD-3 C whose only dependency is zlib's inflate. **And it should never be parsed
in the APO anyway.** Parse in the Qt application, emit our existing
content-addressed IR blob, hand that to the audio side. A malformed SOFA file
then breaks a GUI worker and nothing else. Phase one does not need SOFA at all:
multichannel WAV gets the whole HeSuVi/Impulcifer/ASH ecosystem, and the
14-channel HeSuVi layout is documented (channels 7–12 are contra-ear-first,
which silently mirrors half the soundstage if you get it wrong).

#### Latency honesty

A BRIR contains the acoustic propagation delay from virtual speaker to ear
inside the impulse response — about **8.7 ms at 3 m**, which is real latency
`GetLatency` will not report. Off the RT thread, strip the *minimum onset across
the whole set* (preserving relative ITDs, which is all that matters
perceptually) and add the stripped count to the reported figure. Small change,
and it keeps a contract we have been careful about everywhere else.

#### Crossfeed: replace bs2b with a closed-form model

bs2b is a one-pole lowpass plus a compensating shelf, and its ITD is a byproduct
of the filter's phase. That byproduct has the right sign — real head ITD is
about 1.5× larger below 500 Hz than above — so it is a crude model of the right
thing. But every serious product moved past it: 112dB Redline Monitor, Goodhertz
CanOpener and Sonarworks all expose *geometry* (speaker angle, distance, phantom
centre level) and add room simulation.

The upgrade that fits this architecture better than anything else in this brief
is the **structural / rigid-sphere model** (Brown–Duda; Duda–Martens):
frequency-dependent ITD from the Woodworth/Kuhn formula (`a(θ+sinθ)/c` high,
`3a·sinθ/c` low), head shadow as a one-pole/one-zero whose positions are
analytic functions of incidence angle and head radius, optionally a shoulder
echo as one delayed tap.

Why it is our kind of feature:

- **Closed form** — verifiable against the analytic rigid-sphere scattering
  solution, which is exactly the standard every other stage is held to.
- **No IR blob at all.** No load path, no content addressing, no memory.
- **All parameters are bounded scalars**: head radius 7–11 cm, azimuth 0–90°,
  distance 0.3–5 m, amount 0–100 %. About 20 bytes.
- **~0.05 MFLOP/s** — two orders of magnitude cheaper than anything else here.
- Zero latency, if the ITD is applied only to the contralateral path.
- It gives users Redline's actual controls instead of bs2b's opaque "cut
  frequency" and "feed level".

One honesty note for the UI: I found no blind-listening evidence that crossfeed
is *preferred* at all. Its rationale is sound and the reported reduction in
fatigue on hard-panned recordings is plausible, but it is not established.
Default it off, keep the amount continuous, and do not claim in the interface
that it is objectively correct.

#### Head tracking

Perceptually the strongest single effect available — it improves externalisation
(and partly substitutes for a long reverb tail), collapses front/back confusion,
and is the thing personalisation gets credit for. The latency threshold is
forgiving: detection at 60–70 ms for the best listeners on isolated sounds,
~35–45 ms with a low-latency reference available, and *complex scenes are 10 ms
more forgiving than simple ones*. Compare 2.8 ms for visual latency in AR.

Our honest budget: wired USB tracker with small buffers lands at 30–45 ms, which
is comfortable. A Bluetooth tracker with default Windows buffering lands at
70–100 ms, which will be perceived as a lagging soundstage — Waves' "no latency"
claim for its BLE clip-on is not credible against a 15–30 ms BLE HID floor.

Interpolating between measured directions must not be done by crossfading raw
HRIRs — that comb-filters, because onsets differ by up to 700 µs. The correct
chain is: extract ITD to a scalar, convert the residual to minimum phase (this
is what takes 512 taps down to 32–128), interpolate the short magnitudes,
re-apply minimum-phase reconstruction, then apply the ITD as a fractional delay.
**We already have the last step** — the sub-sample per-channel delay is exactly
an ITD applicator.

It does collide with the parameter channel: orientation at 100 Hz cannot go
through a file. It needs a second transport — a small shared-memory section with
a seqlock carrying `{seq, yaw, pitch, roll}`. That is still bounded scalars,
still validated, still no coefficients; it just needs a channel that is not a
file. Keep a *static* orientation offset in the normal block so users without a
tracker can rotate the soundstage, and treat live tracking as an overlay.

The product problem is that almost nobody owns a tracker. Support `opentrack`'s
UDP output and Supperware's USB-MIDI — both open, both free to implement — and
build no hardware and no webcam tracker of our own.

#### Ambisonics: do not build

Nothing arriving at a Windows endpoint is ambisonic. Encoding a channel bed to
B-format and decoding it back is a lossy round trip that is strictly worse and
more expensive than convolving the channels directly. The one legitimate use —
cheap sound-field rotation for head tracking — does not apply either, because
with a channel bed you can simply re-look-up the HRTFs for the rotated speaker
directions.

---

### 4.2 Acoustic measurement and room correction

**The architectural conclusion first, because it is unusually clean: room
correction needs almost nothing from the APO.** Measurement, deconvolution,
averaging, smoothing and filter design are all control-application work. The
audio side has to provide exactly three things, and none of them is new DSP:

1. **A guaranteed, acknowledged bypass.** `enableMask = 0` is already documented
   as fully transparent. Push it, bump the generation, and **wait for the status
   block to confirm that generation was applied** before emitting a sample.
   Without the acknowledgement you will occasionally measure the correction you
   are trying to design, and the second iteration diverges. `apoInSync()`
   already computes exactly this comparison.
2. **The endpoint's live mix format** at measurement time. `AudioDevices`
   already surfaces it.
3. **The ability to drive one output channel at a time** — done by rendering a
   multichannel buffer with silence elsewhere, *not* by reconfiguring the
   channel matrix, which would make the APO non-transparent during measurement
   and defeat (1).

Nothing else. No measurement mode in the APO, no injection point, no new
real-time code, no change to the wire format.

#### The highest-value feature is not swept sine

It is **moving-microphone pink-noise power averaging below 300 Hz, fitted to
peaking biquads in the existing 32-band equalizer.** Play pink noise, have the
user walk the microphone slowly around their head for 30 seconds, accumulate a
Welch power average.

Why it wins on effort-to-value:

- No deconvolution, no timing reference, **no clock-drift correction** — power
  spectra do not care about either.
- Thousands of spatial samples instead of nine, and **no phase to cancel**, so
  the complex-averaging failure below cannot occur.
- It needs a noise generator, a capture stream and `Fft`. That is all.

Its limitations are real and are exactly tolerable: no phase, no impulse
response, no time-of-flight, so it cannot drive delay alignment or windowing.
Below the transition frequency — which is the only place automatic correction
should be operating — none of that is needed.

#### The averaging choice is not a preference

Given N measured transfer functions, the three ways to combine them fail
differently, and only one is right:

- **Complex average** — above a few hundred Hz the phases at different positions
  are effectively random, so the sum tends to zero. It **invents nulls that
  exist at no position in the room**, and inverting those is catastrophic. Never
  expose this.
- **dB average** — a single deep null drags the mean down hard (a −30 dB null in
  one of five positions moves the average −6 dB), producing systematic
  over-boosting.
- **Power (RMS) average** — the energetically correct spatial average; nulls are
  appropriately de-weighted.

Complex *invents* nulls, dB *over-weights* them, power *under-weights* them. For
deriving a correction, under-weighting is the conservative and correct error.

#### Do not invert nulls, and bound the inverse in the log domain

A null is destructive interference: boosting at the null boosts the reflection
too, so the cancellation survives and the headroom is gone. A measured 17 dB dip
can need 24 dB of gain to fill, and the measurement *understates* it because
smoothing masks the depth.

The textbook bound is Kirkeby/Nelson regularised inversion,
`H⁻¹ = conj(H)·C / (|H|² + β)`, with maximum gain ≈ `1/(2√β)`. We do not need
it. Log-domain clamping gets 90 % of the benefit in a tenth of the code: clamp
the dB error, **re-smooth after clamping** (a hard clamp introduces a kink that
rings), and taper to 0 dB at the band edges.

Proposed limits: **+3 / −12 dB below 300 Hz, +2 / −6 dB above.** Then compute a
global trim offline so the result cannot clip. Do not let the limiter catch
correction overshoot — that turns a design error into an audible one.

#### Minimum phase only

| | Fixes | Cannot fix | Cost |
|---|---|---|---|
| Minimum phase | magnitude; ringing of isolated minimum-phase modes | arrival times, non-minimum-phase nulls | **block latency only** |
| Linear phase | magnitude, constant group delay | same | **half the filter length**, and half the ringing arrives *before* the transient |
| Mixed phase | magnitude + bounded excess phase | anything spatially unstable | ~7–8 ms (Dirac) |

The arithmetic kills linear phase outright. Resolving 20 Hz needs ≥50 ms of
filter; a Q=10 correction at 30 Hz decays over ~106 ms. A symmetric filter that
long has **~85 ms of pre-response and 85 ms of latency**.

A minimum-phase filter of the same length, through our existing convolver, costs
**the block latency and nothing else** — and `convBlockForRate()` makes that a
constant:

| Rate | Block | Latency |
|---|---:|---:|
| 44.1 kHz | 256 | 5.80 ms |
| 48 kHz | 256 | 5.33 ms |
| 96 kHz | 512 | 5.33 ms |
| 192 kHz | 1024 | 5.33 ms |

**5.33 ms regardless of filter length**, because the filter is minimum phase and
the convolver is uniform-partition overlap-save. That is the cheapest correct
answer available and it requires no new DSP at all.

On correcting the room's *excess* phase: don't. Above the transition it is
dominated by reflection nulls — non-minimum-phase zeros — and inverting a zero
outside the unit circle gives a pole outside it: an acausal, exponentially
growing sequence that then gets truncated. Move your head 5 cm and the null
moves; the truncated tail is then uncorrelated with the signal and audible as
pre-ringing. The audibility evidence also cuts against it: Liski, Mäkivirta &
Välimäki (IEEE/ACM TASLP 2021) find group-delay thresholds of −0.56 ms and
+0.64 ms over 500 Hz–4 kHz, but with a worst-case synthetic stimulus, and note
that **negative group delay is more audible than positive** — which is exactly
what an over-corrected acausal filter produces. High risk, low reward.

What *is* legitimate: pure inter-channel delay alignment (always safe, and
`ChannelDelay` already does it).

#### Target curves: ship a control, not a curve

Real numbers: B&K falls to ≈ −6 dB at 20 kHz (≈ −0.85 dB/oct); DRC-FIR's `bk`
target is linear to 400 Hz then −1 dB/oct; Harman/Olive 2013 found an average
preference of a **+6.6 dB shelf below ~105 Hz and −2.4 dB above 2.5 kHz**;
Toole reports 5–6 dB of natural bass-to-treble fall in a typical small room.

**The most important number here is a variance, not a mean.** Olive found
**17 dB of spread in preferred bass and 11 dB in treble across eleven
listeners.** A single "correct" target is not defensible. Ship presets plus two
live controls — tilt in dB/octave and bass shelf gain + corner.

Toole's operative conclusion is the one to encode as a default: **limit
automatic correction to roughly 20–400 Hz**, and treat anything above that as a
deliberate tonal preference rather than a correction. Above the transition the
ear separates direct from reflected sound and largely hears through the room;
flattening the steady-state curve up there degrades the speaker's direct sound.

This is nearly free for us: a target curve is a `GraphicCurve`, and
`parseGraphicCurve` / `resampleCurve` already read and log-interpolate exactly
that `freq gain` format — as does a microphone calibration file, which is the
same shape.

#### Microphone calibration, and why it matters less than it looks

An uncalibrated UMIK-1-class microphone is broadly fine in the midrange; its
errors concentrate above 10 kHz (reports of ~10 dB by 20 kHz) and below 30 Hz.
For a correction that works below 300 Hz and only tilts gently above, that costs
very little of what matters — but it will tempt an unbounded correction into
demanding +8 dB at 18 kHz. **The gain limit protects us from an uncalibrated
microphone more effectively than a calibration file does.** Load the file anyway
(it is ~40 lines against existing infrastructure), and note there are two — 0°
for stereo, 90° for multichannel — and using the wrong one costs several dB
above 5 kHz.

#### If swept sine is built later

Synchronised exponential sine sweep, not MLS: ~15–20 dB more usable dynamic
range, and it *separates* harmonic distortion instead of smearing it into the
noise floor. The m-th harmonic lands at `−L·ln(m)` — **before** the linear
response — so a window from −5 ms removes all distortion for free.

That gives a sweep-length rule: to keep the 2nd harmonic's own room tail out of
the linear response, `T ≥ RT60·ln(f₂/f₁)/ln 2`. For RT60 = 0.5 s over
20 Hz–20 kHz that is **T ≥ 5.0 s**; use 10 s.

The problem everyone underestimates is **clock drift**, not latency. Consumer
crystals are ±20–50 ppm, so a playback device and a USB microphone can differ by
~100 ppm — **1 ms over a 10 s sweep, which is 48 samples, which is 20 cycles at
20 kHz.** The top two octaves decorrelate entirely. Rather than REW's loopback
cable, bracket the emitted signal with two 200 ms marker chirps: the ratio of
measured to rendered span *is* the clock ratio, correctable with our existing
Kaiser resampler, and the first marker also supplies the common t = 0.

Frequency-dependent windowing, when it comes, has a tidy parameterisation: the
Acourate/Audiolense figures (750 ms at 20 Hz, 15 ms at 1 kHz) and REW's
community default are **the same setting — 15 cycles**. 10–20 cycles is the
usable range; beyond 30 you begin correcting comb filtering from individual
reflections, which is valid at one point in space and worse everywhere else.

#### What not to build

Linear-phase or mixed-phase correction (above). Pole-zero modal equalisation —
mode identification from one noisy measurement is fragile and a matched-Q
peaking cut gets most of it with a hundredth of the risk. MIMO/ART-style
multi-subwoofer optimisation — genuinely different technology, but it needs a
full source × position measurement matrix and our users are 2.0 or 5.1 on one
endpoint. MLS, at all.

---

### 4.3 Stereo image, upmix, downmix, decorrelation

#### The constraint, again

A post-mix APO cannot change channel count. "2 → 5.1 upmix" can only mean: the
user already has a 5.1 endpoint, Windows has padded the surrounds with silence,
and we redistribute channels 0/1 into 2–5. That is a legitimate feature and the
interface must say what it requires. On a stereo endpoint the whole area is
inert.

#### The cheapest wins are compositions of what already exists

**Downmix presets are pure configuration of the existing channel matrix.**
BS.775 / ffmpeg defaults:

```
L_out = FL + 0.70711·FC + 0.70711·BL
R_out = FR + 0.70711·FC + 0.70711·BR
```

with **LFE dropped by default** — it is recorded 10 dB below playback level, so
folding it in naively is 10 dB too quiet and "correcting" it blows the headroom
on content the stereo speakers cannot reproduce anyway.

One trap worth copying *away* from: ffmpeg normalises a downmix matrix by the
worst-case row sum only when the output format is integer. For float output no
normalisation happens at all — and the default 5.1→2.0 row sum is 2.414, i.e.
**+7.65 dB of potential gain**. We are float end to end. But dividing by 2.414
unconditionally costs 7.65 dB of loudness on every downmix, which is why
"downmixed 5.1 sounds quiet"; the bound is only reached when L/C/R are fully
correlated. So: offer off / static / metered.

**Bass management is three shipped stages in a trench coat**: LR4 at 80 Hz per
channel, the matrix to sum lows plus LFE into channel 3, `ChannelDelay` to align
the sub. LR4 is the right crossover precisely because its high and low outputs
sum to flat magnitude with an all-pass phase rotation, where two Butterworth
sections would sum +3 dB. The only missing piece is a per-channel multi-instance
LR4 — plumbing, not DSP.

Channel order, which is worth writing down because the 7.1 case is a trap:
`FL FR FC LFE BL BR SL SR`, **LFE at index 3** in both 5.1 and 7.1 — and
`KSAUDIO_SPEAKER_7POINT1_SURROUND` puts *back* L/R at 4/5 and *side* L/R at 6/7,
the opposite of what the physical layout suggests.

#### RACE, and why the algebra is the whole story

RACE (recursive ambiophonic crosstalk elimination) is published as a recursion:

```
Lout[n] = Lin[n] − g·Rout[n−d]
Rout[n] = Rin[n] − g·Lout[n−d]
```

Taking sum and difference collapses it entirely:

```
M_out = M_in / (1 + g·z^−d)
S_out = S_in / (1 − g·z^−d)
```

**It is a pair of complementary one-pole comb filters on mid and side** — a
frequency-dependent width control with a particular shape, derived from an
acoustic argument. That single step explains everything about it. With the
published parameters (A = 2 dB, D = 90 µs):

| Frequency | Mid | Side | Width change |
|---|---:|---:|---:|
| DC | −5.1 dB | +13.7 dB | **+18.8 dB** |
| 2 kHz | −3.6 dB | +0.2 dB | +3.8 dB |
| 5.56 kHz | +13.7 dB | −5.1 dB | **−18.8 dB (inverted)** |

So the low bypass is **structural, not tuning**: without it RACE applies +18.8 dB
of width at DC and is unusable broadband. The high bypass is structural too —
above 1/(2D) the effect reverses sign and starts boosting the centre. The
Equalizer APO community config bypasses above 5 kHz, which is exactly 1/(2D) for
D = 100 µs; they arrived at the right number empirically without deriving it.

It costs under 20 ops/sample and needs a 60–120 µs delay — 2.9–5.8 samples at
48 kHz — which our 3rd-order Lagrange `ChannelDelay` already provides above its
one-sample minimum. And there is a genuine differentiator here: **Equalizer APO
structurally cannot implement the real recursive version** because its config
language has no feedback; the community config is a single-shot approximation.

Ship it labelled honestly — it assumes speakers ≤24° apart, degrades off the
median line, and is actively harmful on headphones — and hard-interlock it
against crossfeed, which is the headphone-side dual of the same problem.

#### Decorrelation: velvet noise, and where it may be applied

The best cost/benefit item in the whole survey. A velvet-noise sequence contains
only −1, 0 and +1, sparsely placed, so convolution has **no multiplies** if the
positive and negative impulse indices are stored separately. With the published
parameters — density 1000 impulses/s, 30 ms length, 4 equal segments at gains
0.85 / 0.55 / 0.35 / 0.20, linear distribution — the cost is:

| Method | ADD | MUL | Total |
|---|---:|---:|---:|
| FFT convolution | 148 | 104 | 252 |
| Exponential VND | 30 | 30 | 60 |
| **Segmented VND** | **30** | **4** | **34** |

34 ops/sample, **zero latency**, and — the detail that makes it fit our rules —
the state is **30 impulse indices per channel regardless of sample rate**,
because density is defined in impulses per second and 30 ms at any rate is still
30 impulses. Generated once in `prepare()`.

**But mono compatibility governs where it may be used**, and this is the rule
that kills most naive wideners:

| Method | Mono sum |
|---|---|
| Velvet noise, different sequence per channel | **Bad** — dense comb across the spectrum |
| Velvet noise on *derived* channels (upmix surrounds, downmix fold-in) | **Irrelevant** — it is a new channel, not a modification of L/R |
| Allpass chains, randomised phase | Level-safe, but transient-smearing ("boings") |
| Lauridsen shuffler | **Safe by construction** — the added signal is antisymmetric and cancels in the sum |
| Complementary comb (`L·H`, `R·(1−H)`) | **Bit-exact identity** for correlated content |
| Haas / fixed delay one side | **Catastrophic** — a single comb on the sum |

The general principle: *anything antisymmetric in L/R cancels in the mono sum.*
Complementary constructions are safe; independent-per-channel constructions are
not. So velvet noise is superb for upmix surrounds and downmix fold-in, and must
never be sold as a stereo widener.

#### Width: three bands, and a defence of the existing code

Nothing in the survey beats mid/side side-gain by enough to matter, and no
listening test was found showing that FFT "spectral widening" outperforms a
well-implemented frequency-dependent mid/side. The worthwhile upgrade is small:
**three bands with independent width** (below ~250 Hz where the head is not an
effective baffle and wide bass only blurs; 250 Hz–2 kHz, the ITD region, left
alone; above 2 kHz, ILD-dominant, where widening is effective and relatively
mono-benign). Two extra Linkwitz-Riley splits and two multiplies. It also
subsumes both "mono bass" and the Blumlein shuffler as parameter settings —
a shuffler is simply low-band width > 1.

**One correction to the research.** It flagged `StereoWidener::process` for
discarding the low band of the side signal rather than folding it into mid,
calling it a net energy loss. The code is right and the suggestion is wrong:
below the corner we want `L = R`, and discarding the low side achieves exactly
that (`L = R = mid`). Folding `lowS` into mid would add `(L−R)/2` to *both*
channels, doubling the left channel's bass and cancelling the right's. The
energy loss is inherent to mono-ing bass — that is what the feature is. Recorded
here because it will be suggested again.

#### Upmix: one estimator, and a result that says the estimator barely matters

If a 2→5 upmix is built, the method is Vickers' geometric decomposition, which
collapses to something almost embarrassingly cheap. With `S = X_L + X_R` and
`D = X_L − X_R` per STFT bin:

```
C = S · 0.5·(1 − |D|/|S|)
L = X_L − 0.5·C,   R = X_R − 0.5·C
```

A single real gain on the sum signal. No matrix inversion, no covariance
tracking, and — unusually — the paper reports it needs **no inter-frame
smoothing at all** at frame sizes of 4096–8192, which removes the forgetting
factor that causes transient artefacts elsewhere. Two details from the paper
worth keeping: when the inputs are anti-phase `|C|` goes negative, and *keeping*
the negative value beats clamping (clamping causes musical noise); and the
magnitude-similarity refinement should be clamped to [0.1, 0.9].

The decisive finding is from Fraunhofer's listening test (Paulus & Torcoli):
they compared about seven primary/ambient decomposition algorithms in one
framework and found *a group including theirs performed equally well with only
small per-item differences*. **The choice of estimator barely matters** — so
take the cheapest one without obvious artefacts. Their test also gives the
default: median preferred rear-to-front ratio **−10.2 dB** (instrumental −5.8,
speech −14.5).

The cost is not CPU (1–3 % of a core) — it is **latency**: N = 4096 at 48 kHz is
85 ms of window and ~43 ms of added algorithmic latency, and it would be the
first stage in DreamDSP that is not sample-synchronous with the host block. That
is the reason it ranks below everything else here.

---

### 4.4 Machine learning, and where it is allowed to live

The conclusion is a rule rather than a feature list, and it is the most useful
thing in this document: **draw the line by what the model emits, not by how
clever it is.**

| Model output | Delivery | Verdict |
|---|---|---|
| **An impulse response** | existing `ImpulseBlob` → `Convolver` | **Free.** An FIR is unconditionally stable and its gain is bounded by its L1 norm — cheaply validatable, unlike the ViPER biquads we declined. Room correction, headphone correction and personalisation all land here. |
| **Bounded scalars** (a target curve, a band gain) | existing `ParamSlots` | **Free.** An analyser that sets the EQ. |
| **Samples** | — | **Requires the model inside `APOProcess`**, and then everything below applies. |

Note that this rule was already written, in criterion 2: *"if the thing it needs
to send is a filter, a network, or a program, the answer is no."* A user-loadable
model is 2.9 MB of unvalidatable opaque numbers — the ViPER DDC objection at
scale. A model compiled into the DLL as a `const` array is not crossing the
boundary at all; it is code, like the RBJ design tables.

#### ONNX Runtime does not go in `audiodg.exe`

Not a judgement call. The research dumped the import table of the inbox
`onnxruntime.dll` (10.6 MB) and found it importing **`d3d12.dll`, `dxgi.dll`,
`DirectML.dll`, `ext-ms-win-dxcore`**, plus file I/O, dynamic library loading,
environment variables, COM and ETW telemetry. Into a LOCAL SERVICE process that
mediates all audio on the machine.

Three more, each independently sufficient:

- **Microsoft documents this exact failure.** From the APO implementation guide:
  an embedded manifest *"triggers the use of certain APIs which are forbidden
  within a protected environment… your APO will fail to load, even if it is
  WHQL-signed."*
- **Its threading model is hostile to a real-time thread.** Default
  `intra_op_num_threads = 0` creates one thread per physical core with affinity,
  and `allow_spinning` is **on by default**. Spin-waiting worker threads
  contending with the MMCSS audio thread is precisely the pathology an APO must
  never introduce.
- **Its memory and error models violate the APO contract.** The arena allocator
  never returns memory to the system and its shrinkage scan is mutex-protected —
  a lock on the audio path. And eliminating C++ exceptions requires
  `--minimal_build`, after which errors *"log the error message and call
  `abort()`"*. `abort()` in `audiodg.exe` means the machine loses its sound.

DirectML and WinML are worse: a D3D12 device from session 0, a PCIe round trip
inside 10 ms, and TDR driver reset as a live failure mode.

**The decisive external evidence is that Microsoft made the same call.** Windows
Studio Effects — including ML noise suppression — does not run as a CPU APO in
audiodg. It requires an **NPU**, is applied below the audio engine via Kernel
Streaming, and is **capture-only**. Microsoft owns the OS, the audio stack and
the models, and still put its ML audio effect on dedicated silicon behind a
driver.

#### Hand-written inference is smaller than the integration would be

For a project that vendors no third-party DSP, the numbers are encouraging.
RNNoise's complete inference engine is **2,202 lines of BSD-3 C** (~1,318
portable scalar, ~884 of AVX2/VNNI int8 kernels), and the whole library
including FFT, pitch and the denoise front-end is 4,801. The op set is small:
dense linear, GRU, 1-D convolution, and the usual activations. No runtime, no
DLL, no loader, no thread pool, no allocator, no exceptions.

#### The one model with a closed-form safety guarantee

RNNoise emits **32 sigmoid band gains**, clamped to ≤ 1, and the final operation
is literally `X[i] *= gain[i]`. **The network can only attenuate: |out| ≤ |in|
per band, by construction, whatever the weights say.** It cannot be unstable and
cannot exceed full scale.

That is a guarantee of exactly the kind criterion 5 demands, and **it is the only
ML topology in the survey that has one.** DeepFilterNet's second stage applies a
network-predicted complex 5-tap filter — arbitrary coefficients, output not
bounded by input. That is the ViPER DDC problem again, regenerated at 100 Hz.

Two corrections to the folklore, both measured rather than cited:

- **"RNNoise is tiny" expired in 2025.** The current shipped weights count
  **2,877,312** — 33× the 87,503 everyone quotes, putting it in DeepFilterNet2's
  complexity class. If we ever vendor it, vendor the **2018** model and say so.
- **Below ~1 M parameters the cost is dispatch, not arithmetic.** Effective
  throughput ranges from 3.08 GFLOP/s (hand-written scalar C, 2013 laptop) to
  17.8 (optimised Rust) down to **0.94** (streaming PyTorch, 2022 desktop). A
  48K-parameter model in PyTorch extracts a third of what hand-written scalar C
  got on a chip nine years older. Published RTFs for tiny models are therefore
  pessimistic, and MACs do not predict cost at this scale.

**But it does not apply to us as we stand.** Every model in this class is trained
to separate one speaker from noise. On a render endpoint it would apply
speech-band gating to music and film — actively destructive, with no literature
supporting it because nobody wants it. **DreamDSP is render-only. This becomes
relevant if and only if a capture path is built.**

#### Source separation, bandwidth extension, declipping: no

**Separation** fails four ways independently. The cheapest CPU-measured *causal*
separator costs ~48 % of one modern core — **20× our entire existing chain** —
at 92 ms latency for 7.79 dB SDR, and is unreleased. The 23 ms options cost
17–40 % of a core for 4.5–5.2 dB, which is audibly artefact-laden. Causality
alone costs 1–2 dB SDR. And the checkpoints that reach usable quality are
explicitly non-commercial: Demucs' author writes that the weights *"are not
covered by the MIT license, and are provided only for scientific purposes"*;
Open-Unmix UMX-L is CC BY-NC-SA. The "real-time" DJ products are pre-analysis
with caching — Serato's own documentation says tracks are *"pre-analysed"*.

**Bandwidth extension** is an exciter with a network inside, and the evidence
base cannot distinguish the two. Every impressive result in the literature is
*guided* (SBR inside HE-AAC, or paired with the codec's own bitstream); we see
final rendered PCM, so only *blind* BWE is available and the results do not
transfer. Objective metrics are documented as unreliable here — in AudioSR's own
evaluation the model with the **best** LSD had the **worst** subjective score.
**No study anywhere compares neural BWE against a well-tuned exciter**, and
nearly all testing uses synthetic brick-wall lowpass on critical-listening rigs
rather than real lossy artefacts on laptop speakers. Our exciter costs 0.29 %,
is causal, and is verifiable.

**Declipping** is blocked by information, not compute: it requires knowing where
the waveform was going, which requires lookahead past the clipped run. The
classical methods are iterative sparse recovery with unbounded time per block.

**What people actually mean when they ask for declipping is a true-peak
limiter** — and that is causal, tractable, and nearly free: ITU-R BS.1770
intersample peaks via the existing `Oversampler2x`, detected and attenuated by a
limiter whose threshold is already an exact guarantee.

#### Where ML genuinely belongs

**Blind room-response estimation in the Qt application, emitting an impulse
response into the convolver.** Inferring the room's response from ordinary
program material, with no measurement ritual and no microphone dance. It uses
`LoopbackCapture`, `ImpulseBlob` and `Convolver` — all built and tested — carries
zero new real-time risk and zero new dependency, and the artefact crossing the
boundary is an FIR, which is validatable.

**The open question that decides it: how accurate is blind RT60 / response
estimation from arbitrary program material?** That single number is worth
answering before committing, and it is the highest-value follow-up in this
document.

---

### 4.5 Loudness, levelling and dynamic range

#### The distinction the whole area turns on

A **leveller** measures perceptual loudness over seconds and moves one gain
slowly enough that nothing inside a phrase changes shape. A **compressor**
measures a rectified envelope over milliseconds and changes the shape of the
waveform on purpose. Anything whose gain can move appreciably within one
syllable is a compressor no matter what the panel calls it.

Orban's Optimod-TV manual states it from the other side, about their own
product: the AGC ahead of the multiband *"makes audio levels more consistent
without significantly altering texture"*, while the multiband *"audibly
change[s] the density of the sound"*. That is the same sentence written by
people who ship these for broadcast.

#### What rate is inaudible — and how weak the evidence actually is

Four independent implementations converge on the same band for steady-state
movement:

| Source | Rate |
|---|---|
| Orban AGC, "effectively freezes" | 0.5 dB/s |
| Orban AGC, "open, natural and non-fatiguing" | 2 dB/s |
| ffmpeg `af_loudnorm`, the `1.0058` per-100 ms creep | 0.5023 dB/s |
| ffmpeg `af_dynaudnorm` at defaults, peak for a 10 dB correction | 1.54 dB/s |
| AC-3 / AC-4 release, 3 s and 10 s time constants | 0.4–1.4 dB/s |

**The convergence is the evidence, and it is not a study.** No controlled
listening test on ramp-rate audibility surfaced. The nearest psychoacoustics —
amplitude-modulation detection thresholds, which put the threshold at roughly
0.5–0.8 dB peak-to-peak for broadband noise — only becomes a *rate* once you
assume an integration window, and that assumption is ours, not a published
one. Treat 1 dB/s as a well-supported convention rather than a measured
threshold.

Two design details are worth taking wholesale because they are the difference
between inaudible and breathing:

- **Orban's target-zone window.** Inside a 3 dB dead zone the release slows to
  0.5 dB/s or freezes, so material that is already even is left alone. Their
  manual is explicit that this is what stops the AGC "applying additional,
  audible gain control to material that is already well controlled".
- **Hard gating.** Below the gate the gain *freezes* rather than decaying to
  unity. A leveller that unwinds during a quiet passage has to redo the work on
  the next entry, which is exactly when it is most audible.

One correction to the standards, which we act on: both AC-4 and ffmpeg smooth
the **linear** gain, which makes a boost slew about three times faster in dB
than a cut of the same size for the same time constant. That asymmetry is an
artefact of the domain, not a choice anyone made. We smooth in decibels.

#### The Dolby DRC curves are obtainable, and they are not where people look

"Night mode" on a Blu-ray player is one of five named compression profiles.
**They are not in the AC-3 specification.** ATSC A/52 standardises only the
transport of the resulting gain word — `dynrng` (±24 dB, 0.25 dB steps, per
5.3 ms block) and `compr` (±48 dB, 0.5 dB steps, per 32 ms frame, and the one
to prefer when peak level must be constrained). Grepping the full text of
A/52:2018 for "film standard", "null band", "boost ratio" returns nothing.

The curves themselves are published in two places that agree: Dolby's own
metadata guide, and **ETSI TS 103 190-1 Table 161**, which restates them
parametrically inside the AC-4 standard so a transcoder can reproduce them.
ETSI is the better source — it also carries the time constants Dolby's guide
omits, and cross-checking the two exposes **two arithmetic slips in Dolby's own
Film Light row** (the early cut begins at −21, not −26, and the cut range ends
at +9, not +4; both follow from Dolby's own stated null-band width).

Also worth recording as a trap: the open-source AC-3 encoder `aften` implements
the profiles, and its Speech `boost_ratio` is `1/R` where the curve needs
`1 − 1/R`. Its Speech profile therefore delivers **3.8 dB of maximum boost
instead of the specified 15**. The four 2:1 profiles are unaffected because
`1/2 = 1 − 1/2`, which is precisely why the bug survived.

And one implementation detail that will silently make a smoother 44% too fast:
AC-4 writes `alpha = 2^(-T/tau)`, so **tau is a half-life, not the 1/e time
constant every compressor textbook uses.**

#### Verdict

Ship a leveller and the five DRC curves. Both are cheap, both are exactly
specified by obtainable documents, and both do something no existing stage
does. The leveller is the one users will actually keep on.

---

### 4.6 Psychoacoustic enhancement, and what is really in these boxes

#### Virtual bass: the mechanism is sound, the implementations are not

Residue pitch is real and old (Schouten; Ritsma). Two results constrain the
design directly. The **dominance region** — harmonics 3 to 5 carry the pitch —
means a 40–80 Hz fundamental is represented by energy at 120–400 Hz, which is
exactly what a small speaker *can* reproduce. And the **lower limit of melodic
pitch** at about 30 Hz means there is nothing below that to evoke.

The evidence on how well it works is worth reading rather than citing. The best
current method (Moliner, Rämö & Välimäki, DAFx-20) ran a webMUSHRA test, 11
retained subjects, four genres. Mean scores against a reference at 98.6:

| | Proposed hybrid | Phase vocoder | Hill hybrid | NLD | **Do nothing** |
|---|---|---|---|---|---|
| Jazz | 50.9 | 23.1 | 24.5 | 30.5 | **40.2** |
| Average | 47.0 | 36.2 | 33.1 | 29.4 | 21.1 |

Read the jazz row. **On acoustic bass, doing nothing beat the phase vocoder,
beat Hill's hybrid, and beat a plain nonlinear device.** Nothing anywhere
reached 51 against a reference at 98.6. This is the state of the art evaluated
by its own authors. Virtual bass is damage limitation, not a free win.

Three findings that change our implementation:

1. **Even symmetry is a correctness bug, not a taste question.** An
   even-symmetric shaper (`|x|`, `x²`, a full-wave rectifier) emits only even
   harmonics, whose greatest common divisor is 2f₀ — so the residue pitch lands
   *an octave above the note*. Our shaper is an asymmetric `tanh`, so we were
   never in this trap, but it is worth writing down that we are not.
2. **Three harmonics, not more.** The one controlled experiment on this
   parameter alone found 2 vs 3 statistically indistinguishable, and 3 vs 4
   significant in *both* directions — more bass perception, worse quality — on
   every programme item. Its authors "do not recommend using more than three".
3. **Harmonic balance moves with level, badly.** Measured on a comparable
   shaper, the second harmonic shifts **26.6 dB relative to the fundamental
   over 21 dB of input level**. There is no cleverer curve that fixes this: a
   memoryless nonlinearity that is exactly level-invariant is degree-1
   homogeneous, which forces every odd harmonic above the fundamental to
   vanish. The fix has to be an explicit normaliser in front of the shaper.

Aliasing, by contrast, is a non-issue here: measured against a 32× reference,
the sub band is 113–121 dB clean at 1× even on two simultaneous notes at
extreme drive.

#### Exciters: the patent's actual idea is the threshold

US 4,150,253 is narrower than the marketing. Highpass at 4 kHz, a **diode soft
clipper with an adjustable threshold**, mixed back at 20–70%. The patent is
explicit about why the threshold is there: *"By selecting the proper threshold
only transient portions of the signal become clipped."* That is the difference
between "air on transients" and a constant fizz, and it is one comparator.

Where our exciter genuinely was defective is aliasing. Measured against a 16×
reference on 3–16 kHz noise at realistic drive: **20 dB alias-to-signal at 1×,
47 dB at 2×, 89 dB at 4×.** Those aliases are not harmonically related to
anything and they fold *downwards*, into the midrange, where nothing masks
them. This is the measurable form of the complaint that exciters sound like
treble with grit on it.

**BBE's group-delay story does not survive scrutiny.** The claim is that a
speaker delays low frequencies less than high, so the box adds delay to lows to
cancel it. A real loudspeaker's group delay is dominated by its high-pass
roll-off at the bottom and by crossover phase — lows arrive *late*, so the
network adds to the error. It is a fixed curve facing a target that depends on
the box, the alignment and the room. And the audible part is the bass and
treble shelf that ships alongside it. No independent measurement substantiating
the mechanism was found.

#### The Zwicker metrics are a category error as controls

Sharpness, roughness, fluctuation strength and tonality are standardised (ISO
532, DIN 45692, ECMA-418-2) and implementable. They are also the wrong tool
here, for a reason that is structural rather than an engineering inconvenience:

> **Every one of them takes absolute sound pressure in pascals.** ECMA-418-2
> §5.1.2: *"The input signal is a discrete time signal containing sound
> pressure values."*

Inside `audiodg.exe` we have float samples and no knowledge of the endpoint
volume position, the DAC level, the amplifier gain or the transducer
sensitivity — easily ±40 dB of uncertainty. Since 1 acum, 1 asper and 1 vacil
are each *defined at 60 dB SPL*, and since all four metrics are level-dependent
by construction, a meter we shipped would not be displaying the quantity the
standard defines.

Three further objections, any one of which is sufficient:

- They are **annoyance metrics for machinery**. A distorted guitar is *supposed*
  to be rough; vibrato is *supposed* to fluctuate at 4 Hz; a cymbal is
  *supposed* to be sharp. Driving them toward zero describes making music
  worse, and there is no published "good" target for music because the concept
  has no referent. The ~50 studies in the SQAT "studies using" list are drones,
  aircraft, wind turbines, fans, heat pumps, EV warning sounds and vehicle
  interiors. **Not one is on music playback.**
- They are **not invertible**. Roughness is a scalar summed over 47 bands from
  squared, clipped, cross-correlated modulation depths. There is no gradient
  onto a filter gain. Sharpness is the one exception, and it is a Bark centroid
   — which maps onto a tilt, meaning you could have computed the tilt directly.
- **Fluctuation strength updates at 2.93 Hz with 1.4 s of latency**, and
  ISO 532-3's reference implementation runs at a real-time factor of about 50.

If musical roughness genuinely interests you, that is a different literature and
a different algorithm — sensory dissonance over partial pairs (Sethares,
Vassilakis), not asper.

#### Verdict

Fix the exciter's oversampling and give it its threshold; normalise the level
into the virtual-bass shaper and cap its harmonic ladder. Ship no Zwicker
metric as a control. Sharpness is the only one worth considering even as a
readout, and it must be labelled relative, because we cannot know the pascals.

---

### 4.7 Clipping, transient shaping, and the DSP infrastructure underneath

#### When a clipper beats a limiter, measured

The practitioner claim — clip the peaks, then limit — turns out to be
straightforwardly measurable. On drum-plus-tone material, tracking the
modulation sidebands the process puts on the *sustained* content:

| Peak reduction | Clipper | Limiter (100 ms release) |
|---|---|---|
| 1 dB | **+0.1 dB** of added slow modulation | **+19.8 dB** |
| 3 dB | +9.2 dB | +31.0 dB |
| 6 dB | +23.4 dB | +37.9 dB |

And the clipper delivers *more* loudness for the same ceiling (+1.00 dB vs
+0.85 at 1 dB of reduction), because the limiter's release never recovers the
full headroom.

The mechanism is duty cycle. At 1 dB, clipping touches **0.02% of samples in
bursts averaging 2.7 samples long**, each sitting under the forward masking of
the transient that caused it. The limiter's gain envelope modulates
*everything* for the whole release time. A sustained sine at 3 dB of clipping
has half of every half-cycle flattened; the same 3 dB on transient-dense
programme touches 0.23% of samples. That ~200:1 ratio is the entire
justification, and it is why sine-based THD figures over-predict audibility
here by an enormous margin.

**The corollary is easy to get wrong**: this only holds for a near-hard knee.
At the same 1 dB of peak reduction, `tanh`, `arctan` and `x/√(1+x²)` each add
**+20 to +24 dB** of modulation — as bad as the limiter — because they compress
everything, always. They are saturators, not peak controllers.

#### The cheapest decibel available

A soft knee about 1 dB wide takes a hard clipper's alias-to-signal ratio at
3 dB of clipping from **51.5 dB to 67.5 dB** for one hundredth of a decibel of
loudness and roughly one extra cycle per sample. That is a better return than
first-order antiderivative antialiasing (58.4 dB) and better than 2×
oversampling the hard clipper (66.7 dB).

*(Our own implementation reproduces this independently: 51.5 dB hard, 69.0 dB
with a 1 dB knee, 82.6 dB adding 2× — see `testClipper` in the harness.)*

#### Why not ADAA

Antiderivative antialiasing is the fashionable answer and it is the wrong one
for us:

- **First-order ADAA at base rate buys only 7–8 dB** — *less than plain 2×
  oversampling*. The widespread claim that it replaces 4× or 8× is false for a
  hard clipper. The published Table 1 that people cite actually says it reduces
  the required rate by 3×, at equal aliasing.
- **It is not transparent.** First-order ADAA reduces in the small-signal limit
  to the two-point average, i.e. `|H(f)| = cos(πf/(M·fs))`. At 48 kHz that is
  **−6.02 dB at 16 kHz**. For anything whose product is high-frequency energy,
  that is disqualifying at base rate, which is why the literature insists on
  pairing it with oversampling — at which point you have paid for both.
- **The epsilon is folklore.** Across five independent implementations of the
  identical algorithm the ill-conditioning threshold ranges over six orders of
  magnitude — 1e-10, 1e-5, 1e-4, 1e-2, 1e-1 — and **neither source paper
  prescribes a value**; both say only "for some threshold ε". In float32 the
  most-cited value (2⁻²⁴, from the mantissa) leaves −37 dB of worst-case error,
  which is grossly audible.
- **The reference implementation is GPLv3.** `chowdsp_waveshapers` is in the
  GPL group, not the BSD group, along with the two modules it depends on.

We are not CPU-bound and we already own a tested 2× oversampler. Buy the
aliasing with oversampling we trust.

#### Transient shaping: no patent, and the level-independence is real

SPL's "Differential Envelope Technology" is a **trademark, not a patent**.
Espacenet, Google Patents and Justia agree that SPL Electronics holds exactly
one patent family and it is the Vitalizer's filter. There is nothing to design
around.

The structure is published in SPL's own manual: **four envelope followers in
two independent pairs**, not two. The attack pair shares a release time and
differs in attack; the sustain pair shares an attack time and differs in
release, with the slow one acting as a peak hold. The inventor's own account
adds the crucial line: *"no threshold was necessary since the difference worked
independently of the input level."*

Three failure modes are worth knowing before implementing:

1. **Linear envelopes are not level-independent.** The most-copied textbook
   implementation computes `gain = 1 + k·(envFast − envSlow)` on linear
   envelopes, so the amount of boost scales with amplitude — the threshold
   behaviour walks back in through the side door. It has to be a difference of
   *logarithms*.
2. **A fixed dB floor is a cliff.** Scaling the input scales every envelope
   except the floor, so near it the ratios stop being ratios. Measured with a
   fixed floor, the same burst 20 dB apart drew gains **3.6 dB apart**; with a
   relative floor, 0.000 dB apart.
3. **Level independence means it chases the noise floor.** On white noise the
   gain range is *identical* at −75 dBFS and −30 dBFS. An absolute gate is not
   a betrayal of the "no threshold" idea; it is the price of it, and every
   shipping implementation has one whether or not it is on the panel.

#### True peak

Our limiter's exact-threshold guarantee was, until now, **sample-domain only**.
A DAC's reconstruction filter draws a continuous curve between the samples that
can overshoot every one of them; measured on a near-worst-case construction,
ours exceeded its own ceiling by **0.90 dBTP**. ITU-R BS.1770 defines true peak
by oversampling, and 4× is the converged industry standard.

This is also the honest answer to "can you undo clipping that already
happened". Classical declipping is sparse recovery: measured real-time factors
run from **14× to 3,600× slower than real time**, and the best neural declipper
explicitly built for low latency still reports 88 ms of algorithmic latency at
16 kHz. What people actually want when they ask for a declipper is a true-peak
limiter, and that is causal, cheap and exact.

---

## 5. What was built, and what was not

### Built

| Change | Why, measured |
|---|---|
| **Transient shaper** (new stage) | Level-independent envelope shaping, which no compressor can do: same burst 20 dB apart draws gain trajectories **0.000 dB** apart. |
| **Soft clipper** (new stage), before the limiter | +0.1 dB of modulation on sustained content at 1 dB of peak reduction, against a limiter's +19.8 dB. A 1 dB knee: **51.5 → 69.0 dB** alias-to-signal; with 2×, **82.6 dB**. |
| **Auto volume** (new stage) | BS.1770 short-term levelling with Orban's dead zone and a dB-domain rate limit. |
| **Night mode** (new stage) | The five published Dolby DRC curves, from ETSI Table 161. All five reproduce their documented ratios and maximum boosts. |
| **Exciter: 4× oversampling** | 20 dB → 89 dB alias-to-signal. The largest measurable defect found. |
| **Exciter: transient threshold** | The actual idea in the Aphex patent. |
| **Exciter and tube: mix inside the oversampler** | A half-wet tube stage was notching its own signal by **17.8 dB at 18.7 kHz**; the exciter, whose wet path is nearly a scaled copy of its dry, was worse. Now 0.00 dB. |
| **Virtual bass: level normaliser** | Harmonic ratios drifted **18.5 dB** over 20 dB of programme level. Now 0.00 dB. |
| **Virtual bass: harmonics capped at 4× the cutoff** | The one controlled experiment on harmonic count. |
| **Limiter: true peak** | The ceiling was exceeded by 0.90 dBTP between the samples. |
| **Limiter: linked final clamp** | The last-resort clamp was per-channel, so the one sample where it engaged could move the stereo image. |
| **Stereo widener: Zotter–Frank phase widening** | Side gain provably cannot widen mono content — at S = 0 the correlation stays 1.0 at any gain. Measured: channels flat to 0.06 dB, mono sum bounded at −2.4 dB, neutral exactly neutral. |
| **Mono-compatibility test** | Would have caught the all-pass mono-safety error immediately. |

Total cost of the four new stages at 96 kHz 7.1: **about 1% of one core**. The
exciter's 4× is the expensive change at 5.4%, and it is only paid when the
exciter is on.

### Deliberately not built

- **Phase-vocoder virtual bass.** Scored *below the do-nothing anchor* on jazz
  in its authors' own listening test, and the latency is unaffordable here.
- **ADAA.** Buys less than plain 2× oversampling at base rate, costs −6 dB at
  16 kHz, and its reference implementation is GPLv3.
- **Zwicker metrics as controls.** They require absolute SPL we structurally
  cannot know, they are annoyance metrics validated on machinery, and they are
  not invertible.
- **Declipping.** 14× to 3,600× slower than real time classically; 88 ms of
  latency at best neurally. The true-peak limiter is what the question is
  actually asking for.
- **Automatic content-adaptive spectral correction.** No listening-test support
  in a playback context, and a clear mechanism for un-doing the mastering
  engineer's intent.
- **Neural inference inside `audiodg.exe`.** ONNX Runtime imports `d3d12.dll`
  and `DirectML.dll`, spin-waits by default, and calls `abort()` on error in a
  minimal build. Microsoft made the same call: Windows Studio Effects requires
  an NPU, runs below the audio engine, and is capture-only.
- **Independent-filter-pair decorrelation.** The belief that an all-pass pair is
  mono-safe is false — two all-passes sum to a full null wherever their phase
  difference reaches 180°. Each channel measures flat, which is exactly why the
  belief survives.

### Still open

- **Blind RT60 estimation** in the Qt application, emitting an impulse response
  into the existing convolver. A 8,737-parameter CNN runs at 110× real time on
  one core with ρ ≈ 0.92 against the ACE benchmark. Accurate enough to pick
  among correction presets; **not** accurate enough to synthesise filter
  coefficients unsupervised, and every paper validates on speech only.
- **True-stereo binaural.** `ConvRoute` is one spectrum per output channel, so
  many-to-one summing is not expressible; the change is to the accumulation
  loop, not the transform.
- **`dsp/BiquadFilter` is float32.** On LR4 crossovers at 192 kHz its own noise
  floor is around −82 dBFS. Promoting it to double is the obvious next
  infrastructure change; `Equalizer` and `KWeighting` already are.
- **Splitting the virtual-bass sub band into 2–3 sub-bands before the shaper**,
  which is the cheapest available attack on intermodulation between
  simultaneous bass notes — the dominant artefact of any nonlinear virtual
  bass, and the thing the entire hybrid literature was invented to avoid.


