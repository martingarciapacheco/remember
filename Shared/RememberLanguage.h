#pragma once
#include <JuceHeader.h>
#include <map>
#include <functional>

namespace RememberUI
{
const std::map<juce::String, juce::String>& spanishSpain();
const std::map<juce::String, juce::String>& spanishDiagnostics();
enum class Language { English, SpanishSpain };
inline std::atomic<Language> language { Language::English };
inline Language languageFromConfig (const juce::var& config)
{ return config.getProperty ("language", "en").toString() == "es-ES" ? Language::SpanishSpain : Language::English; }

// Only explicit interface strings pass through this boundary. User content, paths,
// project names and persisted identifiers are never looked up automatically.
inline juce::String localize (const juce::String& source)
{
    if (language == Language::SpanishSpain)
    {
        if (const auto found = spanishSpain().find (source); found != spanishSpain().end()) return found->second;
        if (const auto found = spanishDiagnostics().find (source); found != spanishDiagnostics().end()) return found->second;
    }
    return source;
}
inline juce::String diagnostic (const juce::String& source, int depth = 0)
{
    if (language != Language::SpanishSpain || depth > 4) return source;
    const auto exact = localize (source);
    if (exact != source) return exact;
    const auto separator = source.indexOf (": ");
    if (separator > 0)
    {
        const auto heading = source.substring (0, separator);
        const auto translated = localize (heading);
        if (translated != heading)
            return translated + ": " + diagnostic (source.substring (separator + 2), depth + 1);
    }
    // Translate known message prefixes and explanatory suffixes, preserving the
    // intervening file path/identifier byte-for-byte. Never scan arbitrary names.
    const std::pair<const juce::String, juce::String>* prefix = nullptr;
    const std::pair<const juce::String, juce::String>* suffix = nullptr;
    for (const auto& item : spanishDiagnostics())
    {
        if (item.first.length() < 8) continue;
        if (source.startsWith (item.first) && (prefix == nullptr || item.first.length() > prefix->first.length())) prefix = &item;
        if ((item.first.startsWithChar ('.') || item.first.startsWithChar (')')) && source.endsWith (item.first)
            && (suffix == nullptr || item.first.length() > suffix->first.length())) suffix = &item;
    }
    const int from = prefix ? prefix->first.length() : 0, to = source.length() - (suffix ? suffix->first.length() : 0);
    if (to < from) return source;
    return (prefix ? prefix->second : juce::String{}) + source.substring (from, to) + (suffix ? suffix->second : juce::String{});
}
struct LocalizedText
{
    explicit LocalizedText (const juce::String& value) : source (value), display (localize (value)) {}
    LocalizedText (const char* value) : LocalizedText (juce::String (value)) {}
    operator juce::String() const { return display; }
    juce::String source, display;
};
inline juce::String operator+ (const LocalizedText& a, const juce::String& b) { return a.display + b; }
inline juce::String operator+ (const juce::String& a, const LocalizedText& b) { return a + b.display; }
inline juce::String operator+ (const LocalizedText& a, const LocalizedText& b) { return a.display + b.display; }
inline juce::String operator+ (const LocalizedText& a, const char* b) { return a.display + b; }
inline juce::String operator+ (const char* a, const LocalizedText& b) { return a + b.display; }
inline LocalizedText tr (const juce::String& source) { return LocalizedText (source); }
inline LocalizedText tr (const char* source) { return LocalizedText (juce::String::fromUTF8 (source)); }
inline juce::String format (const juce::String& source, std::initializer_list<std::pair<juce::String, juce::String>> values)
{
    auto result = localize (source);
    // Replace in one pass so user-supplied text containing another token remains literal.
    juce::String output;
    for (int i = 0; i < result.length();)
    {
        bool replaced = false;
        for (const auto& item : values)
            if (result.substring (i).startsWith (item.first))
            { output += item.second; i += item.first.length(); replaced = true; break; }
        if (! replaced) output += result[i++];
    }
    return output;
}

struct LanguageListener
{
    LanguageListener() { listeners().add (this); }
    virtual ~LanguageListener() { listeners().removeFirstMatchingValue (this); }
    virtual void languageChanged() = 0;
    static juce::Array<LanguageListener*>& listeners() { static juce::Array<LanguageListener*> list; return list; }
};
inline void setLanguage (Language value)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (language == value) return;
    language = value;
    const auto listeners = LanguageListener::listeners();
    for (auto* listener : listeners)
        if (LanguageListener::listeners().contains (listener)) listener->languageChanged();
}
struct LanguageCallback final : LanguageListener
{
    explicit LanguageCallback (std::function<void()> action) : changed (std::move (action)) {}
    void languageChanged() override { if (changed) changed(); }
    std::function<void()> changed;
};

// Bind explicit source strings for live updates. Ordinary String setters retain
// JUCE behaviour and clear a previous binding when a recycled row gets user data.
template<class Base> class LocalizedControl : public Base, private LanguageListener
{
public:
    using Base::Base;
    void setTitle (const juce::String& value) { title = {}; Base::setTitle (value); }
    void setDescription (const juce::String& value) { description = {}; Base::setDescription (value); }
    void setTitle (const char* value) { setTitle (juce::String (value)); }
    void setDescription (const char* value) { setDescription (juce::String (value)); }
    void setTitle (const LocalizedText& content) { title = content.source; Base::setTitle (content); }
    void setDescription (const LocalizedText& content) { description = content.source; Base::setDescription (content); }
    void setTooltip (const juce::String& content) { tooltip = {}; Base::setTooltip (content); }
    void setTooltip (const char* content) { setTooltip (juce::String (content)); }
    void setTooltip (const LocalizedText& content) { tooltip = content.source; Base::setTooltip (content); }
protected:
    void languageChanged() override
    {
        if (title.isNotEmpty()) Base::setTitle (localize (title));
        if (description.isNotEmpty()) Base::setDescription (localize (description));
        if (tooltip.isNotEmpty()) Base::setTooltip (localize (tooltip));
        refreshText(); this->repaint();
    }
    virtual void refreshText() {}
private:
    juce::String title, description, tooltip;
};
class Label : public LocalizedControl<juce::Label>
{
public:
    Label() = default;
    Label (const juce::String& name, const char* value) : LocalizedControl (name, value) {}
    Label (const juce::String& name, const juce::String& value) : LocalizedControl (name, value) {}
    Label (const juce::String& name, const LocalizedText& value) : LocalizedControl (name, value), source (value.source) {}
    void setText (const juce::String& value, juce::NotificationType notify) { source = {}; juce::Label::setText (value, notify); }
    void setText (const char* value, juce::NotificationType notify) { setText (juce::String (value), notify); }
    void setText (const LocalizedText& value, juce::NotificationType notify) { source = value.source; juce::Label::setText (value, notify); }
private:
    void refreshText() override { if (source.isNotEmpty()) juce::Label::setText (localize (source), juce::dontSendNotification); }
    juce::String source;
};
template<class Base> class LocalizedButton : public LocalizedControl<Base>
{
public:
    LocalizedButton() = default;
    explicit LocalizedButton (const char* value) : LocalizedControl<Base> (value) {}
    explicit LocalizedButton (const juce::String& value) : LocalizedControl<Base> (value) {}
    explicit LocalizedButton (const LocalizedText& value) : LocalizedControl<Base> (value), source (value.source) {}
    void setButtonText (const juce::String& value) { source = {}; Base::setButtonText (value); }
    void setButtonText (const char* value) { setButtonText (juce::String (value)); }
    void setButtonText (const LocalizedText& value) { source = value.source; Base::setButtonText (value); }
private:
    void refreshText() override { if (source.isNotEmpty()) Base::setButtonText (localize (source)); }
    juce::String source;
};
using TextButton = LocalizedButton<juce::TextButton>;
using ToggleButton = LocalizedButton<juce::ToggleButton>;
class ComboBox : public LocalizedControl<juce::ComboBox>
{
public:
    using LocalizedControl::LocalizedControl;
    using juce::ComboBox::addItem;
    using juce::ComboBox::addItemList;
    void addItemList (std::initializer_list<LocalizedText> values, int firstId)
    { for (const auto& value : values) addItem (value, firstId++); }
    void addItem (const char* content, int id) { juce::ComboBox::addItem (content, id); }
    void addItem (const LocalizedText& content, int id) { sources[id] = content.source; juce::ComboBox::addItem (content, id); }
    void clear (juce::NotificationType notify = juce::sendNotificationAsync) { sources.clear(); juce::ComboBox::clear (notify); }
private:
    void refreshText() override
    {
        const auto selected = getSelectedId();
        for (const auto& item : sources) changeItemText (item.first, localize (item.second));
        if (sources.count (selected) != 0) setSelectedId (selected, juce::dontSendNotification);
    }
    std::map<int, juce::String> sources;
};
class TextEditor : public LocalizedControl<juce::TextEditor>
{
public:
    using LocalizedControl::LocalizedControl;
    using juce::TextEditor::setTextToShowWhenEmpty;
    void setTextToShowWhenEmpty (const LocalizedText& content, juce::Colour colour)
    { source = content.source; placeholderColour = colour; juce::TextEditor::setTextToShowWhenEmpty (content, colour); }
private:
    void refreshText() override { if (source.isNotEmpty()) juce::TextEditor::setTextToShowWhenEmpty (localize (source), placeholderColour); }
    juce::String source;
    juce::Colour placeholderColour;
};
}

