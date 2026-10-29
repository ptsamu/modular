# One Shot Sampler — Product Direction

## Current checklist

- [ ] Re-work the **Process Chain** tomorrow: clarify routing, make module order immediately readable, and bring its design up to the rest of Modular Beta 2.

## Core idea

**MODULAR** is a modern modular one-shot sampler for FL Studio, designed to turn a simple WAV or AIFF into a playable, tempo-synced creative texture. The sampler is the input; the effects section is the identity.

## First beta

- Drag a WAV, AIFF, or AIF file from Finder onto a large central drop zone.
- Keep a **Load Sample** button as a fallback.
- Show the sample name and waveform after loading.
- Trigger the sample from MIDI.
- One-shot playback, tune, start position, reverse, sample gain, and output gain.
- A volume ADSR envelope: attack, decay, sustain, and release.
- Start with a clean, dry default sound.

## Frameworks visual direction

The sampler belongs to Frameworks and follows its minimal visual system: near-black, off-white, and vivid orange. This replaces the earlier multi-colored modular idea.

- Default theme: warm gray-white background with a very subtle gray dotted grid.
- Dark theme: near-black background with the same subtle dotted grid, available later as an alternate appearance.
- Primary accent: Frameworks orange, used sparingly for active states, sample playhead, selected effect, important values, and the logo.
- Secondary surface: warm off-white for readable panel states and menus.
- Typography: near-black on the default light surface; soft off-white on dark surfaces.
- Controls: simple geometric knobs, quiet dividers, generous spacing, and minimal text.
- The Frameworks mark sits at the top left of the dark header in orange or off-white.
- The waveform is the visual centrepiece; its playhead and active drop state use orange.
- No copied logos, branding, or exact third-party interface layouts.

### Planned layout

1. Minimal near-black top bar: Frameworks mark, **MODULAR**, preset browser, save, settings, and master output.
2. Central sample loading/waveform area with a calm dotted-grid backdrop.
3. Compact routing strip:
   `Sample -> Filter -> Harmonizer -> Chorus/Phaser -> Granular Texture -> Ping-Pong Delay -> Reverb -> Output`
4. Bottom panel: selected effect controls; later, envelopes and LFOs.

### Envelope

The first modulation control is a clear **volume ADSR** envelope. It shapes each sample hit with Attack, Decay, Sustain, and Release controls, plus a compact visual envelope curve. A filter envelope can follow in a later version.

## Effects identity

The effects should be original, creative, musical, and tempo-aware. The aim is a transformative granular/texture workflow, not a copy of another product.

- **Reverb:** clean space, dark wash, and later shimmer-style pitch movement.
- **Ping-pong delay:** stereo repeats, tempo divisions, feedback, and filtering.
- **Chorus:** width, detune, and movement.
- **Phaser:** animated sweep and motion.
- **Harmonizer:** pitch intervals; later, key/scale-aware harmony.
- **Granular Texture:** tiny grains that stretch, reverse, scatter, and smear the loaded sample while following host tempo.

## Character Delay

The delay is a priority and should be an original character delay with warmth, rhythmic movement, and stereo depth.

- Tempo divisions: quarter, eighth, dotted, triplet, and related musical values.
- Ping-pong stereo movement.
- Feedback and wet/dry mix.
- Tone/filter shaping.
- Saturation/drive on repeats.
- Tape-style wobble and flutter.
- Ducking, so repeats make room for the dry sample.
- Original style modes: `Clean`, `Tape`, `Analog`, `Dust`, and `Space`.

Build order: basic ping-pong delay -> tone and drive -> tape movement -> style modes.

## Randomizer

The randomizer creates a musical, repeatable FX scene rather than randomizing every parameter blindly.

- Large **Randomize** button.
- Intensity control from subtle to transformative.
- A seed value, allowing a great result to be recreated.
- Parameter locks.
- Delay timing, grain density, rhythmic gates, and modulation stay host-tempo-synced.
- Safety limits prevent uncontrolled feedback, sudden loudness, and unusable combinations.

## After the first beta: multi-sample mode

- Start with 16 pads/sample slots.
- Drop a separate WAV/AIFF onto each slot.
- Map each slot to a MIDI note (kick, snare, clap, hat, etc.).
- Per-slot gain, tune, start, reverse, and effects sends.
- Add a small internal mixer.
- Later add optional individual DAW outputs.
