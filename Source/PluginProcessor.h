#pragma once
#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

class DrumRandomizerProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numLanes = 6;   // kick, snare, clap, closed hat, open hat, perc

    DrumRandomizerProcessor();
    ~DrumRandomizerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Drum Randomizer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float> *pStyle, *pBars, *pVariation, *pFill, *pSwing,
        *pHumanize, *pSeed, *pEvolve, *pChannel;
    std::array<std::atomic<float>*, numLanes> pOn {}, pNote {};

    // per-lane note state, so every drum note gets a matching note-off
    std::array<bool, numLanes>   laneOn {};
    std::array<int, numLanes>    laneNote {};
    std::array<int, numLanes>    laneChannel {};
    std::array<double, numLanes> laneOffPpq {};

    double lastEndPpq = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumRandomizerProcessor)
};
