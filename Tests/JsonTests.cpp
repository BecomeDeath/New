// Tests for the JSON layer and the Claude request/response handling (no network).

#include <juce_core/juce_core.h>

#include "ClaudeClient.h"
#include "Composer.h"
#include "MidiFileWriter.h"
#include "PromptInterpreter.h"
#include "SpecJson.h"
#include "UpdateCheck.h"

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

/** Structured outputs require every object to be closed and to list all its properties as required. */
static bool schemaIsStrict (const juce::var& node, juce::String path)
{
    if (! node.isObject())
        return true;

    bool ok = true;

    if (node["type"].toString() == "object")
    {
        if (! node.hasProperty ("additionalProperties") || (bool) node["additionalProperties"] != false)
        {
            std::printf ("  schema object at %s is not closed\n", path.toRawUTF8());
            ok = false;
        }

        auto* props = node["properties"].getDynamicObject();
        auto* required = node["required"].getArray();

        if (props == nullptr || required == nullptr || required->size() != props->getProperties().size())
        {
            std::printf ("  schema object at %s doesn't require all properties\n", path.toRawUTF8());
            ok = false;
        }

        if (props != nullptr)
            for (auto& p : props->getProperties())
                ok = schemaIsStrict (p.value, path + "." + p.name.toString()) && ok;
    }

    if (node["type"].toString() == "array")
        ok = schemaIsStrict (node["items"], path + "[]") && ok;

    for (auto* forbidden : { "minimum", "maximum", "minLength", "maxLength", "multipleOf" })
        if (node.hasProperty (forbidden))
        {
            std::printf ("  schema at %s uses unsupported keyword %s\n", path.toRawUTF8(), forbidden);
            ok = false;
        }

    return ok;
}

static juce::String successBody (const juce::String& planJson)
{
    juce::DynamicObject::Ptr textBlock = new juce::DynamicObject();
    textBlock->setProperty ("type", "text");
    textBlock->setProperty ("text", planJson);

    juce::DynamicObject::Ptr thinking = new juce::DynamicObject();
    thinking->setProperty ("type", "thinking");
    thinking->setProperty ("thinking", "");
    thinking->setProperty ("signature", "abc");

    juce::DynamicObject::Ptr msg = new juce::DynamicObject();
    msg->setProperty ("id", "msg_test");
    msg->setProperty ("type", "message");
    msg->setProperty ("role", "assistant");
    msg->setProperty ("model", "claude-opus-5-5");
    msg->setProperty ("stop_reason", "end_turn");
    msg->setProperty ("content", juce::Array<juce::var> { juce::var (thinking.get()), juce::var (textBlock.get()) });
    return juce::JSON::toString (juce::var (msg.get()), true);
}

int main()
{
    std::printf ("updates\n");
    {
        using namespace updates;
        CHECK (compareVersions ("1.10.0", "1.9.2") == 1);
        CHECK (compareVersions ("v1.0", "1.0.0") == 0);
        CHECK (compareVersions ("1.0.0", "1.0.1") == -1);
        CHECK (compareVersions ("1.2.0-beta", "1.2.0") == 0);
        CHECK (compareVersions ("2", "1.99.99") == 1);

        const juce::String json = R"({"version":"v9.4.1","notes":"Faster preview.","page":"https://example.com/download",
                                      "downloads":{"windows":"https://example.com/a.exe","macos":"http://example.com/b.pkg","linux":"https://example.com/c.tar.gz"}})";
        const auto win = parseManifest (json, "windows");
        CHECK (win.version == "9.4.1" && win.notes == "Faster preview." && win.download == "https://example.com/a.exe");
        CHECK (parseManifest (json, "macos").download.isEmpty());          // not https: never opened
        CHECK (parseManifest (json, "linux").page == "https://example.com/download");
        CHECK (! parseManifest ("not json", "windows").isValid());
        CHECK (! parseManifest (R"({"version":"latest"})", "windows").isValid());
        CHECK (! parseManifest (R"({"tag":"1.0.0"})", "windows").isValid());

        Release newer; newer.version = "999.0.0";
        Release older; older.version = "0.0.1";
        CHECK (newer.isNewer() && ! older.isNewer());
        CHECK (currentVersion().isNotEmpty() && currentVersion() != "0.0.0");
    }

    std::printf ("schema\n");
    {
        const auto schema = SpecJson::outputSchema();
        CHECK (schemaIsStrict (schema, "$"));
        CHECK (schema["properties"]["tracks"]["items"]["properties"]["pattern"]["enum"].size() > 30);
        CHECK (schema["properties"]["mode"]["enum"].size() == (int) pm::allModes().size());
        CHECK (schema["properties"]["orchestral"]["type"].toString() == "boolean");
        CHECK (schema["properties"]["length"]["enum"].size() == 6);
    }

    std::printf ("spec round trip\n");
    for (auto* prompt : { "Generate me a fantasy orchestral midi", "Make me a soft piano midi", "jazz in Bb, swing, 24 bars",
                          "celtic jig", "lofi hip hop with harp" })
    {
        const auto spec = pm::interpretPrompt (prompt, 11);
        pm::StyleSpec back;
        juce::String error;
        const auto json = juce::JSON::toString (SpecJson::specToVar (spec));
        CHECK (SpecJson::specFromVar (juce::JSON::parse (json), back, error));
        CHECK (back.tracks.size() == spec.tracks.size());
        CHECK (back.keyRoot == spec.keyRoot && back.mode == spec.mode && back.tempo == spec.tempo && back.bars == spec.bars);
        CHECK (back.tsNum == spec.tsNum && back.tsDen == spec.tsDen && back.progression == spec.progression);
        CHECK (back.progressionB == spec.progressionB && back.finalLift == spec.finalLift);
        CHECK (back.orchestral == spec.orchestral && back.length == spec.length);
        CHECK (pm::writeMidiFile (pm::compose (back, 5)) == pm::writeMidiFile (pm::compose (spec, 5)));
    }

    std::printf ("composition round trip\n");
    for (auto* prompt : { "epic trailer", "Dark fantasy orchestral villain theme", "orchestral battle loop", "lofi beat" })
    {
        const auto comp = pm::compose (pm::interpretPrompt (prompt, 3), 3);
        pm::Composition back;
        CHECK (SpecJson::compositionFromVar (juce::JSON::parse (juce::JSON::toString (SpecJson::compositionToVar (comp))), back));
        CHECK (pm::writeMidiFile (back) == pm::writeMidiFile (comp));
        CHECK (back.chords.size() == comp.chords.size());
        CHECK (back.markers.size() == comp.markers.size() && back.loopStart == comp.loopStart && back.loopEnd == comp.loopEnd);
        CHECK (back.form == comp.form && std::abs (back.quality.score - comp.quality.score) < 1e-6);
        CHECK (back.quality.notesText == comp.quality.notesText);

        // ...and so does the request check, criterion by criterion.
        CHECK (back.request.total() == comp.request.total() && back.request.met() == comp.request.met());

        for (size_t k = 0; k < comp.request.criteria.size() && k < back.request.criteria.size(); ++k)
            CHECK (back.request.criteria[k].label == comp.request.criteria[k].label);

        // Articulations, chord tones, pedals and split parts survive a project save.
        bool same = back.tracks.size() == comp.tracks.size();

        for (size_t t = 0; same && t < comp.tracks.size(); ++t)
        {
            same = same && back.tracks[t].splitFrom == comp.tracks[t].splitFrom && back.tracks[t].notes.size() == comp.tracks[t].notes.size();

            for (size_t n = 0; same && n < comp.tracks[t].notes.size(); ++n)
                same = same && back.tracks[t].notes[n].articulation == comp.tracks[t].notes[n].articulation;
        }

        for (size_t c = 0; same && c < comp.chords.size(); ++c)
            same = same && back.chords[c].pcs == comp.chords[c].pcs && back.chords[c].bassPc == comp.chords[c].bassPc;

        CHECK (same);

        pm::MidiExportOptions ks;
        ks.articulations = pm::ArticulationMap::keyswitchDefaults();
        CHECK (pm::writeMidiFile (back, -1, ks) == pm::writeMidiFile (comp, -1, ks));
    }

    std::printf ("older project states\n");
    {
        // Version-1 states stored four numbers per note and no cue data.
        const auto old = juce::JSON::parse (R"({"title":"Old","tempo":100,"tsNum":4,"tsDen":4,"bars":1,"ticksPerBar":1920,
            "lengthTicks":1920,"keyRoot":0,"mode":"major","chords":[[0,1920,"C","I"]],"progression":["I"],
            "tracks":[{"name":"Piano","role":"melody","program":0,"channel":0,"isDrum":false,"notes":[0,480,60,90,480,480,64,90],"cc":[]}]})");
        pm::Composition c;
        CHECK (SpecJson::compositionFromVar (old, c));
        CHECK (c.tracks.size() == 1 && c.tracks[0].notes.size() == 2 && c.tracks[0].notes[1].pitch == 64);
        CHECK (c.tracks[0].notes[0].articulation == pm::Articulation::Default && c.loopStart == -1 && c.markers.empty());
    }

    std::printf ("request body\n");
    {
        Claude::Options o;
        o.apiKey = "sk-test";
        const auto body = juce::JSON::parse (Claude::buildRequestBody ("soft piano", "key D, 16 bars", o));
        CHECK (body["model"].toString() == "claude-opus-5-5");
        CHECK ((int) body["max_tokens"] == 16000);
        CHECK (body["output_config"]["format"]["type"].toString() == "json_schema");
        CHECK (body["output_config"]["effort"].toString() == "medium");
        CHECK (body["fallbacks"].toString() == "default");
        CHECK (! body.hasProperty ("temperature") && ! body.hasProperty ("thinking"));
        CHECK (body["messages"][0]["content"].toString().contains ("soft piano"));
        CHECK (body["messages"][0]["content"].toString().contains ("key D, 16 bars"));
        CHECK (body["system"].toString().contains ("orchestral") && body["system"].toString().contains ("Violins I"));
        CHECK (Claude::buildHeaders (o).contains ("anthropic-version: 2026-10-21"));
        CHECK (Claude::buildHeaders (o).contains ("anthropic-beta: server-side-fallback-2026-07-01"));

        o.model = "claude-haiku-5-5";
        const auto haiku = juce::JSON::parse (Claude::buildRequestBody ("x", {}, o));
        CHECK (! haiku.hasProperty ("fallbacks"));
        CHECK (! Claude::buildHeaders (o).contains ("anthropic-beta"));
    }

    std::printf ("responses\n");
    {
        const juce::String plan = R"({
            "title": "Moonlit Harbour", "key": "D", "mode": "dorian", "tempo": 88, "timeSignature": "6/8", "bars": 16,
            "progression": ["i", "IV", "VII", "i"], "progressionB": ["III", "VII", "IV", "IV"], "finalKeyChange": 2,
            "chordsPerBar": 1, "swing": 0, "swingSixteenths": false,
            "energy": 0.4, "complexity": 0.3, "humanize": 0.6, "dynamics": "arc", "ending": "resolve",
            "theme": [ {"degree": 0, "beats": 1.5, "rest": false}, {"degree": 2, "beats": 1.5, "rest": false} ],
            "tracks": [
              {"name": "Whistle", "role": "melody", "program": 78, "lowNote": 67, "highNote": 93, "density": 0.5, "velocity": 90, "entryBar": 0, "pattern": "lyrical"},
              {"name": "Guitar", "role": "chords", "program": 25, "lowNote": 45, "highNote": 72, "density": 0.6, "velocity": 75, "entryBar": 0, "pattern": "strum"},
              {"name": "Bodhran", "role": "percussion", "program": 0, "lowNote": 35, "highNote": 81, "density": 0.5, "velocity": 85, "entryBar": 4, "pattern": "folk"}
            ] })";

        const auto r = Claude::parseResponse (200, successBody (plan));
        CHECK (r.ok);
        CHECK (r.model == "claude-opus-5-5");
        CHECK (r.spec.title == "Moonlit Harbour");
        CHECK (r.spec.keyRoot == 2 && r.spec.mode == pm::Mode::Dorian && r.spec.tsNum == 6 && r.spec.tsDen == 8);
        CHECK (r.spec.tracks.size() == 3 && r.spec.tracks[2].channel == 9 && r.spec.tracks[2].entryBar == 4);
        CHECK (r.spec.theme.size() == 2);
        CHECK (r.spec.progressionB.size() == 4 && r.spec.finalLift == 2);
        CHECK (pm::compose (r.spec, 1).noteCount() > 0);
        CHECK (! r.spec.orchestral && r.spec.length == "auto");

        // An orchestral plan from Claude becomes a full cue.
        const juce::String cuePlan = R"({
            "title": "Siege of Emberfall", "key": "D", "mode": "minor", "tempo": 132, "timeSignature": "4/4", "bars": 16,
            "orchestral": true, "length": "cue",
            "progression": ["i", "VI", "III", "VII"], "progressionB": [], "finalKeyChange": 2,
            "chordsPerBar": 1, "swing": 0, "swingSixteenths": false,
            "energy": 0.85, "complexity": 0.2, "humanize": 0.3, "dynamics": "build", "ending": "loop", "theme": [],
            "tracks": [
              {"name": "Horns", "role": "melody", "program": 60, "lowNote": 41, "highNote": 77, "density": 0.45, "velocity": 98, "entryBar": 0, "pattern": "lyrical"},
              {"name": "Trombones", "role": "chords", "program": 57, "lowNote": 40, "highNote": 70, "density": 0.4, "velocity": 94, "entryBar": 0, "pattern": "block"},
              {"name": "Timpani", "role": "bass", "program": 47, "lowNote": 38, "highNote": 57, "density": 0.5, "velocity": 98, "entryBar": 0, "pattern": "timpani"},
              {"name": "Taiko Ensemble", "role": "percussion", "program": 0, "lowNote": 35, "highNote": 81, "density": 0.6, "velocity": 104, "entryBar": 0, "pattern": "tribal"},
              {"name": "Violins I", "role": "pad", "program": 48, "lowNote": 55, "highNote": 96, "density": 0.3, "velocity": 84, "entryBar": 0, "pattern": "sustain"},
              {"name": "Violas", "role": "ostinato", "program": 48, "lowNote": 48, "highNote": 79, "density": 0.7, "velocity": 80, "entryBar": 0, "pattern": "epic"},
              {"name": "Celli", "role": "pad", "program": 48, "lowNote": 36, "highNote": 72, "density": 0.3, "velocity": 82, "entryBar": 0, "pattern": "sustain"},
              {"name": "Basses", "role": "bass", "program": 43, "lowNote": 28, "highNote": 55, "density": 0.3, "velocity": 84, "entryBar": 0, "pattern": "sustain"}
            ] })";

        const auto cue = Claude::parseResponse (200, successBody (cuePlan));
        CHECK (cue.ok && cue.spec.orchestral && cue.spec.length == "cue" && ! cue.spec.resolveEnding);
        const auto cueComp = pm::compose (cue.spec, 2);
        CHECK (cueComp.seconds() > 40.0 && cueComp.loopEnd > cueComp.loopStart && ! cueComp.form.empty());
        CHECK (cueComp.quality.score >= 8.0);

        juce::DynamicObject::Ptr refusal = new juce::DynamicObject();
        refusal->setProperty ("type", "message");
        refusal->setProperty ("stop_reason", "refusal");
        refusal->setProperty ("content", juce::Array<juce::var>());
        const auto rr = Claude::parseResponse (200, juce::JSON::toString (juce::var (refusal.get())));
        CHECK (! rr.ok && rr.error.contains ("declined"));

        const auto unauthorized = Claude::parseResponse (401, R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})");
        CHECK (! unauthorized.ok && ! unauthorized.retryable && unauthorized.error.contains ("API key") && unauthorized.error.contains ("invalid x-api-key"));

        const auto overloaded = Claude::parseResponse (529, R"({"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})");
        CHECK (! overloaded.ok && overloaded.retryable);

        const auto offline = Claude::parseResponse (0, {});
        CHECK (! offline.ok && offline.retryable);

        const auto garbage = Claude::parseResponse (200, successBody ("not json at all"));
        CHECK (! garbage.ok);
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
