#include "Presets.h"
#include "PluginProcessor.h"

namespace
{
    using Values = std::vector<std::pair<const char*, float>>;
    struct Factory { const char* name; Values values; };

    // Los canales son índices: 0 = estéreo, 1 = Mid, 2 = Side. Las pendientes: 0 = 6, 1 = 12, 2 = 24, 3 = 48 dB/oct.
    const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> presets = {
            { "Plano", {} },
            { "Voz: limpieza", {
                { "hp_freq", 90.0f }, { "hp_slope", 2.0f },
                { "b1_freq", 300.0f }, { "b1_gain", -3.0f }, { "b1_q", 1.2f },
                { "b2_freq", 3500.0f }, { "b2_gain", 2.5f }, { "b2_q", 0.9f },
                { "hs_freq", 10000.0f }, { "hs_gain", 2.0f } } },
            { "Bombo", {
                { "hp_freq", 30.0f },
                { "ls_freq", 60.0f }, { "ls_gain", 3.0f },
                { "b1_freq", 350.0f }, { "b1_gain", -4.0f }, { "b1_q", 1.5f },
                { "b2_freq", 4000.0f }, { "b2_gain", 3.0f } } },
            { "Brillo", {
                { "hs_freq", 9000.0f }, { "hs_gain", 4.0f },
                { "b2_freq", 5000.0f }, { "b2_gain", 1.5f } } },
            { "Corte de graves", { { "hp_freq", 120.0f }, { "hp_slope", 3.0f } } },
            { "Tel\u00e9fono", {
                { "hp_freq", 400.0f }, { "hp_slope", 2.0f },
                { "lp_freq", 3400.0f }, { "lp_slope", 2.0f } } },
            { "Master: m\u00e1s aire (Side)", {
                { "hs_freq", 8000.0f }, { "hs_gain", 3.0f }, { "hs_ch", 2.0f },
                { "ls_freq", 150.0f }, { "ls_gain", -2.0f }, { "ls_ch", 2.0f } } },
            { "Voz: de-esser din\u00e1mico", {
                { "hp_freq", 90.0f }, { "hp_slope", 2.0f },
                { "b2_freq", 6500.0f }, { "b2_gain", -8.0f }, { "b2_q", 3.0f },
                { "b2_dyn", 1.0f }, { "b2_thr", -32.0f }, { "b2_ratio", 4.0f },
                { "b2_attack", 2.0f }, { "b2_release", 60.0f } } },
            { "Bajo: graves controlados", {
                { "hp_freq", 35.0f }, { "hp_slope", 2.0f },
                { "ls_freq", 90.0f }, { "ls_gain", -6.0f }, { "ls_type", 1.0f }, { "ls_q", 1.2f },
                { "ls_dyn", 1.0f }, { "ls_thr", -22.0f }, { "ls_ratio", 3.0f },
                { "ls_attack", 25.0f }, { "ls_release", 250.0f } } },
            { "Master: presencia (Mid)", {
                { "b2_freq", 2500.0f }, { "b2_gain", 2.0f }, { "b2_q", 0.8f }, { "b2_ch", 1.0f } } },
        };
        return presets;
    }
}

juce::File PresetManager::folder()
{
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    const auto folder = base.getChildFile ("eCU-10").getChildFile ("Presets");

    // Migración desde el nombre antiguo del plugin (Medidores EQ): se conservan los presets de usuario.
    const auto old = base.getChildFile ("Medidores EQ");
    if (! folder.getParentDirectory().exists() && old.isDirectory())
        old.moveFileTo (folder.getParentDirectory());
    return folder;
}

juce::File PresetManager::fileFor (const juce::String& name)
{
    return folder().getChildFile (juce::File::createLegalFileName (name) + ".xml");
}

juce::StringArray PresetManager::factoryNames() const
{
    juce::StringArray names;
    for (auto& p : factory()) names.add (juce::String::fromUTF8 (p.name));
    return names;
}

juce::StringArray PresetManager::userNames() const
{
    juce::StringArray names;
    for (auto& f : folder().findChildFiles (juce::File::findFiles, false, "*.xml"))
        names.add (f.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

void PresetManager::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void PresetManager::resetToDefaults()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! EQ::isViewParam (rp->paramID))   // la vista (analizador, rango) no cambia con los presets
                p->setValueNotifyingHost (p->getDefaultValue());
}

void PresetManager::loadFactory (const juce::String& name)
{
    for (auto& p : factory())
        if (name == juce::String::fromUTF8 (p.name))
        {
            resetToDefaults();
            for (auto& v : p.values) setParam (v.first, v.second);
            return;
        }
}

bool PresetManager::loadUser (const juce::String& name)
{
    if (auto xml = juce::parseXML (fileFor (name)))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            return true;
        }
    return false;
}

bool PresetManager::saveUser (const juce::String& name)
{
    if (name.trim().isEmpty()) return false;
    const auto file = fileFor (name.trim());
    if (! file.getParentDirectory().createDirectory()) return false;
    if (auto xml = apvts.copyState().createXml())
        return xml->writeTo (file);
    return false;
}

bool PresetManager::removeUser (const juce::String& name)
{
    return fileFor (name).deleteFile();
}
