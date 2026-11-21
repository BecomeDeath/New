#include "PianoRoll.h"
#include "Theme.h"

PianoRoll::PianoRoll()
{
    setOpaque (true);
}

juce::Colour PianoRoll::trackColour (int index)
{
    static const juce::uint32 colours[] = {
        0xff8b7cff, 0xff4fc3f7, 0xff66d9a3, 0xffffc857, 0xffff7a90, 0xffb388ff,
        0xff4dd0e1, 0xffaed581, 0xffffab70, 0xfff48fb1, 0xff90caf9, 0xffe6ee9c,
        0xffce93d8, 0xff80cbc4, 0xffffe082, 0xffbcaaa4
    };

    return juce::Colour (colours[(size_t) (index < 0 ? 0 : index % 16)]);
}

void PianoRoll::setComposition (const pm::Composition* c)
{
    comp = c;
    cacheDirty = true;
    repaint();
}

void PianoRoll::setSelectedTrack (int index)
{
    if (index != selectedTrack)
    {
        selectedTrack = index;
        cacheDirty = true;
        repaint();
    }
}

void PianoRoll::setMutedMask (uint32_t mask)
{
    if (mask != mutedMask)
    {
        mutedMask = mask;
        cacheDirty = true;
        repaint();
    }
}

void PianoRoll::setPlayhead (double tick)
{
    if (std::abs (tick - playhead) > 0.5)
    {
        playhead = tick;
        repaint();
    }
}

void PianoRoll::resized()
{
    cacheDirty = true;
}

bool PianoRoll::hasDrums() const
{
    if (comp == nullptr)
        return false;

    for (auto& t : comp->tracks)
        if (t.isDrum && ! t.notes.empty())
            return true;

    return false;
}

juce::Rectangle<int> PianoRoll::chordLane() const  { return getLocalBounds().removeFromTop (24); }

juce::Rectangle<int> PianoRoll::drumLane() const
{
    return hasDrums() ? getLocalBounds().removeFromBottom (40) : juce::Rectangle<int>();
}

juce::Rectangle<int> PianoRoll::noteArea() const
{
    auto r = getLocalBounds();
    r.removeFromTop (24);

    if (hasDrums())
        r.removeFromBottom (40);

    return r.reduced (0, 4);
}

void PianoRoll::rebuildCache()
{
    cacheDirty = false;

    const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    const float scale = juce::jlimit (1.0f, 4.0f, cacheScale);
    cache = juce::Image (juce::Image::ARGB, (int) (w * scale), (int) (h * scale), true);

    juce::Graphics g (cache);
    g.addTransform (juce::AffineTransform::scale (scale));

    g.fillAll (Theme::panel);

    if (comp == nullptr || comp->lengthTicks <= 0)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawText ("Describe some music above and press Generate", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const double total = comp->lengthTicks;
    auto xFor = [&] (double tick) { return (float) (tick / total * w); };

    // Pitch range of the pitched tracks.
    int lo = 127, hi = 0;

    for (auto& t : comp->tracks)
        if (! t.isDrum)
            for (auto& n : t.notes)
            {
                lo = juce::jmin (lo, n.pitch);
                hi = juce::jmax (hi, n.pitch);
            }

    if (lo > hi)
    {
        lo = 48;
        hi = 72;
    }

    lo -= 2;
    hi += 2;

    const auto area = noteArea();
    const float rowH = (float) area.getHeight() / (float) (hi - lo + 1);
    auto yFor = [&] (int pitch) { return (float) area.getY() + (float) (hi - pitch) * rowH; };

    // Octave shading and C labels
    for (int p = lo; p <= hi; ++p)
    {
        if (p % 12 == 0)
        {
            g.setColour (Theme::grid.withAlpha (0.6f));
            g.drawHorizontalLine ((int) (yFor (p) + rowH), 0.0f, (float) w);
        }
    }

    // Bar / beat grid
    const int ticksPerBar = juce::jmax (1, comp->ticksPerBar);
    const int beatTicks = comp->tsDen == 8 ? pm::kPpq * 3 / 2 : pm::kPpq;
    const bool showBeats = (double) w / (total / beatTicks) > 6.0;

    for (int t = 0; t <= comp->lengthTicks; t += beatTicks)
    {
        const bool bar = t % ticksPerBar == 0;

        if (! bar && ! showBeats)
            continue;

        g.setColour (bar ? Theme::grid.brighter (0.35f) : Theme::grid);
        g.drawVerticalLine ((int) xFor (t), (float) area.getY() - 4.0f, (float) h);
    }

    // Chord lane
    {
        const auto lane = chordLane();
        g.setColour (Theme::panelDark);
        g.fillRect (lane);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));

        for (size_t i = 0; i < comp->chords.size(); ++i)
        {
            const auto& c = comp->chords[i];
            const float x0 = xFor (c.start), x1 = xFor (c.end);
            auto box = juce::Rectangle<float> (x0 + 1.0f, (float) lane.getY() + 3.0f, juce::jmax (1.0f, x1 - x0 - 2.0f), (float) lane.getHeight() - 6.0f);
            g.setColour (Theme::accent.withAlpha (i % 2 == 0 ? 0.20f : 0.13f));
            g.fillRoundedRectangle (box, 3.0f);

            if (box.getWidth() > 18.0f)
            {
                g.setColour (Theme::text.withAlpha (0.9f));
                g.drawText (juce::String::fromUTF8 (c.name.c_str()), box.toNearestInt().reduced (4, 0), juce::Justification::centredLeft, true);
            }
        }
    }

    // Notes, selected track last so it sits on top
    std::vector<int> order;

    for (int i = 0; i < (int) comp->tracks.size(); ++i)
        if (i != selectedTrack)
            order.push_back (i);

    if (selectedTrack >= 0 && selectedTrack < (int) comp->tracks.size())
        order.push_back (selectedTrack);

    const auto drums = drumLane();

    for (int ti : order)
    {
        const auto& t = comp->tracks[(size_t) ti];
        const bool muted = (mutedMask >> ti) & 1u;
        float alpha = muted ? 0.12f : (selectedTrack < 0 || selectedTrack == ti ? 0.95f : 0.30f);
        const auto colour = trackColour (ti);

        if (t.isDrum)
        {
            if (drums.isEmpty())
                continue;

            const float laneH = (float) drums.getHeight() / 3.0f;

            for (auto& n : t.notes)
            {
                int row = 0; // cymbals, hats, toms

                if (n.pitch == 35 || n.pitch == 36)                     row = 2;
                else if (n.pitch >= 37 && n.pitch <= 40)                row = 1;

                const float x = xFor (n.start);
                const float y = (float) drums.getY() + row * laneH + 2.0f;
                g.setColour (colour.withAlpha (alpha * (0.45f + 0.55f * (float) n.velocity / 127.0f)));
                g.fillRect (x, y, 2.5f, laneH - 4.0f);
            }

            continue;
        }

        for (auto& n : t.notes)
        {
            const float x = xFor (n.start);
            const float wNote = juce::jmax (2.0f, xFor (n.start + n.length) - x - 0.5f);
            const float y = yFor (n.pitch);
            const auto r = juce::Rectangle<float> (x, y + 0.5f, wNote, juce::jmax (2.0f, rowH - 1.0f));
            g.setColour (colour.withAlpha (alpha * (0.55f + 0.45f * (float) n.velocity / 127.0f)));
            g.fillRoundedRectangle (r, juce::jmin (2.0f, rowH * 0.4f));
        }
    }

    if (! drums.isEmpty())
    {
        g.setColour (Theme::grid);
        g.drawHorizontalLine (drums.getY(), 0.0f, (float) w);
        g.setColour (Theme::textDim.withAlpha (0.7f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("DRUMS", drums.withTrimmedLeft (4), juce::Justification::topLeft);
    }
}

void PianoRoll::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();

    if (std::abs (scale - cacheScale) > 0.01f)
    {
        cacheScale = scale;
        cacheDirty = true;
    }

    if (cacheDirty || cache.isNull())
        rebuildCache();

    g.drawImage (cache, getLocalBounds().toFloat());

    if (comp != nullptr && comp->lengthTicks > 0 && playhead >= 0.0)
    {
        const float x = (float) (playhead / comp->lengthTicks * getWidth());
        g.setColour (Theme::playhead);
        g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());
    }

    g.setColour (Theme::outline);
    g.drawRect (getLocalBounds());
}
