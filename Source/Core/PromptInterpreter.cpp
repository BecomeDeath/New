#include "PromptInterpreter.h"
#include "Rng.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>

namespace pm
{

namespace
{
using Prog = std::vector<const char*>;

struct TrackTemplate
{
    const char* name;
    Role role;
    int program;
    int low, high;
    double density;
    int velocity;
    int layer;        // 0 = from the start, 1 = after the intro, 2 = second half
    const char* pattern;
};

struct StyleTemplate
{
    const char* id;
    const char* name;
    std::vector<std::pair<const char*, double>> keywords;
    int tempoMin, tempoMax;
    std::vector<Mode> modes;
    std::vector<Prog> progMajor; // used when the mode is exactly Major
    std::vector<Prog> progMinor; // used when the mode is exactly (natural) Minor
    double chordsPerBar;
    double swing;
    bool swing16;
    double energy, complexity, humanize;
    Dynamics dynamics;
    bool resolve;
    int tsNum, tsDen, bars;
    const char* drums; // groove to use if the prompt asks for drums
    std::vector<TrackTemplate> tracks;
};

using R = Role;
using M = Mode;
using D = Dynamics;

const std::vector<StyleTemplate>& styles()
{
    static const std::vector<StyleTemplate> list = {
        { "fantasy_orchestral", "Fantasy Orchestral",
          { { "fantasy", 3 }, { "orchestral", 2.5 }, { "orchestra", 2.5 }, { "symphonic", 2 }, { "symphony", 2 }, { "adventure", 2 },
            { "kingdom", 2 }, { "dragon", 2 }, { "elves", 2 }, { "elven", 2 }, { "elf", 1.5 }, { "magic", 1.5 }, { "quest", 2 },
            { "rpg", 2 }, { "enchanted", 2 }, { "fairy", 2 }, { "fairytale", 2 }, { "castle", 1.5 }, { "wizard", 2 }, { "legend", 1.5 },
            { "legendary", 1.5 }, { "journey", 1.5 }, { "realm", 1.5 }, { "mythical", 1.5 }, { "forest", 1 }, { "knight", 1.5 } },
          78, 108, { M::Minor, M::Minor, M::Dorian, M::Major, M::Lydian },
          { { "I", "V", "vi", "IV" }, { "I", "iii", "IV", "V" }, { "IV", "V", "iii", "vi" }, { "I", "vi", "IV", "V" }, { "I", "IV", "vi", "V" } },
          { { "i", "VI", "III", "VII" }, { "i", "VI", "iv", "V" }, { "i", "iv", "VI", "V" }, { "i", "VII", "VI", "VII" }, { "VI", "VII", "i", "i" }, { "i", "III", "VII", "iv" } },
          1.0, 0.0, false, 0.55, 0.25, 0.5, D::Arc, true, 4, 4, 16, "orchestral",
          { { "Strings",          R::Pad,           48, 43, 79, 0.30, 78, 0, "sustain" },
            { "Harp",             R::Arpeggio,      46, 43, 84, 0.55, 70, 0, "updown" },
            { "Cellos & Basses",  R::Bass,          42, 28, 55, 0.30, 82, 0, "sustain" },
            { "Flute",            R::Melody,        73, 67, 93, 0.50, 88, 1, "lyrical" },
            { "French Horns",     R::CounterMelody, 60, 48, 72, 0.30, 80, 1, "auto" },
            { "Choir",            R::Pad,           52, 52, 76, 0.20, 70, 2, "swell" },
            { "Brass",            R::Chords,        61, 45, 70, 0.35, 92, 2, "block" },
            { "Timpani",          R::Bass,          47, 36, 55, 0.40, 95, 2, "timpani" },
            { "Percussion",       R::Percussion,     0, 35, 81, 0.40, 95, 2, "orchestral" } } },

        { "cinematic_epic", "Epic Cinematic",
          { { "epic", 3 }, { "cinematic", 2.5 }, { "trailer", 3 }, { "battle", 3 }, { "war", 2 }, { "heroic", 2 }, { "hero", 1.5 },
            { "action", 2 }, { "film", 1.5 }, { "movie", 1.5 }, { "score", 1.5 }, { "soundtrack", 1.5 }, { "blockbuster", 2 },
            { "warrior", 2 }, { "siege", 2 }, { "titan", 2 }, { "victory", 1 }, { "army", 1 } },
          84, 128, { M::Minor, M::Minor, M::Minor, M::Dorian },
          { { "I", "V", "vi", "IV" }, { "vi", "IV", "I", "V" }, { "I", "IV", "vi", "V" } },
          { { "i", "VI", "III", "VII" }, { "i", "VII", "VI", "VII" }, { "i", "VI", "VII", "i" }, { "VI", "VII", "i", "i" }, { "i", "iv", "VI", "VII" }, { "i", "III", "VI", "VII" } },
          1.0, 0.0, false, 0.8, 0.15, 0.3, D::Build, true, 4, 4, 16, "orchestral",
          { { "Low Strings Ostinato", R::Ostinato,      48, 36, 62, 0.80,  95, 0, "epic" },
            { "Strings Pad",          R::Pad,           49, 48, 79, 0.30,  80, 0, "sustain" },
            { "Contrabass",           R::Bass,          43, 26, 50, 0.30,  95, 0, "sustain" },
            { "Horns Theme",          R::Melody,        60, 53, 77, 0.45, 100, 1, "lyrical" },
            { "Choir",                R::Pad,           52, 50, 74, 0.20,  85, 1, "swell" },
            { "Brass Stabs",          R::Chords,        61, 43, 67, 0.50, 105, 2, "block" },
            { "Trumpets",             R::CounterMelody, 56, 60, 82, 0.30,  95, 2, "auto" },
            { "Timpani",              R::Bass,          47, 36, 55, 0.50, 105, 1, "timpani" },
            { "Epic Drums",           R::Percussion,     0, 35, 81, 0.70, 110, 1, "orchestral" } } },

        { "solo_piano", "Solo Piano",
          { { "piano", 2 }, { "pianist", 2 }, { "nocturne", 2 }, { "ballad", 1.5 }, { "prelude", 1.5 }, { "etude", 1.5 }, { "keys", 1 } },
          64, 96, { M::Major, M::Minor },
          { { "I", "V", "vi", "IV" }, { "I", "vi", "IV", "V" }, { "vi", "IV", "I", "V" }, { "I", "iii", "vi", "IV" }, { "IV", "I", "V", "vi" }, { "I", "IV", "I", "V" } },
          { { "i", "VI", "III", "VII" }, { "i", "iv", "VI", "V" }, { "i", "VI", "iv", "V" }, { "i", "III", "VII", "VI" } },
          1.0, 0.0, false, 0.45, 0.35, 0.7, D::Arc, true, 4, 4, 16, "pop",
          { { "Piano (Right Hand)", R::Melody, 0, 60, 88, 0.45, 80, 0, "lyrical" },
            { "Piano (Left Hand)",  R::Chords, 0, 36, 64, 0.55, 68, 0, "broken" } } },

        { "classical", "Classical Chamber",
          { { "classical", 3 }, { "baroque", 3 }, { "chamber", 2 }, { "quartet", 2 }, { "string quartet", 3 }, { "sonata", 2 },
            { "minuet", 2 }, { "concerto", 2 }, { "elegant", 1 }, { "courtly", 1.5 }, { "regal", 1 }, { "royal", 1 }, { "aristocratic", 1 } },
          84, 120, { M::Major, M::Major, M::Minor },
          { { "I", "IV", "V", "I" }, { "I", "vi", "ii", "V" }, { "I", "V", "vi", "iii", "IV", "I", "IV", "V" }, { "I", "ii", "V", "I" }, { "I", "IV", "ii", "V" } },
          { { "i", "iv", "V", "i" }, { "i", "VI", "iio", "V" }, { "i", "iv", "VII", "III", "VI", "iio", "V", "i" } },
          1.0, 0.0, false, 0.5, 0.2, 0.4, D::Arc, true, 4, 4, 16, "orchestral",
          { { "Violin I",  R::Melody,        40, 62, 93, 0.55, 88, 0, "lyrical" },
            { "Violin II", R::CounterMelody, 40, 55, 81, 0.40, 76, 0, "auto" },
            { "Viola",     R::Chords,        41, 48, 72, 0.45, 72, 0, "alberti" },
            { "Cello",     R::Bass,          42, 36, 60, 0.40, 80, 0, "root" } } },

        { "jazz", "Jazz Combo",
          { { "jazz", 3 }, { "jazzy", 2 }, { "swing", 2 }, { "bebop", 3 }, { "smooth jazz", 3 }, { "lounge", 2 }, { "cocktail", 2 },
            { "jazz club", 3 }, { "noir", 2 }, { "speakeasy", 2 }, { "big band", 3 }, { "standards", 1 } },
          90, 160, { M::Major, M::Major, M::Minor, M::Dorian },
          { { "ii7", "V7", "Imaj7", "Imaj7" }, { "Imaj7", "vi7", "ii7", "V7" }, { "iii7", "vi7", "ii7", "V7" }, { "Imaj7", "IV7", "iii7", "VI7" },
            { "Imaj7", "vi7", "ii7", "V7", "iii7", "VI7", "ii7", "V7" } },
          { { "iim7b5", "V7", "i7", "i7" }, { "i7", "iv7", "VII7", "IIImaj7" }, { "i7", "iv7", "VII7", "IIImaj7", "VImaj7", "iim7b5", "V7", "i7" } },
          1.0, 0.9, false, 0.5, 0.85, 0.6, D::Flat, false, 4, 4, 16, "jazz",
          { { "Piano Comping", R::Chords,     0, 50, 74, 0.45, 72, 0, "comp" },
            { "Upright Bass",  R::Bass,      32, 28, 52, 0.70, 88, 0, "walking" },
            { "Tenor Sax",     R::Melody,    66, 52, 77, 0.55, 90, 1, "lyrical" },
            { "Drums",         R::Percussion, 0, 35, 81, 0.50, 80, 0, "jazz" } } },

        { "lofi", "Lo-Fi Hip Hop",
          { { "lofi", 3 }, { "lo fi", 3 }, { "chillhop", 3 }, { "study", 1.5 }, { "study beats", 3 }, { "chill", 1.5 }, { "chilled", 1.5 },
            { "cozy", 1.5 }, { "rainy", 1.5 }, { "cafe", 1.5 }, { "coffee", 1.5 }, { "jazzhop", 3 }, { "mellow", 1 } },
          70, 90, { M::Major, M::Dorian, M::Minor },
          { { "Imaj7", "vi7", "ii7", "V7" }, { "IVmaj7", "iii7", "ii7", "Imaj7" }, { "Imaj7", "iii7", "IVmaj7", "iv7" }, { "ii9", "V9", "Imaj9", "Imaj9" } },
          { { "i7", "iv7", "i7", "v7" }, { "i9", "iv9", "VImaj7", "V7" }, { "VImaj7", "v7", "i7", "i7" } },
          1.0, 0.6, true, 0.35, 0.85, 0.8, D::Flat, false, 4, 4, 8, "lofi",
          { { "Electric Piano", R::Chords,     4, 48, 74, 0.30, 70, 0, "block" },
            { "Bass",           R::Bass,      33, 31, 52, 0.40, 85, 0, "syncopated" },
            { "Guitar Melody",  R::Melody,    26, 57, 81, 0.30, 76, 0, "sparse" },
            { "Drums",          R::Percussion, 0, 35, 81, 0.50, 75, 0, "lofi" } } },

        { "hiphop", "Hip Hop Beat",
          { { "hip hop", 3 }, { "hiphop", 3 }, { "rap", 2.5 }, { "boom bap", 3 }, { "boombap", 3 }, { "beat", 1 }, { "old school", 1.5 },
            { "underground", 1 }, { "90s", 1.5 }, { "nineties", 1.5 } },
          84, 98, { M::Minor, M::Dorian, M::Minor },
          { { "Imaj7", "IVmaj7", "Imaj7", "IVmaj7" }, { "I", "vi", "IV", "V" } },
          { { "i", "iv", "VII", "VI" }, { "i", "VI", "III", "VII" }, { "i", "iv", "VI", "V" }, { "i", "i", "VI", "VI" } },
          1.0, 0.45, true, 0.6, 0.5, 0.6, D::Flat, false, 4, 4, 8, "hiphop",
          { { "Piano Loop", R::Chords,     0, 52, 76, 0.40, 72, 0, "block" },
            { "Bass",       R::Bass,      33, 28, 50, 0.45, 92, 0, "syncopated" },
            { "Strings",    R::Pad,       49, 55, 79, 0.20, 60, 0, "sustain" },
            { "Drums",      R::Percussion, 0, 35, 81, 0.60, 100, 0, "hiphop" } } },

        { "trap", "Trap",
          { { "trap", 3 }, { "drill", 3 }, { "808", 2.5 }, { "808s", 3 }, { "type beat", 2 } },
          130, 160, { M::Minor, M::HarmonicMinor, M::Phrygian },
          {},
          { { "i", "i", "VI", "VI" }, { "i", "iv", "VI", "V" }, { "i", "VI", "iv", "V" } },
          0.5, 0.0, false, 0.7, 0.2, 0.2, D::Flat, false, 4, 4, 8, "trap",
          { { "Bells Melody", R::Melody,      9, 72, 96, 0.60,  90, 0, "rhythmic" },
            { "Dark Pad",     R::Pad,        89, 52, 76, 0.20,  65, 0, "sustain" },
            { "808 Bass",     R::Bass,       38, 24, 48, 0.40, 110, 0, "808" },
            { "Drums",        R::Percussion,  0, 35, 81, 0.70, 100, 0, "trap" } } },

        { "edm", "Electronic Dance",
          { { "edm", 3 }, { "house", 2.5 }, { "deep house", 3 }, { "techno", 3 }, { "trance", 3 }, { "dance", 2 }, { "club", 2 },
            { "rave", 2 }, { "festival", 2 }, { "electronic", 2 }, { "electro", 2 }, { "dancefloor", 2 }, { "progressive", 1 }, { "future bass", 3 } },
          120, 128, { M::Minor, M::Minor, M::Major },
          { { "I", "V", "vi", "IV" }, { "vi", "IV", "I", "V" }, { "IV", "V", "vi", "vi" } },
          { { "i", "VI", "III", "VII" }, { "i", "VII", "VI", "VII" }, { "VI", "VII", "i", "i" }, { "i", "III", "VII", "VI" } },
          1.0, 0.0, false, 0.8, 0.3, 0.1, D::Build, false, 4, 4, 16, "four_on_floor",
          { { "Synth Chords", R::Chords,     90, 48, 72, 0.60,  90, 0, "stabs" },
            { "Pluck Arp",    R::Arpeggio,   81, 55, 84, 0.80,  78, 0, "updown" },
            { "Lead",         R::Melody,     81, 64, 88, 0.55,  95, 1, "rhythmic" },
            { "Pad",          R::Pad,        89, 52, 76, 0.20,  70, 0, "sustain" },
            { "Bass",         R::Bass,       38, 28, 50, 0.60, 100, 0, "offbeat" },
            { "Drums",        R::Percussion,  0, 35, 81, 0.70, 105, 0, "four_on_floor" } } },

        { "dnb", "Drum & Bass",
          { { "drum and bass", 3.5 }, { "drum n bass", 3.5 }, { "dnb", 3 }, { "jungle", 2.5 }, { "liquid", 2 }, { "neurofunk", 3 }, { "breakbeat", 2.5 } },
          170, 176, { M::Minor, M::Dorian },
          {},
          { { "i", "VI", "III", "VII" }, { "i", "VII", "VI", "VII" }, { "i", "iv", "VI", "VII" } },
          0.5, 0.0, false, 0.85, 0.4, 0.1, D::Build, false, 4, 4, 16, "breakbeat",
          { { "Atmos Pad",  R::Pad,        89, 52, 79, 0.20,  65, 0, "swell" },
            { "Reese Bass", R::Bass,       38, 26, 48, 0.40, 105, 0, "syncopated" },
            { "Lead",       R::Melody,     81, 64, 88, 0.45,  90, 1, "lyrical" },
            { "Drums",      R::Percussion,  0, 35, 81, 0.75, 105, 0, "breakbeat" } } },

        { "pop", "Pop",
          { { "pop", 3 }, { "catchy", 2 }, { "radio", 1.5 }, { "top 40", 2 }, { "chart", 1 }, { "anthem", 1.5 }, { "love song", 1.5 },
            { "summer", 1 }, { "kpop", 2 }, { "k pop", 2 }, { "pop song", 3 }, { "song", 0.5 } },
          96, 124, { M::Major, M::Major, M::Minor },
          { { "I", "V", "vi", "IV" }, { "vi", "IV", "I", "V" }, { "I", "vi", "IV", "V" }, { "IV", "I", "V", "vi" }, { "I", "IV", "vi", "V" } },
          { { "i", "VI", "III", "VII" }, { "VI", "VII", "i", "i" }, { "i", "iv", "VI", "VII" } },
          1.0, 0.0, false, 0.65, 0.2, 0.4, D::Arc, false, 4, 4, 16, "pop",
          { { "Piano",       R::Chords,     0, 48, 76, 0.55, 80, 0, "pulse" },
            { "Synth Pad",   R::Pad,       89, 55, 79, 0.20, 62, 0, "sustain" },
            { "Vocal Lead",  R::Melody,    54, 60, 79, 0.50, 92, 0, "lyrical" },
            { "Bass",        R::Bass,      33, 28, 52, 0.50, 90, 0, "syncopated" },
            { "Drums",       R::Percussion, 0, 35, 81, 0.60, 95, 0, "pop" } } },

        { "rock", "Rock",
          { { "rock", 3 }, { "hard rock", 3 }, { "punk", 2.5 }, { "grunge", 2 }, { "indie rock", 3 }, { "indie", 1.5 },
            { "alternative", 1.5 }, { "garage", 1.5 }, { "power ballad", 2 } },
          100, 140, { M::Major, M::Minor, M::Mixolydian },
          { { "I", "V", "vi", "IV" }, { "I", "IV", "V", "IV" }, { "I", "bVII", "IV", "I" }, { "I", "V", "IV", "IV" } },
          { { "i", "VI", "VII", "i" }, { "i", "VII", "VI", "VII" }, { "i", "iv", "VII", "i" } },
          1.0, 0.0, false, 0.75, 0.1, 0.4, D::Build, false, 4, 4, 16, "rock",
          { { "Rhythm Guitar", R::Chords,    29, 40, 64, 0.70, 95, 0, "power" },
            { "Lead Guitar",   R::Melody,    29, 55, 84, 0.55, 98, 1, "rhythmic" },
            { "Bass Guitar",   R::Bass,      34, 28, 52, 0.70, 95, 0, "pedal" },
            { "Drums",         R::Percussion, 0, 35, 81, 0.65, 105, 0, "rock" } } },

        { "metal", "Metal",
          { { "metal", 3 }, { "heavy metal", 3.5 }, { "thrash", 3 }, { "djent", 3 }, { "metalcore", 3 }, { "doom", 2 }, { "shred", 2 } },
          140, 180, { M::Phrygian, M::Minor, M::HarmonicMinor },
          {},
          { { "i", "VI", "VII", "i" }, { "i", "bII", "i", "VII" }, { "i", "VI", "iv", "V" } },
          1.0, 0.0, false, 0.95, 0.05, 0.2, D::Build, false, 4, 4, 16, "metal",
          { { "Rhythm Guitar", R::Chords,    30, 40, 64, 0.85, 105, 0, "power" },
            { "Lead Guitar",   R::Melody,    30, 55, 86, 0.65, 100, 1, "rhythmic" },
            { "Bass",          R::Bass,      34, 28, 48, 0.85, 100, 0, "pedal" },
            { "Drums",         R::Percussion, 0, 35, 81, 0.80, 110, 0, "metal" } } },

        { "ambient", "Ambient",
          { { "ambient", 3 }, { "atmospheric", 2.5 }, { "atmosphere", 2 }, { "drone", 2.5 }, { "meditation", 2.5 }, { "meditative", 2.5 },
            { "space", 2 }, { "cosmic", 2 }, { "soundscape", 2.5 }, { "floating", 1.5 }, { "new age", 2.5 }, { "spa", 2 }, { "yoga", 2 },
            { "underwater", 1.5 }, { "ocean", 1 }, { "stars", 1 }, { "relaxing", 1 } },
          60, 80, { M::Lydian, M::Major, M::Dorian, M::Minor },
          { { "I", "IV", "I", "IV" }, { "I", "vi", "IV", "I" }, { "Iadd9", "IVadd9", "vi7", "IV" } },
          { { "i", "VI", "i", "VI" }, { "i", "III", "VI", "iv" } },
          0.5, 0.0, false, 0.25, 0.5, 0.6, D::Arc, true, 4, 4, 16, "ambient",
          { { "Warm Pad",     R::Pad,      89, 48, 76, 0.20, 65, 0, "swell" },
            { "Halo Pad",     R::Pad,      94, 60, 84, 0.15, 55, 1, "sustain" },
            { "Crystal Bells", R::Melody,  98, 67, 91, 0.15, 70, 1, "sparse" },
            { "Sub Bass",     R::Bass,     38, 28, 48, 0.20, 70, 0, "sustain" },
            { "Shimmer Harp", R::Arpeggio, 46, 55, 88, 0.35, 55, 1, "random" } } },

        { "synthwave", "Synthwave",
          { { "synthwave", 3 }, { "retrowave", 3 }, { "outrun", 3 }, { "80s", 3 }, { "eighties", 3 }, { "1980s", 3 }, { "cyberpunk", 2.5 },
            { "retro", 1.5 }, { "neon", 2 }, { "vaporwave", 2.5 }, { "miami", 1.5 }, { "darksynth", 3 }, { "night drive", 2.5 } },
          90, 118, { M::Minor, M::Minor, M::Dorian },
          { { "I", "V", "vi", "IV" }, { "IV", "V", "iii", "vi" } },
          { { "i", "VI", "III", "VII" }, { "i", "VII", "VI", "VII" }, { "VI", "VII", "i", "i" }, { "i", "VI", "VII", "v" } },
          1.0, 0.0, false, 0.65, 0.2, 0.1, D::Build, false, 4, 4, 16, "synthwave",
          { { "Poly Synth", R::Chords,     90, 48, 72, 0.30, 80, 0, "sustain" },
            { "Arp",        R::Arpeggio,   81, 48, 79, 0.85, 82, 0, "up" },
            { "Lead",       R::Melody,     81, 60, 86, 0.50, 92, 1, "lyrical" },
            { "Bass",       R::Bass,       39, 28, 52, 0.80, 95, 0, "octave" },
            { "Drums",      R::Percussion,  0, 35, 81, 0.60, 100, 0, "synthwave" } } },

        { "celtic_folk", "Celtic Folk",
          { { "celtic", 3 }, { "irish", 3 }, { "scottish", 3 }, { "folk", 2.5 }, { "tavern", 3 }, { "medieval", 2.5 }, { "bard", 3 },
            { "jig", 3 }, { "reel", 2.5 }, { "viking", 2 }, { "renaissance", 1.5 }, { "highlands", 2.5 }, { "village", 1.5 }, { "pub", 1.5 },
            { "sea shanty", 3 }, { "shanty", 3 }, { "pirate", 2.5 } },
          100, 132, { M::Dorian, M::Mixolydian, M::Major, M::Minor },
          { { "I", "IV", "I", "V" }, { "I", "V", "IV", "I" }, { "I", "ii", "IV", "I" } },
          { { "i", "VII", "i", "VII" }, { "i", "VII", "VI", "VII" }, { "i", "III", "VII", "i" } },
          1.0, 0.0, false, 0.65, 0.1, 0.6, D::Flat, true, 6, 8, 16, "folk",
          { { "Fiddle",          R::Melody,        110, 62, 88, 0.75, 92, 0, "rhythmic" },
            { "Tin Whistle",     R::CounterMelody,  78, 67, 91, 0.40, 80, 1, "auto" },
            { "Acoustic Guitar", R::Chords,         25, 48, 72, 0.60, 78, 0, "strum" },
            { "Bass",            R::Bass,           32, 31, 52, 0.40, 80, 0, "rootfifth" },
            { "Bodhran",         R::Percussion,      0, 35, 81, 0.50, 85, 1, "folk" } } },

        { "horror", "Dark Horror",
          { { "horror", 3 }, { "scary", 3 }, { "creepy", 3 }, { "haunted", 3 }, { "spooky", 3 }, { "halloween", 3 }, { "nightmare", 2.5 },
            { "eerie", 2.5 }, { "ghost", 2 }, { "zombie", 2 }, { "terror", 2.5 }, { "dread", 2 }, { "dark ambient", 3 } },
          60, 90, { M::HarmonicMinor, M::Phrygian, M::Minor },
          {},
          { { "i", "bII", "i", "bII" }, { "i", "iv", "i", "bII" }, { "i", "VI", "iio", "V" }, { "i", "bV", "i", "bV" } },
          0.5, 0.0, false, 0.35, 0.4, 0.5, D::Build, false, 4, 4, 16, "ambient",
          { { "Low Strings",     R::Pad,       49, 36, 60, 0.20, 75, 0, "swell" },
            { "Tremolo Strings", R::Ostinato,  44, 55, 79, 0.90, 60, 1, "pulse" },
            { "Music Box",       R::Melody,    10, 72, 96, 0.35, 70, 1, "sparse" },
            { "Contrabass",      R::Bass,      43, 24, 45, 0.20, 85, 0, "sustain" },
            { "Hits",            R::Percussion, 0, 35, 81, 0.30, 100, 2, "ambient" } } },

        { "chiptune", "Chiptune",
          { { "chiptune", 3 }, { "8 bit", 3 }, { "8bit", 3 }, { "16 bit", 2.5 }, { "video game", 3 }, { "videogame", 3 }, { "arcade", 2.5 },
            { "nes", 2.5 }, { "gameboy", 3 }, { "game boy", 3 }, { "pixel", 2.5 }, { "platformer", 3 }, { "boss battle", 3 }, { "game", 1.5 } },
          120, 160, { M::Major, M::Minor, M::Dorian },
          { { "I", "V", "vi", "IV" }, { "I", "IV", "V", "I" }, { "vi", "IV", "V", "I" }, { "I", "vi", "IV", "V" } },
          { { "i", "VII", "VI", "VII" }, { "i", "VI", "VII", "i" }, { "i", "iv", "V", "i" } },
          1.0, 0.0, false, 0.75, 0.1, 0.0, D::Flat, false, 4, 4, 16, "chiptune",
          { { "Square Lead",   R::Melody,    80, 64, 91, 0.70, 95, 0, "rhythmic" },
            { "Pulse Harmony", R::Arpeggio,  80, 52, 79, 0.90, 72, 0, "up" },
            { "Chip Bass",     R::Bass,      38, 31, 55, 0.70, 90, 0, "octave" },
            { "Noise Drums",   R::Percussion, 0, 35, 81, 0.60, 90, 0, "chiptune" } } },

        { "bossa", "Bossa Nova",
          { { "bossa", 3 }, { "bossa nova", 3.5 }, { "latin", 2.5 }, { "samba", 2.5 }, { "brazilian", 3 }, { "brazil", 2 }, { "salsa", 2 },
            { "cuban", 2 }, { "beach", 1 }, { "tropical", 1.5 }, { "caribbean", 2 } },
          120, 140, { M::Major, M::Minor, M::Dorian },
          { { "Imaj7", "II7", "ii7", "V7" }, { "Imaj7", "vi7", "ii7", "V7" }, { "iii7", "VI7", "ii7", "V7" } },
          { { "i7", "iv7", "iim7b5", "V7" }, { "i7", "VImaj7", "iim7b5", "V7" } },
          1.0, 0.0, false, 0.5, 0.8, 0.5, D::Flat, false, 4, 4, 8, "bossa",
          { { "Nylon Guitar", R::Chords,     24, 48, 72, 0.60, 75, 0, "bossa" },
            { "Flute",        R::Melody,     73, 64, 88, 0.45, 82, 1, "lyrical" },
            { "Bass",         R::Bass,       32, 28, 52, 0.50, 85, 0, "bossa" },
            { "Percussion",   R::Percussion,  0, 35, 81, 0.50, 75, 0, "bossa" } } },

        { "funk", "Funk & Disco",
          { { "funk", 3 }, { "funky", 3 }, { "disco", 3 }, { "groove", 2 }, { "groovy", 2.5 }, { "boogie", 2 }, { "70s", 2.5 }, { "seventies", 2.5 } },
          100, 120, { M::Dorian, M::Mixolydian, M::Minor },
          {},
          { { "i7", "IV7", "i7", "IV7" }, { "i7", "iv7", "VII7", "i7" } },
          1.0, 0.15, true, 0.75, 0.6, 0.4, D::Flat, false, 4, 4, 8, "funk",
          { { "Clean Guitar",   R::Chords,     27, 52, 76, 0.75,  80, 0, "stabs" },
            { "Slap Bass",      R::Bass,       36, 28, 55, 0.80, 100, 0, "syncopated" },
            { "Electric Piano", R::Chords,      4, 48, 72, 0.40,  70, 0, "comp" },
            { "Brass Section",  R::Melody,     61, 58, 82, 0.60,  95, 1, "rhythmic" },
            { "Drums",          R::Percussion,  0, 35, 81, 0.70, 100, 0, "funk" } } },

        { "blues", "Blues",
          { { "blues", 3 }, { "bluesy", 3 }, { "12 bar", 3 }, { "twelve bar", 3 }, { "delta", 2 }, { "shuffle", 2 }, { "chicago", 1.5 } },
          70, 120, { M::Blues },
          {}, {},
          1.0, 0.9, false, 0.55, 0.4, 0.6, D::Flat, false, 4, 4, 12, "shuffle",
          { { "Piano",       R::Chords,     0, 48, 72, 0.50, 75, 0, "comp" },
            { "Lead Guitar", R::Melody,    27, 55, 84, 0.55, 92, 0, "rhythmic" },
            { "Bass",        R::Bass,      33, 28, 52, 0.70, 90, 0, "boogie" },
            { "Drums",       R::Percussion, 0, 35, 81, 0.55, 90, 0, "shuffle" } } },

        { "lullaby", "Lullaby",
          { { "lullaby", 3 }, { "music box", 3 }, { "nursery", 3 }, { "baby", 2 }, { "cradle", 2.5 }, { "bedtime", 2.5 }, { "sleep", 1.5 },
            { "toy", 2 }, { "childrens", 2 }, { "children", 1.5 }, { "kids", 1.5 }, { "cute", 1.5 }, { "innocent", 1.5 }, { "twinkle", 2 } },
          60, 84, { M::Major, M::Major, M::Lydian },
          { { "I", "IV", "I", "V" }, { "I", "vi", "IV", "V" }, { "I", "V", "vi", "IV" }, { "I", "IV", "V", "I" } },
          {},
          1.0, 0.0, false, 0.25, 0.15, 0.4, D::Fade, true, 3, 4, 16, "waltz",
          { { "Music Box", R::Melody,   10, 72, 96, 0.35, 75, 0, "lyrical" },
            { "Celesta",   R::Arpeggio,  8, 60, 84, 0.40, 55, 0, "up" },
            { "Soft Strings", R::Pad,   49, 48, 72, 0.20, 50, 1, "sustain" },
            { "Harp Bass", R::Bass,     46, 36, 55, 0.30, 60, 0, "root" } } },

        { "eastern", "East Asian",
          { { "japanese", 3 }, { "japan", 2.5 }, { "chinese", 3 }, { "china", 2.5 }, { "asian", 2.5 }, { "oriental", 2 }, { "zen", 2.5 },
            { "samurai", 3 }, { "ninja", 2.5 }, { "koto", 3 }, { "shakuhachi", 3 }, { "cherry blossom", 3 }, { "sakura", 3 }, { "temple", 2 },
            { "kyoto", 3 }, { "far east", 2 } },
          66, 96, { M::Japanese, M::PentatonicMinor, M::PentatonicMajor },
          {}, {},
          0.5, 0.0, false, 0.4, 0.1, 0.6, D::Arc, true, 4, 4, 16, "tribal",
          { { "Koto",       R::Arpeggio,  107, 48, 84, 0.45, 80, 0, "random" },
            { "Shakuhachi", R::Melody,     77, 60, 86, 0.35, 85, 1, "lyrical" },
            { "Strings",    R::Pad,        49, 48, 72, 0.20, 60, 0, "swell" },
            { "Cello",      R::Bass,       42, 36, 55, 0.25, 70, 0, "sustain" } } },

        { "arabian", "Arabian Desert",
          { { "arabian", 3 }, { "arabic", 3 }, { "desert", 3 }, { "middle eastern", 3 }, { "egyptian", 3 }, { "egypt", 2.5 }, { "persian", 3 },
            { "bazaar", 3 }, { "sultan", 2.5 }, { "pharaoh", 3 }, { "oasis", 2.5 }, { "sahara", 3 }, { "belly dance", 3 } },
          90, 120, { M::DoubleHarmonic, M::DoubleHarmonic, M::Phrygian, M::HarmonicMinor },
          {}, {},
          0.5, 0.0, false, 0.6, 0.1, 0.5, D::Flat, false, 4, 4, 16, "arabic",
          { { "Sitar Lead", R::Melody,       104, 57, 86, 0.60, 90, 0, "rhythmic" },
            { "Strings",    R::Pad,           48, 48, 72, 0.20, 70, 0, "sustain" },
            { "Ney Flute",  R::CounterMelody, 75, 60, 84, 0.35, 75, 1, "auto" },
            { "Drone Bass", R::Bass,          43, 26, 48, 0.20, 80, 0, "sustain" },
            { "Darbuka",    R::Percussion,     0, 35, 81, 0.60, 90, 0, "arabic" } } },

        { "march", "Military March",
          { { "march", 3 }, { "military", 3 }, { "marching", 3 }, { "parade", 3 }, { "patriotic", 2.5 }, { "national anthem", 3 },
            { "soldiers", 2 }, { "regiment", 2.5 }, { "fanfare", 2.5 } },
          104, 124, { M::Major, M::Major, M::Minor },
          { { "I", "IV", "V", "I" }, { "I", "V", "I", "IV" }, { "I", "IV", "I", "V" }, { "I", "vi", "IV", "V" } },
          { { "i", "iv", "V", "i" }, { "i", "VI", "iv", "V" } },
          1.0, 0.0, false, 0.8, 0.1, 0.2, D::Build, true, 4, 4, 16, "march",
          { { "Trumpets",          R::Melody,        56, 60, 84, 0.60, 100, 0, "rhythmic" },
            { "Horns",             R::Chords,        60, 48, 72, 0.50,  88, 0, "block" },
            { "Tuba",              R::Bass,          58, 28, 52, 0.50,  95, 0, "rootfifth" },
            { "Piccolo",           R::CounterMelody, 72, 72, 96, 0.50,  80, 1, "flowing" },
            { "Snare & Bass Drum", R::Percussion,     0, 35, 81, 0.60, 100, 0, "march" } } },

        { "waltz", "Waltz",
          { { "waltz", 3 }, { "ballroom", 2.5 }, { "viennese", 3 }, { "carousel", 2 } },
          132, 170, { M::Major, M::Minor },
          { { "I", "IV", "V", "I" }, { "I", "vi", "ii", "V" }, { "I", "I", "V", "V" } },
          { { "i", "iv", "V", "i" }, { "i", "VI", "iv", "V" } },
          1.0, 0.0, false, 0.55, 0.25, 0.5, D::Arc, true, 3, 4, 16, "waltz",
          { { "Violin",           R::Melody,        40, 62, 91, 0.50, 88, 0, "lyrical" },
            { "Strings",          R::Chords,        48, 52, 72, 0.50, 70, 0, "waltz" },
            { "Pizzicato Bass",   R::Bass,          45, 31, 52, 0.40, 85, 0, "root" },
            { "Flute",            R::CounterMelody, 73, 67, 91, 0.30, 75, 1, "auto" } } },

        { "rnb", "R&B Soul",
          { { "rnb", 3 }, { "r&b", 3 }, { "r and b", 3 }, { "soul", 3 }, { "soulful", 2.5 }, { "neo soul", 3 }, { "gospel", 2.5 },
            { "motown", 3 }, { "smooth", 1.5 }, { "sensual", 2 }, { "slow jam", 3 } },
          66, 96, { M::Major, M::Minor, M::Dorian },
          { { "IVmaj7", "iii7", "ii7", "Imaj7" }, { "Imaj9", "vi9", "ii9", "V9" }, { "Imaj7", "iii7", "vi7", "IVmaj7" } },
          { { "i9", "iv9", "VImaj7", "V7" }, { "i7", "iv7", "VImaj7", "v7" } },
          1.0, 0.5, true, 0.5, 0.85, 0.6, D::Flat, false, 4, 4, 8, "hiphop",
          { { "Rhodes",     R::Chords,     4, 48, 74, 0.40, 72, 0, "comp" },
            { "Bass",       R::Bass,      33, 28, 52, 0.50, 88, 0, "syncopated" },
            { "Vocal Lead", R::Melody,    54, 60, 79, 0.45, 88, 0, "lyrical" },
            { "Strings",    R::Pad,       49, 55, 79, 0.20, 55, 1, "sustain" },
            { "Drums",      R::Percussion, 0, 35, 81, 0.45, 85, 0, "hiphop" } } },

        { "reggae", "Reggae",
          { { "reggae", 3 }, { "dub", 2.5 }, { "ska", 2.5 }, { "rasta", 2 }, { "jamaican", 2.5 }, { "dancehall", 2.5 }, { "island", 1.5 } },
          70, 90, { M::Major, M::Minor },
          { { "I", "IV", "I", "V" }, { "I", "V", "vi", "IV" }, { "ii", "V", "I", "I" } },
          { { "i", "iv", "i", "v" }, { "i", "VII", "VI", "VII" } },
          1.0, 0.3, true, 0.55, 0.2, 0.5, D::Flat, false, 4, 4, 8, "reggae",
          { { "Skank Guitar", R::Chords,     27, 55, 76, 0.40, 75, 0, "offbeat" },
            { "Organ",        R::Chords,     16, 48, 72, 0.60, 60, 0, "offbeat" },
            { "Bass",         R::Bass,       33, 28, 50, 0.50, 95, 0, "syncopated" },
            { "Melodica",     R::Melody,     22, 60, 84, 0.40, 80, 1, "lyrical" },
            { "Drums",        R::Percussion,  0, 35, 81, 0.50, 90, 0, "reggae" } } },

        { "country", "Country & Western",
          { { "country", 3 }, { "western", 2.5 }, { "cowboy", 3 }, { "bluegrass", 3 }, { "americana", 2.5 }, { "wild west", 3 }, { "honky tonk", 3 },
            { "saloon", 2.5 }, { "ranch", 2 }, { "prairie", 2 } },
          96, 130, { M::Major, M::Major, M::Mixolydian },
          { { "I", "IV", "I", "V" }, { "I", "IV", "V", "I" }, { "I", "V", "vi", "IV" } },
          { { "i", "VII", "VI", "V" } },
          1.0, 0.1, false, 0.6, 0.1, 0.5, D::Flat, true, 4, 4, 16, "pop",
          { { "Acoustic Guitar", R::Chords,     25, 45, 72, 0.60, 80, 0, "strum" },
            { "Banjo",           R::Arpeggio,  105, 55, 81, 0.80, 72, 0, "updown" },
            { "Fiddle",          R::Melody,    110, 62, 88, 0.55, 90, 1, "lyrical" },
            { "Bass",            R::Bass,       32, 28, 52, 0.50, 85, 0, "rootfifth" },
            { "Drums",           R::Percussion,  0, 35, 81, 0.45, 85, 0, "pop" } } },

        { "hymn", "Sacred Choir",
          { { "hymn", 3 }, { "church", 2.5 }, { "sacred", 2.5 }, { "spiritual", 2 }, { "cathedral", 2.5 }, { "chorale", 3 }, { "choral", 2 },
            { "angelic", 2 }, { "prayer", 2 }, { "requiem", 3 }, { "monks", 2.5 }, { "gregorian", 3 } },
          60, 80, { M::Major, M::Minor, M::Dorian },
          { { "I", "IV", "I", "V" }, { "I", "vi", "IV", "V" }, { "I", "IV", "ii", "V" } },
          { { "i", "iv", "VII", "III" }, { "i", "VI", "iv", "V" } },
          1.0, 0.0, false, 0.4, 0.15, 0.3, D::Arc, true, 4, 4, 16, "ambient",
          { { "Choir Melody",  R::Melody,        52, 60, 81, 0.30, 80, 0, "lyrical" },
            { "Choir Harmony", R::Pad,           52, 48, 72, 0.25, 70, 0, "sustain" },
            { "Church Organ",  R::Pad,           19, 41, 72, 0.25, 65, 0, "sustain" },
            { "Organ Pedal",   R::Bass,          19, 24, 48, 0.30, 70, 0, "sustain" } } },

        { "default_cinematic", "Cinematic Piano & Strings",
          { { "emotional", 1 }, { "inspiring", 2 }, { "inspirational", 2 }, { "documentary", 2.5 }, { "corporate", 2.5 },
            { "background", 1.5 }, { "wedding", 2 }, { "beautiful", 1.5 }, { "uplifting", 1 }, { "piano and strings", 3.5 } },
          70, 100, { M::Major, M::Minor },
          { { "I", "V", "vi", "IV" }, { "vi", "IV", "I", "V" }, { "I", "iii", "vi", "IV" }, { "IV", "I", "V", "vi" } },
          { { "i", "VI", "III", "VII" }, { "i", "iv", "VI", "V" }, { "VI", "III", "VII", "i" } },
          1.0, 0.0, false, 0.5, 0.3, 0.6, D::Arc, true, 4, 4, 16, "orchestral",
          { { "Piano Arpeggio", R::Arpeggio,      0, 48, 79, 0.55, 72, 0, "updown" },
            { "Piano Melody",   R::Melody,        0, 64, 88, 0.40, 82, 1, "lyrical" },
            { "Strings",        R::Pad,          48, 48, 76, 0.25, 70, 0, "swell" },
            { "Cello",          R::Bass,         42, 31, 55, 0.25, 78, 0, "sustain" },
            { "Violins",        R::CounterMelody, 49, 64, 86, 0.30, 72, 2, "sustained" } } },
    };

    return list;
}

const StyleTemplate& findStyle (const std::string& id)
{
    for (auto& s : styles())
        if (id == s.id)
            return s;

    return styles().back();
}

const std::vector<Prog>& modeProgressions (Mode m)
{
    static const std::vector<Prog> major      { { "I", "V", "vi", "IV" }, { "I", "vi", "IV", "V" }, { "vi", "IV", "I", "V" }, { "I", "IV", "vi", "V" },
                                                { "I", "iii", "IV", "V" }, { "IV", "I", "V", "vi" }, { "I", "V", "IV", "V" }, { "I", "ii", "IV", "V" } };
    static const std::vector<Prog> minor      { { "i", "VI", "III", "VII" }, { "i", "iv", "VI", "V" }, { "i", "VII", "VI", "VII" }, { "i", "VI", "iv", "V" },
                                                { "i", "iv", "v", "i" }, { "VI", "VII", "i", "i" }, { "i", "III", "VII", "VI" }, { "i", "VI", "VII", "i" } };
    static const std::vector<Prog> dorian     { { "i", "IV", "i", "IV" }, { "i", "IV", "VII", "i" }, { "i", "III", "IV", "i" }, { "i", "VII", "IV", "i" },
                                                { "i", "IV", "III", "VII" }, { "i", "v", "IV", "VII" } };
    static const std::vector<Prog> phrygian   { { "i", "II", "i", "II" }, { "i", "II", "III", "II" }, { "i", "iv", "II", "i" }, { "i", "II", "vii", "i" } };
    static const std::vector<Prog> lydian     { { "I", "II", "I", "II" }, { "I", "II", "vi", "I" }, { "I", "II", "iii", "II" }, { "I", "vii", "II", "I" } };
    static const std::vector<Prog> mixolydian { { "I", "VII", "IV", "I" }, { "I", "v", "VII", "IV" }, { "I", "VII", "IV", "IV" }, { "I", "IV", "VII", "I" } };
    static const std::vector<Prog> locrian    { { "io", "II", "iv", "II" }, { "io", "II", "io", "VII" } };
    static const std::vector<Prog> harmonic   { { "i", "iv", "V", "i" }, { "i", "VI", "V", "i" }, { "i", "iio", "V", "i" }, { "i", "VI", "iv", "V" }, { "i", "iv", "VI", "V" } };
    static const std::vector<Prog> melodic    { { "i", "IV", "V", "i" }, { "i", "ii", "V", "i" }, { "i", "IV", "i", "V" } };
    static const std::vector<Prog> pentMajor  { { "I", "IV", "I", "V" }, { "I", "vi", "IV", "V" }, { "I", "V", "vi", "IV" } };
    static const std::vector<Prog> pentMinor  { { "i", "VII", "VI", "VII" }, { "i", "iv", "VII", "i" }, { "i", "VI", "VII", "i" } };
    static const std::vector<Prog> blues      { { "I7", "I7", "I7", "I7", "IV7", "IV7", "I7", "I7", "V7", "IV7", "I7", "V7" },
                                                { "I7", "IV7", "I7", "I7", "IV7", "IV7", "I7", "I7", "V7", "IV7", "I7", "V7" } };
    static const std::vector<Prog> doubleHarm { { "I", "II", "I", "II" }, { "I", "II", "iv", "II" }, { "I", "iv", "II", "I" } };
    static const std::vector<Prog> japanese   { { "i", "II", "i", "II" }, { "i", "VI", "II", "i" }, { "i", "iv", "II", "i" } };

    switch (m)
    {
        case Mode::Major:           return major;
        case Mode::Minor:           return minor;
        case Mode::Dorian:          return dorian;
        case Mode::Phrygian:        return phrygian;
        case Mode::Lydian:          return lydian;
        case Mode::Mixolydian:      return mixolydian;
        case Mode::Locrian:         return locrian;
        case Mode::HarmonicMinor:   return harmonic;
        case Mode::MelodicMinor:    return melodic;
        case Mode::PentatonicMajor: return pentMajor;
        case Mode::PentatonicMinor: return pentMinor;
        case Mode::Blues:           return blues;
        case Mode::DoubleHarmonic:  return doubleHarm;
        case Mode::Japanese:        return japanese;
    }

    return major;
}

//==============================================================================
struct Mood
{
    const char* name;
    std::vector<const char*> words;
    std::vector<Mode> modes;
    double tempoMul;
    double energy, density;
    int velocity;
    double complexity, swing;
    int registerShift;
    int dynamics; // -1 = unchanged, otherwise a Dynamics value
};

const std::vector<Mood>& moods()
{
    static const std::vector<Mood> list = {
        { "soft",       { "soft", "gentle", "quiet", "delicate", "tender", "intimate", "subtle", "softly", "light" },
                        {}, 0.9, -0.2, -0.2, -12, 0.0, 0.0, 0, -1 },
        { "calm",       { "calm", "peaceful", "serene", "tranquil", "relaxing", "relaxed", "soothing", "laid back", "lazy" },
                        {}, 0.88, -0.25, -0.15, -10, 0.0, 0.0, 0, -1 },
        { "energetic",  { "energetic", "upbeat", "driving", "intense", "aggressive", "pumping", "hype", "exciting", "lively", "uptempo", "bouncy" },
                        {}, 1.12, 0.3, 0.2, 12, 0.0, 0.0, 0, -1 },
        { "epic",       { "epic", "grand", "majestic", "huge", "massive", "powerful", "colossal" },
                        {}, 1.0, 0.25, 0.1, 10, 0.0, 0.0, 0, (int) Dynamics::Build },
        { "heroic",     { "heroic", "triumphant", "victorious", "glorious", "noble", "brave", "hopeful", "inspiring" },
                        { Mode::Major, Mode::Mixolydian, Mode::Lydian }, 1.0, 0.2, 0.05, 6, 0.0, 0.0, 0, -1 },
        { "sad",        { "sad", "melancholic", "melancholy", "sorrowful", "sorrow", "tragic", "grief", "mournful", "lonely", "heartbreak",
                          "heartbroken", "depressing", "somber", "sombre", "crying", "tears", "lament" },
                        { Mode::Minor, Mode::Minor, Mode::Dorian, Mode::HarmonicMinor }, 0.85, -0.15, -0.1, -6, 0.05, 0.0, 0, -1 },
        { "emotional",  { "emotional", "moving", "touching", "heartfelt", "bittersweet", "nostalgic", "nostalgia", "wistful", "longing" },
                        { Mode::Minor, Mode::Major }, 0.92, 0.0, 0.0, 0, 0.15, 0.0, 0, -1 },
        { "happy",      { "happy", "joyful", "cheerful", "bright", "sunny", "fun", "playful", "positive", "optimistic", "carefree",
                          "celebration", "festive", "uplifting" },
                        { Mode::Major, Mode::Major, Mode::Lydian, Mode::Mixolydian }, 1.05, 0.1, 0.05, 4, 0.0, 0.0, 0, -1 },
        { "dark",       { "dark", "sinister", "ominous", "evil", "menacing", "gloomy", "grim", "brooding", "villain", "shadow", "shadows" },
                        { Mode::HarmonicMinor, Mode::Phrygian, Mode::Minor }, 0.92, 0.0, 0.0, 0, 0.0, 0.0, -5, -1 },
        { "mysterious", { "mysterious", "mystery", "mystical", "mystic", "enigmatic", "curious", "secret", "cryptic" },
                        { Mode::Dorian, Mode::HarmonicMinor, Mode::Lydian }, 0.95, -0.05, 0.0, 0, 0.1, 0.0, 0, -1 },
        { "dreamy",     { "dreamy", "dream", "ethereal", "enchanting", "whimsical", "wonder", "wondrous", "sparkling", "celestial",
                          "heavenly", "magical" },
                        { Mode::Lydian, Mode::Major }, 0.95, -0.1, -0.05, -4, 0.15, 0.0, 0, -1 },
        { "romantic",   { "romantic", "love", "loving", "passionate", "sensual", "lovely" },
                        { Mode::Major, Mode::Minor }, 0.95, 0.0, 0.0, 0, 0.25, 0.0, 0, -1 },
        { "tense",      { "tense", "tension", "suspense", "suspenseful", "anxious", "nervous", "urgent", "thriller", "chase", "danger" },
                        { Mode::HarmonicMinor, Mode::Minor, Mode::Phrygian }, 1.05, 0.15, 0.1, 4, 0.0, 0.0, 0, (int) Dynamics::Build },
        { "jazzy",      { "jazzy", "sophisticated", "classy" },
                        {}, 1.0, 0.0, 0.0, 0, 0.4, 0.4, 0, -1 },
        { "groovy",     { "groovy", "funky", "swinging", "swingy" },
                        {}, 1.0, 0.05, 0.1, 0, 0.1, 0.15, 0, -1 },
        { "simple",     { "simple", "minimal", "minimalist", "basic", "sparse", "easy" },
                        {}, 1.0, -0.05, -0.25, 0, -0.2, 0.0, 0, -1 },
        { "complex",    { "complex", "intricate", "busy", "virtuosic", "technical", "fancy" },
                        {}, 1.0, 0.05, 0.2, 0, 0.2, 0.0, 0, -1 },
        { "slow",       { "slow", "slowly", "adagio", "largo" },
                        {}, 0.8, -0.1, -0.05, 0, 0.0, 0.0, 0, -1 },
        { "fast",       { "fast", "quick", "rapid", "allegro", "presto", "speedy" },
                        {}, 1.2, 0.1, 0.1, 0, 0.0, 0.0, 0, -1 },
        { "building",   { "build", "building", "buildup", "crescendo", "rising", "intro", "climax" },
                        {}, 1.0, 0.0, 0.0, 0, 0.0, 0.0, 0, (int) Dynamics::Build },
        { "fading",     { "fade", "fading", "outro", "ending", "farewell" },
                        {}, 1.0, 0.0, 0.0, 0, 0.0, 0.0, 0, (int) Dynamics::Fade },
        { "deep",       { "deep", "heavy", "rumbling" },
                        {}, 1.0, 0.05, 0.0, 4, 0.0, 0.0, -4, -1 },
        { "airy",       { "airy", "sparkly", "glassy" },
                        {}, 1.0, -0.05, 0.0, 0, 0.0, 0.0, 5, -1 },
    };

    return list;
}

//==============================================================================
struct Instrument
{
    std::vector<const char*> words;
    const char* name;
    int program;       // -1 = drum kit
    Role role;
    int low, high;
    const char* pattern;
    bool generic;      // a generic word ("guitar") keeps a more specific family member that's already there
    int familyLow, familyHigh;
};

const std::vector<Instrument>& instruments()
{
    static const std::vector<Instrument> list = {
        { { "grand piano", "piano", "pianos", "acoustic piano" }, "Piano",         0, R::Chords,         43, 79, "broken",  false, 0, 3 },
        { { "electric piano", "e piano", "rhodes", "wurlitzer", "keys" }, "Electric Piano", 4, R::Chords, 48, 74, "comp", false, 4, 5 },
        { { "harpsichord" },                                       "Harpsichord",     6, R::Chords,         41, 79, "broken",  false, 6, 6 },
        { { "celesta", "celeste" },                                "Celesta",         8, R::Arpeggio,       60, 96, "up",      false, 8, 8 },
        { { "glockenspiel", "glock" },                             "Glockenspiel",    9, R::CounterMelody,  72, 96, "auto",    false, 9, 9 },
        { { "music box" },                                         "Music Box",      10, R::Melody,         72, 96, "lyrical", false, 10, 10 },
        { { "vibraphone", "vibes" },                               "Vibraphone",     11, R::Chords,         53, 89, "comp",    false, 11, 11 },
        { { "marimba" },                                           "Marimba",        12, R::Arpeggio,       48, 84, "updown",  false, 12, 12 },
        { { "xylophone" },                                         "Xylophone",      13, R::Melody,         65, 96, "rhythmic", false, 13, 13 },
        { { "tubular bells", "bells", "chimes", "bell" },          "Bells",          14, R::CounterMelody,  60, 79, "sustained", false, 14, 14 },
        { { "church organ", "pipe organ", "organ" },               "Organ",          19, R::Pad,            36, 84, "sustain", true, 16, 20 },
        { { "hammond", "rock organ" },                             "Rock Organ",     18, R::Chords,         48, 76, "block",   false, 16, 18 },
        { { "accordion" },                                         "Accordion",      21, R::Chords,         48, 76, "waltz",   false, 21, 21 },
        { { "harmonica" },                                         "Harmonica",      22, R::Melody,         60, 84, "lyrical", false, 22, 22 },
        { { "nylon guitar", "classical guitar", "spanish guitar", "flamenco" }, "Nylon Guitar", 24, R::Chords, 40, 76, "broken", false, 24, 24 },
        { { "acoustic guitar", "guitar", "guitars" },              "Acoustic Guitar", 25, R::Chords,        40, 76, "strum",   true, 24, 31 },
        { { "jazz guitar" },                                       "Jazz Guitar",    26, R::Chords,         45, 76, "comp",    false, 26, 26 },
        { { "electric guitar", "clean guitar" },                   "Electric Guitar", 27, R::Chords,        45, 76, "strum",   false, 27, 28 },
        { { "distorted guitar", "distortion", "heavy guitar" },    "Distortion Guitar", 30, R::Chords,      40, 64, "power",   false, 29, 30 },
        { { "lute" },                                              "Lute",           24, R::Arpeggio,       45, 76, "updown",  false, 24, 24 },
        { { "upright bass", "double bass", "contrabass", "acoustic bass" }, "Upright Bass", 32, R::Bass,    28, 52, "auto",    false, 32, 32 },
        { { "synth bass", "bassline" },                            "Synth Bass",     38, R::Bass,           28, 52, "auto",    false, 38, 39 },
        { { "808", "808s" },                                       "808 Bass",       38, R::Bass,           24, 48, "808",     false, 38, 38 },
        { { "bass guitar", "electric bass", "bass" },              "Bass",           33, R::Bass,           28, 52, "auto",    true, 32, 39 },
        { { "violin", "violins" },                                 "Violin",         40, R::Melody,         55, 96, "lyrical", false, 40, 40 },
        { { "viola" },                                             "Viola",          41, R::CounterMelody,  48, 79, "auto",    false, 41, 41 },
        { { "cello", "cellos", "violoncello" },                    "Cello",          42, R::CounterMelody,  36, 67, "sustained", false, 42, 42 },
        { { "pizzicato", "pizz" },                                 "Pizzicato Strings", 45, R::Arpeggio,    43, 79, "random",  false, 45, 45 },
        { { "harp", "harps" },                                     "Harp",           46, R::Arpeggio,       36, 91, "updown",  false, 46, 46 },
        { { "timpani", "timpany" },                                "Timpani",        47, R::Bass,           36, 55, "timpani", false, 47, 47 },
        { { "string section", "string ensemble", "strings", "string" }, "Strings",   48, R::Pad,            43, 84, "sustain", true, 48, 51 },
        { { "choir", "choirs", "chorus", "voices", "vocals", "vocal", "singers", "aahs" }, "Choir", 52, R::Pad, 50, 76, "swell", false, 52, 54 },
        { { "trumpet", "trumpets" },                               "Trumpet",        56, R::Melody,         55, 82, "lyrical", false, 56, 56 },
        { { "trombone", "trombones" },                             "Trombone",       57, R::CounterMelody,  40, 72, "auto",    false, 57, 57 },
        { { "tuba" },                                              "Tuba",           58, R::Bass,           28, 58, "auto",    false, 58, 58 },
        { { "french horn", "french horns", "horn", "horns" },      "French Horn",    60, R::CounterMelody,  41, 77, "auto",    false, 60, 60 },
        { { "brass", "brass section" },                            "Brass",          61, R::Chords,         45, 74, "block",   true, 56, 63 },
        { { "synth brass" },                                       "Synth Brass",    62, R::Chords,         48, 76, "stabs",   false, 62, 63 },
        { { "soprano sax" },                                       "Soprano Sax",    64, R::Melody,         56, 87, "lyrical", false, 64, 64 },
        { { "tenor sax" },                                         "Tenor Sax",      66, R::Melody,         44, 75, "lyrical", false, 66, 66 },
        { { "saxophone", "sax", "alto sax" },                      "Alto Sax",       65, R::Melody,         49, 80, "lyrical", false, 64, 67 },
        { { "oboe" },                                              "Oboe",           68, R::Melody,         58, 91, "lyrical", false, 68, 68 },
        { { "english horn", "cor anglais" },                       "English Horn",   69, R::Melody,         52, 81, "lyrical", false, 69, 69 },
        { { "bassoon" },                                           "Bassoon",        70, R::CounterMelody,  34, 65, "auto",    false, 70, 70 },
        { { "clarinet", "clarinets" },                             "Clarinet",       71, R::Melody,         50, 91, "lyrical", false, 71, 71 },
        { { "piccolo" },                                           "Piccolo",        72, R::CounterMelody,  74, 102, "flowing", false, 72, 72 },
        { { "flute", "flutes" },                                   "Flute",          73, R::Melody,         60, 96, "lyrical", false, 73, 73 },
        { { "recorder" },                                          "Recorder",       74, R::Melody,         60, 91, "lyrical", false, 74, 74 },
        { { "pan flute", "panflute", "pan pipes", "panpipes" },    "Pan Flute",      75, R::Melody,         60, 91, "lyrical", false, 75, 75 },
        { { "shakuhachi" },                                        "Shakuhachi",     77, R::Melody,         55, 84, "lyrical", false, 77, 77 },
        { { "tin whistle", "penny whistle", "whistle" },           "Whistle",        78, R::Melody,         67, 96, "rhythmic", false, 78, 78 },
        { { "ocarina" },                                           "Ocarina",        79, R::Melody,         60, 91, "lyrical", false, 79, 79 },
        { { "synth lead", "lead synth", "saw lead" },              "Synth Lead",     81, R::Melody,         60, 88, "lyrical", false, 80, 87 },
        { { "synth", "synths", "synthesizer", "synthesizers" },    "Poly Synth",     90, R::Chords,         48, 76, "sustain", true, 80, 103 },
        { { "synth pad", "pad", "pads" },                          "Warm Pad",       89, R::Pad,            48, 79, "sustain", true, 88, 95 },
        { { "sitar" },                                             "Sitar",         104, R::Melody,         52, 84, "rhythmic", false, 104, 104 },
        { { "banjo" },                                             "Banjo",         105, R::Arpeggio,       55, 81, "updown",  false, 105, 105 },
        { { "koto" },                                              "Koto",          107, R::Arpeggio,       48, 84, "random",  false, 107, 107 },
        { { "kalimba", "mbira" },                                  "Kalimba",       108, R::Arpeggio,       60, 91, "random",  false, 108, 108 },
        { { "bagpipe", "bagpipes" },                               "Bagpipe",       109, R::Melody,         62, 81, "lyrical", false, 109, 109 },
        { { "fiddle" },                                            "Fiddle",        110, R::Melody,         55, 91, "rhythmic", false, 110, 110 },
        { { "steel drums", "steel drum", "steelpan", "steel pan" }, "Steel Drums",  114, R::Melody,         60, 86, "rhythmic", false, 114, 114 },
        { { "taiko" },                                             "Taiko",          -1, R::Percussion,     35, 81, "tribal",  false, -1, -1 },
        { { "drum kit", "drums", "drum", "percussion", "beat", "beats", "drumbeat" }, "Drums", -1, R::Percussion, 35, 81, "auto", true, -1, -1 },
    };

    return list;
}

//==============================================================================
std::vector<std::string> tokenize (const std::string& prompt)
{
    std::string s;

    for (size_t i = 0; i < prompt.size(); ++i)
    {
        const unsigned char c = (unsigned char) prompt[i];

        if (std::isalnum (c) || c == '#' || c == '/' || c == '&')
            s += (char) std::tolower (c);
        else if (c == '\'')
            continue; // "children's" -> "childrens"
        else if (c == ',' || c == '.' || c == ';' || c == '!' || c == '?')
            s += " , ";
        else
            s += ' ';
    }

    std::vector<std::string> tokens;
    std::string cur;

    for (char c : s)
    {
        if (c == ' ')
        {
            if (! cur.empty())
                tokens.push_back (cur);

            cur.clear();
        }
        else
        {
            cur += c;
        }
    }

    if (! cur.empty())
        tokens.push_back (cur);

    // Split glued numbers like "120bpm", "16bars", "30sec".
    std::vector<std::string> out;

    for (auto& t : tokens)
    {
        size_t digits = 0;

        while (digits < t.size() && std::isdigit ((unsigned char) t[digits]))
            ++digits;

        static const std::set<std::string> units { "bpm", "bar", "bars", "sec", "secs", "second", "seconds", "min", "mins", "minute", "minutes", "measures" };

        if (digits > 0 && digits < t.size() && units.count (t.substr (digits)) > 0)
        {
            out.push_back (t.substr (0, digits));
            out.push_back (t.substr (digits));
        }
        else
        {
            out.push_back (t);
        }
    }

    return out;
}

std::vector<std::string> splitWords (const char* phrase)
{
    std::vector<std::string> words;
    std::string cur;

    for (const char* p = phrase; *p != 0; ++p)
    {
        if (*p == ' ')
        {
            if (! cur.empty())
                words.push_back (cur);

            cur.clear();
        }
        else
        {
            cur += *p;
        }
    }

    if (! cur.empty())
        words.push_back (cur);

    return words;
}

/** Positions where the phrase occurs as a whole-token sequence (also matching a plural 's'). */
std::vector<int> findPhrase (const std::vector<std::string>& tokens, const char* phrase)
{
    const auto words = splitWords (phrase);
    std::vector<int> hits;

    if (words.empty())
        return hits;

    for (size_t i = 0; i + words.size() <= tokens.size(); ++i)
    {
        bool ok = true;

        for (size_t w = 0; w < words.size() && ok; ++w)
        {
            const auto& tok = tokens[i + w];
            const auto& word = words[w];
            const bool last = w + 1 == words.size();
            ok = tok == word || (last && word.back() != 's' && tok == word + "s");
        }

        if (ok)
            hits.push_back ((int) i);
    }

    return hits;
}

bool isNumber (const std::string& s, double& value)
{
    if (s.empty())
        return false;

    char* end = nullptr;
    value = std::strtod (s.c_str(), &end);
    return end != nullptr && *end == 0;
}

std::string titleCase (const std::string& w)
{
    std::string s = w;

    if (! s.empty())
        s[0] = (char) std::toupper ((unsigned char) s[0]);

    return s;
}

std::string makeTitle (const std::vector<std::string>& tokens, const std::vector<bool>& skip, const std::string& fallback)
{
    static const std::set<std::string> filler {
        "generate", "make", "create", "compose", "write", "produce", "give", "me", "us", "a", "an", "the", "some", "please",
        "midi", "midis", "mid", "file", "song", "track", "piece", "music", "tune", "for", "i", "want", "need", "can", "you",
        "could", "would", "like", "id", "im", "of", "that", "is", "sounds", "sound", "something", "kind", "type", "style", ",",
        "bpm", "bar", "bars", "in", "at", "key", "with", "and", "loop", "seconds", "second", "minutes", "minute",
        "major", "minor", "sharp", "flat", "tempo", "no", "without", "very", "really", "kinda", "bit", "little"
    };

    std::string title;
    int words = 0;

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const auto& t = tokens[i];
        double num;

        if ((i < skip.size() && skip[i]) || filler.count (t) > 0 || isNumber (t, num) || t.find ('/') != std::string::npos)
            continue;

        if (words >= 4)
            break;

        if (! title.empty())
            title += ' ';

        title += titleCase (t);
        ++words;
    }

    return title.empty() ? fallback : title;
}

std::string joinList (const std::vector<std::string>& items)
{
    std::string s;

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
            s += ", ";

        s += items[i];
    }

    return s;
}

int familyOf (int program)
{
    if (program < 0)                      return -1;
    if (program >= 40 && program <= 47)   return 1000 + program; // solo strings, harp and timpani are all different
    return program / 8;
}

} // namespace

//==============================================================================
std::vector<StyleInfo> builtInStyles()
{
    std::vector<StyleInfo> out;

    for (auto& s : styles())
        out.push_back ({ s.id, s.name });

    return out;
}

StyleSpec interpretPrompt (const std::string& prompt, uint64_t seed, const PromptOverrides& overrides)
{
    const auto tokens = tokenize (prompt);
    const int nt = (int) tokens.size();

    uint64_t promptHash = 1469598103934665603ull;

    for (auto& t : tokens)
        for (unsigned char c : t)
            promptHash = (promptHash ^ c) * 1099511628211ull;

    Rng rng (seed ^ promptHash);

    std::vector<bool> consumed ((size_t) nt, false);

    auto hasWord = [&] (const char* phrase) { return ! findPhrase (tokens, phrase).empty(); };

    //==========================================================================
    // 1. Genre
    const StyleTemplate* style = nullptr;
    double bestScore = 0.0;
    std::vector<std::string> matchedKeywords;

    for (auto& st : styles())
    {
        double score = 0.0;
        std::vector<std::string> matched;

        for (auto& kw : st.keywords)
        {
            if (hasWord (kw.first))
            {
                score += kw.second;
                matched.push_back (kw.first);
            }
        }

        if (score > bestScore)
        {
            bestScore = score;
            style = &st;
            matchedKeywords = matched;
        }
    }

    const bool defaulted = style == nullptr;

    if (defaulted)
        style = &findStyle ("default_cinematic");

    // Multi-word genre names ("drum and bass", "music box") shouldn't also count as instruments.
    for (auto& kw : style->keywords)
    {
        if (splitWords (kw.first).size() < 2)
            continue;

        for (int pos : findPhrase (tokens, kw.first))
            for (size_t w = 0; w < splitWords (kw.first).size(); ++w)
                consumed[(size_t) pos + w] = true;
    }

    StyleSpec spec;
    spec.styleId = style->id;
    spec.tempo = rng.range (style->tempoMin, style->tempoMax);
    spec.tsNum = style->tsNum;
    spec.tsDen = style->tsDen;
    spec.bars = style->bars;
    spec.chordsPerBar = style->chordsPerBar;
    spec.swing = style->swing;
    spec.swingSixteenths = style->swing16;
    spec.energy = style->energy;
    spec.complexity = style->complexity;
    spec.humanize = style->humanize;
    spec.dynamics = style->dynamics;
    spec.resolveEnding = style->resolve;

    if (defaulted)
        spec.interpretation.push_back (std::string ("Style: ") + style->name + " (no genre recognised, using the default)");
    else
        spec.interpretation.push_back (std::string ("Style: ") + style->name + " (" + joinList (matchedKeywords) + ")");

    //==========================================================================
    // 2. Moods
    double tempoMul = 1.0, energyDelta = 0.0, densityDelta = 0.0, complexityDelta = 0.0, swingDelta = 0.0;
    int velocityDelta = 0, registerShift = 0;
    std::vector<Mode> moodModes;
    std::vector<std::string> moodNames;

    for (auto& mood : moods())
    {
        bool hit = false;

        for (auto* w : mood.words)
            if (hasWord (w))
                hit = true;

        if (! hit)
            continue;

        moodNames.push_back (mood.name);
        tempoMul *= mood.tempoMul;
        energyDelta += mood.energy;
        densityDelta += mood.density;
        velocityDelta += mood.velocity;
        complexityDelta += mood.complexity;
        swingDelta += mood.swing;
        registerShift += mood.registerShift;

        if (mood.dynamics >= 0)
            spec.dynamics = (Dynamics) mood.dynamics;

        if (moodModes.empty() && ! mood.modes.empty())
            moodModes = mood.modes;
    }

    if (hasWord ("very"))
    {
        tempoMul = std::pow (tempoMul, 1.4);
        energyDelta *= 1.3;
    }

    if (! moodNames.empty())
        spec.interpretation.push_back ("Mood: " + joinList (moodNames));

    //==========================================================================
    // 3. Explicit parameters
    int explicitTempo = -1, explicitBars = -1, explicitKey = -1;
    double explicitSeconds = -1.0;
    bool haveExplicitMode = false;
    Mode explicitMode = Mode::Major;

    for (int i = 0; i < nt; ++i)
    {
        const auto& t = tokens[(size_t) i];
        const std::string next = i + 1 < nt ? tokens[(size_t) i + 1] : std::string();
        const std::string prev = i > 0 ? tokens[(size_t) i - 1] : std::string();
        double value = 0.0;

        if (isNumber (t, value))
        {
            if (next == "bpm" || prev == "bpm" || prev == "tempo")
                explicitTempo = (int) std::lround (value);
            else if (next == "bar" || next == "bars" || next == "measure" || next == "measures")
                explicitBars = (int) std::lround (value);
            else if (next == "sec" || next == "secs" || next == "second" || next == "seconds")
                explicitSeconds = value;
            else if (next == "min" || next == "mins" || next == "minute" || next == "minutes")
                explicitSeconds = value * 60.0;
        }

        if (t == "4/4" || t == "3/4" || t == "6/8" || t == "12/8" || t == "2/4")
        {
            const auto slash = t.find ('/');
            spec.tsNum = std::atoi (t.substr (0, slash).c_str());
            spec.tsDen = std::atoi (t.substr (slash + 1).c_str());
        }
    }

    if (hasWord ("waltz") || hasWord ("minuet"))   { spec.tsNum = 3; spec.tsDen = 4; }
    if (hasWord ("jig"))                           { spec.tsNum = 6; spec.tsDen = 8; }
    if (hasWord ("reel") || hasWord ("hornpipe"))  { spec.tsNum = 4; spec.tsDen = 4; }

    // Key: "in D minor", "C# major", "key of Eb", "f sharp minor", "in G"
    std::vector<bool> titleSkip ((size_t) nt, false);
    static const std::set<std::string> modeWords { "major", "minor", "maj", "min", "dorian", "phrygian", "lydian", "mixolydian",
                                                   "locrian", "aeolian", "ionian" };

    for (int i = 0; i < nt && explicitKey < 0; ++i)
    {
        int pc = 0;

        if (tokens[(size_t) i].size() > 2 || ! pitchClassFromString (tokens[(size_t) i], pc))
            continue;

        int next = i + 1;

        if (next < nt && (tokens[(size_t) next] == "sharp" || tokens[(size_t) next] == "flat"))
        {
            pc = wrapPc (pc + (tokens[(size_t) next] == "sharp" ? 1 : -1));
            ++next;
        }

        const std::string w1 = next < nt ? tokens[(size_t) next] : std::string();
        const std::string w2 = next + 1 < nt ? tokens[(size_t) next + 1] : std::string();
        Mode md = Mode::Major;
        bool hasMode = false;

        if (w1 == "harmonic" && w2 == "minor")         { md = Mode::HarmonicMinor; hasMode = true; }
        else if (w1 == "melodic" && w2 == "minor")     { md = Mode::MelodicMinor; hasMode = true; }
        else if (w1 == "minor" && w2 == "pentatonic")  { md = Mode::PentatonicMinor; hasMode = true; }
        else if (w1 == "major" && w2 == "pentatonic")  { md = Mode::PentatonicMajor; hasMode = true; }
        else if (w1 == "blues")                        { md = Mode::Blues; hasMode = true; }
        else if (modeWords.count (w1) > 0)             { hasMode = modeFromString (w1, md); }

        const std::string p1 = i > 0 ? tokens[(size_t) i - 1] : std::string();
        const std::string p2 = i > 1 ? tokens[(size_t) i - 2] : std::string();
        const bool keyContext = p1 == "key" || (p2 == "key" && p1 == "of") || (p1 == "in" && tokens[(size_t) i] != "a");

        if (hasMode || keyContext)
        {
            explicitKey = pc;

            for (int k = i; k < std::min (nt, next + (hasMode ? 1 : 0)); ++k)
                titleSkip[(size_t) k] = true;

            if (hasMode)
            {
                explicitMode = md;
                haveExplicitMode = true;
            }
        }
    }

    if (! haveExplicitMode)
    {
        struct ModeWord { const char* word; Mode mode; };
        static const ModeWord words[] = {
            { "harmonic minor", Mode::HarmonicMinor }, { "melodic minor", Mode::MelodicMinor }, { "minor pentatonic", Mode::PentatonicMinor },
            { "major pentatonic", Mode::PentatonicMajor }, { "pentatonic", Mode::PentatonicMajor }, { "dorian", Mode::Dorian },
            { "phrygian", Mode::Phrygian }, { "lydian", Mode::Lydian }, { "mixolydian", Mode::Mixolydian }, { "locrian", Mode::Locrian },
            { "minor", Mode::Minor }, { "major", Mode::Major },
        };

        for (auto& mw : words)
        {
            if (hasWord (mw.word))
            {
                explicitMode = mw.mode;
                haveExplicitMode = true;
                break;
            }
        }
    }

    if (overrides.keyRoot >= 0)  explicitKey = wrapPc (overrides.keyRoot);
    if (overrides.tempo > 0)     explicitTempo = overrides.tempo;
    if (overrides.bars > 0)      explicitBars = overrides.bars;

    if (overrides.mode >= 0 && overrides.mode <= (int) Mode::Japanese)
    {
        explicitMode = (Mode) overrides.mode;
        haveExplicitMode = true;
    }

    // Region / culture words that imply a scale.
    if (! haveExplicitMode)
    {
        if (hasWord ("chinese") || hasWord ("china"))
        {
            explicitMode = Mode::PentatonicMajor;
            haveExplicitMode = true;
        }
        else if (hasWord ("japanese") || hasWord ("japan") || hasWord ("samurai") || hasWord ("koto"))
        {
            explicitMode = Mode::Japanese;
            haveExplicitMode = true;
        }
    }

    //==========================================================================
    // 4. Key, mode and harmony
    // The random draws happen whether or not they're overridden, so changing the key in the
    // UI doesn't reshuffle every other choice the seed made.
    const Mode styleMode = rng.pick (style->modes);
    const Mode moodMode = moodModes.empty() ? styleMode : rng.pick (moodModes);
    static const std::vector<int> friendlyKeys { 0, 2, 4, 5, 7, 9, 10, 3 }; // keys that sit well on most instruments
    const int randomKey = rng.pick (friendlyKeys);
    const double progressionPick = rng.uniform();

    if (haveExplicitMode)           spec.mode = explicitMode;
    else if (! moodModes.empty())   spec.mode = moodMode;
    else                            spec.mode = styleMode;

    spec.keyRoot = explicitKey >= 0 ? explicitKey : randomKey;

    {
        const std::vector<Prog>* bank = &modeProgressions (spec.mode);

        if (spec.mode == Mode::Major && ! style->progMajor.empty())
            bank = &style->progMajor;
        else if (spec.mode == Mode::Minor && ! style->progMinor.empty())
            bank = &style->progMinor;

        const auto index = std::min (bank->size() - 1, (size_t) (progressionPick * (double) bank->size()));
        const auto& prog = (*bank)[index];
        spec.progression.assign (prog.begin(), prog.end());
    }

    spec.interpretation.push_back ("Key: " + keyName (spec.keyRoot, spec.mode)
                                   + (explicitKey >= 0 || haveExplicitMode ? std::string (" (requested)") : std::string()));

    //==========================================================================
    // 5. Tempo and length
    spec.tempo = (int) std::lround (spec.tempo * tempoMul);

    if (explicitTempo > 0)
        spec.tempo = explicitTempo;

    spec.tempo = std::max (40, std::min (220, spec.tempo));

    if (hasWord ("short"))  spec.bars = 8;
    if (hasWord ("long"))   spec.bars = 32;

    if (hasWord ("loop") || hasWord ("loopable") || hasWord ("seamless"))
        spec.resolveEnding = false;

    if (hasWord ("ending") || hasWord ("resolve") || hasWord ("finale"))
        spec.resolveEnding = true;

    if (explicitSeconds > 0.0)
    {
        const double quartersPerBar = spec.tsNum * 4.0 / spec.tsDen;
        const double bars = explicitSeconds * spec.tempo / 60.0 / quartersPerBar;
        spec.bars = std::max (4, (int) std::lround (bars / 4.0) * 4);
    }

    if (explicitBars > 0)
        spec.bars = explicitBars;

    spec.bars = std::max (1, std::min (128, spec.bars));

    spec.interpretation.push_back ("Tempo: " + std::to_string (spec.tempo) + " BPM" + (explicitTempo > 0 ? " (requested)" : "")
                                   + ", " + std::to_string (spec.bars) + " bars in " + std::to_string (spec.tsNum) + "/" + std::to_string (spec.tsDen));

    //==========================================================================
    // 6. Arrangement
    struct Planned { TrackSpec spec; int layer; bool explicitInstrument; };
    std::vector<Planned> tracks;

    for (auto& tt : style->tracks)
    {
        TrackSpec ts;
        ts.name = tt.name;
        ts.role = tt.role;
        ts.program = tt.program;
        ts.lowNote = tt.low;
        ts.highNote = tt.high;
        ts.density = tt.density;
        ts.velocity = tt.velocity;
        ts.pattern = tt.pattern;
        tracks.push_back ({ ts, tt.layer, false });
    }

    // Find instrument mentions (longest phrases first) and whether they are negated.
    struct Mention { const Instrument* inst; int pos; bool negated; };
    std::vector<Mention> mentions;

    {
        std::vector<std::pair<int, std::pair<const Instrument*, const char*>>> phrases; // word count, (instrument, phrase)

        for (auto& inst : instruments())
            for (auto* w : inst.words)
                phrases.push_back ({ (int) splitWords (w).size(), { &inst, w } });

        std::stable_sort (phrases.begin(), phrases.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });

        static const std::set<std::string> negators { "no", "without", "minus", "except", "not", "zero", "lose", "remove", "skip" };
        static const std::set<std::string> resets { "but", "with", "plus", ",", "and", "then", "featuring", "add" };

        for (auto& ph : phrases)
        {
            for (int pos : findPhrase (tokens, ph.second.second))
            {
                bool free = true;

                for (int w = 0; w < ph.first; ++w)
                    free = free && ! consumed[(size_t) (pos + w)];

                if (! free)
                    continue;

                for (int w = 0; w < ph.first; ++w)
                    consumed[(size_t) (pos + w)] = true;

                bool negated = false;

                for (int back = pos - 1; back >= std::max (0, pos - 4); --back)
                {
                    const auto& tok = tokens[(size_t) back];

                    if (resets.count (tok) > 0)
                        break;

                    if (negators.count (tok) > 0)
                    {
                        negated = true;
                        break;
                    }
                }

                mentions.push_back ({ ph.second.first, pos, negated });
            }
        }

        std::sort (mentions.begin(), mentions.end(), [] (const Mention& a, const Mention& b) { return a.pos < b.pos; });
    }

    std::vector<std::string> added, removed, changed;
    bool anyPositivePitched = false;

    for (auto& m : mentions)
        if (! m.negated && m.inst->program >= 0)
            anyPositivePitched = true;

    // "only piano and cello" / "just strings" / "solo violin" restrict the arrangement to what was named.
    // A bare "solo" inside a genre prompt ("rock with a guitar solo") does not.
    const bool strongSolo = hasWord ("only") || hasWord ("just") || hasWord ("unaccompanied") || hasWord ("alone");
    const bool weakSolo = hasWord ("solo") && (defaulted || std::string (style->id) == "solo_piano" || std::string (style->id) == "classical");
    const bool soloMode = anyPositivePitched && (strongSolo || weakSolo);

    auto makeTrack = [] (const Instrument& inst, Role role, const char* nameSuffix) -> TrackSpec
    {
        TrackSpec ts;
        ts.name = std::string (inst.name) + nameSuffix;
        ts.role = role;
        ts.program = std::max (0, inst.program);
        ts.lowNote = inst.low;
        ts.highNote = inst.high;
        ts.density = 0.45;
        ts.velocity = 85;
        ts.pattern = role == inst.role ? inst.pattern : "auto";
        return ts;
    };

    if (soloMode && anyPositivePitched)
    {
        // Only what was asked for.
        std::vector<Planned> solo;
        bool haveLead = false;

        for (auto& m : mentions)
        {
            if (m.negated)
                continue;

            const Instrument& inst = *m.inst;

            if (inst.program < 0)
            {
                TrackSpec ts = makeTrack (inst, Role::Percussion, "");
                ts.pattern = std::string (inst.pattern) == "auto" ? style->drums : inst.pattern;
                ts.density = 0.5;
                solo.push_back ({ ts, 0, true });
                continue;
            }

            const bool polyphonic = inst.program <= 15 || (inst.program >= 16 && inst.program <= 21)
                                 || (inst.program >= 24 && inst.program <= 27) || inst.program == 46 || inst.program == 105
                                 || inst.program == 107 || inst.program == 108;

            if (polyphonic && ! haveLead)
            {
                TrackSpec lead = makeTrack (inst, Role::Melody, " (Melody)");
                lead.lowNote = std::max (inst.low, 60);
                lead.highNote = std::max (lead.lowNote + 24, inst.high);
                lead.pattern = "lyrical";
                solo.push_back ({ lead, 0, true });

                const Role accRole = (inst.program == 46 || inst.program >= 104) ? Role::Arpeggio : Role::Chords;
                TrackSpec acc = makeTrack (inst, accRole, " (Accompaniment)");
                acc.lowNote = inst.low;
                acc.highNote = std::min (inst.high, 67);
                acc.pattern = accRole == Role::Arpeggio ? "updown" : (std::string (inst.pattern) == "strum" ? "strum" : "broken");
                acc.velocity = 72;
                acc.density = 0.5;
                solo.push_back ({ acc, 0, true });
                haveLead = true;
            }
            else if (inst.role == Role::Melody || inst.role == Role::CounterMelody)
            {
                solo.push_back ({ makeTrack (inst, haveLead ? Role::CounterMelody : Role::Melody, ""), 0, true });
                haveLead = true;
            }
            else
            {
                solo.push_back ({ makeTrack (inst, inst.role, ""), 0, true });
            }

            added.push_back (inst.name);
        }

        tracks = solo;
        spec.interpretation.push_back ("Arrangement: only " + joinList (added));
        added.clear();
    }
    else
    {
        bool leadReplaced = false;

        for (auto& m : mentions)
        {
            const Instrument& inst = *m.inst;

            if (m.negated)
            {
                const size_t before = tracks.size();
                tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [&] (const Planned& p)
                {
                    if (inst.program < 0)
                        return p.spec.role == Role::Percussion;

                    if (p.spec.role == Role::Percussion)
                        return false;

                    if (inst.role == Role::Bass && std::string (inst.name) == "Bass")
                        return p.spec.role == Role::Bass;

                    return p.spec.program >= inst.familyLow && p.spec.program <= inst.familyHigh;
                }), tracks.end());

                if (tracks.size() != before)
                    removed.push_back (inst.name);

                continue;
            }

            if (inst.program < 0)
            {
                auto it = std::find_if (tracks.begin(), tracks.end(), [] (const Planned& p) { return p.spec.role == Role::Percussion; });

                if (it == tracks.end())
                {
                    TrackSpec ts = makeTrack (inst, Role::Percussion, "");
                    ts.pattern = std::string (inst.pattern) == "auto" ? style->drums : inst.pattern;
                    ts.density = 0.5 + densityDelta * 0.5;
                    ts.velocity = 90;
                    tracks.push_back ({ ts, 0, true });
                    added.push_back (inst.name);
                }
                else if (std::string (inst.pattern) != "auto")
                {
                    it->spec.pattern = inst.pattern;
                    changed.push_back (std::string ("drums \xE2\x86\x92 ") + inst.name);
                }

                continue;
            }

            // Already there?
            if (std::any_of (tracks.begin(), tracks.end(), [&] (const Planned& p) { return p.spec.role != Role::Percussion && p.spec.program == inst.program; }))
            {
                for (auto& p : tracks)
                    if (p.spec.program == inst.program)
                        p.explicitInstrument = true;

                continue;
            }

            // A relative of it is there: swap it in (unless the word is generic, like "guitar").
            auto relative = std::find_if (tracks.begin(), tracks.end(), [&] (const Planned& p)
            {
                return p.spec.role != Role::Percussion && familyOf (p.spec.program) == familyOf (inst.program)
                    && ! p.explicitInstrument;
            });

            if (relative != tracks.end())
            {
                relative->explicitInstrument = true;

                if (! inst.generic)
                {
                    changed.push_back (std::string (gmProgramName (relative->spec.program)) + " \xE2\x86\x92 " + inst.name);
                    relative->spec.program = inst.program;
                    relative->spec.name = inst.name;
                }

                continue;
            }

            if (inst.role == Role::Melody && ! leadReplaced)
            {
                auto lead = std::find_if (tracks.begin(), tracks.end(), [] (const Planned& p) { return p.spec.role == Role::Melody && ! p.explicitInstrument; });

                if (lead != tracks.end())
                {
                    changed.push_back (std::string ("lead \xE2\x86\x92 ") + inst.name);
                    lead->spec.program = inst.program;
                    lead->spec.name = inst.name;
                    lead->spec.lowNote = inst.low;
                    lead->spec.highNote = inst.high;
                    lead->explicitInstrument = true;
                    leadReplaced = true;
                    continue;
                }
            }

            if (tracks.size() >= 12)
                continue;

            const bool hasLead = std::any_of (tracks.begin(), tracks.end(), [] (const Planned& p) { return p.spec.role == Role::Melody; });
            const Role role = (inst.role == Role::Melody && hasLead) ? Role::CounterMelody : inst.role;
            TrackSpec ts = makeTrack (inst, role, "");
            tracks.push_back ({ ts, role == Role::Melody ? 0 : 1, true });
            added.push_back (inst.name);
        }
    }

    if (tracks.empty())
    {
        // Everything was removed: keep at least a lead and a harmony part.
        TrackSpec lead;
        lead.name = "Piano (Melody)";
        lead.role = Role::Melody;
        lead.lowNote = 60;
        lead.highNote = 88;
        TrackSpec acc;
        acc.name = "Piano (Accompaniment)";
        acc.role = Role::Chords;
        acc.lowNote = 40;
        acc.highNote = 67;
        acc.pattern = "broken";
        tracks.push_back ({ lead, 0, false });
        tracks.push_back ({ acc, 0, false });
    }

    if (! added.empty())    spec.interpretation.push_back ("Added: " + joinList (added));
    if (! removed.empty())  spec.interpretation.push_back ("Removed: " + joinList (removed));
    if (! changed.empty())  spec.interpretation.push_back ("Changed: " + joinList (changed));

    //==========================================================================
    // 7. Apply moods to the tracks, then stagger entries for longer pieces.
    spec.energy = std::max (0.0, std::min (1.0, spec.energy + energyDelta));
    spec.complexity = std::max (0.0, std::min (1.0, spec.complexity + complexityDelta));
    spec.swing = std::max (0.0, std::min (1.0, spec.swing + swingDelta));

    const int layer1 = spec.bars >= 16 ? 4 : 0;
    const int layer2 = spec.bars >= 8 ? std::min (8, spec.bars / 2) : 0;

    for (auto& p : tracks)
    {
        auto& ts = p.spec;
        ts.density = std::max (0.05, std::min (0.95, ts.density + densityDelta));
        ts.velocity = std::max (20, std::min (127, ts.velocity + velocityDelta));

        if (ts.role != Role::Percussion)
        {
            ts.lowNote = std::max (12, ts.lowNote + registerShift);
            ts.highNote = std::min (115, ts.highNote + registerShift);
        }

        ts.entryBar = p.layer == 0 ? 0 : (p.layer == 1 ? layer1 : layer2);

        if (! spec.resolveEnding && ts.entryBar > 0 && spec.bars <= 8)
            ts.entryBar = 0;

        spec.tracks.push_back (ts);
    }

    spec.title = makeTitle (tokens, titleSkip, style->name);
    normalizeSpec (spec);
    return spec;
}

} // namespace pm
