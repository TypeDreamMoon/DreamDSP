# MIT KEMAR diffuse-field HRTFs

The embedded measurements are by **Bill Gardner and Keith Martin, MIT Media
Laboratory (1994)**: *HRTF Measurements of a KEMAR Dummy-Head Microphone*,
Perceptual Computing Technical Report 280. Copyright 1994 MIT Media Laboratory.

The [official archive page](https://sound.media.mit.edu/resources/KEMAR.html)
permits research and commercial use without other use restrictions, provided
the authors are cited. This attribution must accompany distributions containing
the data. This is the dataset's own permission; it is not the MIT software license.

Sources checked 2026-10-13:

- [Official diffuse.zip](https://sound.media.mit.edu/resources/KEMAR/diffuse.zip)
- [Technical documentation](https://sound.media.mit.edu/resources/KEMAR/hrtfdoc.txt)
- [FAQ, including diffuse equalisation and retained interaural delay](https://sound.media.mit.edu/resources/KEMAR/KEMAR-FAQ.txt)

Archive SHA256: `a48e73137390626ef1e45d1de6a4d98530c6aa570e71b40f7c4442241ed5fa22`.

`generate_kemar.py` converts the official archive into the C++ header using only
Python's standard library. Pass `--archive <diffuse.zip>` to work offline, or omit
it to fetch the pinned archive. The SHA256, WAV format, filenames, ring counts
and rounded azimuth grid are checked before writing. The header retains the
original integer samples, only separating interleaved left/right channels.
There is no learned component and no additional per-direction normalisation.

The ZIP contains 368 stereo PCM16 WAV files: 44.1 kHz, 128 samples per ear,
14 elevation rings from -40 through +90 degrees. The symmetrical small-pinna
dataset represents 710 full-sphere directions. Clockwise azimuth 90 degrees is
the listener's right; the opposite hemisphere uses mirrored azimuth with ears
swapped. At +/-40 degrees, physical azimuth steps are 360/56 degrees, despite
rounded filename angles. The +50-degree ring has 45 full-circle positions and
no measurement exactly at 180 degrees. The overhead ring has only one position.

DreamDSP model 1 interpolates neighbouring azimuths independently on each of
two elevation rings, then interpolates elevation. Preparation resamples HRIRs
with a 64-zero-crossing Hann-windowed sinc; the common causal padding and native
HRIR delays are preserved, including ITD. Downsampling includes a low-pass cutoff
at the target Nyquist frequency; impulse-response amplitudes include the inverse
sample-rate ratio so filter gain is preserved. Native 44.1 kHz copies PCM samples
exactly, with causal padding. No filter is built or resampled in the audio callback.

These are generic mannequin responses measured at 1.4 metres, with diffuse-field
equalisation. They are not a personalised HRTF or a distance model. Elevation
changes measured pinna spectral cues; localisation and front/back resolution
depend on the listener and headphone response. Positions below -40 degrees are
clamped to the measured boundary. Interpolating unaligned HRIRs may soften
high-frequency notches between measured directions. Lower sample rates remove
some high-frequency elevation cues. Model 0 preserves the earlier horizontal
analytic spherical-head renderer and intentionally ignores elevation.
