# PromptMIDI

**Describe the music in words, get multi-track MIDI.** PromptMIDI is a VST3 instrument plug-in (plus AU on macOS and a standalone app) with a prompt window: type *"Generate me a fantasy orchestral midi"* or *"Make me a soft piano midi"*, press **Generate**, then drag the result into your DAW.

![PromptMIDI](docs/screenshot.png)

## What it does

- **Prompt window.** Plain-English requests: genre, mood, instruments, key, tempo, length, time signature. For example:
  - `Generate me a fantasy orchestral midi`
  - `Make me a soft piano midi`
  - `dark lofi beat in F# minor, 80 bpm, no drums`
  - `epic trailer music with choir and harp, 32 bars`
  - `just piano and cello, sad`
  - `celtic tavern jig` (6/8) · `viennese waltz` (3/4) · `30 second synthwave loop at 120 bpm`
- **Two engines:**
  - **Offline** (default): instant, free, no internet. A built-in interpreter recognises 30 styles, 20+ moods, 60+ instruments and explicit settings, then a composition engine writes the music.
  - **Claude AI** (optional): Claude reads your prompt and writes the *composition plan* (key, mode, chord progression, instrumentation, registers, playing patterns, arrangement and a main theme). PromptMIDI's engine then writes the notes from that plan, so the MIDI is always valid and playable, even for unusual prompts such as *"music for a dwarven forge at night"*.
- **Real arrangements, not random notes:** chord progressions written as roman numerals; voice-led chord voicings; melodies built from motifs that repeat and develop in phrases (A A' B A) and land on chord tones; counter-melodies that move against the lead; idiomatic patterns (walking bass, Alberti bass, guitar strums, harp arpeggios, string ostinatos, timpani rolls, house stabs, 808s); 20+ drum grooves with fills and crashes; staggered entries; dynamics curves; swing; humanisation; CC1/CC11 swells for orchestral pads.
- **Get the MIDI into your DAW:**
  - **Drag and drop** the whole piece, or a single track from the track list, straight onto your DAW's timeline.
  - **Export .mid**: a type-1 MIDI file with one named track per instrument, GM program changes, tempo, time and key signatures.
  - **Live MIDI output** in time with the host transport, looping the piece.
- **Built-in preview synth**, so you can hear the result before routing it to your own instruments.
- **Variation** gives a new take (new melody and rhythms) of the same plan. **Key / Scale / Tempo / Bars** controls re-shape the current result instantly. **Seed** with **Lock** makes results reproducible.
- **Mute** tracks to leave them out of playback and export. Everything is saved with your DAW project. The API key is not.

## Download

Every push builds Windows, macOS (Universal) and Linux binaries in GitHub Actions: open the **Actions** tab, select the latest **Build** run and download the `PromptMIDI-<OS>` artifact.

Install the plug-in by copying `PromptMIDI.vst3` to:

| OS | VST3 folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3\` |
| macOS | `/Library/Audio/Plug-Ins/VST3/` (AU: `PromptMIDI.component` into `/Library/Audio/Plug-Ins/Components/`) |
| Linux | `~/.vst3/` |

On macOS the CI builds are only ad-hoc signed. If macOS blocks them, run `xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/PromptMIDI.vst3` (and the same for the `.component`).

Then rescan plug-ins in your DAW. PromptMIDI shows up as an **instrument** (category *Instrument | Generator*).

## Using it in your DAW

PromptMIDI is an instrument that also sends MIDI out, so you can use it in two ways:

1. **Generate, then drag** *(works in every DAW, including Ableton Live, Logic, FL Studio, Studio One, Cubase, Reaper and Bitwig)*. Load PromptMIDI on any track, generate, then drag from **Drag MIDI into your DAW** onto your timeline. Most DAWs split a multi-track file into one track per instrument. To drag a single part, drag its row in the track list.
2. **Play it live as MIDI.** With **MIDI out** and **Follow host** on, PromptMIDI plays the piece (looping, from bar 1) whenever the host plays, and sends it out on one MIDI channel per track:
   - **Reaper:** add a MIDI send from the PromptMIDI track to your instrument track(s).
   - **Cubase / Nuendo:** set an instrument or MIDI track's input to *PromptMIDI*.
   - **Bitwig:** use a *Note Receiver* on the destination track.
   - **FL Studio:** set an output port in the wrapper's MIDI settings and the same port on the target instrument.
   - **Ableton Live and Logic** don't accept MIDI from VST3/AU instruments, so use drag and drop there.

Use **Play** to preview without the host. Turn **Preview sound** off when you route the MIDI to your own instruments.

## Prompt guide (offline engine)

| You can say | Examples |
|---|---|
| Genre | fantasy, orchestral, epic, trailer, piano, classical, baroque, jazz, lofi, hip hop, trap, house, techno, trance, drum and bass, pop, rock, metal, ambient, synthwave, celtic, folk, horror, chiptune, bossa nova, funk, disco, blues, lullaby, japanese, arabian, march, waltz, r&b, soul, reggae, country, hymn |
| Mood | soft, calm, energetic, epic, heroic, sad, emotional, happy, dark, mysterious, dreamy, romantic, tense, jazzy, groovy, simple, complex, slow, fast, building, fading |
| Instruments | piano, rhodes, strings, violin, cello, harp, choir, flute, clarinet, oboe, trumpet, horns, brass, sax, guitar, nylon guitar, bass, 808, synth, pad, organ, music box, celesta, marimba, koto, sitar, bagpipes, steel drums, drums, taiko, and more |
| Remove things | `no drums`, `without bass`, `no piano` |
| Only what you name | `only piano and cello`, `just strings`, `solo violin` |
| Key / scale | `in D minor`, `C# major`, `key of Eb`, `f sharp minor`, `dorian`, `harmonic minor`, `pentatonic` |
| Tempo | `120 bpm`, `tempo 90`, or words like *slow* and *fast* |
| Length | `16 bars`, `8-bar`, `30 seconds`, `2 minutes`, `short`, `long`, `loop` |
| Meter | `3/4`, `6/8`, `12/8`, `waltz`, `jig` |

If no genre is recognised, PromptMIDI falls back to a cinematic piano-and-strings arrangement. The line under the title always shows how the prompt was understood.

## Claude AI mode

1. Click **Claude AI**, then **Settings**.
2. Paste an Anthropic API key. You can also leave the field empty and set the `ANTHROPIC_API_KEY` environment variable instead.
3. Optionally change the model (default `claude-opus-5-5`) and the effort (Low is fastest, High is the most considered).

Notes:

- Requests use the Messages API with **structured outputs**: Claude must return JSON that matches PromptMIDI's plan schema, so the answer is always machine-readable. The plug-in also opts in to server-side **refusal fallbacks** (`fallbacks: "default"`) on models that support them. Transient errors (rate limits, overload) are retried twice with back-off. A request can be cancelled at any time.
- One request per **Generate**. **Variation** and the Key/Tempo/Bars controls reuse the plan without calling the API again. A new **Scale** asks for a fresh plan on the next Generate, because Claude's harmony was written for the old scale.
- The key is stored only on your computer, in the PromptMIDI settings file (`%APPDATA%\PromptMIDI` on Windows, `~/Library/Application Support/PromptMIDI` on macOS, `~/.config/PromptMIDI` on Linux), and is sent only to `api.anthropic.com`. It is never written into DAW projects. Requests are billed to your Anthropic account.

## Building from source

Requirements: CMake 3.22+, a C++17 compiler (Visual Studio 2022, Xcode 14+, GCC 11+ or Clang 14+). JUCE 9.0.6 is downloaded automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Outputs are in `build/PromptMIDI_artefacts/Release/` (`VST3/`, `Standalone/` and `AU/` on macOS). To copy the VST3 into your system plug-in folder automatically, configure with `-DPROMPTMIDI_COPY_PLUGIN=ON`. To use an existing JUCE checkout instead of downloading one, add `-DPROMPTMIDI_JUCE_DIR=/path/to/JUCE`.

On Linux, first install the JUCE dependencies:

```bash
sudo apt install libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev \
  libxcomposite-dev libxi-dev libfreetype-dev libfontconfig1-dev libcurl4-openssl-dev libgl1-mesa-dev
```

The engine alone has no dependencies. `-DPROMPTMIDI_BUILD_PLUGIN=OFF` builds just the engine, its tests and a command-line tool:

```bash
./build/promptmidi-cli "Generate me a fantasy orchestral midi" -o fantasy.mid
./build/promptmidi-cli "soft piano in D minor, 70 bpm" --seed 7 --notes
```

### Tests

- `core_tests`: chord parsing, prompt interpretation, composer invariants (no overlapping notes, ranges, determinism, melody lands on chord tones, endings resolve), MIDI file validity; every style is checked with several seeds and meters.
- `json_tests`: the plan schema is valid for structured outputs; spec and composition JSON round trips; Claude request bodies, headers and response, refusal and error handling.
- `vst3_host_test`: loads the built `PromptMIDI.vst3` through JUCE's VST3 hosting, restores a project state, runs the transport and checks the MIDI notes, the preview audio and the saved state.

`promptmidi_ui_snapshot out.png "prompt"` renders the editor to a PNG without a display (the screenshot above was made this way).

## Project layout

```
Source/Core/     Engine: plain C++17, no JUCE
  MusicTheory    modes, roman-numeral / chord-name parsing, voicing helpers
  StyleSpec      the composition plan (what Claude returns, what the interpreter builds)
  PromptInterpreter  offline prompt -> plan (styles, moods, instruments, explicit settings)
  Composer       plan -> notes (harmony, melody, accompaniment patterns, drums)
  MidiFileWriter notes -> Standard MIDI File
Source/Plugin/   JUCE plug-in: processor, editor, piano roll, preview synth, Claude client, JSON
Tests/           unit tests and the VST3 host test
Tools/           command-line generator and UI snapshot tool
```

## License note

PromptMIDI is built on [JUCE](https://juce.com), which is dual-licensed under AGPLv3 and a commercial licence. If you distribute builds of this plug-in, you must either release your source under the AGPLv3 or hold a JUCE commercial licence. VST is a trademark of Steinberg Media Technologies GmbH.
