#include "ClaudeClient.h"
#include "SpecJson.h"

namespace Claude
{

namespace
{
    const char* const endpoint = "https://api.anthropic.com/v1/messages";
    const char* const apiVersion = "2026-10-21";
    const char* const fallbackBeta = "server-side-fallback-2026-07-01";

    juce::String rolePatternList()
    {
        juce::String s;

        for (auto r : pm::allRoles())
        {
            s << "- " << pm::roleId (r) << ": ";
            juce::StringArray names;

            for (auto& p : pm::patternsForRole (r))
                names.add (juce::String (p));

            s << names.joinIntoString (", ") << "\n";
        }

        return s;
    }

    juce::var makeObject() { return juce::var (new juce::DynamicObject()); }

    void put (juce::var& o, const char* key, const juce::var& value)
    {
        o.getDynamicObject()->setProperty (juce::Identifier (key), value);
    }
}

juce::String systemPrompt()
{
    juce::String s;
    s << "You are the composition planner inside PromptMIDI, a MIDI generator plug-in for music producers. "
         "The user describes the music they want in plain words. You answer with a JSON composition plan; "
         "PromptMIDI's engine turns the plan into multi-track MIDI (voicings, rhythms, voice leading and drums are "
         "written by the engine from your choices).\n\n"

         "Choose what a skilled composer and arranger would for the description: an idiomatic tempo, key, mode, "
         "harmony, instrumentation, registers, playing patterns and arrangement. Honour every explicit request "
         "(key, tempo, length, time signature, instruments to include or leave out, mood). If the plug-in passes "
         "required settings, use them exactly.\n\n"

         "Field guide:\n"
         "- tempo: quarter-note BPM. bars: usually 8 (loops) or 16; 12 for a 12-bar blues; up to 64.\n"
         "- timeSignature: 3/4 for waltzes, 6/8 or 12/8 for jigs and compound feels, otherwise 4/4.\n"
         "- progression: roman numerals relative to the key, one per chord slot, cycled across the piece. Numerals "
         "count scale degrees of the chosen mode (in A minor, i VI III VII = Am F C G). Uppercase = major triad, "
         "lowercase = minor. Suffixes: 7, maj7, m7b5, o (diminished), +, sus2, sus4, add9, 9, maj9, 6. Accidentals "
         "b and # shift the root (bVII, bII). Secondary chords: V7/V. Typically 2-8 chords.\n"
         "- Form: the engine lays out an intro (until the melody enters), then 4-bar phrases A A B A (A B A for three "
         "phrases). progressionB is the harmony of the B section: make it contrast with progression (e.g. start on IV, vi "
         "or bVI), or [] to reuse progression. finalKeyChange lifts the last A up by that many semitones for a climactic "
         "final statement (2 is typical for heroic and epic music, 0 for loops and quiet pieces).\n"
         "- chordsPerBar: 1 is common; 0.5 or 0.25 for slow ambient or trap harmony; 2 for busy harmony.\n"
         "- swing: 0 straight; 0.6-0.9 for jazz and shuffles; with swingSixteenths=true, 0.3-0.6 for hip hop, lo-fi and R&B.\n"
         "- energy, complexity (7ths/9ths, higher for jazz, lo-fi, R&B), humanize: 0-1.\n"
         "- dynamics: flat, build, arc or fade. ending: resolve (final bar lands on the tonic) or loop (seamless loop).\n"
         "- theme: a memorable main motif for the first melody track, 1-4 bars long, total length a whole number of bars. "
         "degree is a scale step from the tonic in the chosen mode (0 tonic, 1 next note up, -1 the note below; "
         "an octave is 7 steps in 7-note modes, 5 in pentatonic modes, 6 in blues). beats is a duration in quarter "
         "notes; rest=true for silence. Give a theme whenever the description implies a tune; use [] otherwise.\n"
         "- orchestral: true for orchestral, cinematic, film-score and game-score requests (and whenever an orchestra is "
         "asked for). The engine then composes a complete cue instead of a loop: intro, theme, restatement, development "
         "(fragmentation, sequence, imitation), a second theme in a related key, a build, a climax and an outro (or, with "
         "ending=loop, an end stinger and loop markers), with phrase-level cadences, pedal points, reharmonised "
         "restatements, articulations, runs, swells and tutti hits. Your tracks are the orchestra: list each section as "
         "its own track in score order - woodwinds (Piccolo 72, Flutes 73, Oboes 68, Clarinets 71, Bassoons 70), brass "
         "(Horns 60, Trumpets 56, Trombones 57, Tuba 58), Timpani 47, percussion, Harp 46 / Celesta 8 / Glockenspiel 9, "
         "Choir 52, soloists, then strings (Violins I 48, Violins II 48, Violas 48, Celli 48, Basses 43). The first melody "
         "track states the main theme (e.g. Horns for heroic music, Oboes for pastoral, Trombones for a villain, a Solo "
         "Cello for a lament). Give orchestral percussion as separate tracks named Bass Drum, Cymbals, Snare Drum, Taiko "
         "Ensemble, Tam-tam or Triangle with pattern orchestral (tribal for taiko). An ostinato-role strings track makes "
         "the cue rhythmically driven; an arpeggio-role harp, celesta or piano adds a flowing texture; a pizzicato "
         "(program 45) track gives a pizzicato accompaniment. Genre colours (fiddle, shakuhachi, koto, oud, bagpipe) join "
         "as soloists.\n"
         "- length (orchestral cues): auto, sketch (16 bars), cue (about 70 seconds), full (about two and a half minutes), "
         "extended (about four minutes) or bars (exactly bars). Prefer full unless the user asks for something short "
         "(a stinger, a fanfare or a sketch) or long.\n"
         "- tracks: 1-10 parts for songs and loops, up to 24 for orchestral cues; for non-orchestral music at most one "
         "percussion track (and none if the user asks for no drums). Lead with the melody track. program is a General MIDI program number: 0 piano, 4 electric piano, 6 harpsichord, "
         "8 celesta, 9 glockenspiel, 10 music box, 11 vibraphone, 12 marimba, 19 church organ, 21 accordion, "
         "24 nylon guitar, 25 steel guitar, 26 jazz guitar, 27 clean electric guitar, 29 overdriven guitar, "
         "30 distortion guitar, 32 upright bass, 33 electric bass, 36 slap bass, 38 synth bass, 40 violin, 41 viola, "
         "42 cello, 43 contrabass, 44 tremolo strings, 45 pizzicato strings, 46 harp, 47 timpani, 48 string ensemble, "
         "49 slow strings, 52 choir, 54 synth voice, 56 trumpet, 57 trombone, 58 tuba, 60 french horn, "
         "61 brass section, 65 alto sax, 66 tenor sax, 68 oboe, 70 bassoon, 71 clarinet, 72 piccolo, 73 flute, "
         "75 pan flute, 77 shakuhachi, 78 whistle, 80 square lead, 81 saw lead, 89 warm pad, 90 polysynth, "
         "94 halo pad, 98 crystal, 104 sitar, 105 banjo, 107 koto, 108 kalimba, 109 bagpipe, 110 fiddle, "
         "114 steel drums. Use 0 for percussion.\n"
         "- lowNote/highNote: the part's register as MIDI notes (60 = middle C), at least 18 semitones apart and "
         "idiomatic for the instrument (bass parts roughly 28-55, leads 60-90).\n"
         "- density 0-1 (how busy the part is), velocity 1-127, entryBar: 0-based bar where the part enters; stagger "
         "entries in longer pieces to build the arrangement (e.g. strings and harp from bar 0, the theme from bar 4, "
         "brass, choir and drums from bar 8).\n"
         "- countermelody pattern double doubles the lead melody in that track's register (e.g. violins an octave above "
         "a horn theme); give it a later entryBar so the doubling arrives at the climax.\n"
         "- Orchestration: write for a real orchestra when the request is orchestral - strings, woodwinds, brass, "
         "percussion - keep each part within its instrument's practical range, and keep pads above the bass register.\n"
         "- pattern: one of the patterns for the track's role, or auto:\n"
      << rolePatternList()
      << "  Percussion patterns name the groove (e.g. orchestral for cinematic drums, four_on_floor for house).\n";

    return s;
}

juce::String buildUserMessage (const juce::String& prompt, const juce::String& constraints)
{
    juce::String m;
    m << "Describe this music as a PromptMIDI plan:\n\n" << prompt.trim();

    if (constraints.isNotEmpty())
        m << "\n\nRequired settings from the plug-in controls: " << constraints;

    return m;
}

bool modelSupportsFallbacks (const juce::String& model)
{
    static const juce::StringArray supported { "claude-fable-5-2", "claude-fable-5-1", "claude-opus-5-5", "claude-opus-5", "claude-sonnet-5-5" };
    return supported.contains (model.trim());
}

juce::String buildRequestBody (const juce::String& prompt, const juce::String& constraints, const Options& opt)
{
    juce::var body = makeObject();
    put (body, "model", opt.model.trim());
    put (body, "max_tokens", 16000);
    put (body, "system", systemPrompt());

    juce::var message = makeObject();
    put (message, "role", "user");
    put (message, "content", buildUserMessage (prompt, constraints));
    put (body, "messages", juce::Array<juce::var> { message });

    juce::var format = makeObject();
    put (format, "type", "json_schema");
    put (format, "schema", SpecJson::outputSchema());

    juce::var outputConfig = makeObject();
    put (outputConfig, "effort", opt.effort.isNotEmpty() ? opt.effort : juce::String ("medium"));
    put (outputConfig, "format", format);
    put (body, "output_config", outputConfig);

    // If a safety classifier declines, let the API retry on its recommended fallback model.
    if (modelSupportsFallbacks (opt.model))
        put (body, "fallbacks", "default");

    return juce::JSON::toString (body, true);
}

juce::String buildHeaders (const Options& opt)
{
    juce::String h;
    h << "x-api-key: " << opt.apiKey.trim() << "\r\n"
      << "anthropic-version: " << apiVersion << "\r\n"
      << "content-type: application/json\r\n";

    if (modelSupportsFallbacks (opt.model))
        h << "anthropic-beta: " << fallbackBeta << "\r\n";

    return h;
}

Result parseResponse (int status, const juce::String& body)
{
    Result r;
    const auto json = juce::JSON::parse (body);

    if (status != 200)
    {
        juce::String apiMessage;

        if (json.isObject() && json["error"].isObject())
            apiMessage = json["error"]["message"].toString();

        juce::String prefix;

        switch (status)
        {
            case 0:   prefix = "Couldn't reach api.anthropic.com. Check your internet connection."; break;
            case 400: prefix = "Claude rejected the request."; break;
            case 401: prefix = "Your API key was rejected. Check it in Settings."; break;
            case 403: prefix = "This API key isn't allowed to use that model."; break;
            case 404: prefix = "Model not found. Check the model name in Settings."; break;
            case 413: prefix = "The prompt is too long."; break;
            case 429: prefix = "Rate limited by the API. Try again in a moment."; break;
            case 529: prefix = "Claude is overloaded right now. Try again shortly."; break;
            default:  prefix = "The Claude API returned HTTP " + juce::String (status) + "."; break;
        }

        r.error = apiMessage.isNotEmpty() ? prefix + " (" + apiMessage + ")" : prefix;
        r.retryable = status == 0 || status == 429 || status >= 500;
        return r;
    }

    if (! json.isObject())
    {
        r.error = "The response from Claude wasn't valid JSON.";
        return r;
    }

    r.model = json["model"].toString();
    const auto stopReason = json["stop_reason"].toString();

    if (stopReason == "refusal")
    {
        r.error = "Claude declined this request. Try rephrasing your prompt, or switch to Offline.";
        return r;
    }

    if (stopReason == "max_tokens")
    {
        r.error = "Claude's answer was cut off. Try a shorter or simpler prompt.";
        return r;
    }

    juce::String text;

    if (auto* content = json["content"].getArray())
        for (auto& block : *content)
            if (block["type"].toString() == "text")
                text << block["text"].toString();

    if (text.trim().isEmpty())
    {
        r.error = "Claude returned an empty answer.";
        return r;
    }

    juce::var plan;
    const auto parseResult = juce::JSON::parse (text, plan);

    if (parseResult.failed() || ! plan.isObject())
    {
        r.error = "Claude's plan couldn't be read (" + parseResult.getErrorMessage() + ").";
        return r;
    }

    juce::String err;

    if (! SpecJson::specFromVar (plan, r.spec, err))
    {
        r.error = "Claude's plan was incomplete: " + err;
        return r;
    }

    r.ok = true;
    return r;
}

//==============================================================================
Job::Job (juce::String p, juce::String c, Options o, Callback cb)
    : juce::Thread ("PromptMIDI Claude request"),
      prompt (std::move (p)), constraints (std::move (c)), options (std::move (o)), callback (std::move (cb))
{
    startThread();
}

Job::~Job()
{
    cancel();
    stopThread (5000);
}

void Job::cancel()
{
    alive->store (false);
    signalThreadShouldExit();

    const juce::ScopedLock sl (streamLock);

    if (stream != nullptr)
        stream->cancel();
}

Result Job::attempt()
{
    juce::URL url = juce::URL (endpoint).withPOSTData (buildRequestBody (prompt, constraints, options));

    {
        const juce::ScopedLock sl (streamLock);

        if (threadShouldExit())
            return {};

        stream = std::make_unique<juce::WebInputStream> (url, true);
        stream->withExtraHeaders (buildHeaders (options))
               .withConnectionTimeout (options.timeoutMs);
    }

    const bool connected = stream->connect (nullptr);
    const int status = connected ? stream->getStatusCode() : 0;
    const auto body = connected ? stream->readEntireStreamAsString() : juce::String();

    {
        const juce::ScopedLock sl (streamLock);
        stream.reset();
    }

    return parseResponse (status, body);
}

void Job::run()
{
    Result result;

    for (int tries = 0; tries < 3 && ! threadShouldExit(); ++tries)
    {
        result = attempt();

        if (result.ok || ! result.retryable)
            break;

        wait (1500 * (1 << tries)); // 1.5 s, 3 s back-off
    }

    if (threadShouldExit())
        return;

    auto token = alive;
    auto cb = callback;

    juce::MessageManager::callAsync ([token, cb, result]
    {
        if (token->load() && cb != nullptr)
            cb (result);
    });
}

} // namespace Claude
