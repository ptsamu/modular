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
    for (auto& cache : eqCache) cache = { -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f };
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
        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f,
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
    // Kept only so older sessions can still restore cleanly. Texture Lab is
    // now controlled directly by its three engine switches.
    layout.add (std::make_unique<juce::AudioParameterBool> ("textureEnabled", "Texture Lab Legacy Master", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("eqLow", "EQ Low", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("eqMid", "EQ Mid", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("eqHigh", "EQ High", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("eqLowFreq", "EQ Low Frequency", juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), 180.0f, juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("eqMidFreq", "EQ Mid Frequency", juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), 1200.0f, juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("eqHighFreq", "EQ High Frequency", juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), 6500.0f, juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("eqLowQ", "EQ Low Width", juce::NormalisableRange<float> (0.2f, 8.0f, 0.01f), 0.75f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("eqMidQ", "EQ Mid Width", juce::NormalisableRange<float> (0.2f, 8.0f, 0.01f), 0.8f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("eqHighQ", "EQ High Width", juce::NormalisableRange<float> (0.2f, 8.0f, 0.01f), 0.75f));
    layout.add (std::make_unique<juce::AudioParameterBool> ("eqEnabled", "EQ Enabled", false));
    const std::array<float, 7> eqFrequencies { 60.0f, 170.0f, 450.0f, 1100.0f, 2800.0f, 7000.0f, 14000.0f };
    for (int band = 0; band < 7; ++band)
    {
        const auto prefix = "eqBand" + juce::String (band + 1);
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Freq", prefix + " Frequency", juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), eqFrequencies[(size_t) band], juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Gain", prefix + " Gain", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (prefix + "Q", prefix + " Q", juce::NormalisableRange<float> (0.2f, 8.0f, 0.01f), 0.8f));
    }

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
    for (auto& voice : voices)
    {
        voice.envelope.setSampleRate (sampleRate);
        voice.active = false;
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
    reverbToneState = {};
    reverbHighPassState = {};
    const auto modulationBufferLength = static_cast<int> (sampleRate * 0.085) + 4;
    chorusBuffer.setSize (2, modulationBufferLength); chorusBuffer.clear();
    flangerBuffer.setSize (2, modulationBufferLength); flangerBuffer.clear();
    flutterBuffer.setSize (2, modulationBufferLength); flutterBuffer.clear();
    chorusWritePosition = flangerWritePosition = flutterWritePosition = 0;
    chorusPhase = flangerPhase = flutterPhase = 0.0f;
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
    grainReadPosition = harmonyReadPosition = randomReadPosition = 0.0f;
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

void OneShotSamplerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const juce::ADSR::Parameters envelopeParameters { parameters.getRawParameterValue ("attack")->load(),
                                                       parameters.getRawParameterValue ("decay")->load(),
                                                       parameters.getRawParameterValue ("sustain")->load(),
                                                       parameters.getRawParameterValue ("release")->load() };
    for (auto& voice : voices)
        voice.envelope.setParameters (envelopeParameters);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("outputGain")->load()));

    const auto sample = std::atomic_load (&loadedSample);
    if (sample == nullptr)
    {
        outputLevel.store (0.0f);
        envelopeLevel.store (0.0f);
        playbackProgress.store (0.0f);
        activeVoiceCount.store (0);
        return;
    }

    float blockEnvelopePeak = 0.0f;
    auto midiEvent = midiMessages.begin();
    const auto midiEnd = midiMessages.end();
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
    {
        while (midiEvent != midiEnd)
        {
            const auto metadata = *midiEvent;
            if (metadata.samplePosition > frame)
                break;
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
            {
                auto* voice = std::find_if (voices.begin(), voices.end(), [] (const Voice& candidate) { return ! candidate.active; });
                if (voice == voices.end())
                    voice = voices.begin();
                voice->midiNote = message.getNoteNumber();
                voice->velocity = message.getFloatVelocity();
                voice->playbackRate = std::pow (2.0, (voice->midiNote - 60) / 12.0);
                voice->samplePosition = 0.0;
                voice->active = true;
                voice->envelope.noteOn();
            }
            else if (message.isNoteOff())
            {
                for (auto& voice : voices)
                    if (voice.active && voice.midiNote == message.getNoteNumber())
                        voice.envelope.noteOff();
            }
            ++midiEvent;
        }

        const auto output = outputGain.getNextValue();
        for (auto& voice : voices)
        {
            if (! voice.active)
                continue;
            const auto sourceIndex = static_cast<int> (voice.samplePosition);
            if (sourceIndex >= sample->audio.getNumSamples() - 1)
            {
                voice.envelope.reset();
                voice.active = false;
                continue;
            }
            const auto envelopeSample = voice.envelope.getNextSample();
            blockEnvelopePeak = juce::jmax (blockEnvelopePeak, envelopeSample * voice.velocity);
            const auto gain = output * envelopeSample * voice.velocity;
            const auto fraction = (float) (voice.samplePosition - (double) sourceIndex);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const auto sourceChannel = juce::jmin (channel, sample->audio.getNumChannels() - 1);
                const auto first = sample->audio.getSample (sourceChannel, sourceIndex);
                const auto second = sample->audio.getSample (sourceChannel, sourceIndex + 1);
                buffer.addSample (channel, frame, (first + (second - first) * fraction) * gain);
            }
            voice.samplePosition += (sample->sampleRate / currentSampleRate) * voice.playbackRate;
            if (! voice.envelope.isActive())
                voice.active = false;
        }
    }

    // Expose the actual voice-envelope peak for the editor's live ADSR follower.
    envelopeLevel.store (juce::jlimit (0.0f, 1.0f, blockEnvelopePeak));
    float progress = 0.0f;
    int liveVoices = 0;
    for (const auto& voice : voices)
        if (voice.active)
        {
            ++liveVoices;
            progress = juce::jmax (progress, (float) (voice.samplePosition / (double) sample->audio.getNumSamples()));
        }
    activeVoiceCount.store (liveVoices);
    playbackProgress.store (juce::jlimit (0.0f, 1.0f, progress));

    auto bpm = 120.0;
    if (const auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (position->getBpm().hasValue()) bpm = *position->getBpm();
    juce::dsp::AudioBlock<float> block (buffer);
    // Some hosts exceed the block size they announced. Never write past the
    // reverb's scratch buffer.
    if (reverbWetBuffer.getNumSamples() < buffer.getNumSamples())
        reverbWetBuffer.setSize (2, buffer.getNumSamples(), false, false, true);
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
    const auto applyEq = [this, &buffer]
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
    const auto applyDelay = [this, &buffer, bpm, &readInterpolated]
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
    const auto applyReverb = [this, &buffer]
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
    const auto applyChorus = [this, &buffer, &readInterpolated]
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
    const auto applyFlanger = [this, &buffer, &readInterpolated]
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
    const auto applyFlutter = [this, &buffer, &readInterpolated]
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
            flutterPhase = std::fmod (flutterPhase + juce::MathConstants<float>::twoPi * rate / (float) currentSampleRate, juce::MathConstants<float>::twoPi);
            flutterWowPhase = std::fmod (flutterWowPhase + juce::MathConstants<float>::twoPi * wowRate / (float) currentSampleRate, juce::MathConstants<float>::twoPi);
            flutterDriftPhase = std::fmod (flutterDriftPhase + juce::MathConstants<float>::twoPi * rate * 1.91f / (float) currentSampleRate, juce::MathConstants<float>::twoPi);
        }
    };
    const auto wrapTexturePosition = [this] (float position) { const auto length = (float) textureBuffer.getNumSamples(); while (position < 0.0f) position += length; while (position >= length) position -= length; return position; };
    const auto readTexture = [this, &wrapTexturePosition] (int channel, float position) { position = wrapTexturePosition (position); const auto first = (int) position; const auto second = (first + 1) % textureBuffer.getNumSamples(); const auto fraction = position - first; return textureBuffer.getSample (channel, first) + (textureBuffer.getSample (channel, second) - textureBuffer.getSample (channel, first)) * fraction; };
    const auto writeTexture = [this, &buffer] { for (int frame = 0; frame < buffer.getNumSamples(); ++frame) { for (int channel = 0; channel < buffer.getNumChannels(); ++channel) textureBuffer.setSample (channel, textureWritePosition, buffer.getSample (channel, frame)); textureWritePosition = (textureWritePosition + 1) % textureBuffer.getNumSamples(); } };

    // Each Texture Lab engine owns its own state. There is deliberately no
    // hidden master gate: turning Granular, Orbit, or Random on is enough.
    granularWet.setTargetValue (parameters.getRawParameterValue ("granularEnabled")->load() > 0.5f ? parameters.getRawParameterValue ("granularMix")->load() : 0.0f);
    harmonyWet.setTargetValue (parameters.getRawParameterValue ("harmonyEnabled")->load() > 0.5f ? parameters.getRawParameterValue ("harmonyMix")->load() : 0.0f);
    randomWet.setTargetValue (parameters.getRawParameterValue ("randomEnabled")->load() > 0.5f ? parameters.getRawParameterValue ("randomMix")->load() : 0.0f);

    const auto applyGranular = [this, &buffer, &readTexture, &wrapTexturePosition, &writeTexture]
    {
        const auto size = parameters.getRawParameterValue ("granularSize")->load() * 0.001f * (float) currentSampleRate;
        const auto resetInterval = juce::jmax (1, (int) (currentSampleRate / parameters.getRawParameterValue ("granularDensity")->load()));
        const auto rate = std::pow (2.0f, parameters.getRawParameterValue ("granularPitch")->load() / 12.0f);
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame) { if (--grainSamplesUntilReset <= 0) { grainSamplesUntilReset = resetInterval; grainReadPosition = wrapTexturePosition ((float) textureWritePosition - size * (0.4f + textureRandom.nextFloat())); } const auto progress = 1.0f - (float) grainSamplesUntilReset / (float) resetInterval; const auto window = std::sin (juce::MathConstants<float>::pi * progress); const auto wet = granularWet.getNextValue() * window; for (int ch = 0; ch < buffer.getNumChannels(); ++ch) { const auto dry = buffer.getSample (ch, frame); const auto grain = readTexture (ch, grainReadPosition); textureBuffer.setSample (ch, textureWritePosition, dry); buffer.setSample (ch, frame, dry + (grain - dry) * wet); } textureWritePosition = (textureWritePosition + 1) % textureBuffer.getNumSamples(); grainReadPosition = wrapTexturePosition (grainReadPosition + rate); }
    };
    const auto applyHarmony = [this, &buffer, &readTexture, &wrapTexturePosition]
    {
        const auto rate = std::pow (2.0f, (parameters.getRawParameterValue ("harmonyShift")->load() + parameters.getRawParameterValue ("harmonyDetune")->load() / 100.0f) / 12.0f);
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame) { if (harmonyReadPosition == 0.0f) harmonyReadPosition = wrapTexturePosition ((float) textureWritePosition - currentSampleRate * 0.12f); const auto wet = harmonyWet.getNextValue(); for (int ch = 0; ch < buffer.getNumChannels(); ++ch) { const auto dry = buffer.getSample (ch, frame); const auto shifted = readTexture (ch, harmonyReadPosition); textureBuffer.setSample (ch, textureWritePosition, dry); buffer.setSample (ch, frame, dry + (shifted - dry) * wet); } textureWritePosition = (textureWritePosition + 1) % textureBuffer.getNumSamples(); harmonyReadPosition = wrapTexturePosition (harmonyReadPosition + rate); }
    };
    const auto applyRandom = [this, &buffer, &readTexture, &wrapTexturePosition]
    {
        const auto amount = parameters.getRawParameterValue ("randomAmount")->load(); const auto interval = juce::jmax (1, (int) (currentSampleRate / parameters.getRawParameterValue ("randomRate")->load()));
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame) { if (--randomSamplesUntilReset <= 0) { randomSamplesUntilReset = interval; randomReadPosition = wrapTexturePosition ((float) textureWritePosition - textureRandom.nextFloat() * textureBuffer.getNumSamples() * amount); } const auto progress = 1.0f - (float) randomSamplesUntilReset / (float) interval; const auto wet = randomWet.getNextValue() * std::sin (juce::MathConstants<float>::pi * progress); for (int ch = 0; ch < buffer.getNumChannels(); ++ch) { const auto dry = buffer.getSample (ch, frame); const auto random = readTexture (ch, randomReadPosition); textureBuffer.setSample (ch, textureWritePosition, dry); buffer.setSample (ch, frame, dry + (random - dry) * wet); } textureWritePosition = (textureWritePosition + 1) % textureBuffer.getNumSamples(); randomReadPosition = wrapTexturePosition (randomReadPosition + 1.0f + (textureRandom.nextFloat() - 0.5f) * amount * 0.12f); }
    };
    const auto applyTextureLab = [this, &buffer, &readTexture, &wrapTexturePosition]
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
        if (! getFxSlotEnabled (slot)) continue;
        switch (getFxSlotEffect (slot))
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
    if (! delayInRack) { delayBuffer.clear(); delayWritePosition = 0; }
    // Transparent safety ceiling for stacked effects. Normal sample levels are
    // left completely unchanged; protection begins only above the threshold.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
        {
            const auto input = buffer.getSample (channel, sampleIndex);
            constexpr float threshold = 0.966f; // -0.3 dBFS ceiling
            const auto magnitude = std::abs (input);
            if (magnitude > threshold)
            {
                const auto limited = threshold + (1.0f - threshold) * std::tanh ((magnitude - threshold) / (1.0f - threshold));
                buffer.setSample (channel, sampleIndex, std::copysign (limited, input));
            }
        }
    outputLevel.store (juce::jlimit (0.0f, 1.0f, buffer.getMagnitude (0, buffer.getNumSamples())));
}

bool OneShotSamplerAudioProcessor::loadSample (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    auto sample = std::make_shared<SampleData>();
    sample->audio.setSize (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
    reader->read (&sample->audio, 0, sample->audio.getNumSamples(), 0, true, true);
    sample->sampleRate = reader->sampleRate;
    sample->name = file.getFileName();
    std::atomic_store (&loadedSample, std::move (sample));
    // Store the original location in the plug-in state, so an FL project can
    // restore this exact sample on reopening (as long as the file stays there).
    parameters.state.setProperty ("sampleFilePath", file.getFullPathName(), nullptr);
    return true;
}

juce::String OneShotSamplerAudioProcessor::getSampleName() const
{
    if (const auto sample = std::atomic_load (&loadedSample))
        return sample->name;
    return "DROP A WAV OR AIFF";
}

bool OneShotSamplerAudioProcessor::hasLoadedSample() const
{
    return std::atomic_load (&loadedSample) != nullptr;
}

float OneShotSamplerAudioProcessor::getSampleLengthSeconds() const
{
    if (const auto sample = std::atomic_load (&loadedSample))
        return sample->sampleRate > 0.0 ? (float) sample->audio.getNumSamples() / (float) sample->sampleRate : 0.0f;
    return 0.0f;
}

int OneShotSamplerAudioProcessor::getSampleChannels() const
{
    if (const auto sample = std::atomic_load (&loadedSample))
        return sample->audio.getNumChannels();
    return 0;
}

std::vector<float> OneShotSamplerAudioProcessor::getWaveformPeaks (int numberOfPoints) const
{
    std::vector<float> peaks ((size_t) juce::jmax (1, numberOfPoints), 0.0f);
    const auto sample = std::atomic_load (&loadedSample);
    if (sample == nullptr || sample->audio.getNumSamples() == 0)
        return peaks;

    const auto framesPerPoint = juce::jmax (1, sample->audio.getNumSamples() / (int) peaks.size());
    for (size_t point = 0; point < peaks.size(); ++point)
    {
        const auto start = (int) point * framesPerPoint;
        const auto end = juce::jmin (sample->audio.getNumSamples(), start + framesPerPoint);
        for (int frame = start; frame < end; ++frame)
            for (int channel = 0; channel < sample->audio.getNumChannels(); ++channel)
                peaks[point] = juce::jmax (peaks[point], std::abs (sample->audio.getSample (channel, frame)));
    }
    return peaks;
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
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
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
            const auto savedSamplePath = parameters.state.getProperty ("sampleFilePath", {}).toString();
            const juce::File savedSample (savedSamplePath);
            if (savedSample.existsAsFile())
                loadSample (savedSample);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OneShotSamplerAudioProcessor();
}
