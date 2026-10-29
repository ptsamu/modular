#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class FrameworksLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    int getCallOutBoxBorderSize (const juce::CallOutBox&) override;
    float getCallOutBoxCornerSize (const juce::CallOutBox&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
};

class TextureLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
};

class EffectPatchMenu final : public juce::Component
{
public:
    EffectPatchMenu (FrameworksLookAndFeel&, std::function<void (int)> onEffectChosen, int activeEffect);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void setActiveEffect (int effect);
private:
    FrameworksLookAndFeel& lookAndFeel;
    std::function<void (int)> chooseEffect;
    int activeEffect = 1;
    std::array<juce::TextButton, 2> effectButtons { juce::TextButton ("EQ"), juce::TextButton ("FLUTTER") };
};

class EnvelopePad final : public juce::Component,
                          private juce::Timer
{
public:
    EnvelopePad (juce::AudioProcessorValueTreeState& state, OneShotSamplerAudioProcessor& processor);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    std::array<juce::Point<float>, 4> getHandles() const;
    void setParameter (const char* id, float value);

    juce::AudioProcessorValueTreeState& parameters;
    OneShotSamplerAudioProcessor& audioProcessor;
    int draggedHandle = -1;
    float animationPhase = 0.0f;
};

class FxSlotButton final : public juce::TextButton
{
public:
    explicit FxSlotButton (int slotIndex) : slot (slotIndex) {}
    void mouseDrag (const juce::MouseEvent&) override;
private:
    int slot = 0;
};

class EqCurvePad final : public juce::Component
{
public:
    EqCurvePad (juce::AudioProcessorValueTreeState& state, OneShotSamplerAudioProcessor& processor)
        : parameters (state), audioProcessor (processor) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    std::array<juce::Point<float>, 7> getHandles() const;
    float getResponseAtFrequency (float frequency) const;
    void setBand (int band, float frequency, float gain);
    int draggedBand = -1;
    juce::Point<float> dragStart;
    bool dragHasMoved = false;
    juce::AudioProcessorValueTreeState& parameters;
    OneShotSamplerAudioProcessor& audioProcessor;
};

class OneShotSamplerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 public juce::FileDragAndDropTarget,
                                                 public juce::DragAndDropContainer,
                                                 public juce::DragAndDropTarget,
                                                 private juce::Timer
{
public:
    explicit OneShotSamplerAudioProcessorEditor (OneShotSamplerAudioProcessor&);
    ~OneShotSamplerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void showFxPage (bool shouldShowFx);
    void showTexturePage();
    void chooseEffectForSlot (int slot);
    void selectEffect (int effect);
    void toggleEffect (int effect);
    void removeEffectFromSlot (int slot);
    void setSlotEnabled (int slot, bool shouldBeEnabled);
    void refreshEffectEnableStates();
    void timerCallback() override;

    OneShotSamplerAudioProcessor& audioProcessor;
    FrameworksLookAndFeel frameworksLookAndFeel;
    TextureLookAndFeel textureLookAndFeel;
    EnvelopePad envelopePad;
    EqCurvePad eqCurvePad;
    juce::Slider outputGainSlider;
    juce::Label outputGainLabel;
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    juce::Slider delayTimeSlider, delayFeedbackSlider, delayMixSlider, reverbMixSlider, reverbRoomSizeSlider, reverbDampingSlider, reverbWidthSlider, reverbFreezeSlider;
    juce::Slider chorusRateSlider, chorusDepthSlider, chorusDelaySlider, chorusFeedbackSlider, chorusMixSlider;
    juce::Slider phaserRateSlider, phaserDepthSlider, phaserFeedbackSlider, phaserMixSlider;
    juce::Slider flangerRateSlider, flangerDepthSlider, flangerDelaySlider, flangerFeedbackSlider, flangerMixSlider;
    juce::Slider flutterRateSlider, flutterDepthSlider, flutterDelaySlider, flutterMixSlider;
    juce::Slider granularSizeSlider, granularDensitySlider, granularPitchSlider, granularMixSlider, harmonyShiftSlider, harmonyDetuneSlider, harmonyMixSlider, randomAmountSlider, randomRateSlider, randomMixSlider;
    juce::Slider eqLowSlider, eqMidSlider, eqHighSlider;
    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel;
    juce::Label delayTimeLabel, delayFeedbackLabel, delayMixLabel, reverbMixLabel, reverbRoomSizeLabel, reverbDampingLabel, reverbWidthLabel, reverbFreezeLabel;
    juce::Label chorusRateLabel, chorusDepthLabel, chorusDelayLabel, chorusFeedbackLabel, chorusMixLabel;
    juce::Label phaserRateLabel, phaserDepthLabel, phaserFeedbackLabel, phaserMixLabel;
    juce::Label flangerRateLabel, flangerDepthLabel, flangerDelayLabel, flangerFeedbackLabel, flangerMixLabel;
    juce::Label flutterRateLabel, flutterDepthLabel, flutterDelayLabel, flutterMixLabel;
    juce::Label granularSizeLabel, granularDensityLabel, granularPitchLabel, granularMixLabel, harmonyShiftLabel, harmonyDetuneLabel, harmonyMixLabel, randomAmountLabel, randomRateLabel, randomMixLabel;
    juce::Label eqLowLabel, eqMidLabel, eqHighLabel;
    juce::TextButton samplerTab { "SAMPLER" }, fxTab { "FX" }, textureTab { "TEXTURE LAB" };
    juce::TextButton delayEnabledButton { "OFF" }, reverbEnabledButton { "OFF" }, chorusEnabledButton { "OFF" }, phaserEnabledButton { "OFF" }, flangerEnabledButton { "OFF" };
    juce::TextButton eqEnabledButton { "OFF" };
    juce::TextButton flutterEnabledButton { "OFF" };
    juce::TextButton granularEnabledButton { "OFF" }, harmonyEnabledButton { "OFF" }, randomEnabledButton { "OFF" };
    juce::TextButton granularTextureTab { "GRANULAR" }, harmonyTextureTab { "ORBIT" }, randomTextureTab { "RANDOM" };
    juce::ComboBox delayPreset, reverbPreset, chorusPreset, phaserPreset, flangerPreset, flutterPreset, granularPreset, harmonyPreset, randomPreset;
    juce::ComboBox eqPreset;
    juce::TextButton loadButton { "LOAD SAMPLE" };
    FxSlotButton fxSlotOne { 0 }, fxSlotTwo { 1 }, fxSlotThree { 2 }, fxSlotFour { 3 }, fxSlotFive { 4 }, fxSlotSix { 5 }, fxSlotSeven { 6 };
    juce::TextButton fxSlotToggleOne { "OFF" }, fxSlotToggleTwo { "OFF" }, fxSlotToggleThree { "OFF" }, fxSlotToggleFour { "OFF" }, fxSlotToggleFive { "OFF" }, fxSlotToggleSix { "OFF" }, fxSlotToggleSeven { "OFF" };
    juce::Label sampleLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment, decayAttachment, sustainAttachment, releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayTimeAttachment, delayFeedbackAttachment, delayMixAttachment, reverbMixAttachment, reverbRoomSizeAttachment, reverbDampingAttachment, reverbWidthAttachment, reverbFreezeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chorusRateAttachment, chorusDepthAttachment, chorusDelayAttachment, chorusFeedbackAttachment, chorusMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> phaserRateAttachment, phaserDepthAttachment, phaserFeedbackAttachment, phaserMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flangerRateAttachment, flangerDepthAttachment, flangerDelayAttachment, flangerFeedbackAttachment, flangerMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flutterRateAttachment, flutterDepthAttachment, flutterDelayAttachment, flutterMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> granularSizeAttachment, granularDensityAttachment, granularPitchAttachment, granularMixAttachment, harmonyShiftAttachment, harmonyDetuneAttachment, harmonyMixAttachment, randomAmountAttachment, randomRateAttachment, randomMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqLowAttachment, eqMidAttachment, eqHighAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> delayEnabledAttachment, reverbEnabledAttachment, chorusEnabledAttachment, phaserEnabledAttachment, flangerEnabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqEnabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> flutterEnabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> granularEnabledAttachment, harmonyEnabledAttachment, randomEnabledAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool showingFx = false;
    bool showingTexture = false;
    int textureEngine = 0;
    // The rack is a fixed seven-effect instrument panel. It always opens with
    // every module visible, while every module remains off until enabled.
    int selectedFx = 1;
    std::array<int, 7> fxChain { 0, 1, 2, 3, 4, 5, 6 };
    std::array<bool, 7> fxSlotEnabled { false, false, false, false, false, false, false };
    std::unique_ptr<EffectPatchMenu> effectPickerMenu;
    int effectPickerSlot = -1;
    float fxVisualPhase = 0.0f;
    juce::Image modularBadge;
    juce::Image darkFrameworksMark;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OneShotSamplerAudioProcessorEditor)
};
