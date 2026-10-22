# Factory sound references and auditions

The user approved the final sounds for Unreal integration on 22 October 2026.
Generated and extracted recordings stay under `assets/audio/` and are not
redistributed. Prompt plans and authoring utilities are reproducible code.

## Robot drives: research on 22 October 2026

- [Kollmorgen's KUKA engineering case study](https://www.kollmorgen.com/en-us/service-and-support/knowledge-center/success-stories/even-higher-power-density)
  identifies synchronous permanent-magnet servo motors in KUKA KR Agilus joints.
  This is evidence for industrial servo technology, not identification of the
  exact motors represented by the purchased welding-cell geometry.
- [Nabtesco's automotive robot joint overview](https://www.nabtesco.com/en/products/robot/)
  explains the use of precision reduction gears to multiply motor torque in
  automotive production robots. Its RV reducers support high loads and precise,
  smooth movement. Gearbox and motor contributions both belong in the reference.
- [Performance Motion Devices' servo-noise article](https://www.automate.org/motion-control/tech-papers/how-fix-servo-motor-noise)
  discusses audible noise from servo tuning, resonance and drive-current
  excitation. Chatter is not a necessary feature of a healthy servo mechanism.

The sound-design inference is a restrained continuous motor/gear whirr whose
pitch and level vary with travel speed, with subtler output during slow
positioning. The rejected stepped, musical and hobby-servo textures are poor
references for these large arms. The new recordings are generated interpretations,
not recordings or acoustic measurements of a particular KUKA installation.

## Review revisions

- First pass: 18 effects, `configs/audio_review.json`.
- Revision 2: seven new takes, eleven reused sources,
  `configs/audio_review_v2.json`.
- Revision 3: geared-servo travel alternatives and precision positioning;
  cooling lowered by 9 dB; the prior NIR charging cue shortened to 1.5 seconds;
  thermal derived from that same cue at two semitones lower and the same duration.
  The approved effects retain their revision-2 source and preview levels.

The shorter charging cues preserve their original attack and ending, with an
80 ms equal-power join removing the middle sustain. Thermal uses a small downward
varispeed pitch shift before the same duration edit. These are deliberately
authored interface effects, not noises required by NIR or thermal sensing.

Revision 4 supersedes the two-semitone thermal distinction: thermal is now one
octave below NIR, with the same 1.5-second duration. The NIR preview is unchanged.

The user rejected the generated robot takes and supplied two video references:
[ABB IRB140](https://www.youtube.com/shorts/FtQbSdRHS6s) and
[ABB IRB 6700 at Eurobots](https://www.youtube.com/watch?v=idbnX6iUvtM).
The second video's audio was extracted for private review. It is a 41-second
recording; published metadata describes a 155 kg payload arm with an IRC5
controller. The review contains the 0.5–7.8-second interval with and without
gentle cleanup, plus the 12.0–21.8-second interval. Motor pitch and speed remain
unchanged. Cleanup uses a 70 Hz high-pass and bounded four-decibel spectral
attenuation estimated from a quieter interval; that interval is not certified
noise-only. Room sound remains and the result is not an isolated studio stem.

`configs/recorded_robot_review.json` records the excerpt ranges and editing
recipe. `scripts/audio/prepare_recorded_robot_review.py` takes a local recording
and its `source.json` provenance, verifies its source hash and exports the
review WAVs. Local extraction used yt-dlp and imageio-ffmpeg; processing uses
NumPy/SciPy. Source audio, downloaded dependencies and review files all remain
in ignored `assets/audio/`. No paid generation calls were made for revision 4.

`scripts/audio/generate_sound_review.py` retains source hashes, request provenance,
offline edits and measurements. Identical requests can reuse a previous private
batch with `--reuse-from`; generation requires explicit `--generate` and a local
`--key-file`. Ambiguous paid requests are never automatically repeated. Review
pages provide previous-take comparisons and an editorial process mix, whose
timing is independent of the simulated assembly sequence.

## Unreal integration

`scripts/audio/prepare_runtime_audio.py` packs 20 roles from the approved sources as mono 48 kHz
PCM in private `assets/audio/runtime/bank.dfau`, with source hashes and editing
provenance in the adjacent manifest. Robot movement uses the cleaned recorded
IRB6700 excerpt; it is looped without a pitch or speed change. Welding and clamp
events fit the approved longer takes to the actual mechanism phases.
The other accepted effects use their final review versions, including quieter
cooling and the distinct octave-lower thermal cue. No paid API call is needed
to rebuild the bank.

The timing revision follows the existing pinch motion: 150 ms squeeze, 500 ms
current, 80 ms pressure hold and 120 ms release. This separation is consistent
with the squeeze/pulse/hold distinction in [AMADA's resistance welding
fundamentals](https://amadaweldtech.com/wp-content/uploads/2023/04/Resistance-Welding-Fundamentals.pdf).
Three recorded crackle excerpts have explicit current attacks/decays and a
restrained synthesized 100/300 Hz transformer layer; they alternate across the
six spots. Jaw opening has its own quieter cue, and the existing cooling effect
starts after the final release. The added tonal layer is illustrative, rather
than a measured signature of this gun or a weld-quality signal. There is no
continuous arc audio between spot pulses. The private
`weld_sequence_preview.wav` auditions the dry six-spot rhythm; live spatial
attenuation and the mixer limiter still apply in Unreal.

The fluorescent cue is compressed offline without a pitch shift to exactly the
1.5-second visible startup, replacing the seven-second audition. The pure
`DarkFactoryFluorescentTiming.h` curve drives both the original six light banks
and a strike envelope baked into PCM during bank loading. No lighting values or
runtime sensor paths change. Switching away from roof-lit mode cancels the cue
with a short smoothed fade; its separate generation counter rejects queued old startups
and allows a fresh retrigger. No extra audio sources or per-frame file reads
are introduced. The original review recordings remain unchanged.

With the local `numpy`/`imageio-ffmpeg` dependencies available:

```powershell
python scripts/audio/prepare_runtime_audio.py --dependencies assets/audio/.tools
.\unreal\Scripts\build.ps1 -SkipContent
.\unreal\Scripts\run.ps1 -Process -ProcessControl agent -Enhancements
```

Interactive imported scenes automatically select the private bank when present.
An absent/invalid bank falls back to the procedural soundscape. Legacy rendered
fixtures keep the original procedural path. No generated Unreal audio packages
or editor-map changes are needed.

The recorded mix defaults to a +10 dB output gain, correcting the quiet first
integration while preserving the approved relative levels. Override it with
`run.ps1 -FactorySoundGainDb 6` (supported range -12 to +18 dB), or pass
`-DFAudioGainDb=6` directly to Unreal. This gain applies after each source's soft
limiter; its 0.11 bound below describes the pre-gain signal, not the final master
mix. The final timing-revision native recording measured 0.150 peak, 0.0265 RMS and zero
clipped samples, with an exactly silent muted tail. These measurements cover
that fixture, rather than every possible spatial overlap.

`DarkFactoryRecordedAudio` replaces the procedural pool with six fixed voices:
four robot/tool sources, one carriage source and one nonspatial room/UI mix.
Movement level follows actual tool translation/rotation; a stationary inspection
or thermal hold does not keep playing a travel motor. Carriage sound follows
actual carriage motion. Sealant follows active bead application; panel pickup,
placement, gripper release and pinch closure follow the corresponding physical
job times. Current pulses follow actual welding emitters. Subdued cooling plays
after the last jaw release, independently of the model's cooling decision. These are
audible presentation effects, not inspection results or readiness signals.

Only a submitted settled-pose R4 readback triggers a shutter cue; the three
discarded settle renders do not. NIR/thermal/roof cues follow observer mode changes.
These hooks observe existing state without adding model inputs or altering
mechanics, gates, sensor images, prompts or inference scheduling.

The audio callback reads immutable PCM and atomic controls through a bounded
eight-entry SPSC queue per voice. Each voice permits four concurrent one-shots,
with a 0.11 pre-gain peak bound and 8 ms gain smoothing. It does not access UObjects,
allocate buffers, read files or lock. Robot and carriage sources are spatial;
the background/UI voice is centered. Pause/mute/disable silence all layers,
preserving the user's mute setting; queued captures are not replayed afterward.
Ordinary samples remain linear; a soft knee above 0.08 absorbs overlapping
transients without flattening them at a hard ceiling. Queue epochs reject a
stale event even if the audio device missed an entire brief pause/resume.

`unreal/Scripts/verify_audio.ps1` runs the separate rendered `DFAudioSmoke`
fixture. It exercises every bank clip through the live callback, validates
finite/bounded active output and settled silence, rejects dropped events and
records the Unreal master mix as private `engine_audio.wav`. This is digital
engine-output evidence; it does not measure the user's speakers or room.
Process reports also record actual audio event counts for the live flow/agent
integration checks. Legacy verification remains `-RealismSmoke` plus
`scripts/verify_realism.py`.
