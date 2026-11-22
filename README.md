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
  - **Offline** (default): instant, free, no internet. A built-in interpreter recognises 40+ styles (including 12 orchestral game and film archetypes), 20+ moods, 60+ instruments and explicit settings, then a composition engine writes the music.
  - **Claude AI** (optional): Claude reads your prompt and writes the *composition plan* (key, mode, chord progression, instrumentation, registers, playing patterns, arrangement and a main theme). PromptMIDI's engine then writes the notes from that plan, so the MIDI is always valid and playable, even for unusual prompts such as *"music for a dwarven forge at night"*.
- **Complete orchestral game and film cues** (see [Orchestral cues](#orchestral-cues)): 2-4 minute pieces with an intro, theme, developed restatements, a second theme, a build, a climax and an outro, or an end stinger plus loop points for in-game looping. A full orchestra in score order, per-note articulations (with keyswitch or UACC export), runs, swells into tutti hits and separate orchestral percussion. Every generation goes through a quality check.
- **Real arrangements, not random notes:**
  - **Form (songs and loops):** an intro, then 4-bar phrases in A A B A (A B A for three phrases). The B section has its own contrasting harmony, and heroic and epic pieces can lift the final statement of the theme into a new key.
  - **Melody:** themes open with a signature leap, repeat and develop their motif, land on chord tones on the beat, and peak around two-thirds of the way through. Counter-melodies move against the lead, and violins or glockenspiel can double the theme at the climax.
  - **Harmony and orchestration:** voice-led chord voicings that stay out of the mud below C3, and every part kept inside its instrument's real range.
  - **Patterns:** idiomatic writing such as walking bass, Alberti bass, guitar strums, harp arpeggios, string ostinatos and gallops, timpani rolls, pizzicato, house stabs and 808s, plus 20+ drum grooves with fills and crashes.
  - **Expression:** staggered entries, dynamics curves, swing and humanisation. CC1 (dynamics) and CC11 (expression) curves are written on every bowed, blown and sung part, which orchestral sample libraries respond to. On a plain General MIDI synth, CC1 adds vibrato instead.
- **Get the MIDI into your DAW:**
  - **Drag and drop** the whole piece, or a single track from the track list, straight onto your DAW's timeline.
  - **Export .mid**: a type-1 MIDI file with one named track per instrument, GM program changes, tempo, time and key signatures, section markers and loop points.
  - **Articulations for sample libraries:** choose General MIDI (pizzicato and tremolo on their own tracks), **keyswitch notes**, or **controller values** such as Spitfire's UACC (CC32) in **Settings**; the numbers are editable to match your library.
  - **Live MIDI output** in time with the host transport. When looping, a cue with loop points plays its intro once and then repeats the loop region, the way a game engine would.
- **Built-in preview synth**, so you can hear the result before routing it to your own instruments.
- **Variation** gives a new take (new melody and rhythms) of the same plan. **Key / Scale / Tempo / Bars / Length** controls re-shape the current result instantly. **Seed** with **Lock** makes results reproducible.
- **Mute** tracks to leave them out of playback and export. Everything is saved with your DAW project. The API key is not.

## Orchestral cues

Any orchestral, cinematic or game-score prompt (*"Generate me a fantasy orchestral midi"*, *"boss battle"*, *"celtic orchestral"*, *"stealth music"*...) is composed as a complete cue rather than a loop.

- **Length:** set with the **Length** control or in the prompt. *Sketch* is 16 bars, *Cue* about a minute, *Full* (the default) about two and a half minutes, *Extended* about four minutes. A bar count or duration in the prompt (`64 bars`, `3 minutes`) is used as given.
- **Form:** Intro, Theme, Theme (tutti), Development, Second Theme, Build, Climax and Outro, scaled to the length (long cues go through a second development and build to a bigger final climax). Asking for a **loop** (`battle loop`, `seamless`) ends the cue with a short **end stinger** and marks a loop region from the theme to the stinger. Sections and loop points are written into the MIDI file as markers and cue points, and are shown in the piano roll.
- **Harmony:** phrase-level progressions with half and authentic cadences, secondary dominants, borrowed chords, tonic and dominant pedal points, a descending-fifths sequence in the development, a second theme in a related key, and a key lift for the climax. Restatements of the theme are **reharmonised** around the tune (in the mode's own chords, so Dorian keeps its major IV and Mixolydian its flat VII), and epic bVI-bVII-I cadences are used only where the melody fits them.
- **Thematic development:** the theme comes back in different instruments, in augmentation in the intro and outro, and in the development as a **sequence that follows the harmony** (the motif keeps its relationship to each chord), then **fragmented** to its head and **liquidated** before the retransition. Builds pass the rising head between two instruments (call and response), and counter-lines move in real counterpoint against the theme (chord tones on the beat, stepwise passing notes, moving while the theme holds).
- **Orchestration:** a full orchestra in score order (woodwinds, brass, timpani, percussion, harp / celesta / glockenspiel, choir, soloists, Violins I, Violins II, Violas, Celli, Basses), each part in its practical range. Each section decides who carries the theme, who doubles it (violins in octaves, piccolo and glockenspiel on top at the climax), who plays the counter-line, the harmony (voiced across the section, with divisi), the rhythmic engine (spiccato ostinati, pizzicato, harp) and the bass.
- **Articulations:** every note carries one: legato, sustain, staccato, spiccato, marcato, pizzicato, tremolo, trill. Export them as keyswitches or controller values (see below), or as separate pizzicato / tremolo tracks for General MIDI.
- **Transitions:** timpani rolls and suspended-cymbal swells into tutti hits (bass drum, crash, tam-tam in dark music, a marcato brass and low-string chord), snare rolls, string and flute runs, and harp glissandi with the pedals set to the chord.
- **Percussion:** separate orchestral instruments (Bass Drum, Cymbals, Snare Drum, Taiko Ensemble, Tam-tam, Triangle), not one General MIDI drum kit.
- **Quality check on every generation:** each cue is composed four times (three for other music) with different seeds; each take is repaired (notes folded into range, stray chord notes moved to chord tones) and scored for out-of-range notes, wrong harmony notes, strong-beat clashes, low-register mud and the shape of the melody. The best take wins, and its score is shown under the title (hover for the details).

### Articulation export

**Settings > MIDI export > Articulations**:

| Option | What the exported / dragged MIDI contains |
|---|---|
| General MIDI | Pizzicato and tremolo passages on their own tracks (`Violins I (pizz.)`), with GM programs 45 and 44. Plays correctly on any GM synth. |
| Keyswitch notes | Each section on one track, with a short keyswitch note just before every change of articulation. Defaults: sustain 12, legato 13, staccato 14, spiccato 15, marcato 16, pizzicato 17, tremolo 18, trill 19 (MIDI 12-19, C-1 to G-1, below every orchestral instrument). |
| Controller | The same, but a controller value instead of a note. The defaults follow Spitfire's UACC chart on CC32: long 1, legato 20, staccato 40, spiccato 42, marcato 52, pizzicato 56, tremolo 11, trill (major 2nd) 71. |

Libraries differ, so check your library's articulation list and edit the eight numbers (`-1` = don't send). Live MIDI output stays General MIDI.

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
| Game and film cues | heroic main theme, boss battle, chase / action, stealth, lament / death scene, exploration / ruins, village / pastoral, space / sci-fi, whimsical / wizard school, villain / dark fantasy, victory fanfare, pirate / high seas |
| Orchestrate any genre | add *orchestral* (or *orchestra*, *symphonic*, *cinematic*): `celtic orchestral`, `orchestral march`, `arabian desert orchestral theme`. The genre keeps its scale, meter and signature instruments (fiddle, shakuhachi, oud...), which join a full orchestra: strings, woodwinds, horns, and for energetic or martial music trumpets, trombones, tuba, timpani and percussion. |
| Mood | soft, calm, energetic, epic, heroic, sad, emotional, happy, dark, mysterious, dreamy, romantic, tense, jazzy, groovy, simple, complex, slow, fast, building, fading |
| Instruments | piano, rhodes, strings, violin, cello, harp, choir, flute, clarinet, oboe, trumpet, horns, brass, sax, guitar, nylon guitar, bass, 808, synth, pad, organ, music box, celesta, marimba, koto, sitar, bagpipes, steel drums, drums, taiko, and more |
| Remove things | `no drums`, `without bass`, `no piano` |
| Only what you name | `only piano and cello`, `just strings`, `solo violin` |
| Key / scale | `in D minor`, `C# major`, `key of Eb`, `f sharp minor`, `dorian`, `harmonic minor`, `pentatonic` |
| Tempo | `120 bpm`, `tempo 90`, or words like *slow* and *fast* |
| Length | `16 bars`, `8-bar`, `30 seconds`, `2 minutes`, `short`, `long`, `loop`; for orchestral cues also `sketch`, `cue`, `full`, `extended` |
| Meter | `3/4`, `6/8`, `12/8`, `waltz`, `jig` |

If no genre is recognised, PromptMIDI falls back to a cinematic piano-and-strings arrangement. The line under the title always shows how the prompt was understood.

## Claude AI mode

1. Click **Claude AI**, then **Settings**.
2. Paste an Anthropic API key. You can also leave the field empty and set the `ANTHROPIC_API_KEY` environment variable instead.
3. Optionally change the model (default `claude-opus-5-5`) and the effort (Low is fastest, High is the most considered).

Notes:

- Requests use the Messages API with **structured outputs**: Claude must return JSON that matches PromptMIDI's plan schema, so the answer is always machine-readable. The plug-in also opts in to server-side **refusal fallbacks** (`fallbacks: "default"`) on models that support them. Transient errors (rate limits, overload) are retried twice with back-off. A request can be cancelled at any time.
- For orchestral requests Claude plans a full cue: it sets `orchestral` and `length`, lists the orchestra section by section (up to 24 tracks) and names the instrument that states the theme; the cue engine does the rest.
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

- `core_tests`: chord parsing, prompt interpretation, composer invariants (no overlapping notes, ranges, determinism, melody lands on chord tones, endings resolve), MIDI file validity; every style is checked with several seeds and meters. For cues: lengths, forms, loop points and markers; 15 orchestral prompts x 3 seeds must pass the quality check (score 8.5+, no wrong harmony notes, nothing out of range, little clash and mud); the full orchestra, articulations, keyswitch and UACC export; and that themes are developed (passed between instruments, restatements reharmonised).
- `json_tests`: the plan schema is valid for structured outputs; spec and composition JSON round trips (including articulations, chord tones, pedals, markers, loop points and quality, and states saved by older versions); Claude request bodies, headers and response, refusal and error handling; an orchestral plan from Claude becomes a full cue.
- `vst3_host_test`: loads the built `PromptMIDI.vst3` through JUCE's VST3 hosting, restores a project state, runs the transport and checks the MIDI notes, the preview audio and the saved state, and that a looping cue keeps cycling through its loop region past the end.

`promptmidi_ui_snapshot out.png "prompt"` renders the editor to a PNG without a display (the screenshot above was made this way).

`Tools/evaluate_orchestral.py` generates 20 orchestral game cues (`python3 Tools/evaluate_orchestral.py build/promptmidi-cli out/`). It measures each one against a checklist of what a professional score does: sections present, instrument ranges, strong-beat dissonance, low-register mud, outer-voice parallels, the theme's shape, form, length and dynamics, articulations, divisi, runs, swell-and-hit transitions, and thematic development (where the motif returns, fragmentation, imitation, reharmonised restatements). It prints a score per dimension, plus the original v1 checklist for comparison. The scores are heuristics that point at weaknesses; they are not a substitute for listening.

## Project layout

```
Source/Core/     Engine: plain C++17, no JUCE
  MusicTheory    modes, roman-numeral / chord-name parsing, voicing helpers
  StyleSpec      the composition plan (what Claude returns, what the interpreter builds)
  PromptInterpreter  offline prompt -> plan (styles, moods, instruments, explicit settings)
  Composer       plan -> notes (harmony, melody, accompaniment patterns, drums); best-of-N takes
  CueEngine      orchestral cues: form, phrase harmony, thematic development, orchestration, transitions
  QualityCheck   the check every generation goes through, and the repair pass
  MidiFileWriter notes -> Standard MIDI File (markers, loop points, keyswitch / CC articulations)
Source/Plugin/   JUCE plug-in: processor, editor, piano roll, preview synth, Claude client, JSON
Tests/           unit tests and the VST3 host test
Tools/           command-line generator and UI snapshot tool
```

## License note

PromptMIDI is built on [JUCE](https://juce.com), which is dual-licensed under AGPLv3 and a commercial licence. If you distribute builds of this plug-in, you must either release your source under the AGPLv3 or hold a JUCE commercial licence. VST is a trademark of Steinberg Media Technologies GmbH.
