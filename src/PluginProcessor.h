#pragma once

// JUCE CMake projects do NOT generate JuceHeader.h.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

class VillainAudioProcessor final : public juce::AudioProcessor
{
public:
    VillainAudioProcessor();
    ~VillainAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    static constexpr const char* paramModelId = "model";
    static constexpr const char* paramMixId   = "mix";

    static constexpr int kNumModels = 10;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static juce::StringArray getModelNames();

    // Preset helpers (file-based preset format via APVTS XML)
    bool savePresetToFile (const juce::File& file, const juce::String& presetName, juce::String& errorOut);
    bool loadPresetFromFile (const juce::File& file, juce::String& loadedPresetName, juce::String& errorOut);

    void setCurrentPresetName (const juce::String& name);
    juce::String getCurrentPresetName() const;

private:
    juce::AudioProcessorValueTreeState apvts;

    // persisted in DAW state (so the preset name shows correctly after reload)
    juce::String currentPresetName { "Default" };

    // Forward-declared DSP engine (defined in .cpp)
    class AnalogEngine;
    std::unique_ptr<AnalogEngine> engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VillainAudioProcessor)
};
