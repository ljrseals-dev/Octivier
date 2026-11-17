#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

/**
    Octivier real-time processor.

    This is a harmonic-timbre sculptor, not a pitch shifter. No octave-up
    pitch translation is guaranteed.

    Concurrency model
    -----------------
    The audio thread performs no allocation and no deallocation. All
    coefficient data is pre-built by a dedicated worker `juce::Thread`
    into a fixed three-slot generation ring, and adopted by the audio
    thread at block boundaries only. No reference-counted coefficient
    pointer is constructed, copied, or destroyed on the audio thread.

    Slots are the ownership protocol. Each slot is in exactly one of:

        Idle     - free for the worker to claim
        Writing  - claimed by the worker; audio thread must not read
        Ready    - worker published; audio thread may adopt at block start
        Reading  - audio thread owns it for the current block

    Only the worker transitions Idle -> Writing and Writing -> Ready.
    Only the audio thread transitions Ready -> Reading and Reading -> Idle.
    At most one slot is in Reading and at most one slot is in Writing at
    any instant, and the two threads never touch the same slot in
    overlapping ownership states.

    The request mailbox is a separate single-slot SPSC channel from the
    audio thread to the worker. The audio thread claims Empty before
    writing the payload and publishes Ready with release semantics. The
    worker claims Ready with acquire and only consumes it after
    successfully claiming an Idle slot.
*/
class OctivierAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kHarmonics   = 16;
    static constexpr int kPitchFrame  = 2048;

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
    double getTailLengthSeconds() const override { return 0.10; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ---- UI telemetry (any thread, lock-free) --------------------------
    float getTrackedFundamental() const noexcept { return trackedFundamental.load(); }
    float getPitchConfidence()    const noexcept { return pitchConfidence.load(); }
    float getOddEnergyDb()        const noexcept { return oddEnergyDb.load(); }
    float getEvenEnergyDb()       const noexcept { return evenEnergyDb.load(); }
    bool  isTracking()            const noexcept { return trackingActive.load(); }

private:
    // =====================================================================
    //  Generation ring
    // =====================================================================
    static constexpr int kGenSlots   = 3;
    static constexpr int kShelfCount = 2;   // 0 = formant, 1 = air

    enum SlotState : int
    {
        SlotIdle    = 0,
        SlotWriting = 1,
        SlotReady   = 2,
        SlotReading = 3
    };

    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    };

    struct Generation
    {
        std::array<Biquad, kHarmonics>  harmonics {};
        std::array<Biquad, kShelfCount> shelves   {};
        std::array<float,  kShelfCount> shelfDb   { 0.0f, 0.0f };

        std::atomic<int>      state      { SlotIdle };
        std::atomic<uint64_t> generation { 0 };
        std::atomic<uint64_t> ackGen     { 0 };   // diagnostic only
    };

    std::array<Generation, kGenSlots> generations;
    int activeSlot = -1;   // audio thread only
    int workerSlot = -1;   // worker thread only

    // =====================================================================
    //  Per-channel biquad state (audio thread only)
    // =====================================================================
    struct BiquadState { float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f; };
    struct ChannelBank
    {
        std::array<BiquadState, kHarmonics>  harmonics {};
        std::array<BiquadState, kShelfCount> shelves   {};
    };
    std::array<ChannelBank, 2> channelBank;

    // =====================================================================
    //  Request mailbox (audio -> worker), single slot
    // =====================================================================
    struct CoeffRequest
    {
        std::atomic<bool> pending { false };
        float f0        = 0.0f;
        float strength  = 0.0f;
        float focus     = 0.0f;
        int   mode      = 0;
        float formantDb = 0.0f;
        float airDb     = 0.0f;
    };
    CoeffRequest coeffRequest;

    // =====================================================================
    //  Worker
    // =====================================================================
    class Worker : public juce::Thread
    {
    public:
        explicit Worker (OctivierAudioProcessor& p)
            : juce::Thread ("OctivierCoeffWorker"), proc (p) {}
        void run() override;
    private:
        OctivierAudioProcessor& proc;
    };
    std::unique_ptr<Worker> worker;

    // =====================================================================
    //  Pitch-tracker scratch (preallocated in prepareToPlay)
    // =====================================================================
    std::array<float, kPitchFrame> pitchScratch {};
    std::vector<double> diffScratch;
    std::vector<double> cmndScratch;
    int minLagCache = 2;
    int maxLagCache = 2;

    std::vector<float> pitchHistory;
    size_t historyWrite = 0;

    // =====================================================================
    //  Decorrelator
    // =====================================================================
    std::vector<float> delayLeft, delayRight;
    size_t delayWrite = 0;
    float  modPhase   = 0.0f;

    // =====================================================================
    //  Oversampler (2x, IIR polyphase half-band)
    // =====================================================================
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    bool oversamplerReady = false;

    // =====================================================================
    //  FFT harmonic meter (diagnostic, not calibrated)
    // =====================================================================
    static constexpr int kMeterFFTOrder = 10;          // 1024
    static constexpr int kMeterFFTSize  = 1 << kMeterFFTOrder;
    std::unique_ptr<juce::dsp::FFT> meterFFT;
    std::array<float, kMeterFFTSize>     meterTime {};
    std::array<float, kMeterFFTSize * 2> meterFreq {};
    juce::dsp::WindowingFunction<float> meterWindow {
        kMeterFFTSize, juce::dsp::WindowingFunction<float>::hann, false };
    int meterWrite = 0;

    // =====================================================================
    //  Smoothing and running state
    // =====================================================================
    double currentSampleRate = 48000.0;
    int    maxBlockSize      = 0;
    int    blockCounter      = 0;

    float smoothedF0     = 0.0f;
    float confidence     = 0.0f;
    float lastEstimate   = 0.0f;
    float smoothStrength = 0.85f;
    float smoothFocus    = 0.0f;
    float smoothMix      = 0.0f;
    float smoothOutput   = 1.0f;
    float smoothFormant  = 0.0f;
    float smoothAir      = 0.0f;
    float smoothAutoGain = 1.0f;
    float smoothCoeff    = 0.01f;

    float inputRmsRef    = 0.0f;
    float agAttackCoeff  = 0.001f;
    float agReleaseCoeff = 0.0005f;

    // =====================================================================
    //  Telemetry
    // =====================================================================
    std::atomic<float> trackedFundamental { 0.0f }, pitchConfidence { 0.0f };
    std::atomic<float> oddEnergyDb { -120.0f }, evenEnergyDb { -120.0f };
    std::atomic<bool>  trackingActive { false };

    // =====================================================================
    //  Helpers
    // =====================================================================
    static Biquad  makePeak      (double sr, float freq, float q, float gainDb) noexcept;
    static Biquad  makeHighShelf (double sr, float freq, float q, float gainDb) noexcept;
    static inline float processBiquad (const Biquad& c, BiquadState& s, float x) noexcept;

    static int  tryAdoptReadySlot (std::array<Generation, kGenSlots>& gens) noexcept;
    void  buildGeneration (int slot, const CoeffRequest& req);
    float estimatePitch (float gateDb, float sensitivity);
    void  pushPitchHistory (const float* data, int samples);
    void  updateMeter (const juce::AudioBuffer<float>& buf);

    static float readParam (const juce::AudioProcessorValueTreeState&, const char* id) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctivierAudioProcessor)
};
