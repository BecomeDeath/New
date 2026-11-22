// In-process tests of the plug-in: the processor's editing, variation, instrument and preview
// behaviour, and the editor's arrangement view. Run with ctest or ./promptmidi_editor_tests

#include <juce_audio_utils/juce_audio_utils.h>

#include "InstrumentPicker.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Timeline.h"

#include <cstdio>

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

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

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

            // A new piece closes the note editor.
            arrangement->selectTrack (1);
            processor.makeVariation();
            processor.dispatchPendingMessages();
            CHECK (arrangement->getSelectedTrack() == -1);
        }

        editor.reset();
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
