// Tests for the JSON layer and the Claude request/response handling (no network).

#include <juce_core/juce_core.h>

#include "ClaudeClient.h"
#include "Composer.h"
#include "MidiFileWriter.h"
#include "PromptInterpreter.h"
#include "SpecJson.h"

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
    std::printf ("schema\n");
    {
        const auto schema = SpecJson::outputSchema();
        CHECK (schemaIsStrict (schema, "$"));
        CHECK (schema["properties"]["tracks"]["items"]["properties"]["pattern"]["enum"].size() > 30);
        CHECK (schema["properties"]["mode"]["enum"].size() == (int) pm::allModes().size());
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
        CHECK (pm::writeMidiFile (pm::compose (back, 5)) == pm::writeMidiFile (pm::compose (spec, 5)));
    }

    std::printf ("composition round trip\n");
    {
        const auto comp = pm::compose (pm::interpretPrompt ("epic trailer", 3), 3);
        pm::Composition back;
        CHECK (SpecJson::compositionFromVar (juce::JSON::parse (juce::JSON::toString (SpecJson::compositionToVar (comp))), back));
        CHECK (pm::writeMidiFile (back) == pm::writeMidiFile (comp));
        CHECK (back.chords.size() == comp.chords.size());
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
            "progression": ["i", "IV", "VII", "i"], "chordsPerBar": 1, "swing": 0, "swingSixteenths": false,
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
        CHECK (pm::compose (r.spec, 1).noteCount() > 0);

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
