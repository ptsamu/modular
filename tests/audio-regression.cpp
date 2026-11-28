#include "PluginProcessor.h"
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::TemporaryFile temp (".wav");
    juce::AudioBuffer<float> tone (1, 44100 * 5);
    for (int i = 0; i < tone.getNumSamples(); ++i)
        tone.setSample (0, i, 0.1f * std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / 44100.0));
    {
        auto stream = temp.getFile().createOutputStream();
        if (! stream) return 1;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), 44100, 1, 24, {}, 0));
        if (! writer) return 2;
        stream.release();
        if (! writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples())) return 3;
    }
    const auto render = [&] (bool on, float depth, int route, int trigger, bool audition, int rate = 1, int tail = 0)
    {
        OneShotSamplerAudioProcessor p;
        if (! p.loadSample (temp.getFile(), 0)) return -1.0f;
        const auto set = [&] (const char* key, float value)
        {
            auto* param = p.parameters.getParameter (key);
            param->setValueNotifyingHost (param->convertTo0to1 (value));
        };
        set ("duckOn", on ? 1.0f : 0.0f);
        set ("duckDepth", depth);
        set ("duckRoute", (float) route);
        set ("duckTrigger", (float) trigger);
        set ("duckRate", (float) rate);
        p.prepareToPlay (44100, 128);
        juce::AudioBuffer<float> buffer (2, 2048);
        juce::MidiBuffer midi;
        if (audition) p.audition (0);
        else midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 197);
        p.processBlock (buffer, midi);
        midi.clear();
        for (int i = 0; i < tail; ++i) p.processBlock (buffer, midi);
        return buffer.getRMSLevel (0, 400, 1600);
    };
    const auto dry = render (false, 100, 2, 0, false);
    const auto ducked = render (true, 100, 2, 0, false);
    const auto fallback = render (true, 100, 2, 1, false);
    const auto auditionDry = render (false, 100, 2, 0, true);
    const auto auditionDuck = render (true, 100, 2, 0, true);
    std::cout << "Dry: " << dry << " Duck: " << ducked << " Sync fallback: " << fallback
              << " Audition dry/duck: " << auditionDry << "/" << auditionDuck << '\n';
    const auto barDry = render (false, 100, 2, 1, false, 3, 43);
    const auto barLoop = render (true, 100, 2, 1, false, 3, 43);
    const auto barNote = render (true, 100, 2, 0, false, 3, 43);
    const auto setParam = [] (OneShotSamplerAudioProcessor& p, const char* key, float value)
    {
        auto* param = p.parameters.getParameter (key);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    const auto tail = [&] (bool masterDown)
    {
        OneShotSamplerAudioProcessor p;
        if (! p.loadSample (temp.getFile(), 0)) return -1.0f;
        setParam (p, "sourceAEnd", 0.02f);
        p.setFxSlotState (1, 1, true);
        setParam (p, "reverbMix", 1.0f);
        p.prepareToPlay (44100, 128);
        juce::AudioBuffer<float> audio (2, 2048);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        p.processBlock (audio, midi);
        midi.clear();
        for (int i = 0; i < 3; ++i) p.processBlock (audio, midi);
        if (p.getActiveVoiceCount() != 0) return -1.0f;
        if (masterDown) setParam (p, "outputGain", -40.0f);
        p.processBlock (audio, midi);
        return audio.getRMSLevel (0, 1024, 1024);
    };
    OneShotSamplerAudioProcessor clipping;
    if (! clipping.loadSample (temp.getFile(), 0) || ! clipping.loadSample (temp.getFile(), 1)) return 5;
    setParam (clipping, "outputGain", 6.0f);
    clipping.prepareToPlay (44100, 128);
    juce::AudioBuffer<float> loudAudio (2, 2048);
    juce::MidiBuffer loudMidi;
    for (int i = 1; i <= 8; ++i) loudMidi.addEvent (juce::MidiMessage::noteOn (i, 60, 1.0f), 0);
    clipping.processBlock (loudAudio, loudMidi);
    const auto peak = loudAudio.getMagnitude (0, 2048);
    const auto liveTail = tail (false), mutedTail = tail (true);
    const auto bypass = [&] (bool toggle)
    {
        OneShotSamplerAudioProcessor p;
        if (! p.loadSample (temp.getFile(), 0)) return std::array<float, 3> { -1, -1, -1 };
        setParam (p, "sourceAEnd", 0.02f);
        p.setFxSlotState (1, 1, true);
        setParam (p, "reverbMix", 1.0f);
        p.prepareToPlay (44100, 128);
        juce::AudioBuffer<float> audio (2, 2048);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        p.processBlock (audio, midi);
        midi.clear();
        for (int i = 0; i < 3; ++i) p.processBlock (audio, midi);
        if (toggle) p.setFxSlotState (1, 1, false);
        p.processBlock (audio, midi);
        const auto front = audio.getRMSLevel (0, 0, 32);
        const auto end = audio.getRMSLevel (0, 1024, 1024);
        p.setFxSlotState (1, 1, true);
        p.processBlock (audio, midi);
        return std::array<float, 3> { front, end, audio.getMagnitude (0, audio.getNumSamples()) };
    };
    const auto wet = bypass (false), off = bypass (true);
    std::cout << "Bypass transition ratio: " << off[0] / wet[0] << " Tail after cutoff/re-enable: "
              << off[1] << "/" << off[2] << '\n';
    if (wet[0] <= 0 || off[0] < wet[0] * 0.9f || off[1] != 0 || off[2] != 0) return 7;
    std::cout << "Peak/reduction: " << peak << "/" << clipping.getProtectionReductionDb()
              << " Tail full/attenuated: " << liveTail << "/" << mutedTail << '\n';
    if (peak > 0.966051f || peak < 0.95f || clipping.getProtectionReductionDb() <= 0
        || liveTail <= 0 || std::abs (mutedTail / liveTail - 0.01f) > 0.001f) return 6;
    OneShotSamplerAudioProcessor texture;
    if (! texture.loadSample (temp.getFile(), 0) || ! texture.loadSample (temp.getFile(), 1)) return 8;
    for (const auto layer : { 0, 1 })
    {
        const auto fx = [&] (const char* id, float value) { setParam (texture, texture.fxParamId (layer, id).toRawUTF8(), value); };
        fx ("layerFxOn7", 2);
        fx ("layerFxOn9", 2);
        fx ("granularMix", 1);
        fx ("randomMix", 1);
        fx ("randomAmount", 1);
        fx ("granularPitch", 24);
        fx ("granularSize", 500);
        fx ("granularDensity", 1);
        fx ("randomRate", 1);
    }
    texture.prepareToPlay (44100, 128);
    juce::AudioBuffer<float> textureAudio (2, 2048);
    juce::MidiBuffer textureMidi;
    textureMidi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    for (int block = 0; block < 250; ++block)
    {
        if (block == 83)
        {
            setParam (texture, "granularDensity", 40);
            setParam (texture, "fxB_randomRate", 24);
        }
        texture.processBlock (textureAudio, textureMidi);
        textureMidi.clear();
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < textureAudio.getNumSamples(); ++i)
                if (! std::isfinite (textureAudio.getSample (ch, i))) return 9;
    }
    if (textureAudio.getMagnitude (0, textureAudio.getNumSamples()) != 0.0f) return 10;
    std::cout << "Dual-layer texture history wraps and settles to silence\n";
    OneShotSamplerAudioProcessor eq;
    if (! eq.loadSample (temp.getFile(), 0) || ! eq.loadSample (temp.getFile(), 1)) return 11;
    setParam (eq, "layerFxOn5", 2);
    setParam (eq, "fxB_layerFxOn5", 2);
    eq.prepareToPlay (8000, 32);
    juce::AudioBuffer<float> eqAudio (2, 64);
    juce::MidiBuffer eqMidi;
    eqMidi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    for (int block = 0; block < 300; ++block)
    {
        for (const auto layer : { 0, 1 })
            for (int band = 1; band <= 7; ++band)
            {
                const auto prefix = eq.fxParamId (layer, "eqBand" + juce::String (band));
                setParam (eq, (prefix + "Freq").toRawUTF8(), ((block + band) % 2 == 0) ? 3900.0f : 160.0f);
                setParam (eq, (prefix + "Gain").toRawUTF8(), ((block + band + layer) % 2 == 0) ? 18.0f : -18.0f);
                setParam (eq, (prefix + "Q").toRawUTF8(), 8.0f);
                eq.parameters.state.setProperty (prefix + "Type", (block + band) % 6, nullptr);
            }
        eq.processBlock (eqAudio, eqMidi);
        eqMidi.clear();
        for (int ch = 0; ch < 2; ++ch)
            for (int frame = 0; frame < eqAudio.getNumSamples(); ++frame)
                if (! std::isfinite (eqAudio.getSample (ch, frame))) return 12;
    }
    std::cout << "Dual-layer EQ automation stays finite at 8 kHz\n";
    OneShotSamplerAudioProcessor notes;
    if (! notes.loadSample (temp.getFile(), 0)) return 13;
    setParam (notes, "release", 0.001f);
    notes.prepareToPlay (44100, 128);
    juce::AudioBuffer<float> noteAudio (2, 1024);
    juce::MidiBuffer events;
    events.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 17);
    events.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 200);
    events.addEvent (juce::MidiMessage::noteOn (2, 60, 1.0f), 400);
    events.addEvent (juce::MidiMessage::noteOff (1, 60), 600);
    notes.processBlock (noteAudio, events);
    if (notes.getActiveVoiceCount() != 2) return 14;
    events.clear();
    events.addEvent (juce::MidiMessage::allSoundOff (1), 0);
    notes.processBlock (noteAudio, events);
    if (notes.getActiveVoiceCount() != 1) return 15;
    std::cout << "Repeated notes release one voice; all-sound-off is channel-scoped\n";
    OneShotSamplerAudioProcessor legacy;
    legacy.parameters.state.setProperty ("fxLayoutVersion", 5, nullptr);
    legacy.setFxSlotState (0, 1, true);
    auto oldState = legacy.parameters.copyState();
    auto both = oldState.getChildWithProperty ("id", legacy.routeParamId (1));
    both.setProperty ("value", 2, nullptr);
    const auto missingPath = temp.getFile().getSiblingFile ("missing-modular-source.wav").getFullPathName();
    oldState.setProperty ("sampleFilePath", missingPath, nullptr);
    juce::MemoryBlock stateBytes;
    juce::AudioProcessor::copyXmlToBinary (*oldState.createXml(), stateBytes);
    OneShotSamplerAudioProcessor restored;
    restored.setStateInformation (stateBytes.getData(), (int) stateBytes.getSize());
    if (! restored.getFxSlotEnabled (1) || ! restored.isLayerFxOn (1, 0, true) || ! restored.isLayerFxOn (1, 1, true)
        || restored.parameters.state.getProperty ("sampleFilePath").toString() != missingPath
        || restored.getMissingSampleName (0) != "missing-modular-source.wav") return 16;
    for (int i = 0; i < 2; ++i)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (restored.createEditor());
        editor->setSize (1440, 680);
        for (const auto* child : editor->getChildren())
            if (child->isVisible() && ! child->getBounds().isEmpty() && ! editor->getLocalBounds().contains (editor->getLocalArea (child, child->getLocalBounds())))
                return 17;
    }
    if (! restored.getFxSlotEnabled (1) || ! restored.isLayerFxOn (1, 1, true)) return 18;
    std::cout << "Legacy Both and missing sample survive restore; compact editor reopens without changing FX\n";
    OneShotSamplerAudioProcessor patches;
    if (patches.getMasterPreset() != 0 || patches.hasLoadedSample (0) || patches.hasLoadedSample (1)) return 19;
    patches.prepareToPlay (44100, 256);
    juce::AudioBuffer<float> patchAudio (2, 256);
    juce::MidiBuffer patchMidi;
    const auto& patchNames = patches.masterPresetNames();
    for (int preset = 1; preset < patchNames.size(); ++preset)
    {
        if (! patches.applyMasterPreset (preset) || patches.getMasterPreset() != preset
            || ! patches.hasLoadedSample (0) || ! patches.hasLoadedSample (1)) return 20;
        float peak = 0.0f;
        for (int block = 0; block < 48; ++block)
        {
            if (block == 0 || block == 26)
                patchMidi.addEvent (juce::MidiMessage::noteOn (1, 60 + (block == 26 ? 7 : 0), 0.8f), 0);
            if (block == 20 || block == 43)
                patchMidi.addEvent (juce::MidiMessage::noteOff (1, 60 + (block == 43 ? 7 : 0)), 0);
            patches.processBlock (patchAudio, patchMidi);
            patchMidi.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < patchAudio.getNumSamples(); ++i)
                {
                    const auto value = patchAudio.getSample (ch, i);
                    if (! std::isfinite (value)) return 21;
                    peak = juce::jmax (peak, std::abs (value));
                }
        }
        std::cout << patchNames[preset] << " render peak " << peak << '\n';
        if (peak < 0.02f || peak > 0.966051f) return 22;
        juce::MemoryBlock bytes;
        patches.getStateInformation (bytes);
        OneShotSamplerAudioProcessor recalled;
        recalled.setStateInformation (bytes.getData(), (int) bytes.getSize());
        if (recalled.getMasterPreset() != preset
            || recalled.getSampleName (0) != patches.getSampleName (0)
            || recalled.getSampleName (1) != patches.getSampleName (1)
            || recalled.getWaveformPeaks (0) != patches.getWaveformPeaks (0)
            || recalled.getWaveformPeaks (1) != patches.getWaveformPeaks (1)) return 23;
    }
    const auto previous = patchAudio.getSample (0, patchAudio.getNumSamples() - 1);
    setParam (patches, "duckOn", 1);
    setParam (patches, "outputGain", -18);
    if (! patches.applyMasterPreset (0) || patches.hasLoadedSample (0) || patches.hasLoadedSample (1)
        || patches.parameters.getRawParameterValue ("duckOn")->load() > 0.0f
        || std::abs (patches.parameters.getRawParameterValue ("outputGain")->load()) > 0.001f) return 24;
    patches.processBlock (patchAudio, patchMidi);
    if (std::abs (previous - patchAudio.getSample (0, 0)) > 0.001f) return 25;
    for (int block = 0; block < 4; ++block)
        patches.processBlock (patchAudio, patchMidi);
    if (patchAudio.getMagnitude (0, patchAudio.getNumSamples()) > 0.0001f) return 26;
    std::cout << "Factory samples recall and Init returns to silence\n";
    std::cout << "One bar dry/sync/note: " << barDry << "/" << barLoop << "/" << barNote << '\n';
    return dry > 0 && ducked >= 0 && ducked < dry * 0.3f && fallback >= 0 && fallback < dry * 0.3f
        && barDry > 0 && barLoop >= 0 && barLoop < barDry * 0.3f && std::abs (barDry - barNote) < 1e-6f
        && std::abs (dry - render (true, 0, 2, 0, false)) < 1e-6f
        && std::abs (dry - render (true, 100, 1, 0, false)) < 1e-6f
        && auditionDuck >= 0 && auditionDuck < auditionDry * 0.3f ? 0 : 4;
}
