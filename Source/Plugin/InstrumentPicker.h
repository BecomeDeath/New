#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Icons.h"
#include "PromptInterpreter.h"

#include <functional>
#include <vector>

/** Choose the instruments to compose for: from the prompt (automatic), or your own list - one
    instrument for a solo piece, or any ensemble - each with an optional role.
*/
class InstrumentPicker : public juce::Component
{
public:
    using Choices = std::vector<pm::InstrumentChoice>;

    /** `current`: the user's list (empty = automatic). `ensemble`: what the piece on screen uses,
        offered as a starting point ("Customise these"). */
    InstrumentPicker (const Choices& current, const Choices& ensemble, std::function<void (const Choices&)> onApply);
    ~InstrumentPicker() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** "Auto", or the instruments' names: "Piano", "Flutes, Harp" ... */
    static juce::String summary (const Choices&);
    static constexpr int maxInstruments = 24;

private:
    class Row;

    void rebuild();
    void setCustom (bool);
    void addInstrument (const pm::CatalogueEntry&);
    void showAddMenu();
    void showPresets();

    Choices choices, ensemble;
    bool custom = false;
    std::function<void (const Choices&)> onApply;

    juce::TextButton autoButton { "From the prompt" }, customButton { "My instruments" };
    IconButton addButton { "add", Icons::Kind::Plus, "Add instrument" };
    juce::TextButton presetsButton { "Presets" }, clearButton { "Clear" }, applyButton { "Apply" };
    juce::TextButton customiseButton { "Customise these" };
    juce::Viewport viewport;
    juce::Component list;
    juce::OwnedArray<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPicker)
};
