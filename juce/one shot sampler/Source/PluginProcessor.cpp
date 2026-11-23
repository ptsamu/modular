#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    enum class FxValue { delayTime, delayFeedback, delayMix, reverbMix, reverbRoomSize, reverbDamping, reverbWidth, reverbFreeze, chorusRate, chorusDepth, chorusDelay, chorusFeedback, chorusMix, flangerRate, flangerDepth, flangerDelay, flangerFeedback, flangerMix, flutterRate, flutterDepth, flutterDelay, flutterMix, granularMix, harmonyMix, randomMix, granularSize, granularDensity, granularPitch, randomAmount, randomRate, harmonyShift, harmonyDetune, phaserRate, phaserDepth, phaserFeedback, phaserMix, chorusDrive };
    constexpr std::array<const char*, 37> fxValueIds { "delayTime", "delayFeedback", "delayMix", "reverbMix", "reverbRoomSize", "reverbDamping", "reverbWidth", "reverbFreeze", "chorusRate", "chorusDepth", "chorusDelay", "chorusFeedback", "chorusMix", "flangerRate", "flangerDepth", "flangerDelay", "flangerFeedback", "flangerMix", "flutterRate", "flutterDepth", "flutterDelay", "flutterMix", "granularMix", "harmonyMix", "randomMix", "granularSize", "granularDensity", "granularPitch", "randomAmount", "randomRate", "harmonyShift", "harmonyDetune", "phaserRate", "phaserDepth", "phaserFeedback", "phaserMix", "chorusDrive" };
}

OneShotSamplerAudioProcessor::OneShotSamplerAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    formatManager.registerBasicFormats();
    // Rack slots are real parameters as well as UI state. This guarantees a
    // DAW restores the rack exactly when an editor window is recreated.
    parameters.state.setProperty ("fxLayoutVersion", 7, nullptr);

    // The compact EQ macros control the first, fourth and seventh bands of
    // the same seven-band engine used by the detailed EQ editor.  Start those
    // anchors as shelves so LOW and AIR behave like useful mastering macros.
    parameters.state.setProperty ("eqBand1Type", 4, nullptr); // low shelf
    parameters.state.setProperty ("eqBand4Type", 0, nullptr); // bell
    parameters.state.setProperty ("eqBand7Type", 5, nullptr); // high shelf

    for (int layer = 0; layer < 2; ++layer)
    {
        for (size_t i = 0; i < fxValueIds.size(); ++i)
            fxValues[(size_t) layer][i] = parameters.getRawParameterValue (fxParamId (layer, fxValueIds[i]));
        for (int band = 0; band < 7; ++band)
        {
            const auto prefix = fxParamId (layer, "eqBand" + juce::String (band + 1));
            eqParamPointers[(size_t) layer][(size_t) band] = { parameters.getRawParameterValue (prefix + "Freq"),
                                                              parameters.getRawParameterValue (prefix + "Gain"),
                                                              parameters.getRawParameterValue (prefix + "Q") };
        }
        const std::array<const char*, 10> ids { "Pitch", "Stretch", "Bend", "Tremolo", "Level", "Start", "End", "Reverse", "TremRate", "Enabled" };
        const std::array<const char*, 4> stages { "attack", "decay", "sustain", "release" };
        for (size_t index = 0; index < stages.size(); ++index)
            envelopeParams[(size_t) layer][index] = parameters.getRawParameterValue (envelopeParamId (layer, stages[index]));
        for (size_t index = 0; index < ids.size(); ++index)
            sourceParams[(size_t) layer][index] = parameters.getRawParameterValue (sourceParamId (layer, ids[index]));
    }
    for (size_t i = 0; i < routeParams.size(); ++i)
    {
        routeParams[i] = parameters.getRawParameterValue (routeParamId ((int) i));
        for (int layer = 0; layer < 2; ++layer)
            layerFxOn[(size_t) layer][i] = parameters.getRawParameterValue (fxParamId (layer, "layerFxOn" + juce::String ((int) i)));
    }
    for (size_t slot = 0; slot < rackEffectParams.size(); ++slot)
    {
        const auto prefix = "rackSlot" + juce::String (static_cast<int> (slot + 1));
        rackEffectParams[slot] = parameters.getRawParameterValue (prefix + "Effect");
        rackEnabledParams[slot] = parameters.getRawParameterValue (prefix + "Enabled");
    }
    refreshEqTypes();
    parameters.state.addListener (this);
}

OneShotSamplerAudioProcessor::~OneShotSamplerAudioProcessor()
{
    parameters.state.removeListener (this);
}

void OneShotSamplerAudioProcessor::refreshEqTypes()
{
    for (int layer = 0; layer < 2; ++layer)
        for (int band = 0; band < 7; ++band)
            eqBandTypes[(size_t) layer][(size_t) band].store ((int) parameters.state.getProperty (
                fxParamId (layer, "eqBand" + juce::String (band + 1) + "Type"), band == 0 ? 4 : band == 6 ? 5 : 0));
}

void OneShotSamplerAudioProcessor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    const auto name = property.toString();
    if ((name.startsWith ("eqBand") || name.startsWith ("fxB_eqBand")) && name.endsWith ("Type"))
        refreshEqTypes();
}

juce::AudioProcessorValueTreeState::ParameterLayout OneShotSamplerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < 3; ++i)
        layout.add (std::make_unique<juce::AudioParameterFloat> ("duckLevel" + juce::String (i),
            "Sidechain Point " + juce::String (i + 1) + " Level", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> ("duckOn", "Sidechain Enabled", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> ("duckTrigger", "Sidechain Trigger", juce::StringArray { "Note", "Sync" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("duckDepth", "Sidechain Mix", juce::NormalisableRange<float> (0, 100, 1), 65));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("duckMidpoint", "Sidechain Curve Timing",
        juce::NormalisableRange<float> (0.15f, 0.85f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("duckEarly", "Sidechain Early Timing",
        juce::NormalisableRange<float> (0.10f, 0.90f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("duckLate", "Sidechain Late Timing",
        juce::NormalisableRange<float> (0.10f, 0.90f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterChoice> ("duckShape", "Sidechain Shape", duckShapeNames(), 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> ("duckRate", "Sidechain Rate", juce::StringArray { "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> ("duckRoute", "Sidechain Target", juce::StringArray { "A", "B", "Both" }, 2));
    for (int layer = 0; layer < 2; ++layer)
        for (int effect = 0; effect < 10; ++effect)
            layout.add (std::make_unique<juce::AudioParameterChoice> (fxParamId (layer, "layerFxOn" + juce::String (effect)),
                juce::String (layer == 0 ? "A FX " : "B FX ") + juce::String (effect + 1),
                juce::StringArray { "Legacy", "Off", "On" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("layerOffsetMs", "Layer Offset",
        juce::NormalisableRange<float> (-40.0f, 40.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "outputGain", "Output Gain",
        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 3.02f), 0.0f, // skew puts 0 dB at 75 on a 0-100 dial
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> ("attack", "Attack", juce::NormalisableRange<float> (0.001f, 3.0f, 0.0001f, 0.35f), 0.01f, juce::AudioParameterFloatAttributes().withLabel ("s")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("decay", "Decay", juce::NormalisableRange<float> (0.001f, 3.0f, 0.0001f, 0.35f), 0.15f, juce::AudioParameterFloatAttributes().withLabel ("s")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("sustain", "Sustain", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("release", "Release", juce::NormalisableRange<float> (0.001f, 5.0f, 0.0001f, 0.35f), 0.25f, juce::AudioParameterFloatAttributes().withLabel ("s")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("delayTime", "Delay Time", juce::NormalisableRange<float> (0.125f, 2.0f, 0.125f), 0.75f, juce::AudioParameterFloatAttributes().withLabel ("beats")));
    layout.add (std::make_unique<juce::AudioParameterBool> ("delayEnabled", "Delay Enabled", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("delayFeedback", "Delay Feedback", juce::NormalisableRange<float> (0.0f, 0.92f, 0.01f), 0.42f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("delayMix", "Delay Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("reverbMix", "Reverb Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.25f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("reverbRoomSize", "Reverb Room Size", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.62f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("reverbDamping", "Reverb Damping", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.4f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("reverbWidth", "Reverb Width", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("reverbFreeze", "Reverb Freeze", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> ("reverbEnabled", "Reverb Enabled", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("chorusRate", "Chorus Rate", juce::NormalisableRange<float> (0.05f, 8.0f, 0.01f), 0.8f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("chorusDepth", "Chorus Depth", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.45f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("chorusDelay", "Chorus Delay", juce::NormalisableRange<float> (1.0f, 30.0f, 0.1f), 7.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("chorusFeedback", "Chorus Feedback", juce::NormalisableRange<float> (-0.95f, 0.95f, 0.01f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("chorusMix", "Chorus Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f));
    layout.add (std::make_unique<juce::AudioParameterBool> ("chorusEnabled", "Chorus Enabled", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("phaserRate", "Phaser Rate", juce::NormalisableRange<float> (0.05f, 8.0f, 0.01f), 0.45f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("phaserDepth", "Phaser Depth", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("phaserFeedback", "Phaser Feedback", juce::NormalisableRange<float> (-0.95f, 0.95f, 0.01f), 0.25f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("phaserMix", "Phaser Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f));
    layout.add (std::make_unique<juce::AudioParameterBool> ("phaserEnabled", "Phaser Enabled", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("flangerRate", "Flanger Rate", juce::NormalisableRange<float> (0.05f, 8.0f, 0.01f), 0.35f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("flangerDepth", "Flanger Depth", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.65f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("flangerDelay", "Flanger Delay", juce::NormalisableRange<float> (1.0f, 20.0f, 0.1f), 3.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("flangerFeedback", "Flanger Feedback", juce::NormalisableRange<float> (-0.95f, 0.95f, 0.01f), 0.35f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("flangerMix", "Flanger Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f));
    layout.add (std::make_unique<juce::AudioParameterBool> ("flangerEnabled", "Flanger Enabled", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("flutterRate", "Flutter Rate", juce::NormalisableRange<float> (0.2f, 12.0f, 0.01f), 4.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("flutterDepth", "Flutter Depth", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("flutterDelay", "Flutter Delay", juce::NormalisableRange<float> (0.1f, 12.0f, 0.1f), 1.8f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("flutterMix", "Flutter Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f));
    layout.add (std::make_unique<juce::AudioParameterBool> ("flutterEnabled", "Flutter Enabled", false));
    const juce::StringArray rackEffects { "EMPTY", "PING PONG DELAY", "REVERB", "CHORUS", "PHASER", "FLANGER", "EQ", "FLUTTER" };
    for (int slot = 0; slot < 7; ++slot)
    {
        const auto slotName = "rackSlot" + juce::String (slot + 1);
        // Each rack position is a permanent module: delay through flutter.
        // The switch defaults off, not the module itself.
        layout.add (std::make_unique<juce::AudioParameterChoice> (slotName + "Effect", slotName + " Effect", rackEffects, slot + 1));
        layout.add (std::make_unique<juce::AudioParameterBool> (slotName + "Enabled", slotName + " Enabled", false));
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> ("granularSize", "Grain Size", juce::NormalisableRange<float> (15.0f, 500.0f, 1.0f), 120.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("granularDensity", "Grain Density", juce::NormalisableRange<float> (1.0f, 40.0f, 0.1f), 9.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("granularPitch", "Grain Pitch", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("granularMix", "Granular Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.45f));
    layout.add (std::make_unique<juce::AudioParameterBool> ("granularEnabled", "Granular Enabled", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("harmonyShift", "Harmony Shift", juce::NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 7.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("harmonyDetune", "Harmony Detune", juce::NormalisableRange<float> (0.0f, 40.0f, 0.1f), 8.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("harmonyMix", "Harmony Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.4f));
    layout.add (std::make_unique<juce::AudioParameterBool> ("harmonyEnabled", "Harmony Enabled", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("randomAmount", "Random Amount", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.45f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("randomRate", "Random Rate", juce::NormalisableRange<float> (1.0f, 24.0f, 0.1f), 6.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("randomMix", "Random Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.01f));
    layout.add (std::make_unique<juce::AudioParameterBool> ("randomEnabled", "Random Enabled", false));
    layout.add (std::make_unique<juce::AudioParameterBool> ("eqEnabled", "EQ Enabled", false));
    const std::array<float, 7> eqFrequencies { 60.0f, 170.0f, 450.0f, 1100.0f, 2800.0f, 7000.0f, 14000.0f };
    for (int band = 0; band < 7; ++band)
    {
        const auto prefix = "eqBand" + juce::String (band + 1);
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Freq", prefix + " Frequency", juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), eqFrequencies[(size_t) band], juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Gain", prefix + " Gain", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Q", prefix + " Q", juce::NormalisableRange<float> (0.2f, 8.0f, 0.01f), 0.8f));
    }

    for (int layer = 0; layer < 2; ++layer)
    {
        const juce::String name = layer == 0 ? "Source A " : "Source B ";
        const auto add = [&] (const char* id, float low, float high, float step, float initial, const char* unit = "")
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (sourceParamId (layer, id), name + id,
                juce::NormalisableRange<float> (low, high, step), initial, juce::AudioParameterFloatAttributes().withLabel (unit)));
        };
        layout.add (std::make_unique<juce::AudioParameterBool> (sourceParamId (layer, "Enabled"), name + "Enabled", true));
        if (layer == 1)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (envelopeParamId (1, "attack"), name + "Attack", juce::NormalisableRange<float> (0.001f, 3.0f, 0.0001f, 0.35f), 0.01f));
            layout.add (std::make_unique<juce::AudioParameterFloat> (envelopeParamId (1, "decay"), name + "Decay", juce::NormalisableRange<float> (0.001f, 3.0f, 0.0001f, 0.35f), 0.15f));
            layout.add (std::make_unique<juce::AudioParameterFloat> (envelopeParamId (1, "sustain"), name + "Sustain", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
            layout.add (std::make_unique<juce::AudioParameterFloat> (envelopeParamId (1, "release"), name + "Release", juce::NormalisableRange<float> (0.001f, 5.0f, 0.0001f, 0.35f), 0.25f));
        }
        add ("Pitch", -24.0f, 24.0f, 1.0f, 0.0f, " st");
        add ("Stretch", 0.5f, 2.5f, 0.01f, 1.0f, " x");
        add ("Bend", -12.0f, 12.0f, 0.1f, 0.0f, " st");
        add ("Tremolo", 0.0f, 100.0f, 1.0f, 0.0f, " %");
        add ("Level", -60.0f, 0.0f, 0.1f, 0.0f, " dB");
        add ("Start", 0.0f, 1.0f, 0.0001f, 0.0f);
        add ("End", 0.0f, 1.0f, 0.0001f, 1.0f);
        layout.add (std::make_unique<juce::AudioParameterBool> (sourceParamId (layer, "Reverse"), name + "Reverse", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (sourceParamId (layer, "TremRate"), name + "Tremolo rate",
            juce::StringArray { "1/4", "1/8", "1/16", "1/32" }, 1));
    }
    const juce::StringArray effectNames { "Delay", "Reverb", "Chorus", "Phaser", "Flanger", "EQ", "Flutter", "Granular", "Orbit", "Random" };
    for (int effect = 0; effect < effectNames.size(); ++effect)
        layout.add (std::make_unique<juce::AudioParameterChoice> (routeParamId (effect), effectNames[effect] + " source",
            juce::StringArray { "A", "B" }, 0));

    // Append B banks to keep existing parameter IDs and ordering intact.
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "delayTime"), "B Delay Time", juce::NormalisableRange<float> (0.125f, 2.0f, 0.125f), 0.75f, juce::AudioParameterFloatAttributes().withLabel ("beats")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "delayFeedback"), "B Delay Feedback", juce::NormalisableRange<float> (0.0f, 0.92f, 0.01f), 0.42f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "delayMix"), "B Delay Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "reverbMix"), "B Reverb Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.25f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "reverbRoomSize"), "B Reverb Room Size", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.62f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "reverbDamping"), "B Reverb Damping", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.4f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "reverbWidth"), "B Reverb Width", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "reverbFreeze"), "B Reverb Freeze", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "chorusRate"), "B Chorus Rate", juce::NormalisableRange<float> (0.05f, 8.0f, 0.01f), 0.8f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "chorusDepth"), "B Chorus Depth", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.45f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "chorusDelay"), "B Chorus Delay", juce::NormalisableRange<float> (1.0f, 30.0f, 0.1f), 7.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "chorusFeedback"), "B Chorus Feedback", juce::NormalisableRange<float> (-0.95f, 0.95f, 0.01f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "chorusMix"), "B Chorus Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "flangerRate"), "B Flanger Rate", juce::NormalisableRange<float> (0.05f, 8.0f, 0.01f), 0.35f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "flangerDepth"), "B Flanger Depth", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.65f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "flangerDelay"), "B Flanger Delay", juce::NormalisableRange<float> (1.0f, 20.0f, 0.1f), 3.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "flangerFeedback"), "B Flanger Feedback", juce::NormalisableRange<float> (-0.95f, 0.95f, 0.01f), 0.35f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "flangerMix"), "B Flanger Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "flutterRate"), "B Flutter Rate", juce::NormalisableRange<float> (0.2f, 12.0f, 0.01f), 4.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "flutterDepth"), "B Flutter Depth", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "flutterDelay"), "B Flutter Delay", juce::NormalisableRange<float> (0.1f, 12.0f, 0.1f), 1.8f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "flutterMix"), "B Flutter Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "granularMix"), "B Granular Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.45f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "harmonyMix"), "B Harmony Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.4f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "randomMix"), "B Random Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.01f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "granularSize"), "B Grain Size", juce::NormalisableRange<float> (15.0f, 500.0f, 1.0f), 120.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "granularDensity"), "B Grain Density", juce::NormalisableRange<float> (1.0f, 40.0f, 0.1f), 9.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "granularPitch"), "B Grain Pitch", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "randomAmount"), "B Random Amount", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.45f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "randomRate"), "B Random Rate", juce::NormalisableRange<float> (1.0f, 24.0f, 0.1f), 6.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "harmonyShift"), "B Harmony Shift", juce::NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 7.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "harmonyDetune"), "B Harmony Detune", juce::NormalisableRange<float> (0.0f, 40.0f, 0.1f), 8.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "phaserRate"), "B Phaser Rate", juce::NormalisableRange<float> (0.05f, 8.0f, 0.01f), 0.45f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "phaserDepth"), "B Phaser Depth", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.6f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "phaserFeedback"), "B Phaser Feedback", juce::NormalisableRange<float> (-0.95f, 0.95f, 0.01f), 0.25f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (fxParamId (1, "phaserMix"), "B Phaser Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f));
    for (int band = 0; band < 7; ++band)
    {
        const auto prefix = fxParamId (1, "eqBand" + juce::String (band + 1));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Freq", prefix + " Frequency", juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), eqFrequencies[(size_t) band], juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Gain", prefix + " Gain", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Q", prefix + " Q", juce::NormalisableRange<float> (0.2f, 8.0f, 0.01f), 0.8f));
    }

    for (int layer = 0; layer < 2; ++layer)
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { fxParamId (layer, "chorusDrive"), 1 },
            layer == 0 ? "Chorus Drive" : "B Chorus Drive",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));

    return layout;
}

const juce::String OneShotSamplerAudioProcessor::getName() const { return JucePlugin_Name; }
int OneShotSamplerAudioProcessor::getFxSlotEffect (int slot) const
{
    if (slot < 0 || slot >= 7)
        return -1;

    // Choice index 0 means an empty rack card; the seven effect choices map
    // directly to internal effects 0–6.
    const auto choice = rackEffectParams[(size_t) slot]->load();
    return juce::jlimit (-1, 6, juce::roundToInt (choice) - 1);
}

bool OneShotSamplerAudioProcessor::getFxSlotEnabled (int slot) const
{
    if (slot < 0 || slot >= 7)
        return false;

    return rackEnabledParams[(size_t) slot]->load() > 0.5f;
}

void OneShotSamplerAudioProcessor::setFxSlotState (int slot, int effect, bool enabled)
{
    if (slot < 0 || slot >= 7)
        return;
    const auto slotName = "rackSlot" + juce::String (slot + 1);
    if (auto* parameter = parameters.getParameter (slotName + "Effect"))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (juce::jlimit (0, 7, effect + 1)));
    if (auto* parameter = parameters.getParameter (slotName + "Enabled"))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (enabled && effect >= 0 ? 1.0f : 0.0f));
}
bool OneShotSamplerAudioProcessor::acceptsMidi() const { return true; }
bool OneShotSamplerAudioProcessor::producesMidi() const { return false; }
bool OneShotSamplerAudioProcessor::isMidiEffect() const { return false; }
double OneShotSamplerAudioProcessor::getTailLengthSeconds() const
{
    // Cover the longest normal reverb and release. Frozen reverb is bounded
    // for host renders rather than advertising an infinite render length.
    double seconds = 10.0;
    const auto tempo = hostBpm.load (std::memory_order_relaxed);
    const auto bpm = std::isfinite (tempo) && tempo > 0.0 ? juce::jlimit (20.0, 400.0, tempo) : 120.0;
    std::array<bool, 2> present {}, legacyEnabled {};
    for (int slot = 0; slot < (int) rackEffectParams.size(); ++slot)
    {
        const auto effect = getFxSlotEffect (slot);
        if (juce::isPositiveAndBelow (effect, (int) present.size()))
        {
            present[(size_t) effect] = true;
            legacyEnabled[(size_t) effect] |= getFxSlotEnabled (slot);
        }
    }
    for (int layer = 0; layer < 2; ++layer)
    {
        const auto& values = fxValues[(size_t) layer];
        if (present[1] && isLayerFxOn (1, layer, legacyEnabled[1]) && values[(size_t) FxValue::reverbFreeze]->load() >= 0.5f)
            seconds = juce::jmax (seconds, 30.0);
        if (present[0] && isLayerFxOn (0, layer, legacyEnabled[0]))
        {
            const auto feedback = juce::jlimit (0.0, 0.86, (double) values[(size_t) FxValue::delayFeedback]->load());
            const auto loopGain = 0.82 * 1.35 * feedback;
            const auto repeatCount = loopGain > 0.0 ? std::ceil (std::log (0.001) / std::log (loopGain)) + 1.0 : 1.0;
            const auto maxTap = juce::jmin (4.0, 1.5 * values[(size_t) FxValue::delayTime]->load() * 60.0 / bpm);
            seconds = juce::jmax (seconds, 10.0 + repeatCount * maxTap);
        }
    }
    return seconds;
}
int OneShotSamplerAudioProcessor::getNumPrograms() { return 1; }
int OneShotSamplerAudioProcessor::getCurrentProgram() { return 0; }
void OneShotSamplerAudioProcessor::setCurrentProgram (int) {}
const juce::String OneShotSamplerAudioProcessor::getProgramName (int) { return {}; }
void OneShotSamplerAudioProcessor::changeProgramName (int, const juce::String&) {}

void OneShotSamplerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    protectionReductionDb.store (0.0f, std::memory_order_relaxed);
    retiringOutput = {};
    retiringFrames = {};
    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("outputGain")->load()));
    for (size_t layer = 0; layer < voices.size(); ++layer)
    {
        for (auto& voice : voices[layer])
        {
            voice.envelope.setSampleRate (sampleRate);
            voice.envelope.reset();
            voice.active = false;
        }
        sourcePower[layer].reset (sampleRate, 0.012);
        sourcePower[layer].setCurrentAndTargetValue (sourceParams[layer][9]->load() > 0.5f ? 1.0f : 0.0f);
        layerGain[layer].reset (sampleRate, 0.01);
        layerGain[layer].setCurrentAndTargetValue (juce::Decibels::decibelsToGain (sourceParams[layer][4]->load()));
        layerBuffers[layer].setSize (2, juce::jmax (1, samplesPerBlock));
        racks[layer].prepare (sampleRate, juce::jmax (1, samplesPerBlock));
        layerProgress[layer].store (-1.0f);
    }
}

void OneShotSamplerAudioProcessor::Rack::prepare (double sampleRate, int samplesPerBlock)
{
    bypassDry.setSize (2, samplesPerBlock);
    effectRunning.fill (false);
    for (auto& mix : bypassMix)
    {
        mix.reset (sampleRate, 0.012);
        mix.setCurrentAndTargetValue (0.0f);
    }
    delayBuffer.setSize (2, static_cast<int> (sampleRate * 4.0));
    delayBuffer.clear();
    delayWritePosition = 0;
    delayToneState = {};
    reverb.prepare ({ sampleRate, static_cast<juce::uint32> (samplesPerBlock), 2 });
    phaser.prepare ({ sampleRate, static_cast<juce::uint32> (samplesPerBlock), 2 });
    reverbPreDelayBuffer.setSize (2, static_cast<int> (sampleRate * 0.12) + 4);
    reverbPreDelayBuffer.clear();
    reverbWetBuffer.setSize (2, juce::jmax (1, samplesPerBlock));
    reverbWetBuffer.clear();
    reverbPreDelayWritePosition = 0;
    reverbFreezeWarmupSamples = 0;
    reverbPreDelayCurrent = 0.0f;
    reverbToneState = {};
    reverbHighPassState = {};
    const auto modulationBufferLength = static_cast<int> (sampleRate * 0.085) + 4;
    chorusBuffer.setSize (2, modulationBufferLength); chorusBuffer.clear();
    flangerBuffer.setSize (2, modulationBufferLength); flangerBuffer.clear();
    flutterBuffer.setSize (2, modulationBufferLength); flutterBuffer.clear();
    chorusWritePosition = flangerWritePosition = flutterWritePosition = 0;
    chorusPhase = flangerPhase = 0.0f;
    modDelayCurrent = modSweepCurrent = {};
    flutterWowPhase = flutterDriftPhase = 0.0f;
    for (auto* smoother : { &delayMixSmooth, &reverbMixSmooth, &chorusMixSmooth, &flangerMixSmooth, &flutterMixSmooth })
        smoother->reset (sampleRate, 0.03);
    delayTimeSmooth.reset (sampleRate, 0.25);
    delayTimeReady = false;
    for (auto& cache : eqCache) cache = { -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f };
    flutterRandom = flutterRandomTarget = 0.0f;
    flutterRandomCountdown = 0;
    chorusFeedbackState = {};
    chorusDriveSmooth.reset (sampleRate, 0.025);
    chorusDriveSmooth.setCurrentAndTargetValue (0.0f);
    previousChorusInput = {};
    flangerFeedbackState = {};
    textureBuffer.setSize (2, static_cast<int> (sampleRate * 2.5));
    textureBuffer.clear();
    textureWritePosition = 0;
    textureValid = 0;
    textureClock = 0.0;
    grainReadPosition = randomReadPosition = 0.0f;
    textureOrbitPhase = 0.0f;
    grainSamplesUntilReset = randomSamplesUntilReset = 0;
    for (auto* wet : { &granularWet, &harmonyWet, &randomWet })
    {
        wet->reset (sampleRate, 0.035);
        wet->setCurrentAndTargetValue (0.0f);
    }
    eqBands = {};
}

void OneShotSamplerAudioProcessor::releaseResources() {}

bool OneShotSamplerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void OneShotSamplerAudioProcessor::Rack::process (OneShotSamplerAudioProcessor& owner, juce::AudioBuffer<float>& buffer, double bpm, int layer)
{
    auto& parameters = owner.parameters;
    const auto currentSampleRate = owner.currentSampleRate;
    const auto& eqParamPointers = owner.eqParamPointers[(size_t) layer];
    const auto& eqBandTypes = owner.eqBandTypes[(size_t) layer];
    const auto value = [&owner, layer] (FxValue index) { return owner.fxValues[(size_t) layer][(size_t) index]->load(); };
    juce::dsp::AudioBlock<float> block (buffer);
    const auto tapSmooth = (float) (1.0 - std::exp (-1.0 / (0.03 * currentSampleRate)));
    const auto readInterpolated = [] (const juce::AudioBuffer<float>& source, int channel, float position)
    {
        const auto length = source.getNumSamples();
        while (position < 0.0f) position += (float) length;
        while (position >= (float) length) position -= (float) length;
        const auto first = (int) position;
        const auto second = (first + 1) % length;
        const auto fraction = position - (float) first;
        const auto a = source.getSample (channel, first);
        return a + (source.getSample (channel, second) - a) * fraction;
    };
    const auto applyEq = [this, &eqParamPointers, &eqBandTypes, currentSampleRate, &buffer]
    {
        using Array = juce::dsp::IIR::ArrayCoefficients<float>;
        const auto fade = juce::jmax (1, (int) (currentSampleRate * 0.006));
        for (int band = 0; band < 7; ++band)
        {
            const auto b = (size_t) band;
            auto& stage = eqBands[b];
            if (stage.remaining != 0) continue;
            // Keep coefficient generation stable even in hosts running at low rates.
            const auto frequency = juce::jlimit (20.0f, (float) currentSampleRate * 0.49f, eqParamPointers[b][0]->load());
            const auto gainDb = eqParamPointers[b][1]->load();
            const auto q = juce::jlimit (0.2f, 8.0f, eqParamPointers[b][2]->load());
            const auto type = eqBandTypes[b].load();
            auto& cache = eqCache[b];
            // Rebuild only on a real change: no allocation in the common case.
            if (cache[0] == frequency && cache[1] == gainDb && cache[2] == q && cache[3] == (float) type)
                continue;
            cache = { frequency, gainDb, q, (float) type };
            const auto gain = juce::Decibels::decibelsToGain (gainDb);
            EqStage::Coefficients coefficients;
            switch (type) {
                case 1: coefficients = Array::makeLowPass (currentSampleRate, frequency, q); break;
                case 2: coefficients = Array::makeHighPass (currentSampleRate, frequency, q); break;
                case 3: coefficients = Array::makeNotch (currentSampleRate, frequency, q); break;
                case 4: coefficients = Array::makeLowShelf (currentSampleRate, frequency, q, gain); break;
                case 5: coefficients = Array::makeHighShelf (currentSampleRate, frequency, q, gain); break;
                default: coefficients = Array::makePeakFilter (currentSampleRate, frequency, q, gain); break;
            }
            const auto inverseA0 = 1.0f / coefficients[3];
            for (auto& coefficient : coefficients) coefficient *= inverseA0;
            if (! stage.initialized)
            {
                stage.active.c = coefficients;
                stage.initialized = true;
            }
            else
            {
                stage.next = {};
                stage.next.c = coefficients;
                stage.remaining = fade;
            }
        }
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
            for (auto& stage : eqBands)
            {
                const auto blending = stage.remaining > 0;
                const auto blend = 1.0f - (float) stage.remaining / (float) fade;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                {
                    const auto input = buffer.getSample (ch, frame);
                    auto output = stage.active.tick (ch, input);
                    if (blending) output += (stage.next.tick (ch, input) - output) * blend;
                    buffer.setSample (ch, frame, output);
                }
                if (blending && --stage.remaining == 0) stage.active = stage.next;
            }
    };
    const auto applyDelay = [this, &value, currentSampleRate, &buffer, bpm, &readInterpolated]
    {
        const auto targetDelay = (float) juce::jlimit (8, delayBuffer.getNumSamples() - 2, static_cast<int> (currentSampleRate * 60.0 / bpm * value (FxValue::delayTime)));
        // Time changes glide like a tape delay instead of jumping (no clicks).
        if (! delayTimeReady) { delayTimeSmooth.setCurrentAndTargetValue (targetDelay); delayTimeReady = true; }
        else delayTimeSmooth.setTargetValue (targetDelay);
        const auto feedback = juce::jlimit (0.0f, 0.86f, value (FxValue::delayFeedback));
        delayMixSmooth.setTargetValue (value (FxValue::delayMix));
        const auto toneCoefficient = 0.07f + (1.0f - feedback) * 0.16f;
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            const auto left = buffer.getSample (0, frame);
            const auto right = buffer.getNumChannels() > 1 ? buffer.getSample (1, frame) : left;
            const auto mix = delayMixSmooth.getNextValue();
            const auto delaySamplesNow = delayTimeSmooth.getNextValue();
            const auto halfHead = delaySamplesNow * 0.5f;
            const auto mainHead = delaySamplesNow;
            const auto longHead = juce::jmin ((float) delayBuffer.getNumSamples() - 2.0f, delaySamplesNow * 1.5f);
            const auto wetLeft = 0.25f * readInterpolated (delayBuffer, 1, (float) delayWritePosition - halfHead)
                               + 0.55f * readInterpolated (delayBuffer, 0, (float) delayWritePosition - mainHead)
                               + 0.20f * readInterpolated (delayBuffer, 1, (float) delayWritePosition - longHead);
            const auto wetRight = 0.25f * readInterpolated (delayBuffer, 0, (float) delayWritePosition - halfHead)
                                + 0.55f * readInterpolated (delayBuffer, 1, (float) delayWritePosition - mainHead)
                                + 0.20f * readInterpolated (delayBuffer, 0, (float) delayWritePosition - longHead);
            delayToneState[0] += toneCoefficient * (wetRight - delayToneState[0]);
            delayToneState[1] += toneCoefficient * (wetLeft - delayToneState[1]);
            const auto feedbackLeft = std::tanh (delayToneState[0] * 1.35f) * 0.82f;
            const auto feedbackRight = std::tanh (delayToneState[1] * 1.35f) * 0.82f;
            delayBuffer.setSample (0, delayWritePosition, left + feedbackLeft * feedback);
            delayBuffer.setSample (1, delayWritePosition, right + feedbackRight * feedback);
            buffer.setSample (0, frame, left * (1.0f - mix * 0.12f) + wetLeft * mix);
            if (buffer.getNumChannels() > 1) buffer.setSample (1, frame, right * (1.0f - mix * 0.12f) + wetRight * mix);
            delayWritePosition = (delayWritePosition + 1) % delayBuffer.getNumSamples();
        }
    };
    const auto applyReverb = [this, &value, currentSampleRate, &buffer, &readInterpolated, tapSmooth]
    {
        juce::dsp::Reverb::Parameters p;
        const auto mix = juce::jlimit (0.0f, 1.0f, value (FxValue::reverbMix));
        const auto room = value (FxValue::reverbRoomSize);
        const auto damping = value (FxValue::reverbDamping);
        p.roomSize = room;
        p.damping = damping;
        p.width = value (FxValue::reverbWidth);
        const auto wantsFreeze = value (FxValue::reverbFreeze) >= 0.5f;
        const auto freezeWarmup = juce::roundToInt (currentSampleRate * 0.12);
        if (! wantsFreeze)
            reverbFreezeWarmupSamples = 0;
        // JUCE disables the reverb input in freeze mode. Let the tank fill
        // first so a frozen preset can capture a note from a cold start.
        p.freezeMode = wantsFreeze && reverbFreezeWarmupSamples >= freezeWarmup ? 1.0f : 0.0f;
        // Keep the tank generous but leave headroom for stacked processing.
        p.wetLevel = 0.40f + room * 0.16f;
        p.dryLevel = 0.0f;
        reverb.setParameters (p);
        const auto preDelaySamples = juce::jlimit (1, reverbPreDelayBuffer.getNumSamples() - 2, (int) (currentSampleRate * (0.009f + room * 0.070f)));
        if (reverbPreDelayCurrent == 0.0f) reverbPreDelayCurrent = (float) preDelaySamples;
        const auto toneCoefficient = 0.26f - damping * 0.20f;
        // Remove rumble before the tank. This stops large rooms accumulating
        // low-end energy while preserving the source's direct bass content.
        const auto highPassCoefficient = 0.0040f + room * 0.0045f;
        if (buffer.getNumChannels() == 1)
            reverbWetBuffer.clear (1, 0, buffer.getNumSamples());
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            reverbPreDelayCurrent += tapSmooth * ((float) preDelaySamples - reverbPreDelayCurrent);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const auto input = buffer.getSample (channel, frame);
                reverbHighPassState[(size_t) channel] += highPassCoefficient * (input - reverbHighPassState[(size_t) channel]);
                const auto tankInput = input - reverbHighPassState[(size_t) channel];
                const auto delayed = readInterpolated (reverbPreDelayBuffer, channel, (float) reverbPreDelayWritePosition - reverbPreDelayCurrent);
                reverbPreDelayBuffer.setSample (channel, reverbPreDelayWritePosition, tankInput);
                reverbToneState[(size_t) channel] += toneCoefficient * (delayed - reverbToneState[(size_t) channel]);
                reverbWetBuffer.setSample (channel, frame, reverbToneState[(size_t) channel]);
            }
            reverbPreDelayWritePosition = (reverbPreDelayWritePosition + 1) % reverbPreDelayBuffer.getNumSamples();
        }
        juce::dsp::AudioBlock<float> wetBlock (reverbWetBuffer);
        auto wetView = wetBlock.getSubBlock (0, (size_t) buffer.getNumSamples());
        reverb.process (juce::dsp::ProcessContextReplacing<float> (wetView));
        if (wantsFreeze && reverbFreezeWarmupSamples < freezeWarmup)
        {
            for (int frame = 0; frame < buffer.getNumSamples() && reverbFreezeWarmupSamples < freezeWarmup; ++frame)
                if (reverbFreezeWarmupSamples > 0
                    || std::abs (reverbWetBuffer.getSample (0, frame)) > 0.00001f
                    || std::abs (reverbWetBuffer.getSample (1, frame)) > 0.00001f)
                    ++reverbFreezeWarmupSamples;
        }
        reverbMixSmooth.setTargetValue (mix);
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            const auto smoothedMix = reverbMixSmooth.getNextValue();
            const auto dryGain = 1.0f - smoothedMix * 0.18f;
            const auto wetGain = smoothedMix * 0.72f;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const auto dry = buffer.getSample (channel, frame);
                const auto wet = std::tanh (reverbWetBuffer.getSample (channel, frame) * 1.05f) * 0.78f;
                buffer.setSample (channel, frame, dry * dryGain + wet * wetGain);
            }
        }
    };
    const auto applyChorus = [this, &value, currentSampleRate, &buffer, &readInterpolated, tapSmooth]
    {
        const auto rate = value (FxValue::chorusRate);
        const auto depth = value (FxValue::chorusDepth);
        const auto delayTarget = value (FxValue::chorusDelay) * 0.001f * (float) currentSampleRate;
        const auto feedback = value (FxValue::chorusFeedback);
        chorusMixSmooth.setTargetValue (value (FxValue::chorusMix));
        chorusDriveSmooth.setTargetValue (value (FxValue::chorusDrive));

        const auto driveInput = [this] (float input, size_t channel, float drive)
        {
            const auto previous = previousChorusInput[channel];
            previousChorusInput[channel] = input;
            if (drive <= 0.0f)
                return input;

            // First-order antiderivative antialiasing for x / (1 + |x|).
            // Process the delay input only, leaving the direct signal clean.
            const auto gain = 1.0f + drive * 10.0f;
            const auto x = input * gain;
            const auto last = previous * gain;
            const auto delta = x - last;
            const auto integral = [] (double v)
            {
                const auto magnitude = std::abs (v);
                return magnitude - std::log1p (magnitude);
            };
            const auto midpoint = (x + last) * 0.5f;
            const auto shaped = std::abs (delta) > 0.001f
                ? static_cast<float> ((integral (x) - integral (last)) / delta)
                : midpoint / (1.0f + std::abs (midpoint));
            const auto saturated = shaped / std::sqrt (gain);
            return input + drive * (saturated - input);
        };
        // Never let the sweep reach below ~2 samples of delay (that read the oldest audio in the buffer).
        const auto depthTarget = 6.0f + depth * (float) currentSampleRate * 0.010f;
        if (modDelayCurrent[0] == 0.0f) { modDelayCurrent[0] = delayTarget; modSweepCurrent[0] = depthTarget; }
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            const auto left = buffer.getSample (0, frame);
            const auto right = buffer.getNumChannels() > 1 ? buffer.getSample (1, frame) : left;
            const auto mix = chorusMixSmooth.getNextValue();
            const auto phaseLeft = chorusPhase;
            const auto phaseRight = chorusPhase + 0.87f;
            auto& baseDelay = modDelayCurrent[0];
            auto& sweep = modSweepCurrent[0];
            baseDelay += tapSmooth * (delayTarget - baseDelay);
            sweep += tapSmooth * (depthTarget - sweep);
            const auto depthSamples = juce::jmin (sweep, juce::jmax (0.0f, baseDelay - 2.0f));
            const auto leftVoiceA = readInterpolated (chorusBuffer, 0, (float) chorusWritePosition - baseDelay - depthSamples * std::sin (phaseLeft));
            const auto leftVoiceB = readInterpolated (chorusBuffer, 0, (float) chorusWritePosition - baseDelay - depthSamples * std::sin (phaseLeft + 2.13f));
            const auto rightVoiceA = readInterpolated (chorusBuffer, 1, (float) chorusWritePosition - baseDelay - depthSamples * std::sin (phaseRight));
            const auto rightVoiceB = readInterpolated (chorusBuffer, 1, (float) chorusWritePosition - baseDelay - depthSamples * std::sin (phaseRight + 2.13f));
            const auto wetLeft = 0.5f * (leftVoiceA + leftVoiceB);
            const auto wetRight = 0.5f * (rightVoiceA + rightVoiceB);
            chorusFeedbackState[0] = std::tanh (chorusFeedbackState[0] * feedback + wetLeft * 0.35f);
            chorusFeedbackState[1] = std::tanh (chorusFeedbackState[1] * feedback + wetRight * 0.35f);
            const auto drive = chorusDriveSmooth.getNextValue();
            chorusBuffer.setSample (0, chorusWritePosition, driveInput (left, 0, drive) + chorusFeedbackState[0] * std::abs (feedback) * 0.30f);
            chorusBuffer.setSample (1, chorusWritePosition, driveInput (right, 1, drive) + chorusFeedbackState[1] * std::abs (feedback) * 0.30f);
            buffer.setSample (0, frame, left * (1.0f - mix) + wetLeft * mix);
            if (buffer.getNumChannels() > 1) buffer.setSample (1, frame, right * (1.0f - mix) + wetRight * mix);
            chorusWritePosition = (chorusWritePosition + 1) % chorusBuffer.getNumSamples();
            chorusPhase = std::fmod (chorusPhase + juce::MathConstants<float>::twoPi * rate / (float) currentSampleRate, juce::MathConstants<float>::twoPi);
        }
    };
    const auto applyFlanger = [this, &value, currentSampleRate, &buffer, &readInterpolated, tapSmooth]
    {
        const auto rate = value (FxValue::flangerRate);
        const auto depth = value (FxValue::flangerDepth);
        const auto delayTarget = value (FxValue::flangerDelay) * 0.001f * (float) currentSampleRate;
        const auto feedback = value (FxValue::flangerFeedback);
        flangerMixSmooth.setTargetValue (value (FxValue::flangerMix));
        const auto sweepTarget = 2.0f + depth * (float) currentSampleRate * 0.008f;
        if (modDelayCurrent[1] == 0.0f) { modDelayCurrent[1] = delayTarget; modSweepCurrent[1] = sweepTarget; }
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            const auto left = buffer.getSample (0, frame);
            const auto right = buffer.getNumChannels() > 1 ? buffer.getSample (1, frame) : left;
            const auto mix = flangerMixSmooth.getNextValue();
            auto& baseDelay = modDelayCurrent[1];
            auto& sweepSamples = modSweepCurrent[1];
            baseDelay += tapSmooth * (delayTarget - baseDelay);
            sweepSamples += tapSmooth * (sweepTarget - sweepSamples);
            const auto leftDelay = baseDelay + sweepSamples * (0.5f + 0.5f * std::sin (flangerPhase));
            const auto rightDelay = baseDelay + sweepSamples * (0.5f + 0.5f * std::sin (flangerPhase + 0.45f));
            const auto wetLeft = readInterpolated (flangerBuffer, 0, (float) flangerWritePosition - leftDelay);
            const auto wetRight = readInterpolated (flangerBuffer, 1, (float) flangerWritePosition - rightDelay);
            flangerFeedbackState[0] += 0.16f * (wetLeft - flangerFeedbackState[0]);
            flangerFeedbackState[1] += 0.16f * (wetRight - flangerFeedbackState[1]);
            flangerBuffer.setSample (0, flangerWritePosition, left + std::tanh (flangerFeedbackState[0] * feedback * 1.4f) * 0.62f);
            flangerBuffer.setSample (1, flangerWritePosition, right + std::tanh (flangerFeedbackState[1] * feedback * 1.4f) * 0.62f);
            buffer.setSample (0, frame, left * (1.0f - mix) + wetLeft * mix);
            if (buffer.getNumChannels() > 1) buffer.setSample (1, frame, right * (1.0f - mix) + wetRight * mix);
            flangerWritePosition = (flangerWritePosition + 1) % flangerBuffer.getNumSamples();
            flangerPhase = std::fmod (flangerPhase + juce::MathConstants<float>::twoPi * rate / (float) currentSampleRate, juce::MathConstants<float>::twoPi);
        }
    };
    const auto applyFlutter = [this, &value, currentSampleRate, &buffer, &readInterpolated, tapSmooth]
    {
        const auto rate = value (FxValue::flutterRate);
        const auto depth = value (FxValue::flutterDepth);
        const auto delayTarget = value (FxValue::flutterDelay) * 0.001f * (float) currentSampleRate;
        flutterMixSmooth.setTargetValue (value (FxValue::flutterMix));
        const auto wowRate = 0.12f + rate * 0.10f;
        const auto sweepTarget = depth * (float) currentSampleRate * 0.0042f;
        if (modDelayCurrent[2] == 0.0f) { modDelayCurrent[2] = delayTarget; modSweepCurrent[2] = sweepTarget; }
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            if (--flutterRandomCountdown <= 0)
            {
                flutterRandomCountdown = juce::jmax (12, (int) (currentSampleRate * (0.018f + (1.0f - depth) * 0.045f)));
                flutterRandomTarget = textureRandom.nextFloat() * 2.0f - 1.0f;
            }
            flutterRandom += (flutterRandomTarget - flutterRandom) * 0.0025f;
            // Each component has its own continuous phase. (They used to be derived
            // from a phase that wrapped every cycle, which made the delay jump.)
            const auto modulation = 0.62f * std::sin (flutterWowPhase)
                                  + 0.23f * std::sin (flutterDriftPhase) + 0.15f * flutterRandom;
            const auto mix = flutterMixSmooth.getNextValue();
            auto& baseDelay = modDelayCurrent[2];
            auto& flutterSamples = modSweepCurrent[2];
            baseDelay += tapSmooth * (delayTarget - baseDelay);
            flutterSamples += tapSmooth * (sweepTarget - flutterSamples);
            const auto delay = juce::jmax (1.0f, baseDelay + flutterSamples * modulation);
            const auto left = buffer.getSample (0, frame);
            const auto right = buffer.getNumChannels() > 1 ? buffer.getSample (1, frame) : left;
            const auto wetLeft = readInterpolated (flutterBuffer, 0, (float) flutterWritePosition - delay);
            const auto wetRight = readInterpolated (flutterBuffer, 1, (float) flutterWritePosition - delay * 1.03f);
            flutterBuffer.setSample (0, flutterWritePosition, left);
            flutterBuffer.setSample (1, flutterWritePosition, right);
            buffer.setSample (0, frame, left * (1.0f - mix) + wetLeft * mix);
            if (buffer.getNumChannels() > 1) buffer.setSample (1, frame, right * (1.0f - mix) + wetRight * mix);
            flutterWritePosition = (flutterWritePosition + 1) % flutterBuffer.getNumSamples();
            flutterWowPhase = std::fmod (flutterWowPhase + juce::MathConstants<float>::twoPi * wowRate / (float) currentSampleRate, juce::MathConstants<float>::twoPi);
            flutterDriftPhase = std::fmod (flutterDriftPhase + juce::MathConstants<float>::twoPi * rate * 1.91f / (float) currentSampleRate, juce::MathConstants<float>::twoPi);
        }
    };
    const auto textureValidity = [this] (double position)
    {
        const auto age = textureClock - position;
        // Leave two samples for interpolation and fade at both validity edges.
        return (float) juce::jlimit (0.0, 1.0, juce::jmin (age - 2.0, textureValid - age) / 64.0);
    };
    const auto readTexture = [this] (int channel, double position)
    {
        const auto length = textureBuffer.getNumSamples();
        const auto whole = (int64_t) std::floor (position);
        const auto first = (int) (whole % length);
        const auto second = (first + 1) % length;
        const auto a = textureBuffer.getSample (channel, first);
        return a + (textureBuffer.getSample (channel, second) - a) * (float) (position - whole);
    };

    // Each Texture Lab engine owns its own state. There is deliberately no
    // hidden master gate: turning Granular, Orbit, or Random on is enough.
    granularWet.setTargetValue (owner.isLayerFxOn (7, layer, parameters.getRawParameterValue ("granularEnabled")->load() > 0.5f) ? value (FxValue::granularMix) : 0.0f);
    harmonyWet.setTargetValue (owner.isLayerFxOn (8, layer, parameters.getRawParameterValue ("harmonyEnabled")->load() > 0.5f) ? value (FxValue::harmonyMix) : 0.0f);
    randomWet.setTargetValue (owner.isLayerFxOn (9, layer, parameters.getRawParameterValue ("randomEnabled")->load() > 0.5f) ? value (FxValue::randomMix) : 0.0f);

    const auto applyTextureLab = [this, &value, currentSampleRate, &buffer, &readTexture, &textureValidity]
    {
        const auto grainSize = value (FxValue::granularSize) * 0.001f * (float) currentSampleRate;
        const auto grainInterval = juce::jmax (1, (int) (currentSampleRate / value (FxValue::granularDensity)));
        const auto grainRate = std::pow (2.0f, value (FxValue::granularPitch) / 12.0f);
        const auto randomAmount = value (FxValue::randomAmount);
        const auto randomInterval = juce::jmax (1, (int) (currentSampleRate / value (FxValue::randomRate)));
        const auto orbitWidth = 0.025f + ((value (FxValue::harmonyShift) + 12.0f) / 24.0f) * 0.13f;
        const auto orbitRate = 0.06f + value (FxValue::harmonyDetune) / 40.0f * 0.85f;

        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            const auto writePosition = textureWritePosition;
            std::array<float, 2> source { buffer.getSample (0, frame), buffer.getNumChannels() > 1 ? buffer.getSample (1, frame) : buffer.getSample (0, frame) };
            std::array<float, 2> output = source;

            if (--grainSamplesUntilReset <= 0)
            {
                grainSamplesUntilReset = grainInterval;
                latchedGrainInterval = grainInterval;
                latchedGrainDuration = juce::jlimit (1, grainInterval, (int) grainSize);
                latchedGrainRate = grainRate;
                const auto catchup = juce::jmax (0.0f, grainRate - 1.0f) * latchedGrainDuration;
                grainReadPosition = textureClock - juce::jmax (catchup + 67.0f, grainSize * (0.45f + textureRandom.nextFloat() * 0.55f));
            }
            const auto grainProgress = juce::jlimit (0.0f, 1.0f, (float) (latchedGrainInterval - grainSamplesUntilReset) / (float) latchedGrainDuration);
            const auto grainWindow = std::sin (juce::MathConstants<float>::pi * grainProgress);
            const auto grainBlend = granularWet.getNextValue() * grainWindow * textureValidity (grainReadPosition);
            if (grainBlend > 0.00001f)
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                {
                    const auto grain = readTexture (channel, grainReadPosition);
                    output[(size_t) channel] += (grain - output[(size_t) channel]) * grainBlend;
                }
            grainReadPosition += latchedGrainRate;

            const auto orbitBlend = harmonyWet.getNextValue();
            if (orbitBlend > 0.00001f)
            {
                const auto orbit = std::sin (textureOrbitPhase);
                const auto leftPosition = textureClock - currentSampleRate * (0.17f + orbitWidth * orbit);
                const auto rightPosition = textureClock - currentSampleRate * (0.17f - orbitWidth * orbit);
                if (const auto valid = textureValidity (leftPosition); valid > 0.0f)
                    output[0] += (readTexture (0, leftPosition) - output[0]) * orbitBlend * valid;
                if (const auto valid = textureValidity (rightPosition); valid > 0.0f && buffer.getNumChannels() > 1)
                    output[1] += (readTexture (1, rightPosition) - output[1]) * orbitBlend * valid;
            }
            textureOrbitPhase = std::fmod (textureOrbitPhase + juce::MathConstants<float>::twoPi * orbitRate / (float) currentSampleRate, juce::MathConstants<float>::twoPi);

            if (--randomSamplesUntilReset <= 0)
            {
                randomSamplesUntilReset = randomInterval;
                latchedRandomInterval = randomInterval;
                latchedRandomAmount = randomAmount;
                const auto history = juce::jmax (0.0f, (float) textureValid - 67.0f);
                randomReadPosition = textureClock - (67.0f + textureRandom.nextFloat() * history * randomAmount);
            }
            const auto randomWindow = std::sin (juce::MathConstants<float>::pi * (1.0f - (float) randomSamplesUntilReset / (float) latchedRandomInterval));
            const auto randomBlend = randomWet.getNextValue() * randomWindow * textureValidity (randomReadPosition);
            if (randomBlend > 0.00001f)
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                {
                    const auto scattered = readTexture (channel, randomReadPosition);
                    output[(size_t) channel] += (scattered - output[(size_t) channel]) * randomBlend;
                }
            randomReadPosition += 1.0f + (textureRandom.nextFloat() - 0.5f) * latchedRandomAmount * 0.08f;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                textureBuffer.setSample (channel, writePosition, source[(size_t) channel]);
                buffer.setSample (channel, frame, output[(size_t) channel]);
            }
            textureWritePosition = (textureWritePosition + 1) % textureBuffer.getNumSamples();
            textureValid = juce::jmin (textureValid + 1, textureBuffer.getNumSamples());
            textureClock += 1.0;
        }
    };
    // Capture texture from the source before any spatial tails are added.
    // The engines share one history and must advance it exactly once.
    if (granularWet.getCurrentValue() > 0.0001f || granularWet.getTargetValue() > 0.0001f
        || harmonyWet.getCurrentValue() > 0.0001f || harmonyWet.getTargetValue() > 0.0001f
        || randomWet.getCurrentValue() > 0.0001f || randomWet.getTargetValue() > 0.0001f)
        applyTextureLab();
    else
    {
        // Discard history without clearing the whole buffer on the audio thread.
        textureValid = textureWritePosition = 0;
        textureClock = 0.0;
        grainSamplesUntilReset = randomSamplesUntilReset = 0;
    }

    std::array<bool, 7> enabled {};
    std::array<int, 7> order {};
    size_t count = 0;
    for (int slot = 0; slot < 7; ++slot)
    {
        const auto effect = owner.getFxSlotEffect (slot);
        if (! juce::isPositiveAndBelow (effect, 7)) continue;
        enabled[(size_t) effect] |= owner.isLayerFxOn (effect, layer, owner.getFxSlotEnabled (slot));
        if (effect >= 2 && std::find (order.begin(), order.begin() + (int) count, effect) == order.begin() + (int) count)
            order[count++] = effect;
    }
    for (int effect = 2; effect < 7; ++effect)
        if (std::find (order.begin(), order.begin() + (int) count, effect) == order.begin() + (int) count)
            order[count++] = effect;
    order[count++] = 0;
    order[count++] = 1;
    const auto reset = [&] (int effect)
    {
        switch (effect)
        {
            case 0: delayBuffer.clear(); delayWritePosition = 0; delayToneState = {}; delayTimeReady = false; break;
            case 1: reverb.reset(); reverbPreDelayBuffer.clear(); reverbPreDelayWritePosition = 0;
                    reverbFreezeWarmupSamples = 0; reverbToneState = {}; reverbHighPassState = {}; reverbPreDelayCurrent = 0.0f; break;
            case 2: chorusBuffer.clear(); chorusWritePosition = 0; chorusFeedbackState = {}; previousChorusInput = {}; modDelayCurrent[0] = 0.0f; break;
            case 3: phaser.reset(); break;
            case 4: flangerBuffer.clear(); flangerWritePosition = 0; flangerFeedbackState = {}; modDelayCurrent[1] = 0.0f; break;
            case 5: eqBands = {}; for (auto& cache : eqCache) cache[0] = -1.0f; break;
            case 6: flutterBuffer.clear(); flutterWritePosition = 0; flutterRandom = flutterRandomTarget = 0;
                    flutterRandomCountdown = 0; modDelayCurrent[2] = 0.0f; break;
            default: break;
        }
    };
    const auto frames = buffer.getNumSamples();
    const auto channels = buffer.getNumChannels();
    for (size_t index = 0; index < count; ++index)
    {
        const auto effect = order[index];
        auto& mix = bypassMix[(size_t) effect];
        mix.setTargetValue (enabled[(size_t) effect] ? 1.0f : 0.0f);
        if (! mix.isSmoothing() && mix.getCurrentValue() == 0.0f)
        {
            if (effectRunning[(size_t) effect]) reset (effect);
            effectRunning[(size_t) effect] = false;
            continue;
        }
        effectRunning[(size_t) effect] = true;
        const auto transition = mix.isSmoothing();
        if (transition)
            for (int ch = 0; ch < channels; ++ch)
                bypassDry.copyFrom (ch, 0, buffer, ch, 0, frames);
        switch (effect)
        {
            case 0: applyDelay(); break;
            case 1: applyReverb(); break;
            case 2: applyChorus(); break;
            case 3: phaser.setRate (value (FxValue::phaserRate)); phaser.setDepth (value (FxValue::phaserDepth)); phaser.setFeedback (value (FxValue::phaserFeedback)); phaser.setMix (value (FxValue::phaserMix)); phaser.process (juce::dsp::ProcessContextReplacing<float> (block)); break;
            case 4: applyFlanger(); break;
            case 5: applyEq(); break;
            case 6: applyFlutter(); break;
            default: break;
        }
        if (transition)
            for (int frame = 0; frame < frames; ++frame)
            {
                const auto amount = mix.getNextValue();
                for (int ch = 0; ch < channels; ++ch)
                {
                    const auto dry = bypassDry.getSample (ch, frame);
                    const auto wet = buffer.getSample (ch, frame);
                    buffer.setSample (ch, frame, dry + ((std::isfinite (wet) ? wet : dry) - dry) * amount);
                }
            }
    }
}

void OneShotSamplerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() == 0 || layerBuffers[0].getNumSamples() == 0)
        return;

    double tempo = 0.0, beat = 0.0, beatsPerBar = 4.0;
    juce::Optional<double> lastBar;
    juce::Optional<int64_t> hostBarCount;
    bool sync = false;
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
        {
            if (auto bpm = position->getBpm())
                tempo = *bpm;
            if (const auto meter = position->getTimeSignature(); meter && meter->numerator > 0 && meter->denominator > 0)
                beatsPerBar = 4.0 * meter->numerator / meter->denominator;
            if (const auto ppq = position->getPpqPosition(); position->getIsPlaying() && ppq && std::isfinite (*ppq))
            {
                beat = *ppq;
                lastBar = position->getPpqPositionOfLastBarStart();
                hostBarCount = position->getBarCount();
                sync = true;
            }
        }
    hostBpm.store (tempo, std::memory_order_relaxed);
    const auto bpm = tempo > 0.0 && std::isfinite (tempo) ? juce::jlimit (20.0, 400.0, tempo) : 120.0;
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("outputGain")->load()));
    std::array<double, 2> pitch {}, stretch {}, bendDecay {}, tremStep {};
    std::array<float, 2> tremDepth {}, start {}, end {}, bend {};
    std::array<bool, 2> reverse {}, auditionNow {}, enabled {};
    const auto declickFrames = juce::jmax (1, juce::roundToInt (currentSampleRate * 0.003));
    const auto retire = [&] (size_t layer, Voice& voice)
    {
        if (voice.active && (voice.lastOutput[0] != 0.0f || voice.lastOutput[1] != 0.0f))
        {
            const auto oldScale = (float) retiringFrames[layer] / (float) declickFrames;
            for (size_t ch = 0; ch < 2; ++ch)
                retiringOutput[layer][ch] = retiringOutput[layer][ch] * oldScale + voice.lastOutput[ch];
            retiringFrames[layer] = declickFrames;
        }
        voice.active = false;
        voice.envelope.reset();
    };
    for (size_t layer = 0; layer < 2; ++layer)
    {
        auto sample = std::atomic_load (&loadedSamples[layer]);
        if (sample != playingSamples[layer])
        {
            for (auto& v : voices[layer]) retire (layer, v);
            playingSamples[layer] = std::move (sample);
        }
        const auto& ep = envelopeParams[layer];
        const juce::ADSR::Parameters adsr { ep[0]->load(), ep[1]->load(), ep[2]->load(), ep[3]->load() };
        for (auto& v : voices[layer]) v.envelope.setParameters (adsr);
        const auto& p = sourceParams[layer];
        enabled[layer] = p[9]->load() > 0.5f;
        sourcePower[layer].setTargetValue (enabled[layer] ? 1.0f : 0.0f);
        pitch[layer] = std::exp2 (p[0]->load() / 12.0);
        stretch[layer] = p[1]->load();
        bend[layer] = p[2]->load();
        tremDepth[layer] = p[3]->load() * 0.01f;
        layerGain[layer].setTargetValue (juce::Decibels::decibelsToGain (p[4]->load()));
        start[layer] = p[5]->load();
        end[layer] = p[6]->load();
        reverse[layer] = p[7]->load() > 0.5f;
        tremStep[layer] = bpm / 60.0 * (1 << (int) p[8]->load()) / currentSampleRate;
        bendDecay[layer] = std::exp (-1.0 / (currentSampleRate * 0.055));
        auditionNow[layer] = auditionPending[layer].exchange (false);
    }

    const auto offsetMs = parameters.getRawParameterValue ("layerOffsetMs")->load();
    const auto continuous = parameters.getRawParameterValue ("duckTrigger")->load() > 0.5f;
    sync = sync && continuous;
    const auto duck = parameters.getRawParameterValue ("duckOn")->load() > 0.5f
        ? parameters.getRawParameterValue ("duckDepth")->load() * 0.01f : 0.0f;
    const auto shape = (int) parameters.getRawParameterValue ("duckShape")->load();
    const std::array<float, 3> duckLevels { parameters.getRawParameterValue ("duckLevel0")->load(),
        parameters.getRawParameterValue ("duckLevel1")->load(), parameters.getRawParameterValue ("duckLevel2")->load() };
    const auto handles = duckHandles ({ parameters.getRawParameterValue ("duckEarly")->load(),
        parameters.getRawParameterValue ("duckMidpoint")->load(), parameters.getRawParameterValue ("duckLate")->load() });
    const auto route = (int) parameters.getRawParameterValue ("duckRoute")->load();
    const auto rateIndex = juce::jlimit (0, 5, (int) parameters.getRawParameterValue ("duckRate")->load());
    const auto period = std::array<double, 6> { 0.5, 1.0, 2.0, beatsPerBar, 2.0 * beatsPerBar, 4.0 * beatsPerBar }
        [(size_t) rateIndex];
    if (sync && rateIndex >= 3 && lastBar && std::isfinite (*lastBar))
    {
        const auto bars = 1 << (rateIndex - 3);
        const auto barNumber = hostBarCount ? (double) *hostBarCount : std::floor (*lastBar / beatsPerBar);
        const auto inCycle = barNumber - bars * std::floor (barNumber / bars);
        beat = inCycle * beatsPerBar + (beat - *lastBar);
    }
    const bool paired = enabled[0] && enabled[1] && playingSamples[0] && playingSamples[1];
    const auto trigger = [&] (size_t layer, int note, float velocity, int delay = 0, int channel = 0)
    {
        const auto& sample = playingSamples[layer];
        if (sample == nullptr || ! enabled[layer]) return;
        auto& bank = voices[layer];
        auto voice = std::find_if (bank.begin(), bank.end(), [] (const Voice& v) { return ! v.active; });
        if (voice == bank.end())
        {
            voice = std::min_element (bank.begin(), bank.end(), [] (const Voice& a, const Voice& b) { return a.serial < b.serial; });
            retire (layer, *voice);
        }
        const auto frames = sample->audio.getNumSamples();
        voice->firstFrame = juce::jlimit (0, frames - 1, (int) (start[layer] * frames));
        voice->endFrame = juce::jlimit (voice->firstFrame + 1, frames, (int) (end[layer] * frames));
        voice->midiNote = note;
        voice->midiChannel = channel;
        voice->keyHeld = channel != 0;
        voice->serial = ++voiceSerial;
        voice->lastOutput = {};
        voice->velocity = velocity;
        voice->playbackRate = std::exp2 ((note - 60) / 12.0) * sample->sampleRate / currentSampleRate;
        voice->samplePosition = voice->elapsed = voice->tremPhase = 0.0;
        voice->stretchRatio = stretch[layer];
        voice->grainRead = {};
        voice->grainFrame = 0;
        voice->onsetDelay = delay;
        voice->bendMultiplier = std::exp2 (bend[layer] / 12.0);
        voice->reverse = reverse[layer];
        voice->active = true;
        voice->envelope.reset();
        voice->envelope.noteOn();
    };
    for (size_t layer = 0; layer < 2; ++layer)
        if (auditionNow[layer]) trigger (layer, 60, 0.85f);

    std::array<float, 2> blockEnvelopePeak {};
    auto event = midi.begin();
    auto duckEvent = midi.begin();
    if (duck == 0.0f || continuous) duckNotePhase = 1.0;
    else if (auditionNow[0] || auditionNow[1]) duckNotePhase = 0.0;
    const auto channels = buffer.getNumChannels();
    const auto grainHop = juce::jmax (8, juce::roundToInt (currentSampleRate * 0.024));
    const auto maxFrames = layerBuffers[0].getNumSamples();
    for (int offset = 0; offset < buffer.getNumSamples(); offset += maxFrames)
    {
        const auto count = juce::jmin (maxFrames, buffer.getNumSamples() - offset);
        juce::AudioBuffer<float> a (layerBuffers[0].getArrayOfWritePointers(), channels, count);
        juce::AudioBuffer<float> b (layerBuffers[1].getArrayOfWritePointers(), channels, count);
        a.clear(); b.clear();
        const std::array<juce::AudioBuffer<float>*, 2> layers { &a, &b };
        for (int frame = 0; frame < count; ++frame)
        {
            while (event != midi.end() && (*event).samplePosition <= offset + frame)
            {
                const auto m = (*event).getMessage();
                if (m.isNoteOn())
                {
                    for (size_t layer = 0; layer < 2; ++layer)
                        trigger (layer, m.getNoteNumber(), m.getFloatVelocity(), paired
                            ? juce::roundToInt (juce::jmax (0.0f, layer == 0 ? -offsetMs : offsetMs) * currentSampleRate * 0.001)
                            : 0, m.getChannel());
                    lastNote.store (m.getNoteNumber(), std::memory_order_relaxed);
                    lastVelocity.store (m.getFloatVelocity(), std::memory_order_relaxed);
                    noteCounter.fetch_add (1, std::memory_order_relaxed);
                }
                else if (m.isNoteOff() || m.isAllNotesOff() || m.isAllSoundOff())
                {
                    for (auto& bank : voices)
                    {
                        Voice* oldest = nullptr;
                        for (auto& voice : bank)
                            if (voice.active && voice.midiChannel == m.getChannel())
                            {
                                if (m.isNoteOff())
                                {
                                    if (voice.keyHeld && voice.midiNote == m.getNoteNumber()
                                        && (oldest == nullptr || voice.serial < oldest->serial))
                                        oldest = &voice;
                                }
                                else
                                {
                                    voice.keyHeld = false;
                                    if (m.isAllSoundOff() || voice.onsetDelay > 0) { voice.envelope.reset(); voice.active = false; }
                                    else voice.envelope.noteOff();
                                }
                            }
                        if (oldest != nullptr)
                        {
                            oldest->keyHeld = false;
                            if (oldest->onsetDelay > 0) { oldest->envelope.reset(); oldest->active = false; }
                            else oldest->envelope.noteOff();
                        }
                    }
                }
                ++event;
            }
            for (size_t layer = 0; layer < 2; ++layer)
            {
                const auto gain = layerGain[layer].getNextValue();
                if (retiringFrames[layer] > 0)
                {
                    const auto fade = (float) retiringFrames[layer] / (float) declickFrames;
                    for (int ch = 0; ch < channels; ++ch)
                        layers[layer]->addSample (ch, frame, retiringOutput[layer][(size_t) ch] * fade);
                    --retiringFrames[layer];
                }
                const auto& sample = playingSamples[layer];
                if (! sample) continue;
                for (auto& v : voices[layer])
                {
                    if (! v.active) continue;
                    if (v.onsetDelay > 0) { --v.onsetDelay; continue; }
                    const auto length = v.endFrame - v.firstFrame;
                    if (v.samplePosition >= length || ! v.envelope.isActive())
                    {
                        v.envelope.reset(); v.active = false; continue;
                    }
                    const auto env = v.envelope.getNextSample() * v.velocity;
                    blockEnvelopePeak[layer] = juce::jmax (blockEnvelopePeak[layer], env);
                    const auto tremolo = tremDepth[layer] == 0.0f ? 1.0f
                        : 1.0f - tremDepth[layer] * (0.5f - 0.5f * (float) std::cos (v.tremPhase * juce::MathConstants<double>::twoPi));
                    v.tremPhase += tremStep[layer];
                    if (v.tremPhase >= 1.0) v.tremPhase -= 1.0;
                    const auto rate = v.playbackRate * pitch[layer] * v.bendMultiplier;
                    const auto transport = rate / v.stretchRatio;
                    v.bendMultiplier = 1.0 + (v.bendMultiplier - 1.0) * bendDecay[layer];

                    const bool stretched = std::abs (v.stretchRatio - 1.0) > 0.0001;
                    const auto read = [&] (int ch, double position)
                    {
                        const auto p = juce::jlimit (0.0, (double) length - 1.0, position);
                        const auto index = (int) p;
                        const auto i0 = v.reverse ? v.endFrame - 1 - index : v.firstFrame + index;
                        const auto i1 = v.reverse ? juce::jmax (v.firstFrame, i0 - 1) : juce::jmin (v.endFrame - 1, i0 + 1);
                        const auto s0 = sample->audio.getSample (ch, i0);
                        return s0 + (sample->audio.getSample (ch, i1) - s0) * (float) (p - index);
                    };
                    float weight = 1.0f;
                    if (stretched)
                    {
                        // Start on the original attack. Replace each read head
                        // only at zero weight, searching near its expected
                        // position for a phase-compatible overlap.
                        if (v.grainFrame == 0 || v.grainFrame == grainHop)
                        {
                            const auto incoming = (size_t) (v.grainFrame == 0 ? 1 : 0);
                            const auto outgoing = 1u - incoming;
                            const auto middle = rate * grainHop * 0.5;
                            const auto nominal = v.samplePosition - (rate - transport) * grainHop;
                            const auto range = sample->sampleRate * 0.012;
                            const auto tapSpacing = juce::jmax (1.0, rate * grainHop / 22.0);
                            constexpr int taps = 12;
                            std::array<float, taps> reference {};
                            const auto matchSample = [&] (double at)
                            {
                                const auto left = read (0, at);
                                return sample->audio.getNumChannels() > 1 ? 0.5f * (left + read (1, at)) : left;
                            };
                            float referencePower = 0.0f;
                            for (int i = 0; i < taps; ++i)
                            {
                                const auto delta = (i - (taps - 1) * 0.5) * tapSpacing;
                                reference[(size_t) i] = matchSample (v.grainRead[outgoing] + middle + delta);
                                referencePower += reference[(size_t) i] * reference[(size_t) i];
                            }
                            double best = juce::jlimit (0.0, (double) length - 1.0, nominal);
                            float bestScore = -2.0f;
                            if (referencePower > 1.0e-7f)
                                for (int lag = -12; lag <= 12; ++lag)
                                {
                                    const auto candidate = nominal + range * lag / 12.0;
                                    if (candidate < 0.0 || candidate + middle >= length) continue;
                                    float dot = 0.0f, power = 0.0f;
                                    for (int i = 0; i < taps; ++i)
                                    {
                                        const auto delta = (i - (taps - 1) * 0.5) * tapSpacing;
                                        const auto value = matchSample (candidate + middle + delta);
                                        dot += value * reference[(size_t) i];
                                        power += value * value;
                                    }
                                    if (power < 1.0e-7f) continue;
                                    const auto score = dot / std::sqrt (referencePower * power) - 0.055f * std::abs (lag) / 12.0f;
                                    if (score > bestScore) { best = candidate; bestScore = score; }
                                }
                            v.grainRead[incoming] = best;
                        }
                        weight = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) v.grainFrame / grainHop);
                        // Keep the very start single-headed, then ease into the overlap.
                        const auto onset = juce::jlimit (0.0f, 1.0f, (float) ((v.elapsed / currentSampleRate - 0.008) / 0.008));
                        weight = 1.0f - (1.0f - weight) * onset * onset * (3.0f - 2.0f * onset);
                    }
                    const auto fadeLength = juce::jmax (1.0, juce::jmin (length * 0.25, transport * currentSampleRate * 0.005));
                    const auto edge = (float) juce::jlimit (0.0, 1.0, (length - v.samplePosition) / fadeLength);
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        const auto src = juce::jmin (ch, sample->audio.getNumChannels() - 1);
                        const auto value = stretched ? read (src, v.grainRead[0]) * weight + read (src, v.grainRead[1]) * (1.0f - weight)
                                                     : read (src, v.samplePosition);
                        const auto output = value * gain * env * tremolo * edge;
                        layers[layer]->addSample (ch, frame, output);
                        v.lastOutput[(size_t) ch] = output;
                    }
                    v.samplePosition += transport;
                    if (stretched)
                    {
                        for (auto& head : v.grainRead) head += rate;
                        if (++v.grainFrame == 2 * grainHop) v.grainFrame = 0;
                    }
                    v.elapsed += 1.0;
                }
            }
        }

        for (int layer = 0; layer < 2; ++layer)
            if (playingSamples[(size_t) layer])
                racks[(size_t) layer].process (*this, *layers[(size_t) layer], bpm, layer);
        for (size_t layer = 0; layer < 2; ++layer)
        {
            if (sourcePower[layer].isSmoothing())
            {
                for (int frame = 0; frame < count; ++frame)
                {
                    const auto power = sourcePower[layer].getNextValue();
                    for (int ch = 0; ch < channels; ++ch)
                        layers[layer]->setSample (ch, frame, layers[layer]->getSample (ch, frame) * power);
                }
            }
            else if (! enabled[layer])
                layers[layer]->clear();
            if (! enabled[layer] && ! sourcePower[layer].isSmoothing())
                for (auto& voice : voices[layer]) { voice.active = false; voice.envelope.reset(); }
        }
        const auto step = bpm / (60.0 * currentSampleRate * period);
        auto phase = sync ? beat / period + offset * step : duckFreePhase;
        phase -= std::floor (phase);
        if (duck > 0 || duckGain[0] != 1.0f || duckGain[1] != 1.0f)
        {
            const auto smooth = (float) (1.0 - std::exp (-1.0 / (0.002 * currentSampleRate)));
            for (int frame = 0; frame < count; ++frame)
            {
                if (! continuous && duck > 0.0f)
                    while (duckEvent != midi.end() && (*duckEvent).samplePosition <= offset + frame)
                    {
                        if ((*duckEvent).getMessage().isNoteOn()) duckNotePhase = 0.0;
                        ++duckEvent;
                    }
                const auto position = continuous ? phase : duckNotePhase;
                const auto gain = position < 1.0
                    ? 1.0f - duck * (1.0f - editedDuckCurve (duckWarp ((float) position, handles), shape, duckLevels))
                    : 1.0f;
                for (size_t layer = 0; layer < 2; ++layer)
                {
                    const auto target = route == 2 || route == (int) layer ? gain : 1.0f;
                    auto& g = duckGain[layer];
                    g += smooth * (target - g);
                    if (std::abs (target - g) < 0.00001f) g = target;
                    for (int ch = 0; ch < channels; ++ch)
                        layers[layer]->getWritePointer (ch)[frame] *= g;
                }
                phase += step;
                if (phase >= 1.0) phase -= 1.0;
                if (! continuous && duckNotePhase < 1.0) duckNotePhase = juce::jmin (1.0, duckNotePhase + step);
            }
        }
        else
            phase += count * step;
        duckFreePhase = phase - std::floor (phase);
        for (int ch = 0; ch < channels; ++ch)
        {
            buffer.copyFrom (ch, offset, a, ch, 0, count);
            buffer.addFrom (ch, offset, b, ch, 0, count);
        }
    }

    int liveVoices = 0;
    float progress = 0.0f;
    for (size_t layer = 0; layer < 2; ++layer)
    {
        float position = -1.0f;
        const auto& sample = playingSamples[layer];
        if (sample)
            for (const auto& v : voices[layer])
                if (v.active)
                {
                    ++liveVoices;
                    const auto length = v.endFrame - v.firstFrame;
                    const auto played = juce::jlimit (0.0, (double) length, v.samplePosition);
                    if (position < 0.0f)
                        position = (float) ((v.reverse ? v.endFrame - played : v.firstFrame + played) / sample->audio.getNumSamples());
                    progress = juce::jmax (progress, (float) (played / length));
                }
        layerProgress[layer].store (position);
    }
    activeVoiceCount.store (liveVoices);
    playbackProgress.store (progress);
    for (size_t layer = 0; layer < 2; ++layer)
        envelopeLevel[layer].store (enabled[layer] ? blockEnvelopePeak[layer] : 0.0f);

    // Apply master gain once per frame, to the complete post-FX stereo sum.
    // Link protection across channels to avoid shifting the stereo image.
    constexpr float ceiling = 0.9660508f; // -0.3 dBFS, sample peak only.
    constexpr float knee = 0.90f;
    float minimumProtectionGain = 1.0f;
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
    {
        const auto master = outputGain.getNextValue();
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto x = buffer.getSample (ch, frame) * master;
            const auto finite = std::isfinite (x) ? x : 0.0f;
            buffer.setSample (ch, frame, finite);
            peak = juce::jmax (peak, std::abs (finite));
        }
        if (peak > knee)
        {
            const auto target = knee + (ceiling - knee) * std::tanh ((peak - knee) / (ceiling - knee));
            const auto gain = target / peak;
            minimumProtectionGain = juce::jmin (minimumProtectionGain, gain);
            for (int ch = 0; ch < channels; ++ch)
                buffer.setSample (ch, frame, juce::jlimit (-ceiling, ceiling, buffer.getSample (ch, frame) * gain));
        }
    }
    const auto reduction = -juce::Decibels::gainToDecibels (minimumProtectionGain);
    const auto previous = protectionReductionDb.load (std::memory_order_relaxed);
    const auto meterDecay = std::exp (-(float) buffer.getNumSamples() / (0.18f * (float) currentSampleRate));
    protectionReductionDb.store (juce::jmax (reduction, previous * meterDecay), std::memory_order_relaxed);
    outputLevel.store (juce::jlimit (0.0f, 1.0f, buffer.getMagnitude (0, buffer.getNumSamples())));

    // Feed the editor's scope and meters. Mono mix into a ring buffer; the
    // editor reads it without locks and finds its own trigger point.
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();
    if (numChannels > 0 && numSamples > 0)
    {
        const auto* left = buffer.getReadPointer (0);
        const auto* right = buffer.getReadPointer (numChannels > 1 ? 1 : 0);
        auto write = scopeWritePosition.load (std::memory_order_relaxed);
        float blockPeakLeft = 0.0f, blockPeakRight = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            blockPeakLeft = juce::jmax (blockPeakLeft, std::abs (left[i]));
            blockPeakRight = juce::jmax (blockPeakRight, std::abs (right[i]));
            scopeBuffer[(size_t) write].store (0.5f * (left[i] + right[i]), std::memory_order_relaxed);
            write = (write + 1) & (scopeBufferSize - 1);
        }
        scopeWritePosition.store (write, std::memory_order_release);
        peakLeft.store (juce::jlimit (0.0f, 1.0f, blockPeakLeft), std::memory_order_relaxed);
        peakRight.store (juce::jlimit (0.0f, 1.0f, blockPeakRight), std::memory_order_relaxed);
    }
}

void OneShotSamplerAudioProcessor::copyScopeSamples (float* destination, int numberOfSamples) const noexcept
{
    numberOfSamples = juce::jmin (numberOfSamples, scopeBufferSize);
    auto read = (scopeWritePosition.load (std::memory_order_acquire) - numberOfSamples + scopeBufferSize) & (scopeBufferSize - 1);
    for (int i = 0; i < numberOfSamples; ++i)
    {
        destination[i] = scopeBuffer[(size_t) read].load (std::memory_order_relaxed);
        read = (read + 1) & (scopeBufferSize - 1);
    }
}

juce::String OneShotSamplerAudioProcessor::sourceParamId (int layer, const char* suffix)
{
    return juce::String (layer == 0 ? "sourceA" : "sourceB") + suffix;
}

juce::String OneShotSamplerAudioProcessor::envelopeParamId (int layer, const char* stage)
{
    return layer == 0 ? juce::String (stage) : sourceParamId (1, stage);
}

juce::String OneShotSamplerAudioProcessor::fxParamId (int layer, const juce::String& id)
{
    return layer == 1 ? "fxB_" + id : id;
}

juce::StringArray OneShotSamplerAudioProcessor::duckShapeNames()
{
    return { "Tight", "Pump", "Hold", "Snap", "Ramp", "Slow rise", "Late lift", "Soft knee",
             "Fast tail", "Long tail", "Step up", "Two stage", "Double dip", "Triple dip", "Offbeat", "Reverse" };
}

float OneShotSamplerAudioProcessor::duckCurve (float phase, int shape) noexcept
{
    const auto unit = [] (float t) { return juce::jlimit (0.0f, 1.0f, t); };
    const auto ease = [&] (float t) { t = unit (t); return t * t * (3.0f - 2.0f * t); };
    struct Rise { float hold, length; };
    constexpr std::array<Rise, 4> classic {{ { 0, .42f }, { 0, .90f }, { .18f, .72f }, { 0, .21f } }};
    const auto t = unit (phase);
    switch (shape)
    {
        case 4: return unit (t / .78f);
        case 5: { const auto x = unit (t / .98f); return x * x; }
        case 6: return ease ((t - .46f) / .43f);
        case 7: return .22f + .78f * ease (t / .68f);
        case 8: { const auto x = 1.0f - unit (t / .48f); return 1.0f - x * x * x; }
        case 9: { const auto x = 1.0f - t; return 1.0f - x * x; }
        case 10: return .46f * ease (t / .15f) + .54f * ease ((t - .48f) / .14f);
        case 11: return .64f * ease (t / .30f) + .36f * ease ((t - .32f) / .64f);
        case 12: return t < .50f ? ease (t / .39f) * (1 - .68f * ease ((t - .39f) / .11f))
                                 : .32f + .68f * ease ((t - .50f) / .37f);
        case 13: { const auto x = t * 3.0f; return ease (x - std::floor (x)); }
        case 14: return 1 - ease ((t - .30f) / .16f) * (1 - ease ((t - .46f) / .42f));
        case 15: return 1 - ease ((t - .15f) / .67f);
        default: { const auto r = classic[(size_t) juce::jlimit (0, 3, shape)]; return ease ((t - r.hold) / r.length); }
    }
}

int OneShotSamplerAudioProcessor::getEffectLayer (int effect) const noexcept
{
    return juce::isPositiveAndBelow (effect, (int) routeParams.size()) && routeParams[(size_t) effect]->load() >= 0.5f ? 1 : 0;
}

bool OneShotSamplerAudioProcessor::isLayerFxOn (int effect, int layer, bool legacy) const noexcept
{
    if (! juce::isPositiveAndBelow (effect, 10) || ! juce::isPositiveAndBelow (layer, 2)) return false;
    const auto state = (int) layerFxOn[(size_t) layer][(size_t) effect]->load();
    return state == 2 || (state == 0 && legacy && getEffectLayer (effect) == layer);
}

juce::String OneShotSamplerAudioProcessor::routeParamId (int effect)
{
    return "effectSource" + juce::String (effect);
}

bool OneShotSamplerAudioProcessor::loadSample (const juce::File& file, int layer)
{
    if (layer < 0 || layer > 1) return false;
    const juce::ScopedLock lock (sampleLoadLock);
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > 16000000
        || ! std::isfinite (reader->sampleRate) || reader->sampleRate <= 0.0)
        return false;

    auto sample = std::make_shared<SampleData>();
    sample->audio.setSize (juce::jlimit (1, 2, (int) reader->numChannels), (int) reader->lengthInSamples);
    if (! reader->read (&sample->audio, 0, sample->audio.getNumSamples(), 0, true, true))
        return false;
    for (int ch = 0; ch < sample->audio.getNumChannels(); ++ch)
    {
        auto* data = sample->audio.getWritePointer (ch);
        for (int i = 0; i < sample->audio.getNumSamples(); ++i)
            data[i] = std::isfinite (data[i]) ? juce::jlimit (-32.0f, 32.0f, data[i]) : 0.0f;
    }
    sample->sampleRate = reader->sampleRate;
    sample->name = file.getFileName();
    const auto frames = sample->audio.getNumSamples();
    const auto bins = juce::jmin (512, frames);
    sample->peaks.resize ((size_t) bins);
    for (int bin = 0; bin < bins; ++bin)
    {
        const auto first = (int) ((int64_t) frames * bin / bins);
        const auto last = (int) ((int64_t) frames * (bin + 1) / bins);
        for (int ch = 0; ch < sample->audio.getNumChannels(); ++ch)
            sample->peaks[(size_t) bin] = juce::jmax (sample->peaks[(size_t) bin], sample->audio.getMagnitude (ch, first, last - first));
    }

    if (auto old = std::atomic_exchange (&loadedSamples[(size_t) layer], std::move (sample)))
        retiredSamples.push_back (std::move (old));
    retiredSamples.erase (std::remove_if (retiredSamples.begin(), retiredSamples.end(),
        [] (const auto& old) { return old.use_count() == 1; }), retiredSamples.end());
    parameters.state.setProperty (layer == 0 ? "sampleFilePath" : "sampleFilePathB", file.getFullPathName(), nullptr);
    ++sampleRevision[(size_t) layer];
    return true;
}

void OneShotSamplerAudioProcessor::clearSample (int layer, bool forgetPath)
{
    if (layer < 0 || layer > 1) return;
    const juce::ScopedLock lock (sampleLoadLock);
    if (auto old = std::atomic_exchange (&loadedSamples[(size_t) layer], std::shared_ptr<SampleData>()))
        retiredSamples.push_back (std::move (old));
    retiredSamples.erase (std::remove_if (retiredSamples.begin(), retiredSamples.end(),
        [] (const auto& old) { return old.use_count() == 1; }), retiredSamples.end());
    if (forgetPath) parameters.state.removeProperty (layer == 0 ? "sampleFilePath" : "sampleFilePathB", nullptr);
    ++sampleRevision[(size_t) layer];
}

juce::String OneShotSamplerAudioProcessor::getSampleName (int layer) const
{
    if (const auto sample = std::atomic_load (&loadedSamples[(size_t) layer]))
        return sample->name;
    return {};
}

juce::String OneShotSamplerAudioProcessor::getMissingSampleName (int layer) const
{
    if (layer < 0 || layer > 1 || hasLoadedSample (layer)) return {};
    const auto path = parameters.state.getProperty (layer == 0 ? "sampleFilePath" : "sampleFilePathB").toString();
    return path.isNotEmpty() ? juce::File (path).getFileName() : juce::String();
}

bool OneShotSamplerAudioProcessor::hasLoadedSample (int layer) const
{
    return std::atomic_load (&loadedSamples[(size_t) layer]) != nullptr;
}

std::vector<float> OneShotSamplerAudioProcessor::getWaveformPeaks (int layer) const
{
    if (const auto sample = std::atomic_load (&loadedSamples[(size_t) layer]))
        return sample->peaks;
    return {};
}

bool OneShotSamplerAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* OneShotSamplerAudioProcessor::createEditor()
{
    return new OneShotSamplerAudioProcessorEditor (*this);
}

void OneShotSamplerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto state = parameters.copyState(); state.isValid())
        copyXmlToBinary (*state.createXml(), destData);
}

void OneShotSamplerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            for (const auto* stage : { "attack", "decay", "sustain", "release" })
            {
                const auto id = envelopeParamId (1, stage);
                const auto a = state.getChildWithProperty ("id", stage);
                if (! state.getChildWithProperty ("id", id).isValid() && a.isValid())
                {
                    auto b = a.createCopy();
                    b.setProperty ("id", id, nullptr);
                    state.appendChild (b, nullptr);
                }
            }
            std::array<bool, 10> formerBoth {};
            for (int effect = 0; effect < (int) routeParams.size(); ++effect)
                if (auto route = state.getChildWithProperty ("id", routeParamId (effect)); route.isValid()
                    && (int) route.getProperty ("value") == 2)
                {
                    formerBoth[(size_t) effect] = true;
                    route.setProperty ("value", 0, nullptr);
                }
            const auto originalCount = state.getNumChildren();
            for (int i = 0; i < originalCount; ++i)
            {
                const auto original = state.getChild (i);
                const auto id = original.getProperty ("id").toString();
                const auto bId = fxParamId (1, id);
                if (parameters.getParameter (bId) != nullptr && ! state.getChildWithProperty ("id", bId).isValid())
                {
                    auto copy = original.createCopy();
                    copy.setProperty ("id", bId, nullptr);
                    state.appendChild (copy, nullptr);
                }
            }
            for (int band = 1; band <= 7; ++band)
            {
                const auto type = "eqBand" + juce::String (band) + "Type";
                if (! state.hasProperty (fxParamId (1, type)) && state.hasProperty (type))
                    state.setProperty (fxParamId (1, type), state.getProperty (type), nullptr);
            }
            for (int layer = 0; layer < 2; ++layer)
            {
                const auto id = fxParamId (layer, "chorusDrive");
                if (! state.getChildWithProperty ("id", id).isValid())
                {
                    juce::ValueTree drive ("PARAM");
                    drive.setProperty ("id", id, nullptr);
                    drive.setProperty ("value", 0.0f, nullptr);
                    state.appendChild (drive, nullptr);
                }
            }
            parameters.replaceState (state);
            if ((int) parameters.state.getProperty ("fxLayoutVersion", 0) < 7)
            {
                std::array<bool, 10> active {};
                for (int slot = 0; slot < 7; ++slot)
                {
                    const auto effect = getFxSlotEffect (slot);
                    if (juce::isPositiveAndBelow (effect, 7)) active[(size_t) effect] |= getFxSlotEnabled (slot);
                }
                const std::array<const char*, 3> textureIds { "granularEnabled", "harmonyEnabled", "randomEnabled" };
                for (size_t i = 0; i < textureIds.size(); ++i)
                    active[i + 7] = parameters.getRawParameterValue (textureIds[i])->load() > 0.5f;
                for (int slot = 0; slot < 7; ++slot) setFxSlotState (slot, slot, active[(size_t) slot]);
                for (int effect = 0; effect < (int) formerBoth.size(); ++effect)
                    if (formerBoth[(size_t) effect])
                        for (int layer = 0; layer < 2; ++layer)
                            if (auto* p = parameters.getParameter (fxParamId (layer, "layerFxOn" + juce::String (effect))); p && p->getValue() == 0.0f)
                                p->setValueNotifyingHost (p->convertTo0to1 (active[(size_t) effect] ? 2.0f : 1.0f));
            }
            parameters.state.setProperty ("fxLayoutVersion", 7, nullptr);
            refreshEqTypes();
            for (int layer = 0; layer < 2; ++layer)
            {
                const auto path = parameters.state.getProperty (layer == 0 ? "sampleFilePath" : "sampleFilePathB").toString();
                if (path.isEmpty() || ! loadSample (juce::File (path), layer))
                    clearSample (layer, false);
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OneShotSamplerAudioProcessor();
}
