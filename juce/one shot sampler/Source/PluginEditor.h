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
    juce::Rectangle<float> getGraphArea() const;
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
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::function<void()> onPress;
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

// The Beta 2 header uses one custom selector instead of JUCE's generic
// ComboBox. It keeps the active module's preset context visible without an
// operating-system-style black menu breaking the processing-board language.
class PresetSelector final : public juce::Component
{
public:
    void setLayoutBounds (juce::Rectangle<int> bounds);
    void setModule (int effect, int selectedPreset);
    void setOpen (bool shouldOpen);
    bool isOpen() const noexcept { return isOpen_; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    std::function<void (int)> onModuleSelected;
    std::function<void (int, int)> onPresetSelected;

private:
    static juce::Colour accentForEffect (int effect);
    static juce::String labelForEffect (int effect);
    static juce::String presetNameFor (int effect, int preset);
    static juce::String presetIdFor (int effect, int preset);
    static juce::String presetMetaFor (int effect, int preset);
    static int effectForFilterIndex (int index);
    static int filterIndexForEffect (int effect);
    juce::Rectangle<int> pillBounds() const;
    juce::Rectangle<int> menuBounds() const;
    juce::Rectangle<int> filterBounds (int index) const;
    juce::Rectangle<int> presetRowBounds (int index) const;
    void updateBounds();

    juce::Rectangle<int> closedBounds;
    int activeEffect = 1;
    int filteredEffect = 1;
    int selectedPreset = 0;
    int hoveredPreset = -1;
    bool isOpen_ = false;
};

// A hands-on macro surface for Beta 2. It is deliberately independent from
// the audio graph: the editor maps its X/Y position to safe parameter ranges.
class PerformancePad final : public juce::Component, private juce::Timer
{
public:
    PerformancePad();
    void setPosition (float newX, float newY, bool notify = false);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    std::function<void (float, float)> onPositionChanged;

private:
    void updateFromPointer (juce::Point<float> point);
    void timerCallback() override;
    void pushTrailPoint();
    float positionX = 0.32f;
    float positionY = 0.42f;
    float targetX = 0.32f;
    float targetY = 0.42f;
    float animationPhase = 0.0f;
    std::array<juce::Point<float>, 32> trail {};
    int trailHead = 0;
    bool trailInitialised = false;
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
    bool isEffectEnabled (int effect) const;
    void setEffectEnabled (int effect, bool shouldBeEnabled);
    void removeEffectFromSlot (int slot);
    void setSlotEnabled (int slot, bool shouldBeEnabled);
    void refreshEffectEnableStates();
    void setPerformancePosition (float x, float y);
    void applyPerformanceScene (int scene);
    void rebuildBeta2DotLayers();
    void timerCallback() override;

    OneShotSamplerAudioProcessor& audioProcessor;
    FrameworksLookAndFeel frameworksLookAndFeel;
    TextureLookAndFeel textureLookAndFeel;
    EnvelopePad envelopePad;
    EqCurvePad eqCurvePad;
    PresetSelector presetSelector;
    PerformancePad performancePad;
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
    juce::TextButton cleanSceneButton { "CLEAN" }, wideSceneButton { "WIDE" }, warpSceneButton { "WARP" }, chaosSceneButton { "CHAOS" };
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
    int activePerformanceScene = 0;
    // Per-card presentation state. These only affect the UI, never DSP or
    // routing, so activation remains immediate and automation-safe.
    std::array<float, 5> fxCardReveal {};
    std::array<float, 5> fxCardPulse {};
    // Meter smoothing belongs on the UI thread. It makes signal-reactive
    // motion feel musical while keeping the processor lock-free.
    float visualSignalLevel = 0.0f;
    juce::Image modularBadge;
    juce::Image darkFrameworksMark;
    juce::Image frameworksHalftoneMark;
    // Static pegboard layers are rasterised once when the fixed Beta 2 board
    // lays out. The live paint path only has to draw motion and controls.
    juce::Image beta2CanvasDotLayer;
    juce::Image beta2PanelDotLayer;
    // Beta 2 is a single processing board.  These hit areas keep the visual
    // chain and the actual editor selection in lock-step.
    juce::Rectangle<int> beta2SourceBounds, beta2ModuleBounds, beta2SignalBounds;
    juce::Rectangle<int> beta2EnvelopeBounds, beta2OutputBounds, beta2ChainBounds;
    std::array<juce::Rectangle<int>, 4> beta2SceneBounds;
    juce::Rectangle<int> beta2PresetBounds;
    std::array<juce::Rectangle<int>, 5> beta2EffectTabBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OneShotSamplerAudioProcessorEditor)
};
