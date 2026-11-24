// In-process tests of the plug-in: the processor's editing, variation, instrument and preview
// behaviour, and the editor's arrangement view. Run with ctest or ./promptmidi_editor_tests

#include <juce_audio_utils/juce_audio_utils.h>

#include "InstrumentPicker.h"
#include "Judge.h"
#include "PresetIO.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Timeline.h"

#include <cstdio>
#include <tuple>
#include <vector>

static int failures = 0, checks = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++checks;                                                                     \
        if (! (cond)) {                                                               \
            ++failures;                                                               \
            std::printf ("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                             \
    } while (0)

struct Head : public juce::AudioPlayHead
{
    bool playing = false;
    double ppq = 0.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        info.setBpm (120.0);
        return info;
    }
};

static float render (PromptMidiProcessor& p, int blocks, int& noteOns)
{
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    float peak = 0.0f;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        midi.clear();
        p.processBlock (buffer, midi);
        peak = juce::jmax (peak, buffer.getMagnitude (0, 0, 512));

        for (const auto m : midi)
            noteOns += m.getMessage().isNoteOn() ? 1 : 0;
    }

    return peak;
}

/** Every note of a piece, for comparing two pieces. */
static std::vector<std::tuple<int, int, int>> allNotes (const pm::Composition& c)
{
    std::vector<std::tuple<int, int, int>> all;

    for (const auto& t : c.tracks)
        for (const auto& n : t.notes)
            all.emplace_back (n.start, n.pitch, n.length);

    return all;
}

/** Runs the message loop until `done` or the time is up. */
template <typename Condition>
static bool waitFor (Condition done, int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    while (! done() && juce::Time::getMillisecondCounter() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

    return done();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Most tests answer at once (Quick, on this thread); thinking has its own tests at the end.
    PromptMidiProcessor::defaultThinkInBackground = false;
    PromptMidiProcessor::defaultDepthOverride = 0;

    PromptMidiProcessor processor;
    processor.seedLocked = true;
    processor.seed = 11;
    processor.generate ("Generate me a fantasy orchestral midi");
    processor.dispatchPendingMessages();

    std::printf ("editing with undo\n");
    {
        CHECK (processor.hasComposition());
        const int piece = processor.getPieceVersion();
        const int track = 0;
        const auto before = processor.getComposition().tracks[(size_t) track].notes;
        auto edited = before;
        pm::Note n;
        n.start = 480;
        n.length = 480;
        n.pitch = 72;
        n.velocity = 100;
        edited.push_back (n);

        processor.editTrackNotes (track, edited, "Add note");
        CHECK (processor.getComposition().tracks[(size_t) track].notes.size() == before.size() + 1);
        CHECK (processor.isEdited());
        CHECK (processor.getPieceVersion() == piece);   // an edit is not a new piece

        CHECK (processor.getUndoManager().undo());
        CHECK (processor.getComposition().tracks[(size_t) track].notes.size() == before.size());
        CHECK (! processor.isEdited());          // undoing every edit gives back the piece as composed
        CHECK (processor.getUndoManager().redo());
        CHECK (processor.getComposition().tracks[(size_t) track].notes.size() == before.size() + 1);
        CHECK (processor.isEdited());

        // Invalid edits are made valid.
        auto bad = before;
        pm::Note b;
        b.start = -50;
        b.length = 0;
        b.pitch = 200;
        b.velocity = 0;
        bad.push_back (b);
        processor.editTrackNotes (track, bad, "Bad note");
        bool valid = true;

        for (auto& x : processor.getComposition().tracks[(size_t) track].notes)
            valid = valid && x.start >= 0 && x.length > 0 && x.pitch <= 127 && x.velocity >= 1;

        CHECK (valid);
    }

    std::printf ("variation keeps the settings, changes the music\n");
    {
        const auto before = processor.getComposition();
        processor.makeVariation();
        const auto& after = processor.getComposition();
        CHECK (after.tempo == before.tempo && after.keyRoot == before.keyRoot);
        CHECK (after.tracks.size() >= before.tracks.size() - 4);
        CHECK (! processor.isEdited());
        CHECK (! processor.getUndoManager().canUndo());

        bool different = false;

        for (size_t t = 0; t < after.tracks.size() && t < before.tracks.size(); ++t)
            different = different || after.tracks[t].notes.size() != before.tracks[t].notes.size();

        CHECK (different);
    }

    std::printf ("takes: nothing is lost by trying another\n");
    {
        PromptMidiProcessor p;
        p.seedLocked = true;
        p.seed = 5;
        p.generate ("Make me a soft piano midi");
        CHECK (p.getNumTakes() == 1 && p.getCurrentTake() == 0);

        auto addNote = [&p]
        {
            auto notes = p.getComposition().tracks[0].notes;
            pm::Note n;
            n.start = 960;
            n.length = 240;
            n.pitch = 101;
            n.velocity = 99;
            notes.push_back (n);
            p.editTrackNotes (0, notes, "Add note");
        };

        addNote();
        p.setTrackMuted (1, true);
        const auto editedCount = p.getComposition().tracks[0].notes.size();
        const auto firstTitle = p.getComposition().title;

        p.makeVariation();
        CHECK (p.getNumTakes() == 2 && p.getCurrentTake() == 1);
        CHECK (! p.isEdited());

        const int piece = p.getPieceVersion();
        p.selectTake (0);
        CHECK (p.getCurrentTake() == 0);
        CHECK (p.isEdited());
        CHECK (p.getComposition().tracks[0].notes.size() == editedCount);
        CHECK (p.getComposition().title == firstTitle);
        CHECK (p.isTrackMuted (1));
        CHECK (p.getPieceVersion() != piece);     // the view refits
        CHECK (! p.getUndoManager().canUndo());
        CHECK (p.getTakeTitle (0).endsWith ("(edited)"));

        p.selectTake (1);
        CHECK (! p.isEdited());
        p.selectTake (7);                          // out of range: ignored
        CHECK (p.getCurrentTake() == 1);

        // New settings re-compose the take on screen...
        p.controls.tempo = 90;
        p.controlsChanged();
        CHECK (p.getNumTakes() == 2 && p.getComposition().tempo == 90);

        // ...but an edited take is kept as it was.
        addNote();
        p.controls.tempo = 100;
        p.controlsChanged();
        CHECK (p.getNumTakes() == 3 && p.getCurrentTake() == 2);
        p.selectTake (1);
        CHECK (p.isEdited() && p.getComposition().tempo == 90);

        // Generate starts a new take too; the history is bounded.
        p.generate ("Lo-fi hip hop beat");
        CHECK (p.getNumTakes() == 4 && p.getCurrentTake() == 3);

        for (int i = 0; i < 30; ++i)
            p.makeVariation();

        CHECK (p.getNumTakes() == PromptMidiProcessor::maxTakes);
        CHECK (p.getCurrentTake() == PromptMidiProcessor::maxTakes - 1);

        // A project restore starts a fresh history with the restored piece.
        juce::MemoryBlock state;
        p.getStateInformation (state);
        PromptMidiProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (q.getNumTakes() == 1 && q.hasComposition());
    }

    std::printf ("presets and MIDI files\n");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("promptmidi_preset_test");
        dir.deleteRecursively();
        dir.createDirectory();

        PromptMidiProcessor a;
        a.seedLocked = true;
        a.seed = 21;
        a.controls.tempo = 97;
        a.generate ("Make me a soft piano midi");

        // An edit travels with the preset.
        auto notes = a.getComposition().tracks[0].notes;
        pm::Note extra;
        extra.start = 0;
        extra.length = 480;
        extra.pitch = 100;
        extra.velocity = 77;
        notes.push_back (extra);
        a.editTrackNotes (0, notes, "Add note");
        a.setTrackMuted (1, true);

        const auto file = dir.getChildFile ("Soft piano.promptmidi");
        CHECK (a.savePreset (file, "Soft piano"));

        PromptMidiProcessor b;
        juce::String error;
        CHECK (b.loadPreset (file, error));
        CHECK (b.getComposition().noteCount() == a.getComposition().noteCount());
        CHECK (b.getComposition().title == a.getComposition().title);
        CHECK (b.isEdited());
        CHECK (b.isTrackMuted (1));
        CHECK (b.controls.tempo == 97);
        CHECK (b.prompt == "Make me a soft piano midi");
        CHECK (b.getTakeTitle (b.getCurrentTake()).startsWith ("Preset"));

        // A preset with only a prompt and settings is composed fresh.
        auto promptOnly = a.presetToVar ("Prompt only");
        promptOnly.getDynamicObject()->removeProperty ("piece");
        PromptMidiProcessor c;
        CHECK (c.presetFromVar (promptOnly, error));
        CHECK (c.hasComposition() && c.controls.tempo == 97);

        // Not a preset.
        const auto junk = dir.getChildFile ("junk.promptmidi");
        junk.replaceWithText ("{\"hello\": 1}");
        CHECK (! c.loadPreset (junk, error) && error.isNotEmpty());

        // Factory presets compose their prompt.
        PromptMidiProcessor f;
        f.loadFactoryPreset (6);
        CHECK (f.hasComposition() && f.prompt == PresetIO::factoryPresets()[6].prompt);

        // MIDI out and back in: the same notes, tempo, length and key.
        const auto mid = dir.getChildFile ("piece.mid");
        CHECK (a.writeMidiFile (mid));
        PromptMidiProcessor m;
        CHECK (m.importMidi (mid, error));
        int expected = 0;

        for (int t = 0; t < (int) a.getComposition().tracks.size(); ++t)
            if (! a.isTrackMuted (t))
                expected += (int) a.getComposition().tracks[(size_t) t].notes.size();

        CHECK (m.getComposition().noteCount() == expected);
        CHECK (m.getComposition().tempo == a.getComposition().tempo);
        CHECK (m.getComposition().bars == a.getComposition().bars);
        CHECK (m.getComposition().keyRoot == a.getComposition().keyRoot);
        CHECK (m.isImportedPiece());

        // Variation on an imported piece: new music for the same instruments. (Compares the notes
        // themselves: a new piece can happen to have the same number of notes.)
        const auto notesOf = [] (const pm::Composition& c)
        {
            std::vector<std::tuple<int, int, int>> all;
            for (const auto& t : c.tracks)
                for (const auto& n : t.notes)
                    all.emplace_back (n.start, n.pitch, n.length);
            return all;
        };
        const auto imported = notesOf (m.getComposition());
        m.makeVariation();
        CHECK (! m.isImportedPiece() && m.hasComposition());
        CHECK (notesOf (m.getComposition()) != imported);

        // Not MIDI.
        const auto text = dir.getChildFile ("notes.mid");
        text.replaceWithText ("not a midi file");
        CHECK (! m.importMidi (text, error) && error.isNotEmpty());

        dir.deleteRecursively();
    }

    std::printf ("instrument summary\n");
    {
        InstrumentPicker::Choices c;
        CHECK (InstrumentPicker::summary (c) == "Auto");
        pm::InstrumentChoice a, b;
        a.name = "Flutes";
        b.name = "Harp";
        c = { a, b };
        CHECK (InstrumentPicker::summary (c) == "Flutes, Harp");
    }

    std::printf ("your own instruments\n");
    {
        pm::InstrumentChoice piano;
        piano.name = "Piano";
        piano.program = 0;
        processor.controls.instruments = { piano };
        processor.controlsChanged();
        const auto& c = processor.getComposition();
        CHECK (c.tracks.size() == 1 && c.tracks[0].name == "Piano");

        processor.controls.instruments.clear();
        processor.controlsChanged();
        CHECK (processor.getComposition().tracks.size() > 10);
    }

    std::printf ("the preview stays inside the plug-in\n");
    {
        Head head;
        processor.setPlayHead (&head);
        processor.prepareToPlay (48000.0, 512);

        int ons = 0;
        processor.setPreviewPlaying (true, 0.0);
        const float previewPeak = render (processor, 80, ons);
        CHECK (previewPeak > 0.001f);   // heard while previewing
        CHECK (ons == 0);               // ...but nothing leaves as MIDI

        // The host starts: the preview stops, and nothing plays along with the DAW.
        head.playing = true;
        render (processor, 4, ons);
        CHECK (! processor.isPreviewPlaying());
        ons = 0;
        render (processor, 70, ons);    // let the release ring out
        const float hostPeak = render (processor, 40, ons);
        CHECK (hostPeak < 1.0e-6f);
        CHECK (ons == 0);

        // A note clicked in the editor sounds when the host is stopped, never while it plays.
        processor.auditionNote (0, 64, 100);
        ons = 0;
        CHECK (render (processor, 20, ons) < 1.0e-6f);
        head.playing = false;
        render (processor, 80, ons);
        processor.auditionNote (0, 64, 100);
        CHECK (render (processor, 20, ons) > 0.001f);

        processor.setPlayHead (nullptr);
        processor.releaseResources();
    }

    std::printf ("the preview plays real instruments\n");
    {
        auto& engine = processor.getPreviewEngine();
        engine.setSource ("");
        CHECK (engine.getBank().waitUntilLoaded (20000));
        engine.refresh();
        CHECK (engine.getDescription() == "GeneralUser GS");

        Head head;
        processor.setPlayHead (&head);
        processor.prepareToPlay (48000.0, 512);
        processor.setPreviewPlaying (true, 0.0);
        int ons = 0;
        const float peak = render (processor, 120, ons);
        CHECK (processor.isPreviewSampled());
        CHECK (peak > 0.01f && peak < 1.0f);
        CHECK (ons == 0);                          // still nothing leaves as MIDI

        // ...and still stops when the host plays.
        head.playing = true;
        render (processor, 4, ons);
        render (processor, 200, ons);              // let the release and the reverb ring out
        CHECK (render (processor, 20, ons) < 1.0e-4f);

        // The sketch synth on request.
        head.playing = false;
        engine.setSource ("sketch");
        CHECK (engine.getBank().waitUntilLoaded (5000));
        engine.refresh();
        processor.setPreviewPlaying (true, 0.0);
        CHECK (render (processor, 60, ons) > 0.001f);
        CHECK (! processor.isPreviewSampled());

        // A missing SoundFont falls back to the sketch synth and says so.
        engine.setSource ("/nonexistent/folder/none.sf2");
        CHECK (engine.getBank().waitUntilLoaded (5000));
        engine.refresh();
        CHECK (engine.getDescription().startsWith ("Sketch synth"));
        CHECK (render (processor, 60, ons) > 0.001f);

        engine.setSource ("");
        engine.getBank().waitUntilLoaded (20000);
        engine.refresh();
        processor.setPreviewPlaying (false);
        processor.setPlayHead (nullptr);
        processor.releaseResources();
    }

    std::printf ("play cursor\n");
    {
        processor.setCursorTick (-100.0);
        CHECK (processor.getCursorTick() < 1.0e-9);
        processor.setCursorTick (1.0e9);
        CHECK (processor.getCursorTick() <= processor.getComposition().lengthTicks);
        processor.setCursorTick (1920.0);
        CHECK (std::abs (processor.getCursorTick() - 1920.0) < 1.0e-9);
    }

    std::printf ("arrangement view\n");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        editor->setSize (1240, 800);
        processor.dispatchPendingMessages();
        auto* arrangement = dynamic_cast<Arrangement*> (editor->findChildWithID ("arrangement"));
        CHECK (arrangement != nullptr);

        if (arrangement != nullptr)
        {
            CHECK (arrangement->getSelectedTrack() == -1);
            arrangement->selectTrack (3);
            CHECK (arrangement->getSelectedTrack() == 3);
            arrangement->zoomBy (4.0, 100.0f);
            arrangement->zoomBy (1000.0, 100.0f);   // clamped, no crash
            arrangement->fit();
            arrangement->setPlayhead (5000.0);
            arrangement->selectTrack (999);
            CHECK (arrangement->getSelectedTrack() == -1);

            // Opening the editor on a whole-piece view zooms in far enough to edit notes.
            arrangement->fit();
            const int tpb = processor.getComposition().ticksPerBar;
            CHECK (arrangement->getAxis().pixelsPerTick * tpb < 40.0);
            arrangement->selectTrack (2);
            CHECK (arrangement->getAxis().pixelsPerTick * tpb >= 40.0);

            // Esc closes it (with nothing selected).
            for (auto* child : arrangement->getChildren())
                if (child->isVisible() && child->getWantsKeyboardFocus())
                    child->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));

            CHECK (arrangement->getSelectedTrack() == -1);

            // Ctrl / Cmd + Z undoes a note edit from anywhere in the window, Shift redoes it.
            const auto before = processor.getComposition().tracks[0].notes.size();
            auto notes = processor.getComposition().tracks[0].notes;
            notes.pop_back();
            processor.editTrackNotes (0, notes, "Delete note");
            CHECK (processor.getComposition().tracks[0].notes.size() == before - 1);
            editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 'z'));
            CHECK (processor.getComposition().tracks[0].notes.size() == before);
            editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 'Z'));
            CHECK (processor.getComposition().tracks[0].notes.size() == before - 1);
            processor.dispatchPendingMessages();

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            CHECK (image.getWidth() == 1240);

            // Files dropped on the window: MIDI and presets, nothing else.
            if (auto* target = dynamic_cast<juce::FileDragAndDropTarget*> (editor.get()))
            {
                CHECK (target->isInterestedInFileDrag ({ "/tmp/a.mid" }));
                CHECK (target->isInterestedInFileDrag ({ "/tmp/a.promptmidi" }));
                CHECK (! target->isInterestedInFileDrag ({ "/tmp/a.txt" }));
            }

            // A new piece closes the note editor.
            arrangement->selectTrack (1);
            processor.makeVariation();
            processor.dispatchPendingMessages();
            CHECK (arrangement->getSelectedTrack() == -1);
        }

        editor.reset();
    }

    std::printf ("thinking before it answers\n");
    {
        const juce::String prompt ("Make me a soft piano midi");
        PromptMidiProcessor quick, thorough;

        for (auto* p : { &quick, &thorough })
        {
            p->seedLocked = true;
            p->seed = 5;
        }

        quick.depthOverride = 0;
        thorough.depthOverride = 1;
        quick.generate (prompt);
        thorough.generate (prompt);

        // Quick answers at once with the first reading; Thorough considers hundreds of drafts
        // across several plans and keeps one at least as good, by the same judge.
        CHECK (quick.hasComposition() && thorough.hasComposition());
        CHECK (quick.getThoughtSummary().isEmpty());
        CHECK (thorough.getThoughtSummary().startsWith ("Thought for"));
        CHECK (thorough.getThoughtReasoning().size() >= 5);
        CHECK (thorough.getComposition().quality.candidates >= 200);
        CHECK (thorough.getComposition().request.allMet());
        CHECK (pm::judgeDraft (thorough.getComposition(), thorough.getSpec()).total
               >= pm::judgeDraft (quick.getComposition(), quick.getSpec()).total - 1e-9);

        // The seed shown (and locked) is the one it thought from: the same seed, the same piece.
        const auto chosen = allNotes (thorough.getComposition());
        CHECK (thorough.seed == 5);
        thorough.generate (prompt);
        CHECK (allNotes (thorough.getComposition()) == chosen);

        // The controls re-compose the chosen draft, not a new one: nothing changed, nothing moves.
        thorough.controlsChanged();
        CHECK (allNotes (thorough.getComposition()) == chosen);

        // ... and a saved project brings back the seeds that made it.
        juce::MemoryBlock state;
        thorough.getStateInformation (state);
        PromptMidiProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        restored.controlsChanged();
        CHECK (allNotes (restored.getComposition()) == chosen);

        // A thought-out variation is new music, and meets everything (including "different").
        thorough.makeVariation();
        CHECK (thorough.getCurrentTake() == thorough.getNumTakes() - 1);
        CHECK (allNotes (thorough.getComposition()) != chosen);
        CHECK (thorough.getComposition().request.allMet());
        CHECK (thorough.getThoughtSummary().startsWith ("Thought for"));
        thorough.selectTake (0);
        CHECK (thorough.getThoughtReasoning().size() >= 5);   // each take keeps how it was chosen
    }
    {
        // On a worker thread: the window stays usable, progress comes in, and the best so far can
        // be taken at any time - or the whole thing cancelled, leaving the take on screen alone.
        PromptMidiProcessor p;
        p.thinkInBackground = true;
        p.depthOverride = 2;
        p.generate ("Generate me a fantasy orchestral midi");
        CHECK (p.isThinking() && p.isGenerating());
        CHECK (p.getStatus() == PromptMidiProcessor::Status::Working);
        CHECK (waitFor ([&] { return p.getThinkingProgress().drafts >= 5; }, 20000));
        CHECK (p.getThinkingProgress().totalDrafts >= 900);
        p.finishThinkingNow();
        CHECK (waitFor ([&] { return ! p.isThinking(); }, 20000));
        CHECK (p.hasComposition());
        CHECK (p.getStatusMessage().contains ("stopped early"));
        CHECK (p.getComposition().quality.candidates < 900);

        const auto kept = allNotes (p.getComposition());
        p.generate ("happy ukulele song");
        CHECK (p.isThinking());
        p.cancelGeneration();
        CHECK (! p.isThinking() && ! p.isGenerating());
        CHECK (p.getStatusMessage() == "Cancelled.");
        CHECK (allNotes (p.getComposition()) == kept);

        // Closing the plug-in while it thinks is safe.
        p.generate ("happy ukulele song");
        CHECK (p.isThinking());
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
