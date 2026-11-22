#include "SpecJson.h"

namespace SpecJson
{

namespace
{
    juce::var makeObject() { return juce::var (new juce::DynamicObject()); }

    void put (juce::var& o, const char* key, const juce::var& value)
    {
        o.getDynamicObject()->setProperty (juce::Identifier (key), value);
    }

    bool has (const juce::var& o, const char* key)
    {
        return o.isObject() && o.hasProperty (juce::Identifier (key)) && ! o[key].isVoid() && ! o[key].isUndefined();
    }

    double getDouble (const juce::var& o, const char* key, double fallback)
    {
        if (! has (o, key))
            return fallback;

        const auto& v = o[key];

        if (v.isString())
            return v.toString().getDoubleValue();

        return (double) v;
    }

    int getInt (const juce::var& o, const char* key, int fallback)
    {
        return (int) std::lround (getDouble (o, key, (double) fallback));
    }

    bool getBool (const juce::var& o, const char* key, bool fallback)
    {
        if (! has (o, key))
            return fallback;

        const auto& v = o[key];

        if (v.isString())
            return v.toString().equalsIgnoreCase ("true");

        return (bool) v;
    }

    juce::String getString (const juce::var& o, const char* key, const juce::String& fallback)
    {
        return has (o, key) ? o[key].toString() : fallback;
    }

    std::string toStd (const juce::String& s) { return s.toStdString(); }

    juce::var stringArray (const std::vector<std::string>& items)
    {
        juce::Array<juce::var> a;

        for (auto& s : items)
            a.add (juce::String (s));

        return a;
    }

    juce::var enumOf (const juce::StringArray& values)
    {
        juce::var o = makeObject();
        put (o, "type", "string");
        juce::Array<juce::var> e;

        for (auto& v : values)
            e.add (v);

        put (o, "enum", e);
        return o;
    }

    juce::var typed (const char* type, const char* description = nullptr)
    {
        juce::var o = makeObject();
        put (o, "type", type);

        if (description != nullptr)
            put (o, "description", description);

        return o;
    }

    juce::var closedObject (const std::vector<std::pair<const char*, juce::var>>& props)
    {
        juce::var o = makeObject();
        put (o, "type", "object");

        juce::var properties = makeObject();
        juce::Array<juce::var> required;

        for (auto& p : props)
        {
            put (properties, p.first, p.second);
            required.add (juce::String (p.first));
        }

        put (o, "properties", properties);
        put (o, "required", required);
        put (o, "additionalProperties", false);
        return o;
    }

    juce::var withDescription (juce::var v, const char* description)
    {
        put (v, "description", description);
        return v;
    }
}

juce::String keyName (int pc)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[pm::wrapPc (pc)];
}

juce::var specToVar (const pm::StyleSpec& s)
{
    juce::var o = makeObject();
    put (o, "title", juce::String (s.title));
    put (o, "key", keyName (s.keyRoot));
    put (o, "mode", juce::String (pm::modeInfo (s.mode).id));
    put (o, "tempo", s.tempo);
    put (o, "timeSignature", juce::String (s.tsNum) + "/" + juce::String (s.tsDen));
    put (o, "bars", s.bars);
    put (o, "progression", stringArray (s.progression));
    put (o, "progressionB", stringArray (s.progressionB));
    put (o, "finalKeyChange", s.finalLift);
    put (o, "chordsPerBar", s.chordsPerBar);
    put (o, "swing", s.swing);
    put (o, "swingSixteenths", s.swingSixteenths);
    put (o, "energy", s.energy);
    put (o, "complexity", s.complexity);
    put (o, "humanize", s.humanize);
    put (o, "dynamics", juce::String (pm::dynamicsId (s.dynamics)));
    put (o, "ending", s.resolveEnding ? "resolve" : "loop");
    put (o, "orchestral", s.orchestral);
    put (o, "length", juce::String (s.length));

    juce::Array<juce::var> theme;

    for (auto& n : s.theme)
    {
        juce::var t = makeObject();
        put (t, "degree", n.degree);
        put (t, "beats", n.beats);
        put (t, "rest", n.rest);
        theme.add (t);
    }

    put (o, "theme", theme);

    juce::Array<juce::var> tracks;

    for (auto& t : s.tracks)
    {
        juce::var tr = makeObject();
        put (tr, "name", juce::String (t.name));
        put (tr, "role", juce::String (pm::roleId (t.role)));
        put (tr, "program", t.program);
        put (tr, "lowNote", t.lowNote);
        put (tr, "highNote", t.highNote);
        put (tr, "density", t.density);
        put (tr, "velocity", t.velocity);
        put (tr, "entryBar", t.entryBar);
        put (tr, "pattern", juce::String (t.pattern));

        if (t.mergeInto >= 0)
            put (tr, "mergeInto", t.mergeInto);

        tracks.add (tr);
    }

    put (o, "tracks", tracks);

    if (! s.styleId.empty())
        put (o, "styleId", juce::String (s.styleId));

    if (! s.interpretation.empty())
        put (o, "interpretation", stringArray (s.interpretation));

    return o;
}

bool specFromVar (const juce::var& o, pm::StyleSpec& s, juce::String& error)
{
    if (! o.isObject())
    {
        error = "Expected a JSON object";
        return false;
    }

    s = pm::StyleSpec();
    s.title = toStd (getString (o, "title", "Untitled"));
    s.styleId = toStd (getString (o, "styleId", ""));

    if (has (o, "key"))
    {
        int pc = 0;

        if (o["key"].isString() && pm::pitchClassFromString (toStd (o["key"].toString()), pc))
            s.keyRoot = pc;
        else if (! o["key"].isString())
            s.keyRoot = pm::wrapPc ((int) o["key"]);
    }

    pm::Mode mode;

    if (pm::modeFromString (toStd (getString (o, "mode", "major")), mode))
        s.mode = mode;

    s.tempo = getInt (o, "tempo", s.tempo);

    const auto ts = getString (o, "timeSignature", "4/4");

    if (ts.containsChar ('/'))
    {
        s.tsNum = ts.upToFirstOccurrenceOf ("/", false, false).getIntValue();
        s.tsDen = ts.fromFirstOccurrenceOf ("/", false, false).getIntValue();
    }

    s.bars = getInt (o, "bars", s.bars);

    if (auto* prog = o["progression"].getArray())
    {
        s.progression.clear();

        for (auto& c : *prog)
            s.progression.push_back (toStd (c.toString()));
    }

    if (auto* progB = o["progressionB"].getArray())
        for (auto& c : *progB)
            s.progressionB.push_back (toStd (c.toString()));

    s.finalLift = getInt (o, "finalKeyChange", 0);
    s.chordsPerBar = getDouble (o, "chordsPerBar", s.chordsPerBar);
    s.swing = getDouble (o, "swing", s.swing);
    s.swingSixteenths = getBool (o, "swingSixteenths", s.swingSixteenths);
    s.energy = getDouble (o, "energy", s.energy);
    s.complexity = getDouble (o, "complexity", s.complexity);
    s.humanize = getDouble (o, "humanize", s.humanize);

    pm::Dynamics dyn;

    if (pm::dynamicsFromString (toStd (getString (o, "dynamics", "arc")), dyn))
        s.dynamics = dyn;

    s.resolveEnding = getString (o, "ending", "resolve").trim().equalsIgnoreCase ("resolve");
    s.orchestral = getBool (o, "orchestral", false);
    s.length = toStd (getString (o, "length", "auto").trim().toLowerCase());

    if (auto* theme = o["theme"].getArray())
    {
        for (auto& n : *theme)
        {
            pm::ThemeNote tn;
            tn.degree = getInt (n, "degree", 0);
            tn.beats = getDouble (n, "beats", 1.0);
            tn.rest = getBool (n, "rest", false);
            s.theme.push_back (tn);
        }
    }

    s.tracks.clear();

    if (auto* tracks = o["tracks"].getArray())
    {
        for (auto& t : *tracks)
        {
            if (! t.isObject())
                continue;

            pm::TrackSpec tr;
            tr.name = toStd (getString (t, "name", ""));

            pm::Role role;

            if (pm::roleFromString (toStd (getString (t, "role", "melody")), role))
                tr.role = role;

            tr.program = getInt (t, "program", 0);
            tr.lowNote = getInt (t, "lowNote", 48);
            tr.highNote = getInt (t, "highNote", 84);
            tr.density = getDouble (t, "density", 0.5);
            tr.velocity = getInt (t, "velocity", 90);
            tr.entryBar = getInt (t, "entryBar", 0);
            tr.pattern = toStd (getString (t, "pattern", "auto"));
            tr.mergeInto = getInt (t, "mergeInto", -1);
            s.tracks.push_back (tr);
        }
    }

    if (auto* notes = o["interpretation"].getArray())
        for (auto& n : *notes)
            s.interpretation.push_back (toStd (n.toString()));

    pm::normalizeSpec (s);
    return true;
}

juce::var compositionToVar (const pm::Composition& c)
{
    juce::var o = makeObject();
    put (o, "title", juce::String (c.title));
    put (o, "tempo", c.tempo);
    put (o, "tsNum", c.tsNum);
    put (o, "tsDen", c.tsDen);
    put (o, "bars", c.bars);
    put (o, "ticksPerBar", c.ticksPerBar);
    put (o, "lengthTicks", c.lengthTicks);
    put (o, "keyRoot", c.keyRoot);
    put (o, "endKeyRoot", c.endKeyRoot);
    put (o, "mode", juce::String (pm::modeInfo (c.mode).id));

    juce::Array<juce::var> chords;

    for (auto& ch : c.chords)
    {
        juce::Array<juce::var> pcs;

        for (int pc : ch.pcs)
            pcs.add (pc);

        juce::Array<juce::var> a { ch.start, ch.end, juce::String (ch.name), juce::String (ch.roman), pcs, ch.bassPc };
        chords.add (a);
    }

    put (o, "chords", chords);
    put (o, "progression", stringArray (c.progression));

    juce::Array<juce::var> markers;

    for (auto& m : c.markers)
        markers.add (juce::Array<juce::var> { m.tick, juce::String (m.text) });

    put (o, "markers", markers);
    put (o, "loopStart", c.loopStart);
    put (o, "loopEnd", c.loopEnd);
    put (o, "form", juce::String (c.form));

    juce::var q = makeObject();
    put (q, "score", c.quality.score);
    put (q, "candidates", c.quality.candidates);
    put (q, "notes", stringArray (c.quality.notesText));
    put (o, "quality", q);

    juce::Array<juce::var> criteria;

    for (auto& cr : c.request.criteria)
    {
        juce::var item = makeObject();
        put (item, "label", juce::String::fromUTF8 (cr.label.c_str()));
        put (item, "met", cr.met);
        put (item, "detail", juce::String::fromUTF8 (cr.detail.c_str()));
        criteria.add (item);
    }

    put (o, "request", criteria);

    juce::Array<juce::var> tracks;

    for (auto& t : c.tracks)
    {
        juce::var tr = makeObject();
        put (tr, "name", juce::String (t.name));
        put (tr, "role", juce::String (pm::roleId (t.role)));
        put (tr, "program", t.program);
        put (tr, "channel", t.channel);
        put (tr, "isDrum", t.isDrum);
        put (tr, "splitFrom", t.splitFrom);

        // start, length, pitch, velocity, articulation
        juce::Array<juce::var> notes;
        notes.ensureStorageAllocated ((int) t.notes.size() * 5);

        for (auto& n : t.notes)
        {
            notes.add (n.start);
            notes.add (n.length);
            notes.add (n.pitch);
            notes.add (n.velocity);
            notes.add ((int) n.articulation);
        }

        put (tr, "noteFields", 5);
        put (tr, "notes", notes);

        juce::Array<juce::var> cc;

        for (auto& ev : t.controls)
        {
            cc.add (ev.tick);
            cc.add (ev.controller);
            cc.add (ev.value);
        }

        put (tr, "cc", cc);
        tracks.add (tr);
    }

    put (o, "tracks", tracks);
    return o;
}

bool compositionFromVar (const juce::var& o, pm::Composition& c)
{
    if (! o.isObject())
        return false;

    c = pm::Composition();
    c.title = toStd (getString (o, "title", ""));
    c.tempo = juce::jlimit (20, 400, getInt (o, "tempo", 120));
    c.tsNum = getInt (o, "tsNum", 4);
    c.tsDen = getInt (o, "tsDen", 4);
    c.bars = getInt (o, "bars", 0);
    c.ticksPerBar = juce::jmax (1, getInt (o, "ticksPerBar", 4 * pm::kPpq));
    c.lengthTicks = juce::jmax (0, getInt (o, "lengthTicks", 0));
    c.keyRoot = pm::wrapPc (getInt (o, "keyRoot", 0));
    c.endKeyRoot = pm::wrapPc (getInt (o, "endKeyRoot", c.keyRoot));

    pm::Mode mode;

    if (pm::modeFromString (toStd (getString (o, "mode", "major")), mode))
        c.mode = mode;

    if (auto* chords = o["chords"].getArray())
    {
        for (auto& ch : *chords)
        {
            if (auto* a = ch.getArray(); a != nullptr && a->size() >= 4)
            {
                pm::ChordSlot slot;
                slot.start = (int) (*a)[0];
                slot.end = (int) (*a)[1];
                slot.name = toStd ((*a)[2].toString());
                slot.roman = toStd ((*a)[3].toString());

                if (a->size() >= 6)
                {
                    if (auto* pcs = (*a)[4].getArray())
                        for (auto& pc : *pcs)
                            slot.pcs.push_back (pm::wrapPc ((int) pc));

                    slot.bassPc = (int) (*a)[5] >= 0 ? pm::wrapPc ((int) (*a)[5]) : -1;
                }

                c.chords.push_back (slot);
            }
        }
    }

    if (auto* prog = o["progression"].getArray())
        for (auto& p : *prog)
            c.progression.push_back (toStd (p.toString()));

    if (auto* markers = o["markers"].getArray())
        for (auto& m : *markers)
            if (auto* a = m.getArray(); a != nullptr && a->size() >= 2)
                c.markers.push_back ({ juce::jmax (0, (int) (*a)[0]), toStd ((*a)[1].toString()) });

    c.loopStart = getInt (o, "loopStart", -1);
    c.loopEnd = getInt (o, "loopEnd", -1);

    if (c.loopStart < 0 || c.loopEnd <= c.loopStart || c.loopEnd > juce::jmax (c.lengthTicks, c.loopStart + 1))
        c.loopStart = c.loopEnd = -1;

    c.form = toStd (getString (o, "form", ""));

    if (o["quality"].isObject())
    {
        const auto& q = o["quality"];
        c.quality.score = getDouble (q, "score", 0.0);
        c.quality.candidates = getInt (q, "candidates", 1);

        if (auto* notes = q["notes"].getArray())
            for (auto& n : *notes)
                c.quality.notesText.push_back (toStd (n.toString()));
    }

    if (auto* criteria = o["request"].getArray())
        for (auto& cr : *criteria)
            if (cr.isObject())
                c.request.criteria.push_back ({ toStd (getString (cr, "label", "")), (bool) cr["met"], toStd (getString (cr, "detail", "")) });

    if (auto* tracks = o["tracks"].getArray())
    {
        for (auto& t : *tracks)
        {
            pm::Track tr;
            tr.name = toStd (getString (t, "name", ""));

            pm::Role role;

            if (pm::roleFromString (toStd (getString (t, "role", "melody")), role))
                tr.role = role;

            tr.program = juce::jlimit (0, 127, getInt (t, "program", 0));
            tr.channel = juce::jlimit (0, 15, getInt (t, "channel", 0));
            tr.isDrum = getBool (t, "isDrum", false);
            tr.splitFrom = getInt (t, "splitFrom", -1);

            // Older states stored four numbers per note (no articulation).
            const int stride = getInt (t, "noteFields", 4) == 5 ? 5 : 4;

            if (auto* notes = t["notes"].getArray())
            {
                for (int i = 0; i + stride - 1 < notes->size(); i += stride)
                {
                    pm::Note n { (int) (*notes)[i], (int) (*notes)[i + 1],
                                 juce::jlimit (0, 127, (int) (*notes)[i + 2]), juce::jlimit (1, 127, (int) (*notes)[i + 3]) };

                    if (stride == 5)
                        n.articulation = (pm::Articulation) juce::jlimit (0, 8, (int) (*notes)[i + 4]);

                    tr.notes.push_back (n);
                }
            }

            if (auto* cc = t["cc"].getArray())
                for (int i = 0; i + 2 < cc->size(); i += 3)
                    tr.controls.push_back ({ (int) (*cc)[i], juce::jlimit (0, 127, (int) (*cc)[i + 1]), juce::jlimit (0, 127, (int) (*cc)[i + 2]) });

            c.tracks.push_back (tr);
        }
    }

    for (auto& t : c.tracks)
        if (t.splitFrom >= (int) c.tracks.size())
            t.splitFrom = -1;

    return true;
}

juce::var outputSchema()
{
    juce::StringArray keys;

    for (int pc = 0; pc < 12; ++pc)
        keys.add (keyName (pc));

    juce::StringArray modes;

    for (auto& m : pm::allModes())
        modes.add (m.id);

    juce::StringArray roles;

    for (auto r : pm::allRoles())
        roles.add (pm::roleId (r));

    juce::StringArray patterns;

    for (auto& p : pm::allPatternIds())
        patterns.add (juce::String (p));

    auto themeNote = closedObject ({
        { "degree", typed ("integer", "Scale step relative to the tonic in the chosen mode (0 = tonic, negative = below)") },
        { "beats",  typed ("number", "Duration in quarter notes") },
        { "rest",   typed ("boolean", "true for a rest of this duration") },
    });

    auto track = closedObject ({
        { "name",     typed ("string", "Track name shown in the DAW, e.g. \"Violins\"") },
        { "role",     enumOf (roles) },
        { "program",  typed ("integer", "General MIDI program number 0-127 (ignored for percussion)") },
        { "lowNote",  typed ("integer", "Lowest MIDI note the part may use (60 = middle C)") },
        { "highNote", typed ("integer", "Highest MIDI note the part may use") },
        { "density",  typed ("number", "0 = very sparse, 1 = very busy") },
        { "velocity", typed ("integer", "Base loudness 1-127") },
        { "entryBar", typed ("integer", "0-based bar where this part comes in") },
        { "pattern",  withDescription (enumOf (patterns), "Playing pattern; must suit the role, or \"auto\"") },
    });

    juce::var themeArray = makeObject();
    put (themeArray, "type", "array");
    put (themeArray, "items", themeNote);

    juce::var trackArray = makeObject();
    put (trackArray, "type", "array");
    put (trackArray, "items", track);

    juce::var progression = makeObject();
    put (progression, "type", "array");
    put (progression, "items", typed ("string"));

    return closedObject ({
        { "title",           typed ("string", "Short title for the piece") },
        { "key",             enumOf (keys) },
        { "mode",            enumOf (modes) },
        { "tempo",           typed ("integer", "Beats per minute (quarter notes), 40-220") },
        { "timeSignature",   enumOf ({ "4/4", "3/4", "2/4", "6/8", "12/8" }) },
        { "bars",            typed ("integer", "Length in bars (4-160); for orchestral cues only used when length is \"bars\"") },
        { "orchestral",      typed ("boolean", "true for orchestral, cinematic, film or game-score music: the engine composes a complete cue") },
        { "length",          withDescription (enumOf ({ "auto", "sketch", "cue", "full", "extended", "bars" }),
                                              "Orchestral cue length: sketch 16 bars, cue about 70 s, full about 2.5 min, extended about 3.75 min, bars = use bars") },
        { "progression",     withDescription (progression, "Roman numerals relative to the key and mode for the A sections") },
        { "progressionB",    withDescription (progression, "Contrasting progression for the B section (bridge); [] to reuse the A progression") },
        { "finalKeyChange",  typed ("integer", "Semitones the final statement of the theme lifts up (0 = no key change, 1-3 typical)") },
        { "chordsPerBar",    typed ("number", "Harmonic rhythm: 0.25, 0.5, 1, 2 or 4") },
        { "swing",           typed ("number", "0 = straight, 1 = full triplet swing") },
        { "swingSixteenths", typed ("boolean", "Swing 16th notes (hip hop) instead of 8ths (jazz)") },
        { "energy",          typed ("number", "0-1 overall intensity") },
        { "complexity",      typed ("number", "0-1 chord colour (7ths, 9ths)") },
        { "humanize",        typed ("number", "0-1 timing and velocity looseness") },
        { "dynamics",        enumOf ({ "flat", "build", "arc", "fade" }) },
        { "ending",          enumOf ({ "resolve", "loop" }) },
        { "theme",           withDescription (themeArray, "Optional main melody motif for the lead track; empty to let the engine write one") },
        { "tracks",          trackArray },
    });
}

} // namespace SpecJson
