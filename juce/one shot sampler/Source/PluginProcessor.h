#pragma once

#include <JuceHeader.h>

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

    bool loadSample (const juce::File& file);
    juce::String getSampleName() const;
    bool hasLoadedSample() const;
    std::vector<float> getWaveformPeaks (int numberOfPoints) const;
    float getOutputLevel() const noexcept { return outputLevel.load(); }
    float getEnvelopeLevel() const noexcept { return envelopeLevel.load(); }
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
    };

    double currentSampleRate = 44100.0;
    struct Voice
    {
        double samplePosition = 0.0;
        double playbackRate = 1.0;
        int midiNote = -1;
        float velocity = 1.0f;
        bool active = false;
        juce::ADSR envelope;
    };
    std::array<Voice, 8> voices;
    juce::SmoothedValue<float> outputGain;
    juce::AudioFormatManager formatManager;
    std::shared_ptr<SampleData> loadedSample;
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
    // EQ band types live in the value tree (message thread). The audio thread
    // only ever reads these atomic mirrors, and coefficients are rebuilt only
    // when a value actually changes.
    std::array<std::atomic<int>, 7> eqBandTypes {};
    std::array<std::array<std::atomic<float>*, 3>, 7> eqParamPointers {};
    std::array<std::array<float, 4>, 7> eqCache {};
    juce::SmoothedValue<float> delayMixSmooth, reverbMixSmooth, chorusMixSmooth, flangerMixSmooth, flutterMixSmooth, delayTimeSmooth;
    bool delayTimeReady = false;
    bool delayHasContent = false;
    float flutterWowPhase = 0.0f, flutterDriftPhase = 0.0f;
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<float> envelopeLevel { 0.0f };
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
