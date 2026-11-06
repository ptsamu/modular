#pragma once

#include <JuceHeader.h>

class CurvedEnvelope
{
public:
    using Parameters = juce::ADSR::Parameters;
    void setSampleRate (double rate) noexcept { sampleRate = rate; }
    void setParameters (Parameters next) noexcept
    {
        params = next;
        steps = { stepFor (params.attack), stepFor (params.decay), stepFor (params.release) };
    }
    void reset() noexcept { stage = Stage::idle; level = phase = 0.0f; }
    void noteOn() noexcept { stage = Stage::attack; phase = 0.0f; }
    void noteOff() noexcept
    {
        if (stage == Stage::idle || stage == Stage::release) return;
        releaseStart = level;
        phase = 0.0f;
        stage = Stage::release;
    }
    bool isActive() const noexcept { return stage != Stage::idle; }
    float getNextSample() noexcept
    {
        switch (stage)
        {
            case Stage::idle: return 0.0f;
            case Stage::attack:
                phase = juce::jmin (1.0f, phase + steps[0]);
                level = ease (phase);
                if (phase >= 1.0f) { stage = Stage::decay; phase = 0.0f; }
                break;
            case Stage::decay:
                phase = juce::jmin (1.0f, phase + steps[1]);
                level = 1.0f - (1.0f - params.sustain) * ease (phase);
                if (phase >= 1.0f) stage = Stage::sustain;
                break;
            case Stage::sustain:
                level = params.sustain;
                break;
            case Stage::release:
                phase = juce::jmin (1.0f, phase + steps[2]);
                level = releaseStart * (1.0f - ease (phase));
                if (phase >= 1.0f) reset();
                break;
        }
        return level;
    }
    static float ease (float t) noexcept { return t * (2.0f - t); }

private:
    enum class Stage { idle, attack, decay, sustain, release };
    float stepFor (float seconds) const noexcept { return (float) (1.0 / juce::jmax (1.0, sampleRate * seconds)); }
    Parameters params;
    double sampleRate = 44100.0;
    std::array<float, 3> steps {};
    Stage stage = Stage::idle;
    float level = 0.0f, phase = 0.0f, releaseStart = 0.0f;
};

class OneShotSamplerAudioProcessor final : public juce::AudioProcessor,
                                           private juce::ValueTree::Listener
{
public:
    OneShotSamplerAudioProcessor();
    ~OneShotSamplerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool loadSample (const juce::File& file, int layer = 0);
    void clearSample (int layer);
    juce::String getSampleName (int layer = 0) const;
    bool hasLoadedSample (int layer = 0) const;
    std::vector<float> getWaveformPeaks (int layer) const;
    unsigned int getSampleRevision (int layer) const noexcept { return sampleRevision[(size_t) layer].load(); }
    float getLayerProgress (int layer) const noexcept { return layerProgress[(size_t) layer].load(); }
    void audition (int layer) noexcept { auditionPending[(size_t) layer].store (true); }
    static juce::String sourceParamId (int layer, const char* suffix);
    static juce::String routeParamId (int effect);
    float getOutputLevel() const noexcept { return outputLevel.load(); }
    float getEnvelopeLevel (int layer) const noexcept { return envelopeLevel[(size_t) layer].load(); }
    static juce::String envelopeParamId (int layer, const char* stage);
    float getPlaybackProgress() const noexcept { return playbackProgress.load(); }
    int getActiveVoiceCount() const noexcept { return activeVoiceCount.load(); }
    // Lock-free taps for the editor's oscilloscope, meters and note display.
    static constexpr int scopeBufferSize = 4096;
    void copyScopeSamples (float* destination, int numberOfSamples) const noexcept;
    float getPeakLeft() const noexcept { return peakLeft.load (std::memory_order_relaxed); }
    float getPeakRight() const noexcept { return peakRight.load (std::memory_order_relaxed); }
    double getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }
    int getLastNote() const noexcept { return lastNote.load (std::memory_order_relaxed); }
    float getLastVelocity() const noexcept { return lastVelocity.load (std::memory_order_relaxed); }
    unsigned int getNoteCounter() const noexcept { return noteCounter.load (std::memory_order_relaxed); }
    int getFxSlotEffect (int slot) const;
    bool getFxSlotEnabled (int slot) const;
    void setFxSlotState (int slot, int effect, bool enabled);

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void refreshEqTypes();

    struct SampleData
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        juce::String name;
        std::vector<float> peaks;
    };

    double currentSampleRate = 44100.0;
    struct Voice
    {
        double samplePosition = 0.0;
        double playbackRate = 1.0;
        double elapsed = 0.0, bendMultiplier = 1.0, tremPhase = 0.0;
        int firstFrame = 0, endFrame = 0;
        bool reverse = false;
        int midiNote = -1;
        float velocity = 1.0f;
        bool active = false;
        CurvedEnvelope envelope;
    };
    std::array<std::array<Voice, 8>, 2> voices;
    juce::SmoothedValue<float> outputGain;
    juce::AudioFormatManager formatManager;
    std::array<std::shared_ptr<SampleData>, 2> loadedSamples;
    std::vector<std::shared_ptr<SampleData>> retiredSamples;
    juce::CriticalSection sampleLoadLock;
    struct Rack
    {
        void prepare (double sampleRate, int blockSize);
        void process (OneShotSamplerAudioProcessor&, juce::AudioBuffer<float>&, double bpm, int layer);
        juce::AudioBuffer<float> delayBuffer;
        int delayWritePosition = 0;
        std::array<float, 2> delayToneState {};
        juce::dsp::Reverb reverb;
        juce::AudioBuffer<float> reverbPreDelayBuffer, reverbWetBuffer;
        int reverbPreDelayWritePosition = 0;
        std::array<float, 2> reverbToneState {};
        std::array<float, 2> reverbHighPassState {};
        juce::dsp::Phaser<float> phaser;
        juce::AudioBuffer<float> chorusBuffer, flangerBuffer, flutterBuffer;
        int chorusWritePosition = 0, flangerWritePosition = 0, flutterWritePosition = 0;
        float chorusPhase = 0.0f, flangerPhase = 0.0f;
        float flutterRandom = 0.0f, flutterRandomTarget = 0.0f;
        int flutterRandomCountdown = 0;
        std::array<float, 2> chorusFeedbackState {};
        std::array<float, 2> flangerFeedbackState {};
        juce::AudioBuffer<float> textureBuffer;
        int textureWritePosition = 0;
        float grainReadPosition = 0.0f, randomReadPosition = 0.0f;
        float textureOrbitPhase = 0.0f;
        int grainSamplesUntilReset = 0, randomSamplesUntilReset = 0;
        juce::Random textureRandom;
        juce::SmoothedValue<float> granularWet, harmonyWet, randomWet;
        std::array<std::array<juce::dsp::IIR::Filter<float>, 2>, 7> eqBands;
        std::array<std::array<float, 4>, 7> eqCache {};
        juce::SmoothedValue<float> delayMixSmooth, reverbMixSmooth, chorusMixSmooth, flangerMixSmooth, flutterMixSmooth, delayTimeSmooth;
        bool delayTimeReady = false;
        bool delayHasContent = false;
        float flutterWowPhase = 0.0f, flutterDriftPhase = 0.0f;
    };
    std::array<Rack, 2> racks;
    // EQ band types live in the value tree (message thread). The audio thread
    // only ever reads these atomic mirrors, and coefficients are rebuilt only
    // when a value actually changes.
    std::array<std::atomic<int>, 7> eqBandTypes {};
    std::array<std::array<std::atomic<float>*, 3>, 7> eqParamPointers {};
    std::array<juce::AudioBuffer<float>, 2> layerBuffers;
    std::array<std::shared_ptr<SampleData>, 2> playingSamples;
    std::array<std::atomic<float>, 2> layerProgress {};
    std::array<std::atomic<unsigned int>, 2> sampleRevision {};
    std::array<std::atomic<bool>, 2> auditionPending {};
    std::array<juce::SmoothedValue<float>, 2> layerGain, sourcePower;
    std::array<std::array<std::atomic<float>*, 4>, 2> envelopeParams {};
    std::array<std::array<std::atomic<float>*, 10>, 2> sourceParams {};
    std::array<std::atomic<float>*, 10> routeParams {};
    std::atomic<float> outputLevel { 0.0f };
    std::array<std::atomic<float>, 2> envelopeLevel {};
    std::atomic<float> playbackProgress { 0.0f };
    std::atomic<int> activeVoiceCount { 0 };
    std::array<std::atomic<float>, scopeBufferSize> scopeBuffer {};
    std::atomic<int> scopeWritePosition { 0 };
    std::atomic<float> peakLeft { 0.0f }, peakRight { 0.0f };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<int> lastNote { -1 };
    std::atomic<float> lastVelocity { 0.0f };
    std::atomic<unsigned int> noteCounter { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OneShotSamplerAudioProcessor)
};
