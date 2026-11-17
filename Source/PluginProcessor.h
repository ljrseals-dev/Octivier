#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include "AcfPitchTracker.h"
#include "YinPitchTracker.h"

/** Octivier Harmonic Sculptor audio processor.
    Harmonic-timbre sculptor; it does not guarantee octave translation.
    Coefficients are built on a worker thread and published through fixed
    ownership-state slots. The audio thread uses plain coefficient/state data.
*/
class OctivierAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kHarmonics = 16;
    enum class Mode { Classic = 0, Sub = 1, Hollow = 2, Square = 3 };
    enum class Character { Clean = 0, Tube = 1, Tape = 2 };

    OctivierAudioProcessor();
    ~OctivierAudioProcessor() override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.25; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float getTrackedFundamental() const noexcept { return trackedFundamental.load(std::memory_order_relaxed); }
    float getPitchConfidence() const noexcept { return pitchConfidence.load(std::memory_order_relaxed); }
    float getOddEnergyDb() const noexcept { return oddEnergyDb.load(std::memory_order_relaxed); }
    float getEvenEnergyDb() const noexcept { return evenEnergyDb.load(std::memory_order_relaxed); }
    bool isTracking() const noexcept { return trackingActive.load(std::memory_order_relaxed); }

private:
    static constexpr int kGenerationSlots = 3;
    static constexpr int kShelfCount = 2;
    static constexpr int kMeterFftOrder = 11;
    static constexpr int kMeterFftSize = 1 << kMeterFftOrder;
    enum SlotState : int { SlotIdle = 0, SlotWriting = 1, SlotReady = 2, SlotReading = 3 };
    enum MailboxState : int { MailboxEmpty = 0, MailboxWriting = 1, MailboxReady = 2, MailboxReading = 3 };

    struct Biquad { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };
    static constexpr Biquad identityBiquad() noexcept { return { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f }; }
    struct BiquadState { float s1 = 0.0f, s2 = 0.0f; };
    struct Generation
    {
        std::array<Biquad, kHarmonics> harmonics {};
        std::array<Biquad, kShelfCount> shelves {};
        float shelfDb[kShelfCount] { 0.0f, 0.0f };
        std::atomic<int> state { SlotIdle };
        std::atomic<std::uint64_t> generation { 0 };
        std::atomic<std::uint64_t> ackGen { 0 }; // diagnostic only
    };
    struct CoeffRequest
    {
        float f0 = 0.0f, strength = 0.0f, focus = 0.0f;
        float oddCut = 18.0f, evenBoost = 8.0f, formantDb = 0.0f, airDb = 0.0f;
        int mode = 0;
    };
    struct RequestMailbox
    {
        std::atomic<int> state { MailboxEmpty };
        CoeffRequest payload {};
    };
    struct ChannelBank
    {
        std::array<BiquadState, kHarmonics> harmonics {};
        std::array<BiquadState, kShelfCount> shelves {};
    };
    class Worker final : public juce::Thread
    {
    public:
        explicit Worker (OctivierAudioProcessor& p) : juce::Thread ("OctivierCoeffWorker"), owner (p) {}
        void run() override;
    private:
        OctivierAudioProcessor& owner;
    };

    double currentSampleRate = 48000.0;
    int blockCounter = 0;
    std::vector<float> pitchHistory, delayLeft, delayRight, yinFrameScratch;
    AcfPitchTracker acfTracker;
    YinPitchTracker yinTracker;
    std::unique_ptr<juce::dsp::FFT> meterFft;
    std::array<float, kMeterFftSize> meterWindow {};
    std::array<float, 2 * kMeterFftSize> meterBuffer {};
    std::array<float, kMeterFftSize> meterRing {};
    size_t meterWrite = 0;
    std::uint64_t meterBlockCounter = 0;
    size_t historyWrite = 0, delayWrite = 0;
    std::uint64_t modSampleCount = 0;
    float smoothedF0 = 0.0f, confidence = 0.0f;
    float smoothStrength = 0.85f, smoothFocus = 0.0f, smoothMix = 0.0f, smoothOutput = 1.0f;
    std::array<Generation, kGenerationSlots> generations;
    std::array<ChannelBank, 2> channelBank;
    RequestMailbox coeffRequest;
    std::unique_ptr<Worker> worker;
    int activeSlot = -1; // audio-thread-owned; never read by worker
    std::atomic<float> trackedFundamental { 0.0f }, pitchConfidence { 0.0f };
    std::atomic<float> oddEnergyDb { -120.0f }, evenEnergyDb { -120.0f };
    std::atomic<bool> trackingActive { false };

    float estimatePitch (float gateDb, float sensitivity);
    float estimatePitchYinForValidation (float gateDb, float& outConfidence) noexcept;
    void pushPitchHistory (const float* data, int samples);
    bool publishRequest (const CoeffRequest&) noexcept;
    void buildGeneration (Generation&, const CoeffRequest&);
    static Biquad makePeak (double sr, float freq, float q, float gainDb) noexcept;
    static Biquad makeHighShelf (double sr, float freq, float slope, float gainDb) noexcept;
    static float processBiquad (const Biquad&, BiquadState&, float) noexcept;
    static float readParam (const juce::AudioProcessorValueTreeState&, const char* id) noexcept;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctivierAudioProcessor)
};
