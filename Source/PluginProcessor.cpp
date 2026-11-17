#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr float kMinF0 = 55.0f;
constexpr float kMaxF0 = 1200.0f;
constexpr float kPi = juce::MathConstants<float>::pi;
float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }
float gainToDb (float g) noexcept { return 20.0f * std::log10 (juce::jmax (g, 1.0e-6f)); }
}

juce::AudioProcessorValueTreeState::ParameterLayout OctivierAudioProcessor::createParameterLayout()
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    using APC = juce::AudioParameterChoice;
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    p.add (std::make_unique<APF> ("strength", "Octave Strength", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.78f));
    p.add (std::make_unique<APF> ("focus", "Harmonic Focus", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    p.add (std::make_unique<APF> ("oddCut", "Odd Cut", juce::NormalisableRange<float> (0.0f, 36.0f, 0.1f), 18.0f));
    p.add (std::make_unique<APF> ("evenBoost", "Even Boost", juce::NormalisableRange<float> (0.0f, 18.0f, 0.1f), 8.0f));
    p.add (std::make_unique<APF> ("tracking", "Tracking Sensitivity", juce::NormalisableRange<float> (0.1f, 1.0f, 0.001f), 0.62f));
    p.add (std::make_unique<APF> ("gate", "Gate (dBFS)", juce::NormalisableRange<float> (-90.0f, -18.0f, 0.1f), -55.0f));
    p.add (std::make_unique<APF> ("delay", "Decorrelate Delay (ms)", juce::NormalisableRange<float> (1.0f, 80.0f, 0.1f), 42.0f));
    p.add (std::make_unique<APF> ("width", "Width", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.55f));
    p.add (std::make_unique<APF> ("modDepth", "Delay Mod Depth (ms)", juce::NormalisableRange<float> (0.0f, 4.0f, 0.01f), 0.35f));
    p.add (std::make_unique<APF> ("formant", "Formant Shelf (dB)", juce::NormalisableRange<float> (-9.0f, 9.0f, 0.1f), 0.0f));
    p.add (std::make_unique<APF> ("air", "Air Shelf (dB)", juce::NormalisableRange<float> (-9.0f, 9.0f, 0.1f), 0.0f));
    p.add (std::make_unique<APC> ("mode", "Mode", juce::StringArray { "Classic", "Sub", "Hollow", "Square" }, 0));
    p.add (std::make_unique<APC> ("character", "Character", juce::StringArray { "Clean", "Tube", "Tape" }, 0));
    p.add (std::make_unique<APF> ("characterAmt", "Character Amount", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    p.add (std::make_unique<APF> ("mix", "Dry Blend", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    p.add (std::make_unique<APF> ("output", "Output (dB)", juce::NormalisableRange<float> (-18.0f, 12.0f, 0.1f), 0.0f));
    p.add (std::make_unique<APB> ("autoGain", "RMS Compensation", true));
    p.add (std::make_unique<APB> ("freeze", "Freeze Tracking", false));
    p.add (std::make_unique<APF> ("manualF0", "Manual Fundamental (Hz)", juce::NormalisableRange<float> (55.0f, 1200.0f, 0.1f), 220.0f));
    return p;
}

OctivierAudioProcessor::OctivierAudioProcessor()
 : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
   parameters (*this, nullptr, "OCTIVIER_STATE", createParameterLayout()),
   worker (std::make_unique<Worker> (*this))
{}

OctivierAudioProcessor::~OctivierAudioProcessor()
{
    if (worker != nullptr) worker->stopThread (-1);
}

float OctivierAudioProcessor::readParam (const juce::AudioProcessorValueTreeState& s, const char* id) noexcept
{
    if (auto* v = s.getRawParameterValue (id)) return v->load (std::memory_order_relaxed);
    return 0.0f;
}

bool OctivierAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && in == out;
}

void OctivierAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Reconfiguration is a non-real-time lifecycle operation: stop the worker
    // before touching any data it can access, then restart only after reset.
    if (worker != nullptr) worker->stopThread (-1);
    currentSampleRate = juce::jmax (sampleRate, 8000.0);
    const auto historySize = static_cast<size_t> (juce::jmax (4096, static_cast<int> (currentSampleRate * 0.10)));
    pitchHistory.assign (historySize, 0.0f);
    historyWrite = delayWrite = 0;
    modSampleCount = 0;
    const bool acfPrepared = acfTracker.prepare (currentSampleRate, 2048, kMinF0, kMaxF0);
    jassert (acfPrepared);
    juce::ignoreUnused (acfPrepared);
    meterFft = std::make_unique<juce::dsp::FFT> (kMeterFftOrder);
    for (int i = 0; i < kMeterFftSize; ++i)
        meterWindow[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (2.0f * kPi * static_cast<float> (i) / static_cast<float> (kMeterFftSize - 1));
    meterRing.fill (0.0f);
    meterBuffer.fill (0.0f);
    meterWrite = 0;
    meterBlockCounter = 0;
    const auto maxDelay = static_cast<size_t> (currentSampleRate * 0.10) + static_cast<size_t> (juce::jmax (samplesPerBlock, 32)) + 8;
    delayLeft.assign (maxDelay, 0.0f);
    delayRight.assign (maxDelay, 0.0f);
    smoothedF0 = confidence = 0.0f;
    blockCounter = 0;
    smoothStrength = readParam (parameters, "strength");
    smoothFocus = readParam (parameters, "focus");
    smoothMix = readParam (parameters, "mix");
    smoothOutput = dbToGain (readParam (parameters, "output"));
    for (auto& cb : channelBank) cb = {};

    for (auto& g : generations)
    {
        g.state.store (SlotIdle, std::memory_order_relaxed);
        g.generation.store (0, std::memory_order_relaxed);
        g.ackGen.store (0, std::memory_order_relaxed);
        for (auto& c : g.harmonics) c = identityBiquad();
        g.shelves[0] = makeHighShelf (currentSampleRate, 3200.0f, 0.707f, 0.0f);
        g.shelves[1] = makeHighShelf (currentSampleRate, 10000.0f, 0.707f, 0.0f);
        g.shelfDb[0] = g.shelfDb[1] = 0.0f;
    }
    generations[0].generation.store (1, std::memory_order_relaxed);
    generations[0].state.store (SlotReady, std::memory_order_release);
    activeSlot = -1;
    coeffRequest.state.store (MailboxEmpty, std::memory_order_relaxed);
    if (worker != nullptr) worker->startThread();
}

void OctivierAudioProcessor::releaseResources()
{
    if (worker != nullptr) worker->stopThread (-1);
}

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
    constexpr int frameN = 2048;
    const size_t histSize = pitchHistory.size();
    if (histSize < static_cast<size_t> (frameN)) return 0.0f;

    // historyWrite is the next-write index. Start at the oldest sample in the
    // newest complete frame and copy it into chronological linear storage.
    std::array<float, frameN> frame {};
    const size_t start = (historyWrite + histSize - static_cast<size_t> (frameN)) % histSize;
    for (int i = 0; i < frameN; ++i)
        frame[static_cast<size_t> (i)] =
            pitchHistory[(start + static_cast<size_t> (i)) % histSize];

    return acfTracker.estimate (frame.data(), frameN, gateDb, sensitivity, confidence);
}

OctivierAudioProcessor::Biquad OctivierAudioProcessor::makePeak (double sr, float freq, float q, float gainDb) noexcept
{
    const double A = std::pow (10.0, static_cast<double> (gainDb) / 40.0);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sr;
    const double cw = std::cos (w0), sw = std::sin (w0), alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha / A;
    return { static_cast<float> ((1.0 + alpha * A) / a0), static_cast<float> (-2.0 * cw / a0),
             static_cast<float> ((1.0 - alpha * A) / a0), static_cast<float> (-2.0 * cw / a0),
             static_cast<float> ((1.0 - alpha / A) / a0) };
}

OctivierAudioProcessor::Biquad OctivierAudioProcessor::makeHighShelf (double sr, float freq, float slope, float gainDb) noexcept
{
    const double A = std::pow (10.0, static_cast<double> (gainDb) / 40.0);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sr;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double S = juce::jlimit (0.1, 1.0, static_cast<double> (slope));
    const double alpha = sw * 0.5 * std::sqrt ((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
    const double beta = 2.0 * std::sqrt (A) * alpha;
    const double b0 = A * ((A + 1.0) + (A - 1.0) * cw + beta);
    const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
    const double b2 = A * ((A + 1.0) + (A - 1.0) * cw - beta);
    const double a0 = (A + 1.0) - (A - 1.0) * cw + beta;
    const double a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
    const double a2 = (A + 1.0) - (A - 1.0) * cw - beta;
    return { static_cast<float> (b0 / a0), static_cast<float> (b1 / a0), static_cast<float> (b2 / a0),
             static_cast<float> (a1 / a0), static_cast<float> (a2 / a0) };
}

float OctivierAudioProcessor::processBiquad (const Biquad& c, BiquadState& s, float x) noexcept
{
    const float y = c.b0 * x + s.s1;
    s.s1 = c.b1 * x - c.a1 * y + s.s2;
    s.s2 = c.b2 * x - c.a2 * y;
    return y;
}

bool OctivierAudioProcessor::publishRequest (const CoeffRequest& req) noexcept
{
    int expected = MailboxEmpty;
    if (! coeffRequest.state.compare_exchange_strong (expected, MailboxWriting,
                                                       std::memory_order_acquire, std::memory_order_relaxed))
        return false;
    coeffRequest.payload = req;
    coeffRequest.state.store (MailboxReady, std::memory_order_release);
    return true;
}

void OctivierAudioProcessor::buildGeneration (Generation& out, const CoeffRequest& req)
{
    const auto mode = static_cast<Mode> (juce::jlimit (0, 3, req.mode));
    for (int h = 1; h <= kHarmonics; ++h)
    {
        const float hz = req.f0 * static_cast<float> (h);
        float db = 0.0f;
        const bool odd = (h & 1) != 0;
        const float center = 2.0f + (req.focus + 1.0f) * 3.0f;
        const float weight = std::min (1.0f, std::exp (-0.5f * std::pow ((static_cast<float> (h) - center) / 2.7f, 2.0f)) /
            std::max (std::exp (-0.5f * std::pow ((2.0f - center) / 2.7f, 2.0f)), 1.0e-4f));
        switch (mode)
        {
            case Mode::Classic: db = odd ? -req.oddCut * req.strength : req.evenBoost * req.strength * weight; break;
            case Mode::Sub: db = odd ? req.evenBoost * req.strength * weight : -req.oddCut * req.strength; break;
            case Mode::Hollow: db = odd ? -req.oddCut * req.strength : 0.0f; break;
            case Mode::Square: db = odd ? req.evenBoost * req.strength * weight : 0.0f; break;
        }
        if (req.f0 < kMinF0 || req.f0 > kMaxF0 || hz < 25.0f || hz >= currentSampleRate * 0.47)
            out.harmonics[static_cast<size_t> (h - 1)] = identityBiquad();
        else
            out.harmonics[static_cast<size_t> (h - 1)] = makePeak (currentSampleRate, hz, juce::jlimit (2.0f, 14.0f, 7.5f + 5.0f * req.strength), db);
    }
    out.shelves[0] = makeHighShelf (currentSampleRate, 3200.0f, 0.707f, req.formantDb);
    out.shelves[1] = makeHighShelf (currentSampleRate, static_cast<float> (juce::jmin (10000.0, currentSampleRate * 0.42)), 0.707f, req.airDb);
    out.shelfDb[0] = req.formantDb;
    out.shelfDb[1] = req.airDb;
}

void OctivierAudioProcessor::Worker::run()
{
    while (! threadShouldExit())
    {
        int chosen = -1;
        for (int i = 0; i < kGenerationSlots; ++i)
        {
            int expected = SlotIdle;
            if (owner.generations[static_cast<size_t> (i)].state.compare_exchange_strong (
                    expected, SlotWriting, std::memory_order_acquire, std::memory_order_relaxed))
            { chosen = i; break; }
        }
        if (chosen < 0) { wait (2); continue; }

        int expectedMailbox = MailboxReady;
        if (! owner.coeffRequest.state.compare_exchange_strong (expectedMailbox, MailboxReading,
                                                                 std::memory_order_acquire, std::memory_order_relaxed))
        {
            owner.generations[static_cast<size_t> (chosen)].state.store (SlotIdle, std::memory_order_release);
            wait (2);
            continue;
        }
        const CoeffRequest snapshot = owner.coeffRequest.payload;
        owner.buildGeneration (owner.generations[static_cast<size_t> (chosen)], snapshot);
        auto& slot = owner.generations[static_cast<size_t> (chosen)];
        slot.generation.fetch_add (1, std::memory_order_relaxed);
        slot.state.store (SlotReady, std::memory_order_release);
        owner.coeffRequest.state.store (MailboxEmpty, std::memory_order_release);
    }
}

void OctivierAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples(), channels = buffer.getNumChannels();
    if (numSamples <= 0 || channels <= 0 || pitchHistory.empty() || delayLeft.size() < 4) return;

    // Adopt only a fully published generation. If no newer one is ready, keep
    // the existing Reading slot; never retire it before a replacement exists.
    int replacement = -1;
    for (int i = 0; i < kGenerationSlots; ++i)
    {
        int expected = SlotReady;
        if (generations[static_cast<size_t> (i)].state.compare_exchange_strong (
                expected, SlotReading, std::memory_order_acquire, std::memory_order_relaxed))
        { replacement = i; break; }
    }
    if (replacement >= 0)
    {
        const int old = activeSlot;
        activeSlot = replacement;
        if (old >= 0 && old != replacement)
        {
            auto& retired = generations[static_cast<size_t> (old)];
            retired.ackGen.store (retired.generation.load (std::memory_order_relaxed), std::memory_order_release);
            retired.state.store (SlotIdle, std::memory_order_release);
        }
    }

    pushPitchHistory (buffer.getReadPointer (0), numSamples);
    const bool freeze = readParam (parameters, "freeze") > 0.5f;
    const int modeIndex = juce::jlimit (0, 3, static_cast<int> (readParam (parameters, "mode")));
    const float sensitivity = readParam (parameters, "tracking");
    float detected = 0.0f;
    blockCounter += numSamples;
    const bool pitchUpdateDue = blockCounter >= static_cast<int> (currentSampleRate * 0.010);
    if (pitchUpdateDue) blockCounter = 0;
    if (freeze) detected = readParam (parameters, "manualF0");
    else if (pitchUpdateDue)
    {
        const float estimate = estimatePitch (readParam (parameters, "gate"), sensitivity);
        if (estimate > 0.0f)
        {
            if (smoothedF0 <= 0.0f || confidence < 0.25f) smoothedF0 = estimate;
            else
            {
                const float semitones = std::abs (12.0f * std::log2 (estimate / smoothedF0));
                const float a = semitones > 1.0f ? 0.55f : (0.10f + 0.25f * sensitivity);
                smoothedF0 += a * (estimate - smoothedF0);
            }
        }
        else
        {
            // A closed gate invalidates the previous pitch immediately. This
            // disables pitch-shaped filters and forces a fresh note to snap.
            smoothedF0 = 0.0f;
        }
        detected = smoothedF0;
    }
    if (detected > 0.0f) smoothedF0 = detected;
    const float conf = freeze ? 1.0f : confidence;
    trackedFundamental.store (smoothedF0, std::memory_order_relaxed);
    pitchConfidence.store (conf, std::memory_order_relaxed);
    trackingActive.store (smoothedF0 >= kMinF0 && conf >= 0.2f, std::memory_order_relaxed);

    const float targetStrength = readParam (parameters, "strength") * conf;
    const float targetFocus = readParam (parameters, "focus");
    const float targetMix = readParam (parameters, "mix");
    const float outputGain = dbToGain (readParam (parameters, "output"));
    const float width = readParam (parameters, "width");
    const float delayMs = readParam (parameters, "delay");
    const float modDepth = readParam (parameters, "modDepth");
    const int character = juce::jlimit (0, 2, static_cast<int> (readParam (parameters, "character")));
    const float charAmount = readParam (parameters, "characterAmt");
    const float formantDb = readParam (parameters, "formant"), airDb = readParam (parameters, "air");
    smoothStrength += 0.03f * (targetStrength - smoothStrength);
    smoothFocus += 0.03f * (targetFocus - smoothFocus);
    smoothMix += 0.03f * (targetMix - smoothMix);
    smoothOutput += 0.03f * (outputGain - smoothOutput);

    // The audio thread only writes the mailbox after claiming Empty. Payload
    // is published with release semantics; worker claims Ready with acquire.
    bool shelfChanged = activeSlot < 0;
    if (activeSlot >= 0)
    {
        const auto& g = generations[static_cast<size_t> (activeSlot)];
        shelfChanged = std::abs (g.shelfDb[0] - formantDb) >= 0.25f || std::abs (g.shelfDb[1] - airDb) >= 0.25f;
    }
    if ((pitchUpdateDue || shelfChanged) && smoothedF0 >= kMinF0 && smoothedF0 <= kMaxF0 && conf >= 0.15f)
    {
        CoeffRequest req;
        req.f0 = smoothedF0; req.strength = smoothStrength; req.focus = smoothFocus;
        req.oddCut = readParam (parameters, "oddCut"); req.evenBoost = readParam (parameters, "evenBoost");
        req.formantDb = formantDb; req.airDb = airDb; req.mode = modeIndex;
        publishRequest (req); // If mailbox is occupied, the next tracker/shelf tick retries.
    }

    const size_t delayCapacity = delayLeft.size();
    const int baseDelay = juce::jlimit (1, static_cast<int> (delayCapacity - 2), static_cast<int> (delayMs * 0.001 * currentSampleRate));
    const auto* gen = activeSlot >= 0 ? &generations[static_cast<size_t> (activeSlot)] : nullptr;
    const std::uint64_t modBase = modSampleCount;
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& states = channelBank[static_cast<size_t> (std::min (ch, 1))];
        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = data[i];
            float wet = dry;
            if (gen != nullptr && smoothedF0 >= kMinF0 && conf >= 0.15f)
                for (int h = 0; h < kHarmonics; ++h)
                    wet = processBiquad (gen->harmonics[static_cast<size_t> (h)], states.harmonics[static_cast<size_t> (h)], wet);
            if (gen != nullptr)
            {
                wet = processBiquad (gen->shelves[0], states.shelves[0], wet);
                wet = processBiquad (gen->shelves[1], states.shelves[1], wet);
            }
            if (character == 1 && charAmount > 0.0f)
            {
                const float driven = std::tanh (wet * (1.0f + 4.0f * charAmount));
                wet = wet * (1.0f - charAmount) + driven * charAmount;
            }
            else if (character == 2 && charAmount > 0.0f)
            {
                const float driven = std::tanh (wet * (1.0f + 3.0f * charAmount));
                wet = wet * (1.0f - charAmount) + (driven * (1.0f - 0.15f * std::abs (driven))) * charAmount;
            }
            const size_t write = delayWrite;
            const size_t read = (write + delayCapacity - static_cast<size_t> (baseDelay)) % delayCapacity;
            if (ch == 0) delayLeft[write] = wet; else delayRight[write] = wet;
            const double modTimeSec = static_cast<double> (modBase + static_cast<std::uint64_t> (i)) / currentSampleRate;
            const float mod = static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 0.31 * modTimeSec))
                              * modDepth * 0.001f * static_cast<float> (currentSampleRate);
            const size_t modRead = (write + delayCapacity - static_cast<size_t> (juce::jlimit (1.0f, static_cast<float> (delayCapacity - 2), static_cast<float> (baseDelay) + mod))) % delayCapacity;
            const float modDelayed = ch == 0 ? delayLeft[modRead] : delayRight[modRead];
            const float decor = ch == 1 ? ((1.0f - width) * wet + width * modDelayed) : wet;
            data[i] = ((1.0f - smoothMix) * decor + smoothMix * dry) * smoothOutput;
            if (ch == channels - 1) delayWrite = (delayWrite + 1) % delayCapacity;
            juce::ignoreUnused (read);
        }
    }
    if (readParam (parameters, "autoGain") > 0.5f)
    {
        // Intentionally neutral until a true input/output loudness-matching path is implemented.
    }
    modSampleCount += static_cast<std::uint64_t> (numSamples);
    // Measure odd/even harmonic energy from the rendered output. This is a
    // diagnostic single-bin integration, updated at 20 Hz without allocation.
    for (int i = 0; i < numSamples; ++i)
    {
        const float mono = channels == 1 ? buffer.getSample (0, i)
                                         : 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i));
        meterRing[meterWrite] = mono;
        meterWrite = (meterWrite + 1) % static_cast<size_t> (kMeterFftSize);
    }
    meterBlockCounter += static_cast<std::uint64_t> (numSamples);
    if (meterFft != nullptr
        && smoothedF0 >= kMinF0 && smoothedF0 <= kMaxF0
        && conf >= 0.20f
        && meterBlockCounter >= static_cast<std::uint64_t> (currentSampleRate * 0.050))
    {
        meterBlockCounter = 0;
        meterBuffer.fill (0.0f);
        for (int i = 0; i < kMeterFftSize; ++i)
        {
            const auto idx = (meterWrite + static_cast<size_t> (i)) % static_cast<size_t> (kMeterFftSize);
            meterBuffer[static_cast<size_t> (i)] = meterRing[idx] * meterWindow[static_cast<size_t> (i)];
        }
        meterFft->performFrequencyOnlyForwardTransform (meterBuffer.data());
        const double binHz = currentSampleRate / static_cast<double> (kMeterFftSize);
        const int maxBin = kMeterFftSize / 2;
        double oddSq = 0.0, evenSq = 0.0;
        for (int harmonic = 1; harmonic <= kHarmonics; ++harmonic)
        {
            const int bin = static_cast<int> (std::lround (static_cast<double> (harmonic) * smoothedF0 / binHz));
            if (bin < 1 || bin > maxBin) continue;
            const float mag = meterBuffer[static_cast<size_t> (bin)];
            const double power = static_cast<double> (mag) * mag;
            if ((harmonic & 1) != 0) oddSq += power;
            else evenSq += power;
        }
        oddEnergyDb.store (gainToDb (static_cast<float> (std::sqrt (oddSq + 1.0e-12))), std::memory_order_relaxed);
        evenEnergyDb.store (gainToDb (static_cast<float> (std::sqrt (evenSq + 1.0e-12))), std::memory_order_relaxed);
    }
    juce::ignoreUnused (midi);
}

void OctivierAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = parameters.copyState().createXml()) copyXmlToBinary (*xml, dest);
}
void OctivierAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType())) parameters.replaceState (juce::ValueTree::fromXml (*xml));
}
juce::AudioProcessorEditor* OctivierAudioProcessor::createEditor() { return new OctivierAudioProcessorEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OctivierAudioProcessor(); }
