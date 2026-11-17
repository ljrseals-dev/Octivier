#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr float kMinF0 = 55.0f;
    constexpr float kMaxF0 = 1200.0f;

    inline float dbToGain (float db) noexcept
    {
        return std::pow (10.0f, db / 20.0f);
    }

    inline float gainToDb (float g) noexcept
    {
        return 20.0f * std::log10 (juce::jmax (g, 1.0e-6f));
    }
}

// ===========================================================================
//  Parameter layout
// ===========================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
OctivierAudioProcessor::createParameterLayout()
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    using APC = juce::AudioParameterChoice;

    juce::AudioProcessorValueTreeState::ParameterLayout p;

    p.add (std::make_unique<APF> ("strength", "Octave Strength",
           juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.78f));
    p.add (std::make_unique<APF> ("focus", "Harmonic Focus",
           juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    p.add (std::make_unique<APF> ("oddCut", "Odd Cut",
           juce::NormalisableRange<float> (0.0f, 36.0f, 0.1f), 18.0f));
    p.add (std::make_unique<APF> ("evenBoost", "Even Boost",
           juce::NormalisableRange<float> (0.0f, 18.0f, 0.1f), 8.0f));
    p.add (std::make_unique<APF> ("tracking", "Tracking Sensitivity",
           juce::NormalisableRange<float> (0.1f, 1.0f, 0.001f), 0.62f));
    p.add (std::make_unique<APF> ("gate", "Gate (dBFS)",
           juce::NormalisableRange<float> (-90.0f, -18.0f, 0.1f), -55.0f));
    p.add (std::make_unique<APF> ("delay", "Decorrelate Delay (ms)",
           juce::NormalisableRange<float> (1.0f, 80.0f, 0.1f), 42.0f));
    p.add (std::make_unique<APF> ("width", "Width",
           juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.55f));
    p.add (std::make_unique<APF> ("modDepth", "Delay Mod Depth (ms)",
           juce::NormalisableRange<float> (0.0f, 4.0f, 0.01f), 0.35f));
    p.add (std::make_unique<APF> ("formant", "Formant Shelf (dB)",
           juce::NormalisableRange<float> (-9.0f, 9.0f, 0.1f), 0.0f));
    p.add (std::make_unique<APF> ("air", "Air Shelf (dB)",
           juce::NormalisableRange<float> (-9.0f, 9.0f, 0.1f), 0.0f));
    p.add (std::make_unique<APC> ("mode", "Mode",
           juce::StringArray { "Classic", "Sub", "Hollow", "Square" }, 0));
    p.add (std::make_unique<APC> ("character", "Character",
           juce::StringArray { "Clean", "Tube", "Tape" }, 0));
    p.add (std::make_unique<APF> ("characterAmt", "Character Amount",
           juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    p.add (std::make_unique<APF> ("mix", "Dry Blend",
           juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    p.add (std::make_unique<APF> ("output", "Output (dB)",
           juce::NormalisableRange<float> (-18.0f, 12.0f, 0.1f), 0.0f));
    p.add (std::make_unique<APB> ("autoGain", "RMS Compensation", true));
    p.add (std::make_unique<APB> ("freeze", "Freeze Tracking", false));
    p.add (std::make_unique<APF> ("manualF0", "Manual Fundamental (Hz)",
           juce::NormalisableRange<float> (55.0f, 1200.0f, 0.1f), 220.0f));

    return p;
}

// ===========================================================================
//  Lifecycle
// ===========================================================================
OctivierAudioProcessor::OctivierAudioProcessor()
 : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
   parameters (*this, nullptr, "OCTIVIER_STATE", createParameterLayout())
{
    worker = std::make_unique<Worker> (*this);
    worker->startThread (juce::Thread::Priority::low);
}

OctivierAudioProcessor::~OctivierAudioProcessor()
{
    if (worker)
    {
        worker->signalThreadShouldExit();
        worker->stopThread (1000);
    }
}

bool OctivierAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
        && in == out;
}

void OctivierAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Stop the worker while we reconfigure shared state. The host lifecycle
    // guarantees prepareToPlay is not concurrent with processBlock.
    if (worker)
    {
        worker->signalThreadShouldExit();
        worker->stopThread (1000);
    }

    currentSampleRate = juce::jmax (sampleRate, 8000.0);
    maxBlockSize      = juce::jmax (samplesPerBlock, 32);

    // ---- pitch history ----
    pitchHistory.assign (static_cast<size_t> (currentSampleRate * 0.12), 0.0f);
    historyWrite = 0;

    // ---- pitch scratch ----
    minLagCache = juce::jmax (2, static_cast<int> (currentSampleRate / kMaxF0));
    maxLagCache = juce::jmin (kPitchFrame / 2, static_cast<int> (currentSampleRate / kMinF0));
    diffScratch.assign (static_cast<size_t> (maxLagCache + 1), 0.0);
    cmndScratch.assign (static_cast<size_t> (maxLagCache + 1), 0.0);

    // ---- delay line ----
    const auto maxDelaySamples =
        static_cast<size_t> (currentSampleRate * (0.080 + 0.004)) +
        static_cast<size_t> (samplesPerBlock) + 16;
    delayLeft.assign  (maxDelaySamples, 0.0f);
    delayRight.assign (maxDelaySamples, 0.0f);
    delayWrite = 0;
    modPhase   = 0.0f;

    // ---- smoothing constants ----
    smoothCoeff    = 1.0f - std::exp (-1.0f / (0.020f * static_cast<float> (currentSampleRate)));
    agAttackCoeff  = 1.0f - std::exp (-1.0f / (0.050f * static_cast<float> (currentSampleRate)));
    agReleaseCoeff = 1.0f - std::exp (-1.0f / (0.150f * static_cast<float> (currentSampleRate)));
    inputRmsRef    = 0.0f;
    smoothAutoGain = 1.0f;

    // ---- generation ring ----
    for (auto& g : generations)
    {
        g.state.store (SlotIdle, std::memory_order_relaxed);
        g.generation.store (0, std::memory_order_relaxed);
        g.ackGen.store (0, std::memory_order_relaxed);
        for (auto& c : g.harmonics)
            c = makePeak (currentSampleRate, 1000.0f, 1.0f, 0.0f);
        g.shelves[0] = makeHighShelf (currentSampleRate, 3200.0f,  0.707f, 0.0f);
        g.shelves[1] = makeHighShelf (currentSampleRate, 10000.0f, 0.707f, 0.0f);
        g.shelfDb[0] = 0.0f;
        g.shelfDb[1] = 0.0f;
    }
    // Publish slot 0 as the initial neutral generation so the very first
    // processBlock has something valid to adopt.
    generations[0].state.store (SlotReady, std::memory_order_release);
    generations[0].generation.store (1, std::memory_order_release);
    activeSlot = -1;
    workerSlot = -1;

    // ---- per-channel filter state ----
    for (auto& cbank : channelBank)
    {
        for (auto& s : cbank.harmonics) s = {};
        for (auto& s : cbank.shelves)   s = {};
    }

    // ---- request mailbox ----
    coeffRequest.pending.store (false, std::memory_order_relaxed);

    // ---- oversampler ----
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing (static_cast<size_t> (samplesPerBlock));
    oversamplerReady = true;

    // ---- FFT meter ----
    meterFFT = std::make_unique<juce::dsp::FFT> (kMeterFFTOrder);
    meterTime.fill (0.0f);
    meterFreq.fill (0.0f);
    meterWrite = 0;

    // ---- running state ----
    smoothedF0     = 0.0f;
    confidence     = 0.0f;
    lastEstimate   = 0.0f;
    blockCounter   = 0;
    smoothStrength = readParam (parameters, "strength");
    smoothFocus    = readParam (parameters, "focus");
    smoothMix      = readParam (parameters, "mix");
    smoothOutput   = dbToGain (readParam (parameters, "output"));
    smoothFormant  = readParam (parameters, "formant");
    smoothAir      = readParam (parameters, "air");

    // ---- latency ----
    if (oversampler)
        setLatencySamples (static_cast<int> (oversampler->getLatencyInSamples()));

    // ---- restart the worker ----
    if (worker)
        worker->startThread (juce::Thread::Priority::low);
}

void OctivierAudioProcessor::releaseResources()
{
    if (worker)
    {
        worker->signalThreadShouldExit();
        worker->stopThread (1000);
    }
    oversamplerReady = false;
}

// ===========================================================================
//  Biquad construction (worker-thread side)
// ===========================================================================
OctivierAudioProcessor::Biquad
OctivierAudioProcessor::makePeak (double sr, float freq, float q, float gainDb) noexcept
{
    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = 2.0 * juce::MathConstants<double>::pi * freq / sr;
    const double cosw  = std::cos (w0);
    const double sinw  = std::sin (w0);
    const double alpha = sinw / (2.0 * q);

    const double b0 =  1.0 + alpha * A;
    const double b1 = -2.0 * cosw;
    const double b2 =  1.0 - alpha * A;
    const double a0 =  1.0 + alpha / A;
    const double a1 = -2.0 * cosw;
    const double a2 =  1.0 - alpha / A;

    Biquad c;
    c.b0 = static_cast<float> (b0 / a0);
    c.b1 = static_cast<float> (b1 / a0);
    c.b2 = static_cast<float> (b2 / a0);
    c.a1 = static_cast<float> (a1 / a0);
    c.a2 = static_cast<float> (a2 / a0);
    return c;
}

OctivierAudioProcessor::Biquad
OctivierAudioProcessor::makeHighShelf (double sr, float freq, float q, float gainDb) noexcept
{
    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = 2.0 * juce::MathConstants<double>::pi * freq / sr;
    const double cosw  = std::cos (w0);
    const double sinw  = std::sin (w0);
    const double alpha = sinw / (2.0 * q);
    const double sqrtA = std::sqrt (A);

    const double b0 =    A * ((A + 1.0) + (A - 1.0) * cosw + 2.0 * sqrtA * alpha);
    const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw);
    const double b2 =    A * ((A + 1.0) + (A - 1.0) * cosw - 2.0 * sqrtA * alpha);
    const double a0 =        (A + 1.0) - (A - 1.0) * cosw + 2.0 * sqrtA * alpha;
    const double a1 =    2.0 * ((A - 1.0) - (A + 1.0) * cosw);
    const double a2 =        (A + 1.0) - (A - 1.0) * cosw - 2.0 * sqrtA * alpha;

    Biquad c;
    c.b0 = static_cast<float> (b0 / a0);
    c.b1 = static_cast<float> (b1 / a0);
    c.b2 = static_cast<float> (b2 / a0);
    c.a1 = static_cast<float> (a1 / a0);
    c.a2 = static_cast<float> (a2 / a0);
    return c;
}

inline float OctivierAudioProcessor::processBiquad (const Biquad& c,
                                                     BiquadState& s, float x) noexcept
{
    // Transposed direct form II
    const float y = c.b0 * x + s.x1;
    s.x1 = c.b1 * x - c.a1 * y + s.x2;
    s.x2 = c.b2 * x - c.a2 * y;
    return y;
}

// ===========================================================================
//  Generation build (worker thread)
// ===========================================================================
void OctivierAudioProcessor::buildGeneration (int slot, const CoeffRequest& req)
{
    auto& gen = generations[static_cast<size_t> (slot)];

    const float oddCut    = readParam (parameters, "oddCut");
    const float evenBoost = readParam (parameters, "evenBoost");
    const float nyq       = static_cast<float> (currentSampleRate * 0.5);
    const float ceiling   = nyq * 0.95f;

    for (int h = 1; h <= kHarmonics; ++h)
    {
        const float hz  = req.f0 * static_cast<float> (h);
        const bool  odd = (h & 1) != 0;

        const float centre = 2.0f + (req.focus + 1.0f) * 3.0f;
        const float w = std::exp (-0.5f * std::pow ((static_cast<float> (h) - centre) / 2.7f, 2.0f));

        float db = 0.0f;
        switch (req.mode)
        {
            case 0: db = odd ? -oddCut * req.strength :  evenBoost * req.strength * w; break;
            case 1: db = odd ?  evenBoost * req.strength * w : -oddCut * req.strength; break;
            case 2: db = odd ? -oddCut * req.strength : 0.0f;                          break;
            case 3: db = odd ?  evenBoost * req.strength * w : 0.0f;                   break;
        }

        if (hz < 25.0f || hz >= ceiling)
            gen.harmonics[static_cast<size_t> (h - 1)] =
                makePeak (currentSampleRate, 1000.0f, 1.0f, 0.0f);
        else
        {
            const float q = juce::jlimit (2.0f, 14.0f, 7.5f + 5.0f * req.strength);
            gen.harmonics[static_cast<size_t> (h - 1)] =
                makePeak (currentSampleRate, hz, q, db);
        }
    }

    gen.shelves[0] = makeHighShelf (currentSampleRate, 3200.0f,  0.707f, req.formantDb);
    gen.shelves[1] = makeHighShelf (currentSampleRate, 10000.0f, 0.707f, req.airDb);
    gen.shelfDb[0] = req.formantDb;
    gen.shelfDb[1] = req.airDb;
}

// ===========================================================================
//  Worker
// ===========================================================================
void OctivierAudioProcessor::Worker::run()
{
    while (! threadShouldExit())
    {
        if (proc.coeffRequest.pending.load (std::memory_order_acquire))
        {
            // Snapshot the request. The audio thread will not touch the
            // payload while pending is true.
            CoeffRequest local;
            local.f0        = proc.coeffRequest.f0;
            local.strength  = proc.coeffRequest.strength;
            local.focus     = proc.coeffRequest.focus;
            local.mode      = proc.coeffRequest.mode;
            local.formantDb = proc.coeffRequest.formantDb;
            local.airDb     = proc.coeffRequest.airDb;

            // Find an Idle slot. Do NOT steal a Ready or Reading slot.
            int chosen = -1;
            for (int i = 0; i < kGenSlots; ++i)
            {
                if (proc.generations[i].state.load (std::memory_order_acquire) == SlotIdle)
                { chosen = i; break; }
            }

            if (chosen >= 0)
            {
                auto& slot = proc.generations[static_cast<size_t> (chosen)];
                slot.state.store (SlotWriting, std::memory_order_release);

                proc.buildGeneration (chosen, local);

                slot.generation.fetch_add (1, std::memory_order_release);
                slot.state.store (SlotReady, std::memory_order_release);

                proc.workerSlot = chosen;

                // Consume the request ONLY after a successful publish.
                proc.coeffRequest.pending.store (false, std::memory_order_release);
            }
            // else: no slot available; leave pending set. Retry next cycle.
        }

        juce::Thread::sleep (2);
    }
}

// ===========================================================================
//  Pitch tracking
// ===========================================================================
void OctivierAudioProcessor::pushPitchHistory (const float* data, int samples)
{
    const auto n = pitchHistory.size();
    if (n == 0) return;
    for (int i = 0; i < samples; ++i)
    {
        pitchHistory[historyWrite] = data[i];
        historyWrite = (historyWrite + 1) % n;
    }
}

float OctivierAudioProcessor::estimatePitch (float gateDb, float sensitivity)
{
    const size_t n = pitchHistory.size();
    if (n < static_cast<size_t> (kPitchFrame)) return 0.0f;

    // Read the most recent kPitchFrame samples in chronological order.
    double mean = 0.0;
    for (int i = 0; i < kPitchFrame; ++i)
    {
        const auto idx = (historyWrite + n - kPitchFrame + static_cast<size_t> (i)) % n;
        pitchScratch[static_cast<size_t> (i)] = pitchHistory[idx];
        mean += pitchScratch[static_cast<size_t> (i)];
    }
    mean /= static_cast<double> (kPitchFrame);

    double energy = 0.0;
    for (auto& v : pitchScratch) { v -= static_cast<float> (mean); energy += v * v; }

    const float rms = static_cast<float> (std::sqrt (energy / kPitchFrame));
    if (gainToDb (rms) < gateDb) { confidence *= 0.75f; return 0.0f; }

    const int minLag = minLagCache;
    const int maxLag = maxLagCache;
    if (maxLag <= minLag + 2) return 0.0f;

    // Canonical YIN difference function, tau = 1 .. maxLag
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double sum = 0.0;
        const int m = kPitchFrame - tau;
        for (int i = 0; i < m; ++i)
        {
            const double diff = pitchScratch[static_cast<size_t> (i)]
                              - pitchScratch[static_cast<size_t> (i + tau)];
            sum += diff * diff;
        }
        diffScratch[static_cast<size_t> (tau)] = sum;
    }

    // Cumulative mean normalized difference, from tau = 1
    cmndScratch[0] = 1.0;
    cmndScratch[1] = 1.0;
    double running = diffScratch[1];
    for (int tau = 2; tau <= maxLag; ++tau)
    {
        running += diffScratch[static_cast<size_t> (tau)];
        cmndScratch[static_cast<size_t> (tau)] = (running > 1e-12)
            ? diffScratch[static_cast<size_t> (tau)] * tau / running : 1.0;
    }

    // First local minimum below threshold, restricted to [minLag, maxLag]
    const double threshold = 0.10 + 0.30 * (1.0 - static_cast<double> (sensitivity));
    int bestTau = -1;
    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        if (cmndScratch[static_cast<size_t> (tau)] < threshold)
        {
            int k = tau;
            while (k + 1 <= maxLag &&
                   cmndScratch[static_cast<size_t> (k + 1)] < cmndScratch[static_cast<size_t> (k)])
                ++k;
            bestTau = k;
            break;
        }
    }
    if (bestTau < 0)
    {
        double best = 1e30;
        for (int tau = minLag; tau <= maxLag; ++tau)
            if (cmndScratch[static_cast<size_t> (tau)] < best)
            { best = cmndScratch[static_cast<size_t> (tau)]; bestTau = tau; }
        if (bestTau < 0 || best > 0.60) { confidence *= 0.8f; return 0.0f; }
    }

    // Parabolic refinement
    double tauF = bestTau;
    if (bestTau > minLag && bestTau < maxLag)
    {
        const double a = cmndScratch[static_cast<size_t> (bestTau - 1)];
        const double b = cmndScratch[static_cast<size_t> (bestTau)];
        const double c = cmndScratch[static_cast<size_t> (bestTau + 1)];
        const double denom = a - 2.0 * b + c;
        if (std::abs (denom) > 1e-12)
            tauF += 0.5 * (a - c) / denom;
    }

    float f0 = static_cast<float> (currentSampleRate / tauF);
    if (f0 < kMinF0 || f0 > kMaxF0) { confidence *= 0.8f; return 0.0f; }

    // Octave-error guard relative to previous estimate
    if (lastEstimate > 0.0f)
    {
        const float r = f0 / lastEstimate;
        if (r > 1.85f && r < 2.15f) f0 *= 0.5f;
        else if (r > 0.46f && r < 0.54f) f0 *= 2.0f;
    }
    lastEstimate = f0;

    const double depth = 1.0 - cmndScratch[static_cast<size_t> (bestTau)];
    confidence = juce::jlimit (0.0f, 1.0f, static_cast<float> ((depth - 0.15) / 0.75));
    return f0;
}

// ===========================================================================
//  Slot adoption
// ===========================================================================
int OctivierAudioProcessor::tryAdoptReadySlot (std::array<Generation, kGenSlots>& gens) noexcept
{
    for (int i = 0; i < kGenSlots; ++i)
    {
        int expected = SlotReady;
        if (gens[static_cast<size_t> (i)].state.compare_exchange_strong (
                expected, SlotReading,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
            return i;
    }
    return -1;
}

// ===========================================================================
//  FFT harmonic meter (diagnostic, not calibrated)
// ===========================================================================
void OctivierAudioProcessor::updateMeter (const juce::AudioBuffer<float>& buf)
{
    if (! meterFFT) return;

    const int n     = buf.getNumSamples();
    const int chans = buf.getNumChannels();

    for (int i = 0; i < n; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < chans; ++ch) mono += buf.getSample (ch, i);
        mono /= static_cast<float> (chans);

        meterTime[static_cast<size_t> (meterWrite)] = mono;
        meterWrite = (meterWrite + 1) % kMeterFFTSize;

        if (meterWrite == 0)
        {
            std::copy (meterTime.begin(), meterTime.end(), meterFreq.begin());
            meterWindow.multiplyWithWindowingTable (meterFreq.data(), kMeterFFTSize);
            std::fill (meterFreq.begin() + kMeterFFTSize, meterFreq.end(), 0.0f);
            meterFFT->performFrequencyOnlyForwardTransform (meterFreq.data());
        }
    }

    if (smoothedF0 < kMinF0) return;

    const float binHz = static_cast<float> (currentSampleRate) / static_cast<float> (kMeterFFTSize);
    const float band  = juce::jmax (0.15f * smoothedF0, 1.5f * binHz);

    double oddE = 0.0, evenE = 0.0;
    for (int h = 1; h <= kHarmonics; ++h)
    {
        const float hz = smoothedF0 * static_cast<float> (h);
        if (hz >= 0.45f * static_cast<float> (currentSampleRate)) break;

        const int lo = juce::jmax (0, static_cast<int> ((hz - band) / binHz));
        const int hi = juce::jmin (kMeterFFTSize / 2 - 1, static_cast<int> ((hz + band) / binHz));

        double e = 0.0;
        for (int b = lo; b <= hi; ++b)
            e += meterFreq[static_cast<size_t> (b)] * meterFreq[static_cast<size_t> (b)];

        if (h & 1) oddE  += e;
        else       evenE += e;
    }

    oddEnergyDb.store  (gainToDb (static_cast<float> (std::sqrt (oddE  + 1e-12))), std::memory_order_relaxed);
    evenEnergyDb.store (gainToDb (static_cast<float> (std::sqrt (evenE + 1e-12))), std::memory_order_relaxed);
}

// ===========================================================================
//  processBlock
// ===========================================================================
void OctivierAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int channels   = buffer.getNumChannels();
    if (numSamples <= 0 || channels <= 0) return;

    // ---------- BLOCK START: adopt a Ready generation, retire old ----------
    const int candidate = tryAdoptReadySlot (generations);
    if (candidate >= 0)
    {
        if (activeSlot >= 0 && activeSlot != candidate)
            generations[static_cast<size_t> (activeSlot)].state.store (
                SlotIdle, std::memory_order_release);
        activeSlot = candidate;
    }
    else if (activeSlot < 0)
    {
        // Nothing published yet. Feed the tracker, then pass through.
        pushPitchHistory (buffer.getReadPointer (0), numSamples);
        juce::ignoreUnused (midi);
        return;
    }

    const auto& gen         = generations[static_cast<size_t> (activeSlot)];
    const auto& harmCoeffs  = gen.harmonics;
    const auto& shelfCoeffs = gen.shelves;

    // ---------- tracker feed ----------
    pushPitchHistory (buffer.getReadPointer (0), numSamples);

    const bool  freeze = readParam (parameters, "freeze") > 0.5f;
    const int   modeI  = juce::jlimit (0, 3, static_cast<int> (readParam (parameters, "mode")));
    const float sens   = readParam (parameters, "tracking");

    blockCounter += numSamples;
    const bool pitchUpdateDue = (blockCounter >= static_cast<int> (currentSampleRate * 0.010));
    if (pitchUpdateDue) blockCounter = 0;

    float detected = 0.0f;
    if (freeze)
    {
        detected   = readParam (parameters, "manualF0");
        confidence = 1.0f;
    }
    else if (pitchUpdateDue)
    {
        const float est = estimatePitch (readParam (parameters, "gate"), sens);
        if (est > 0.0f)
        {
            if (smoothedF0 <= 0.0f || confidence < 0.25f)
                smoothedF0 = est;
            else
            {
                const float semis = std::abs (12.0f * std::log2 (est / smoothedF0));
                const float a     = semis > 1.0f ? 0.55f : (0.10f + 0.25f * sens);
                smoothedF0 += a * (est - smoothedF0);
            }
        }
        detected = smoothedF0;
    }
    if (detected > 0.0f) smoothedF0 = detected;

    const float conf = freeze ? 1.0f : confidence;
    trackedFundamental.store (smoothedF0, std::memory_order_relaxed);
    pitchConfidence.store    (conf,       std::memory_order_relaxed);
    trackingActive.store     (smoothedF0 >= kMinF0 && conf >= 0.2f, std::memory_order_relaxed);

    // ---------- parameter snapshot ----------
    const float targetStrength = readParam (parameters, "strength") * conf;
    const float targetFocus    = readParam (parameters, "focus");
    const float targetMix      = readParam (parameters, "mix");
    const float targetOutput   = dbToGain (readParam (parameters, "output"));
    const float targetFormant  = readParam (parameters, "formant");
    const float targetAir      = readParam (parameters, "air");
    const float width          = juce::jlimit (0.0f, 1.0f, readParam (parameters, "width"));
    const float delayMs        = juce::jlimit (1.0f, 80.0f, readParam (parameters, "delay"));
    const float modDepthMs     = juce::jlimit (0.0f, 4.0f,  readParam (parameters, "modDepth"));
    const int   charMode       = juce::jlimit (0, 2, static_cast<int> (readParam (parameters, "character")));
    const float charAmt        = juce::jlimit (0.0f, 1.0f, readParam (parameters, "characterAmt"));
    const bool  autoGainOn     = readParam (parameters, "autoGain") > 0.5f;

    // ---------- publish a new coefficient request? ----------
    bool wantNew = false;
    if (pitchUpdateDue && smoothedF0 >= kMinF0 && smoothedF0 <= kMaxF0 && conf >= 0.15f)
        wantNew = true;
    if (std::abs (smoothFormant - gen.shelfDb[0]) > 0.5f ||
        std::abs (smoothAir     - gen.shelfDb[1]) > 0.5f)
        wantNew = true;

    if (wantNew)
    {
        bool expected = false;
        if (coeffRequest.pending.compare_exchange_strong (expected, true,
                                                          std::memory_order_acq_rel))
        {
            coeffRequest.f0        = smoothedF0;
            coeffRequest.strength  = smoothStrength;
            coeffRequest.focus     = smoothFocus;
            coeffRequest.mode      = modeI;
            coeffRequest.formantDb = smoothFormant;
            coeffRequest.airDb     = smoothAir;
        }
    }

    // ---------- input RMS for auto-gain ----------
    double inSq = 0.0;
    if (autoGainOn)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* r = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i) inSq += r[i] * r[i];
        }
    }
    const float inRms = (inSq > 1e-12)
        ? static_cast<float> (std::sqrt (inSq / (numSamples * channels))) : 0.0f;

    if (inRms > inputRmsRef) inputRmsRef += agAttackCoeff  * (inRms - inputRmsRef);
    else                     inputRmsRef += agReleaseCoeff * (inRms - inputRmsRef);

    // ---------- decorrelation geometry ----------
    const size_t cap        = delayLeft.size();
    const int    baseDelayI = juce::jlimit (1, static_cast<int> (cap - 2),
                                            static_cast<int> (delayMs * 0.001 * currentSampleRate));
    const float  modDepthS  = modDepthMs * 0.001f * static_cast<float> (currentSampleRate);
    const float  modRate    = 0.31f;

    // ---------- main sample loop ----------
    const float sc = smoothCoeff;
    auto step = [sc] (float& s, float t) { s += sc * (t - s); };

    for (int i = 0; i < numSamples; ++i)
    {
        step (smoothStrength, targetStrength);
        step (smoothFocus,    targetFocus);
        step (smoothMix,      targetMix);
        step (smoothOutput,   targetOutput);
        step (smoothFormant,  targetFormant);
        step (smoothAir,      targetAir);

        modPhase += 2.0f * juce::MathConstants<float>::pi * modRate
                  / static_cast<float> (currentSampleRate);
        if (modPhase > juce::MathConstants<float>::twoPi)
            modPhase -= juce::MathConstants<float>::twoPi;
        const float modOffset = std::sin (modPhase) * modDepthS;

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data  = buffer.getWritePointer (ch);
            auto& cbank = channelBank[static_cast<size_t> (juce::jmin (ch, 1))];
            const float dry = data[i];

            float wet = dry;
            if (smoothedF0 >= kMinF0 && conf >= 0.15f)
                for (int h = 0; h < kHarmonics; ++h)
                    wet = processBiquad (harmCoeffs[static_cast<size_t> (h)],
                                         cbank.harmonics[static_cast<size_t> (h)], wet);

            wet = processBiquad (shelfCoeffs[0], cbank.shelves[0], wet);
            wet = processBiquad (shelfCoeffs[1], cbank.shelves[1], wet);

            const size_t write = delayWrite;
            if (ch == 0) delayLeft[write]  = wet;
            if (ch == 1) delayRight[write] = wet;

            auto readAt = [&] (float delaySamples)
            {
                const float d  = juce::jlimit (1.0f, static_cast<float> (cap - 2), delaySamples);
                const int   i0 = static_cast<int> (d);
                const float fr = d - static_cast<float> (i0);
                const size_t a = (write + cap - static_cast<size_t> (i0))     % cap;
                const size_t b = (write + cap - static_cast<size_t> (i0 + 1)) % cap;
                const float* line = (ch == 0) ? delayLeft.data() : delayRight.data();
                return line[a] * (1.0f - fr) + line[b] * fr;
            };
            const float fixed  = readAt (static_cast<float> (baseDelayI));
            const float modded = readAt (static_cast<float> (baseDelayI) + modOffset);

            const float decor = (ch == 0) ? ((1.0f - width) * wet + width * fixed)
                              : (ch == 1) ? ((1.0f - width) * wet + width * modded)
                                          : wet;

            const float mixed = (1.0f - smoothMix) * decor + smoothMix * dry;
            data[i] = mixed;

            if (ch == channels - 1) delayWrite = (delayWrite + 1) % cap;
        }
    }

    // ---------- oversampled saturation ----------
    if (charMode != 0 && charAmt > 0.0f && oversamplerReady)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto up = oversampler->processSamplesUp (block);
        const int upN  = static_cast<int> (up.getNumSamples());
        const int upCh = static_cast<int> (up.getNumChannels());
        const float drive = 1.0f + 4.0f * charAmt;

        for (int ch = 0; ch < upCh; ++ch)
        {
            auto* x = up.getChannelPointer (static_cast<size_t> (ch));
            for (int i = 0; i < upN; ++i)
            {
                const float v = x[i];
                float y;
                if (charMode == 1)   // tube
                {
                    y = std::tanh (drive * v) / std::tanh (drive);
                    y = (v >= 0.0f) ? y : 0.88f * y;
                }
                else                 // tape
                {
                    const float d = drive * 0.7f;
                    y = std::tanh (d * v) / std::tanh (d);
                    y -= 0.15f * y * y * y;
                }
                x[i] = (1.0f - charAmt) * v + charAmt * y;
            }
        }
        oversampler->processSamplesDown (block);
    }

    // ---------- auto-gain ----------
    double outSq = 0.0;
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* r = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i) outSq += r[i] * r[i];
    }
    const float outRms = (outSq > 1e-12)
        ? static_cast<float> (std::sqrt (outSq / (numSamples * channels))) : 0.0f;

    if (autoGainOn && inputRmsRef > 1e-6f && outRms > 1e-6f)
    {
        const float ratio      = inputRmsRef / outRms;
        const float targetGain = juce::jlimit (0.25f, 4.0f, ratio);
        smoothAutoGain += 0.05f * (targetGain - smoothAutoGain);
    }
    else
    {
        smoothAutoGain += 0.05f * (1.0f - smoothAutoGain);
    }

    // ---------- output gain ----------
    const float finalGain = smoothOutput * smoothAutoGain;
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i) data[i] *= finalGain;
    }

    // ---------- meter ----------
    updateMeter (buffer);

    // ---------- BLOCK END: acknowledge ----------
    gen.ackGen.store (gen.generation.load (std::memory_order_relaxed),
                      std::memory_order_release);

    juce::ignoreUnused (midi);
}

// ===========================================================================
//  State
// ===========================================================================
void OctivierAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void OctivierAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* OctivierAudioProcessor::createEditor()
{
    return new OctivierAudioProcessorEditor (*this);
}

float OctivierAudioProcessor::readParam (const juce::AudioProcessorValueTreeState& s,
                                          const char* id) noexcept
{
    if (auto* v = s.getRawParameterValue (id))
        return v->load (std::memory_order_relaxed);
    return 0.0f;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OctivierAudioProcessor();
}
