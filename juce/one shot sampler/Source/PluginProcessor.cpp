#include "PluginProcessor.h"
#include "PluginEditor.h"

OneShotSamplerAudioProcessor::OneShotSamplerAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    formatManager.registerBasicFormats();
    // Rack slots are real parameters as well as UI state. This guarantees a
    // DAW restores the rack exactly when an editor window is recreated.
    parameters.state.setProperty ("fxLayoutVersion", 6, nullptr);

    // The compact EQ macros control the first, fourth and seventh bands of
    // the same seven-band engine used by the detailed EQ editor.  Start those
    // anchors as shelves so LOW and AIR behave like useful mastering macros.
    parameters.state.setProperty ("eqBand1Type", 4, nullptr); // low shelf
    parameters.state.setProperty ("eqBand4Type", 0, nullptr); // bell
    parameters.state.setProperty ("eqBand7Type", 5, nullptr); // high shelf

    for (int band = 0; band < 7; ++band)
    {
        const auto prefix = "eqBand" + juce::String (band + 1);
        eqParamPointers[(size_t) band] = { parameters.getRawParameterValue (prefix + "Freq"),
                                           parameters.getRawParameterValue (prefix + "Gain"),
                                           parameters.getRawParameterValue (prefix + "Q") };
    }
    for (int layer = 0; layer < 2; ++layer)
    {
        const std::array<const char*, 10> ids { "Pitch", "Stretch", "Bend", "Tremolo", "Level", "Start", "End", "Reverse", "TremRate", "Enabled" };
        const std::array<const char*, 4> stages { "attack", "decay", "sustain", "release" };
        for (size_t index = 0; index < stages.size(); ++index)
            envelopeParams[(size_t) layer][index] = parameters.getRawParameterValue (envelopeParamId (layer, stages[index]));
        for (size_t index = 0; index < ids.size(); ++index)
            sourceParams[(size_t) layer][index] = parameters.getRawParameterValue (sourceParamId (layer, ids[index]));
    }
    for (size_t i = 0; i < routeParams.size(); ++i)
        routeParams[i] = parameters.getRawParameterValue (routeParamId ((int) i));
    refreshEqTypes();
    parameters.state.addListener (this);
}

OneShotSamplerAudioProcessor::~OneShotSamplerAudioProcessor()
{
    parameters.state.removeListener (this);
}

void OneShotSamplerAudioProcessor::refreshEqTypes()
{
    for (int band = 0; band < 7; ++band)
        eqBandTypes[(size_t) band].store ((int) parameters.state.getProperty ("eqBand" + juce::String (band + 1) + "Type",
                                                                           band == 0 ? 4 : band == 6 ? 5 : 0));
}

void OneShotSamplerAudioProcessor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    const auto name = property.toString();
    if (name.startsWith ("eqBand") && name.endsWith ("Type"))
        refreshEqTypes();
}

juce::AudioProcessorValueTreeState::ParameterLayout OneShotSamplerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

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

    return layout;
}

const juce::String OneShotSamplerAudioProcessor::getName() const { return JucePlugin_Name; }
int OneShotSamplerAudioProcessor::getFxSlotEffect (int slot) const
{
    if (slot < 0 || slot >= 7)
        return -1;

    // Choice index 0 means an empty rack card; the seven effect choices map
    // directly to internal effects 0–6.
    const auto id = "rackSlot" + juce::String (slot + 1) + "Effect";
    const auto choice = parameters.getRawParameterValue (id)->load();
    return juce::jlimit (-1, 6, juce::roundToInt (choice) - 1);
}

bool OneShotSamplerAudioProcessor::getFxSlotEnabled (int slot) const
{
    if (slot < 0 || slot >= 7)
        return false;

    return parameters.getRawParameterValue ("rackSlot" + juce::String (slot + 1) + "Enabled")->load() > 0.5f;
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
double OneShotSamplerAudioProcessor::getTailLengthSeconds() const { return 4.0; }
int OneShotSamplerAudioProcessor::getNumPrograms() { return 1; }
int OneShotSamplerAudioProcessor::getCurrentProgram() { return 0; }
void OneShotSamplerAudioProcessor::setCurrentProgram (int) {}
const juce::String OneShotSamplerAudioProcessor::getProgramName (int) { return {}; }
void OneShotSamplerAudioProcessor::changeProgramName (int, const juce::String&) {}

void OneShotSamplerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
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
    reverbToneState = {};
    reverbHighPassState = {};
    const auto modulationBufferLength = static_cast<int> (sampleRate * 0.085) + 4;
    chorusBuffer.setSize (2, modulationBufferLength); chorusBuffer.clear();
    flangerBuffer.setSize (2, modulationBufferLength); flangerBuffer.clear();
    flutterBuffer.setSize (2, modulationBufferLength); flutterBuffer.clear();
    chorusWritePosition = flangerWritePosition = flutterWritePosition = 0;
    chorusPhase = flangerPhase = 0.0f;
    flutterWowPhase = flutterDriftPhase = 0.0f;
    for (auto* smoother : { &delayMixSmooth, &reverbMixSmooth, &chorusMixSmooth, &flangerMixSmooth, &flutterMixSmooth })
        smoother->reset (sampleRate, 0.03);
    delayTimeSmooth.reset (sampleRate, 0.25);
    delayTimeReady = false;
    for (auto& cache : eqCache) cache = { -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f };
    flutterRandom = flutterRandomTarget = 0.0f;
    flutterRandomCountdown = 0;
    chorusFeedbackState = {};
    flangerFeedbackState = {};
    textureBuffer.setSize (2, static_cast<int> (sampleRate * 2.5));
    textureBuffer.clear();
    textureWritePosition = 0;
    grainReadPosition = randomReadPosition = 0.0f;
    textureOrbitPhase = 0.0f;
    grainSamplesUntilReset = randomSamplesUntilReset = 0;
    for (auto* wet : { &granularWet, &harmonyWet, &randomWet })
    {
        wet->reset (sampleRate, 0.035);
        wet->setCurrentAndTargetValue (0.0f);
    }
    for (auto& band : eqBands) for (auto& channel : band) channel.reset();
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
    const auto& eqParamPointers = owner.eqParamPointers;
    const auto& eqBandTypes = owner.eqBandTypes;
    const auto routed = [&] (int effect)
    {
        const auto route = (int) owner.routeParams[(size_t) effect]->load();
        return route == layer;
    };
    juce::dsp::AudioBlock<float> block (buffer);
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
        for (int band = 0; band < 7; ++band)
        {
            const auto b = (size_t) band;
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
            juce::dsp::IIR::Coefficients<float>::Ptr coefficients;
            switch (type) {
                case 1: coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, frequency, q); break;
                case 2: coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, frequency, q); break;
                case 3: coefficients = juce::dsp::IIR::Coefficients<float>::makeNotch (currentSampleRate, frequency, q); break;
                case 4: coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf (currentSampleRate, frequency, q, gain); break;
                case 5: coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (currentSampleRate, frequency, q, gain); break;
                default: coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, frequency, q, gain); break;
            }
            for (size_t channel = 0; channel < 2; ++channel)
                eqBands[b][channel].coefficients = coefficients;
        }
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
            {
                auto value = buffer.getSample (channel, sampleIndex);
                for (int band = 0; band < 7; ++band) value = eqBands[(size_t) band][(size_t) channel].processSample (value);
                buffer.setSample (channel, sampleIndex, value);
            }
    };
    const auto applyDelay = [this, &parameters, currentSampleRate, &buffer, bpm, &readInterpolated]
    {
        const auto targetDelay = (float) juce::jlimit (8, delayBuffer.getNumSamples() - 2, static_cast<int> (currentSampleRate * 60.0 / bpm * parameters.getRawParameterValue ("delayTime")->load()));
        // Time changes glide like a tape delay instead of jumping (no clicks).
        if (! delayTimeReady) { delayTimeSmooth.setCurrentAndTargetValue (targetDelay); delayTimeReady = true; }
        else delayTimeSmooth.setTargetValue (targetDelay);
        const auto feedback = juce::jlimit (0.0f, 0.86f, parameters.getRawParameterValue ("delayFeedback")->load());
        delayMixSmooth.setTargetValue (parameters.getRawParameterValue ("delayMix")->load());
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
    const auto applyReverb = [this, &parameters, currentSampleRate, &buffer]
    {
        juce::dsp::Reverb::Parameters p;
        const auto mix = juce::jlimit (0.0f, 1.0f, parameters.getRawParameterValue ("reverbMix")->load());
        const auto room = parameters.getRawParameterValue ("reverbRoomSize")->load();
        const auto damping = parameters.getRawParameterValue ("reverbDamping")->load();
        p.roomSize = room;
        p.damping = damping;
        p.width = parameters.getRawParameterValue ("reverbWidth")->load();
        p.freezeMode = juce::jmin (0.85f, parameters.getRawParameterValue ("reverbFreeze")->load());
        // Keep the tank generous but leave headroom for stacked processing.
        p.wetLevel = 0.40f + room * 0.16f;
        p.dryLevel = 0.0f;
        reverb.setParameters (p);
        const auto preDelaySamples = juce::jlimit (1, reverbPreDelayBuffer.getNumSamples() - 2, (int) (currentSampleRate * (0.009f + room * 0.070f)));
        const auto toneCoefficient = 0.26f - damping * 0.20f;
        // Remove rumble before the tank. This stops large rooms accumulating
        // low-end energy while preserving the source's direct bass content.
        const auto highPassCoefficient = 0.0040f + room * 0.0045f;
        if (buffer.getNumChannels() == 1)
            reverbWetBuffer.clear (1, 0, buffer.getNumSamples());
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const auto input = buffer.getSample (channel, frame);
                reverbHighPassState[(size_t) channel] += highPassCoefficient * (input - reverbHighPassState[(size_t) channel]);
                const auto tankInput = input - reverbHighPassState[(size_t) channel];
                const auto readPosition = (reverbPreDelayWritePosition - preDelaySamples + reverbPreDelayBuffer.getNumSamples()) % reverbPreDelayBuffer.getNumSamples();
                const auto delayed = reverbPreDelayBuffer.getSample (channel, readPosition);
                reverbPreDelayBuffer.setSample (channel, reverbPreDelayWritePosition, tankInput);
                reverbToneState[(size_t) channel] += toneCoefficient * (delayed - reverbToneState[(size_t) channel]);
                reverbWetBuffer.setSample (channel, frame, reverbToneState[(size_t) channel]);
            }
            reverbPreDelayWritePosition = (reverbPreDelayWritePosition + 1) % reverbPreDelayBuffer.getNumSamples();
        }
        juce::dsp::AudioBlock<float> wetBlock (reverbWetBuffer);
        auto wetView = wetBlock.getSubBlock (0, (size_t) buffer.getNumSamples());
        reverb.process (juce::dsp::ProcessContextReplacing<float> (wetView));
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
    const auto applyChorus = [this, &parameters, currentSampleRate, &buffer, &readInterpolated]
    {
        const auto rate = parameters.getRawParameterValue ("chorusRate")->load();
        const auto depth = parameters.getRawParameterValue ("chorusDepth")->load();
        const auto baseDelay = parameters.getRawParameterValue ("chorusDelay")->load() * 0.001f * (float) currentSampleRate;
        const auto feedback = parameters.getRawParameterValue ("chorusFeedback")->load();
        chorusMixSmooth.setTargetValue (parameters.getRawParameterValue ("chorusMix")->load());
        // Never let the sweep reach below ~2 samples of delay (that read the oldest audio in the buffer).
        const auto depthSamples = juce::jmin (6.0f + depth * (float) currentSampleRate * 0.010f, juce::jmax (0.0f, baseDelay - 2.0f));
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            const auto left = buffer.getSample (0, frame);
            const auto right = buffer.getNumChannels() > 1 ? buffer.getSample (1, frame) : left;
            const auto mix = chorusMixSmooth.getNextValue();
            const auto phaseLeft = chorusPhase;
            const auto phaseRight = chorusPhase + 0.87f;
            const auto leftVoiceA = readInterpolated (chorusBuffer, 0, (float) chorusWritePosition - baseDelay - depthSamples * std::sin (phaseLeft));
            const auto leftVoiceB = readInterpolated (chorusBuffer, 0, (float) chorusWritePosition - baseDelay - depthSamples * std::sin (phaseLeft + 2.13f));
            const auto rightVoiceA = readInterpolated (chorusBuffer, 1, (float) chorusWritePosition - baseDelay - depthSamples * std::sin (phaseRight));
            const auto rightVoiceB = readInterpolated (chorusBuffer, 1, (float) chorusWritePosition - baseDelay - depthSamples * std::sin (phaseRight + 2.13f));
            const auto wetLeft = 0.5f * (leftVoiceA + leftVoiceB);
            const auto wetRight = 0.5f * (rightVoiceA + rightVoiceB);
            chorusFeedbackState[0] = std::tanh (chorusFeedbackState[0] * feedback + wetLeft * 0.35f);
            chorusFeedbackState[1] = std::tanh (chorusFeedbackState[1] * feedback + wetRight * 0.35f);
            chorusBuffer.setSample (0, chorusWritePosition, left + chorusFeedbackState[0] * 0.30f);
            chorusBuffer.setSample (1, chorusWritePosition, right + chorusFeedbackState[1] * 0.30f);
            buffer.setSample (0, frame, left * (1.0f - mix) + wetLeft * mix);
            if (buffer.getNumChannels() > 1) buffer.setSample (1, frame, right * (1.0f - mix) + wetRight * mix);
            chorusWritePosition = (chorusWritePosition + 1) % chorusBuffer.getNumSamples();
            chorusPhase = std::fmod (chorusPhase + juce::MathConstants<float>::twoPi * rate / (float) currentSampleRate, juce::MathConstants<float>::twoPi);
        }
    };
    const auto applyFlanger = [this, &parameters, currentSampleRate, &buffer, &readInterpolated]
    {
        const auto rate = parameters.getRawParameterValue ("flangerRate")->load();
        const auto depth = parameters.getRawParameterValue ("flangerDepth")->load();
        const auto baseDelay = parameters.getRawParameterValue ("flangerDelay")->load() * 0.001f * (float) currentSampleRate;
        const auto feedback = parameters.getRawParameterValue ("flangerFeedback")->load();
        flangerMixSmooth.setTargetValue (parameters.getRawParameterValue ("flangerMix")->load());
        const auto sweepSamples = 2.0f + depth * (float) currentSampleRate * 0.008f;
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            const auto left = buffer.getSample (0, frame);
            const auto right = buffer.getNumChannels() > 1 ? buffer.getSample (1, frame) : left;
            const auto mix = flangerMixSmooth.getNextValue();
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
    const auto applyFlutter = [this, &parameters, currentSampleRate, &buffer, &readInterpolated]
    {
        const auto rate = parameters.getRawParameterValue ("flutterRate")->load();
        const auto depth = parameters.getRawParameterValue ("flutterDepth")->load();
        const auto baseDelay = parameters.getRawParameterValue ("flutterDelay")->load() * 0.001f * (float) currentSampleRate;
        flutterMixSmooth.setTargetValue (parameters.getRawParameterValue ("flutterMix")->load());
        const auto wowRate = 0.12f + rate * 0.10f;
        const auto flutterSamples = depth * (float) currentSampleRate * 0.0042f;
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
    const auto wrapTexturePosition = [this] (float position) { const auto length = (float) textureBuffer.getNumSamples(); while (position < 0.0f) position += length; while (position >= length) position -= length; return position; };
    const auto readTexture = [this, &wrapTexturePosition] (int channel, float position) { position = wrapTexturePosition (position); const auto first = (int) position; const auto second = (first + 1) % textureBuffer.getNumSamples(); const auto fraction = position - first; return textureBuffer.getSample (channel, first) + (textureBuffer.getSample (channel, second) - textureBuffer.getSample (channel, first)) * fraction; };

    // Each Texture Lab engine owns its own state. There is deliberately no
    // hidden master gate: turning Granular, Orbit, or Random on is enough.
    granularWet.setTargetValue (routed (7) && parameters.getRawParameterValue ("granularEnabled")->load() > 0.5f ? parameters.getRawParameterValue ("granularMix")->load() : 0.0f);
    harmonyWet.setTargetValue (routed (8) && parameters.getRawParameterValue ("harmonyEnabled")->load() > 0.5f ? parameters.getRawParameterValue ("harmonyMix")->load() : 0.0f);
    randomWet.setTargetValue (routed (9) && parameters.getRawParameterValue ("randomEnabled")->load() > 0.5f ? parameters.getRawParameterValue ("randomMix")->load() : 0.0f);

    const auto applyTextureLab = [this, &parameters, currentSampleRate, &buffer, &readTexture, &wrapTexturePosition]
    {
        const auto grainSize = parameters.getRawParameterValue ("granularSize")->load() * 0.001f * (float) currentSampleRate;
        const auto grainInterval = juce::jmax (1, (int) (currentSampleRate / parameters.getRawParameterValue ("granularDensity")->load()));
        const auto grainRate = std::pow (2.0f, parameters.getRawParameterValue ("granularPitch")->load() / 12.0f);
        const auto randomAmount = parameters.getRawParameterValue ("randomAmount")->load();
        const auto randomInterval = juce::jmax (1, (int) (currentSampleRate / parameters.getRawParameterValue ("randomRate")->load()));
        const auto orbitWidth = 0.025f + ((parameters.getRawParameterValue ("harmonyShift")->load() + 12.0f) / 24.0f) * 0.13f;
        const auto orbitRate = 0.06f + parameters.getRawParameterValue ("harmonyDetune")->load() / 40.0f * 0.85f;

        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            const auto writePosition = textureWritePosition;
            std::array<float, 2> source { buffer.getSample (0, frame), buffer.getNumChannels() > 1 ? buffer.getSample (1, frame) : buffer.getSample (0, frame) };
            std::array<float, 2> output = source;

            if (--grainSamplesUntilReset <= 0)
            {
                grainSamplesUntilReset = grainInterval;
                grainReadPosition = wrapTexturePosition ((float) writePosition - grainSize * (0.45f + textureRandom.nextFloat() * 0.55f));
            }
            const auto grainWindow = std::sin (juce::MathConstants<float>::pi * (1.0f - (float) grainSamplesUntilReset / (float) grainInterval));
            const auto grainBlend = granularWet.getNextValue() * grainWindow;
            if (grainBlend > 0.00001f)
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                {
                    const auto grain = readTexture (channel, grainReadPosition);
                    output[(size_t) channel] += (grain - output[(size_t) channel]) * grainBlend;
                }
            grainReadPosition = wrapTexturePosition (grainReadPosition + grainRate);

            const auto orbitBlend = harmonyWet.getNextValue();
            if (orbitBlend > 0.00001f)
            {
                const auto orbit = std::sin (textureOrbitPhase);
                const auto leftPosition = (float) writePosition - currentSampleRate * (0.17f + orbitWidth * orbit);
                const auto rightPosition = (float) writePosition - currentSampleRate * (0.17f - orbitWidth * orbit);
                const auto orbitLeft = readTexture (0, leftPosition);
                const auto orbitRight = readTexture (buffer.getNumChannels() > 1 ? 1 : 0, rightPosition);
                output[0] += (orbitLeft - output[0]) * orbitBlend;
                if (buffer.getNumChannels() > 1) output[1] += (orbitRight - output[1]) * orbitBlend;
            }
            textureOrbitPhase = std::fmod (textureOrbitPhase + juce::MathConstants<float>::twoPi * orbitRate / (float) currentSampleRate, juce::MathConstants<float>::twoPi);

            if (--randomSamplesUntilReset <= 0)
            {
                randomSamplesUntilReset = randomInterval;
                randomReadPosition = wrapTexturePosition ((float) writePosition - textureRandom.nextFloat() * textureBuffer.getNumSamples() * randomAmount);
            }
            const auto randomWindow = std::sin (juce::MathConstants<float>::pi * (1.0f - (float) randomSamplesUntilReset / (float) randomInterval));
            const auto randomBlend = randomWet.getNextValue() * randomWindow;
            if (randomBlend > 0.00001f)
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                {
                    const auto scattered = readTexture (channel, randomReadPosition);
                    output[(size_t) channel] += (scattered - output[(size_t) channel]) * randomBlend;
                }
            randomReadPosition = wrapTexturePosition (randomReadPosition + 1.0f + (textureRandom.nextFloat() - 0.5f) * randomAmount * 0.08f);

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                textureBuffer.setSample (channel, writePosition, source[(size_t) channel]);
                buffer.setSample (channel, frame, output[(size_t) channel]);
            }
            textureWritePosition = (textureWritePosition + 1) % textureBuffer.getNumSamples();
        }
    };
    bool delayInRack = false;
    for (int slot = 0; slot < 7; ++slot)
    {
        if (! owner.getFxSlotEnabled (slot)) continue;
        const auto effect = owner.getFxSlotEffect (slot);
        if (effect < 0 || ! routed (effect)) continue;
        switch (effect)
        {
            case 0: delayInRack = true; applyDelay(); break;
            case 1: applyReverb(); break;
            case 2: applyChorus(); break;
            case 3: phaser.setRate (parameters.getRawParameterValue ("phaserRate")->load()); phaser.setDepth (parameters.getRawParameterValue ("phaserDepth")->load()); phaser.setFeedback (parameters.getRawParameterValue ("phaserFeedback")->load()); phaser.setMix (parameters.getRawParameterValue ("phaserMix")->load()); phaser.process (juce::dsp::ProcessContextReplacing<float> (block)); break;
            case 4: applyFlanger(); break;
            case 5: applyEq(); break;
            case 6: applyFlutter(); break;
            default: break;
        }
    }
    // Texture Lab runs as its own optional final creative layer, independent
    // of which FX cards happen to be in the rack.
    // One coordinated texture pass means several engines can run together
    // without each one advancing the shared circular buffer independently.
    if (granularWet.getCurrentValue() > 0.0001f || granularWet.getTargetValue() > 0.0001f
        || harmonyWet.getCurrentValue() > 0.0001f || harmonyWet.getTargetValue() > 0.0001f
        || randomWet.getCurrentValue() > 0.0001f || randomWet.getTargetValue() > 0.0001f)
        applyTextureLab();
    if (delayInRack)
        delayHasContent = true;
    else if (delayHasContent)
    {
        delayBuffer.clear();
        delayWritePosition = 0;
        delayHasContent = false;
    }
}

void OneShotSamplerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() == 0 || layerBuffers[0].getNumSamples() == 0)
        return;

    double tempo = 0.0;
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            if (auto bpm = position->getBpm())
                tempo = *bpm;
    hostBpm.store (tempo, std::memory_order_relaxed);
    const auto bpm = tempo > 0.0 && std::isfinite (tempo) ? juce::jlimit (20.0, 400.0, tempo) : 120.0;
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("outputGain")->load()));
    std::array<double, 2> pitch {}, stretch {}, bendDecay {}, tremStep {};
    std::array<float, 2> tremDepth {}, start {}, end {}, bend {};
    std::array<bool, 2> reverse {}, auditionNow {}, enabled {};
    for (size_t layer = 0; layer < 2; ++layer)
    {
        auto sample = std::atomic_load (&loadedSamples[layer]);
        if (sample != playingSamples[layer])
        {
            for (auto& v : voices[layer]) { v.active = false; v.envelope.reset(); }
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

    const auto trigger = [&] (size_t layer, int note, float velocity)
    {
        const auto& sample = playingSamples[layer];
        if (sample == nullptr || ! enabled[layer]) return;
        auto& bank = voices[layer];
        auto voice = std::find_if (bank.begin(), bank.end(), [] (const Voice& v) { return ! v.active; });
        if (voice == bank.end())
            voice = std::max_element (bank.begin(), bank.end(), [] (const Voice& a, const Voice& b) { return a.elapsed < b.elapsed; });
        const auto frames = sample->audio.getNumSamples();
        voice->firstFrame = juce::jlimit (0, frames - 1, (int) (start[layer] * frames));
        voice->endFrame = juce::jlimit (voice->firstFrame + 1, frames, (int) (end[layer] * frames));
        voice->midiNote = note;
        voice->velocity = velocity;
        voice->playbackRate = std::exp2 ((note - 60) / 12.0) * sample->sampleRate / currentSampleRate;
        voice->samplePosition = voice->elapsed = voice->tremPhase = 0.0;
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
    const auto channels = buffer.getNumChannels();
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
                        trigger (layer, m.getNoteNumber(), m.getFloatVelocity());
                    lastNote.store (m.getNoteNumber(), std::memory_order_relaxed);
                    lastVelocity.store (m.getFloatVelocity(), std::memory_order_relaxed);
                    noteCounter.fetch_add (1, std::memory_order_relaxed);
                }
                else if (m.isNoteOff() || m.isAllNotesOff() || m.isAllSoundOff())
                {
                    for (auto& bank : voices)
                        for (auto& voice : bank)
                            if (voice.active && (! m.isNoteOff() || voice.midiNote == m.getNoteNumber()))
                            {
                                if (m.isAllSoundOff()) { voice.envelope.reset(); voice.active = false; }
                                else voice.envelope.noteOff();
                            }
                }
                ++event;
            }
            const auto master = outputGain.getNextValue();
            for (size_t layer = 0; layer < 2; ++layer)
            {
                const auto gain = master * layerGain[layer].getNextValue();
                const auto& sample = playingSamples[layer];
                if (! sample) continue;
                for (auto& v : voices[layer])
                {
                    if (! v.active) continue;
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
                    const auto transport = rate / stretch[layer];
                    v.bendMultiplier = 1.0 + (v.bendMultiplier - 1.0) * bendDecay[layer];

                    // Two complementary Hann grains: the transport controls
                    // duration while the read heads travel at the pitched rate.
                    constexpr double grainSeconds = 0.045;
                    const bool stretched = std::abs (stretch[layer] - 1.0) > 0.0001;
                    const auto grainFrames = currentSampleRate * grainSeconds;
                    const auto cycle = stretched ? v.elapsed / grainFrames : 0.0;
                    const auto phase = cycle - std::floor (cycle);
                    const auto shifted = phase < 0.5 ? phase + 0.5 : phase - 0.5;
                    const auto weight = stretched ? 0.5 + 0.5 * std::cos (phase * juce::MathConstants<double>::twoPi) : 1.0;
                    const auto offsetPerGrain = stretched ? (rate - transport) * grainFrames : 0.0;
                    const auto headA = v.samplePosition + (shifted - 0.5) * offsetPerGrain;
                    const auto headB = v.samplePosition + (phase - 0.5) * offsetPerGrain;
                    const auto read = [&] (int ch, double position)
                    {
                        const auto p = juce::jlimit (0.0, (double) length - 1.0, position);
                        const auto index = (int) p;
                        const auto i0 = v.reverse ? v.endFrame - 1 - index : v.firstFrame + index;
                        const auto i1 = v.reverse ? juce::jmax (v.firstFrame, i0 - 1) : juce::jmin (v.endFrame - 1, i0 + 1);
                        const auto s0 = sample->audio.getSample (ch, i0);
                        return s0 + (sample->audio.getSample (ch, i1) - s0) * (float) (p - index);
                    };
                    const auto edge = (stretched || v.reverse || v.endFrame != sample->audio.getNumSamples())
                        ? (float) juce::jlimit (0.0, 1.0, (length - v.samplePosition) / juce::jmax (1.0, transport * currentSampleRate * 0.005))
                        : 1.0f;
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        const auto src = juce::jmin (ch, sample->audio.getNumChannels() - 1);
                        auto value = read (src, headA);
                        if (stretched) value = (float) (value * weight + read (src, headB) * (1.0 - weight));
                        layers[layer]->addSample (ch, frame, value * gain * env * tremolo * edge);
                    }
                    v.samplePosition += transport;
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

    // Transparent safety ceiling for stacked effects. Normal sample levels are
    // left completely unchanged; protection begins only above the threshold.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
        {
            auto input = buffer.getSample (channel, sampleIndex);
            if (! std::isfinite (input))
            {
                // A NaN or infinity would latch inside the delay/chorus feedback loops.
                buffer.setSample (channel, sampleIndex, 0.0f);
                continue;
            }
            constexpr float threshold = 0.966f; // -0.3 dBFS ceiling
            const auto magnitude = std::abs (input);
            if (magnitude > threshold)
            {
                const auto limited = threshold + (1.0f - threshold) * std::tanh ((magnitude - threshold) / (1.0f - threshold));
                buffer.setSample (channel, sampleIndex, std::copysign (limited, input));
            }
        }
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

void OneShotSamplerAudioProcessor::clearSample (int layer)
{
    if (layer < 0 || layer > 1) return;
    const juce::ScopedLock lock (sampleLoadLock);
    if (auto old = std::atomic_exchange (&loadedSamples[(size_t) layer], std::shared_ptr<SampleData>()))
        retiredSamples.push_back (std::move (old));
    retiredSamples.erase (std::remove_if (retiredSamples.begin(), retiredSamples.end(),
        [] (const auto& old) { return old.use_count() == 1; }), retiredSamples.end());
    parameters.state.removeProperty (layer == 0 ? "sampleFilePath" : "sampleFilePathB", nullptr);
    ++sampleRevision[(size_t) layer];
}

juce::String OneShotSamplerAudioProcessor::getSampleName (int layer) const
{
    if (const auto sample = std::atomic_load (&loadedSamples[(size_t) layer]))
        return sample->name;
    return {};
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
            for (int effect = 0; effect < (int) routeParams.size(); ++effect)
                if (auto route = state.getChildWithProperty ("id", routeParamId (effect)); route.isValid()
                    && (int) route.getProperty ("value") == 2)
                    route.setProperty ("value", 0, nullptr); // Former Both routes now belong to A.
            parameters.replaceState (state);
            const auto legacyRackState = (int) parameters.state.getProperty ("fxLayoutVersion", 0) < 6;
            if (legacyRackState)
            {
                // Version 6 replaces the editable chain with a fixed panel.
                // Existing sessions keep their sound safe: every module starts
                // disabled until the producer deliberately enables it again.
                for (int slot = 0; slot < 7; ++slot)
                    setFxSlotState (slot, slot, false);
            }
            parameters.state.setProperty ("fxLayoutVersion", 6, nullptr);
            refreshEqTypes();
            for (int layer = 0; layer < 2; ++layer)
            {
                const auto path = parameters.state.getProperty (layer == 0 ? "sampleFilePath" : "sampleFilePathB").toString();
                if (path.isEmpty() || ! loadSample (juce::File (path), layer))
                    clearSample (layer);
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OneShotSamplerAudioProcessor();
}
