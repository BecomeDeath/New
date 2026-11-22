#include "InstrumentPicker.h"

#include <map>

namespace
{
    juce::String fromStd (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    struct RoleItem { const char* id; const char* label; };

    const RoleItem roleItems[] = {
        { "auto", "Auto" }, { "melody", "Melody" }, { "countermelody", "Counter-melody" }, { "chords", "Chords" },
        { "pad", "Pad" }, { "arpeggio", "Arpeggio" }, { "bass", "Bass" }, { "ostinato", "Ostinato" },
    };

    struct Preset { const char* name; std::vector<const char*> instruments; };

    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> list = {
            { "Solo piano",          { "Piano" } },
            { "Solo guitar",         { "Nylon Guitar" } },
            { "Solo cello",          { "Solo Cello" } },
            { "Flute and harp",      { "Flutes", "Harp" } },
            { "String quartet",      { "Solo Violin", "Violins II", "Violas", "Celli" } },
            { "String orchestra",    { "Violins I", "Violins II", "Violas", "Celli", "Basses" } },
            { "Woodwind quintet",    { "Flutes", "Oboes", "Clarinets", "Horns", "Bassoons" } },
            { "Brass ensemble",      { "Trumpets", "Horns", "Trombones", "Tuba", "Timpani" } },
            { "Choir and organ",     { "Choir", "Organ" } },
            { "Jazz trio",           { "Piano", "Upright Bass", "Drums" } },
            { "Rock band",           { "Electric Guitar", "Distortion Guitar", "Bass", "Drums" } },
            { "Synth trio",          { "Synth Lead", "Warm Pad", "Synth Bass", "Drums" } },
        };

        return list;
    }

    pm::InstrumentChoice choiceFor (const pm::CatalogueEntry& e)
    {
        pm::InstrumentChoice c;
        c.name = e.name;
        c.program = e.program;
        c.role = "auto";
        return c;
    }
}

//==============================================================================
class InstrumentPicker::Row : public juce::Component
{
public:
    Row (InstrumentPicker& p, int i) : picker (p), index (i)
    {
        int id = 1;

        for (auto& r : roleItems)
            roleBox.addItem (r.label, id++);

        const auto& c = picker.choices[(size_t) index];
        int selectedId = 1;

        for (int k = 0; k < (int) std::size (roleItems); ++k)
            if (c.role == roleItems[k].id)
                selectedId = k + 1;

        roleBox.setSelectedId (selectedId, juce::dontSendNotification);
        roleBox.setVisible (c.program >= 0); // percussion plays percussion
        roleBox.setTooltip ("What this instrument plays (Auto: PromptMIDI decides, like an arranger)");
        roleBox.onChange = [this] { picker.choices[(size_t) index].role = roleItems[roleBox.getSelectedId() - 1].id; };
        addAndMakeVisible (roleBox);

        remove.setFlat (true);
        remove.setTooltip ("Remove");
        remove.onClick = [this]
        {
            const int at = index;
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<InstrumentPicker> (&picker), at]
            {
                if (safe != nullptr && at < (int) safe->choices.size())
                {
                    safe->choices.erase (safe->choices.begin() + at);
                    safe->rebuild();
                }
            });
        };
        addAndMakeVisible (remove);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (0, 2);
        g.setColour (Theme::raised);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (Theme::trackColour (index));
        g.fillEllipse (r.removeFromLeft (22).withSizeKeepingCentre (8, 8).toFloat());
        g.setColour (Theme::text);
        g.setFont (Theme::font (13.5f, Theme::Weight::Medium));
        const auto& c = picker.choices[(size_t) index];
        g.drawText (fromStd (c.name), r.withRight (roleBox.getX() - 6), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 6);
        remove.setBounds (r.removeFromRight (26));
        r.removeFromRight (6);
        roleBox.setBounds (r.removeFromRight (150));
    }

private:
    InstrumentPicker& picker;
    int index;
    juce::ComboBox roleBox;
    IconButton remove { "remove", Icons::Kind::Cross };
};

//==============================================================================
InstrumentPicker::InstrumentPicker (const Choices& current, std::function<void (const Choices&)> apply)
    : choices (current), custom (! current.empty()), onApply (std::move (apply))
{
    for (auto* b : { &autoButton, &customButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (2001);
        addAndMakeVisible (b);
    }

    autoButton.setTooltip ("PromptMIDI picks the instruments from your prompt and its style");
    customButton.setTooltip ("Compose for exactly the instruments you list - one for a solo piece, or any ensemble");
    autoButton.onClick = [this] { if (autoButton.getToggleState()) setCustom (false); };
    customButton.onClick = [this] { if (customButton.getToggleState()) setCustom (true); };

    addButton.onClick = [this] { showAddMenu(); };
    presetsButton.onClick = [this] { showPresets(); };
    clearButton.onClick = [this] { choices.clear(); rebuild(); };
    applyButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    applyButton.onClick = [this]
    {
        if (onApply)
            onApply (custom ? choices : Choices {});

        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };

    for (auto* c : std::initializer_list<juce::Component*> { &addButton, &presetsButton, &clearButton, &applyButton, &viewport })
        addAndMakeVisible (c);

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);

    setCustom (custom);
    setSize (440, 470);
}

InstrumentPicker::~InstrumentPicker() = default;

juce::String InstrumentPicker::summary (const Choices& c)
{
    if (c.empty())
        return "Auto";

    if (c.size() == 1)
        return fromStd (c.front().name);

    return fromStd (c.front().name) + " + " + juce::String ((int) c.size() - 1);
}

void InstrumentPicker::setCustom (bool shouldBeCustom)
{
    custom = shouldBeCustom;
    autoButton.setToggleState (! custom, juce::dontSendNotification);
    customButton.setToggleState (custom, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &addButton, &presetsButton, &clearButton, &viewport })
        c->setEnabled (custom);

    rebuild();
}

void InstrumentPicker::rebuild()
{
    rows.clear();

    if (custom)
        for (int i = 0; i < (int) choices.size(); ++i)
            list.addAndMakeVisible (rows.add (new Row (*this, i)));

    resized();
    repaint();
}

void InstrumentPicker::addInstrument (const pm::CatalogueEntry& e)
{
    if (choices.size() >= 16)
        return;

    choices.push_back (choiceFor (e));

    if (! custom)
        setCustom (true);
    else
        rebuild();

    viewport.setViewPosition (0, list.getHeight());
}

void InstrumentPicker::showAddMenu()
{
    juce::PopupMenu menu;
    std::map<std::string, juce::PopupMenu> categories;
    std::vector<std::string> order;
    const auto& cat = pm::instrumentCatalogue();

    for (int i = 0; i < (int) cat.size(); ++i)
    {
        if (categories.count (cat[(size_t) i].category) == 0)
            order.push_back (cat[(size_t) i].category);

        categories[cat[(size_t) i].category].addItem (i + 1, fromStd (cat[(size_t) i].name));
    }

    for (auto& name : order)
        menu.addSubMenu (fromStd (name), categories[name]);

    juce::Component::SafePointer<InstrumentPicker> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton), [safe] (int result)
    {
        if (safe != nullptr && result > 0)
            safe->addInstrument (pm::instrumentCatalogue()[(size_t) result - 1]);
    });
}

void InstrumentPicker::showPresets()
{
    juce::PopupMenu menu;

    for (int i = 0; i < (int) presets().size(); ++i)
        menu.addItem (i + 1, presets()[(size_t) i].name);

    juce::Component::SafePointer<InstrumentPicker> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton), [safe] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;

        safe->choices.clear();

        for (auto* name : presets()[(size_t) result - 1].instruments)
            for (auto& e : pm::instrumentCatalogue())
                if (e.name == name)
                {
                    safe->choices.push_back (choiceFor (e));
                    break;
                }

        safe->setCustom (true);
    });
}

void InstrumentPicker::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    auto r = getLocalBounds().reduced (16);

    g.setColour (Theme::text);
    g.setFont (Theme::font (16.0f, Theme::Weight::SemiBold));
    g.drawText ("Instruments", r.removeFromTop (24), juce::Justification::centredLeft);

    if (! custom || choices.empty())
    {
        auto body = viewport.getBounds();
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (13.0f));
        g.drawFittedText (custom ? juce::String ("Add instruments, or start from a preset. One instrument gives a solo piece "
                                                 "(a piano, harp or guitar plays melody and accompaniment); three or more in an "
                                                 "orchestral style give a full cue.")
                                 : juce::String ("PromptMIDI chooses the instruments from your prompt and its style - a full orchestra "
                                                 "for orchestral music, a band for rock, and so on. Pick \"My instruments\" to choose them yourself."),
                          body.reduced (6), juce::Justification::topLeft, 6);
    }
}

void InstrumentPicker::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (30);

    auto seg = r.removeFromTop (32);
    autoButton.setBounds (seg.removeFromLeft (seg.getWidth() / 2).withTrimmedRight (3));
    customButton.setBounds (seg.withTrimmedLeft (3));
    r.removeFromTop (12);

    auto buttons = r.removeFromBottom (34);
    applyButton.setBounds (buttons.removeFromRight (96));
    r.removeFromBottom (10);

    auto tools = r.removeFromTop (30);
    addButton.setBounds (tools.removeFromLeft (150));
    tools.removeFromLeft (8);
    presetsButton.setBounds (tools.removeFromLeft (90));
    clearButton.setBounds (tools.removeFromRight (70));
    r.removeFromTop (10);

    viewport.setBounds (r);
    const int rowH = 46;
    const int w = viewport.getMaximumVisibleWidth();
    list.setSize (w, rowH * rows.size());

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowH, w, rowH);
}
