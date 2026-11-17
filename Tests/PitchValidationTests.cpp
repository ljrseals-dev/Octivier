#include "AcfPitchTracker.h"
#include "YinPitchTracker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr std::array<double, 5> kSnrDb {{ 100.0, 20.0, 10.0, 0.0, -5.0 }};

class IPitchTracker
{
public:
    virtual ~IPitchTracker() = default;
    virtual bool prepare (double sr, float minF0, float maxF0, int frameSize,
                          float periodMultiplier) = 0;
    virtual float estimate (const float* frame, int size, float gateDb, float& confidence) = 0;
    virtual int getFrameSize() const = 0;
    virtual int getTauMin() const = 0;
    virtual int getTauMax() const = 0;
    virtual const char* name() const = 0;
};

class AcfAdapter final : public IPitchTracker
{
public:
    bool prepare (double sr, float lo, float hi, int size, float) override
    { confidence = 0.0f; return tracker.prepare (sr, size, lo, hi); }
    float estimate (const float* frame, int size, float gate, float& conf) override
    { const float hz = tracker.estimate (frame, size, gate, confidence); conf = confidence; return hz; }
    int getFrameSize() const override { return tracker.getFrameSize(); }
    int getTauMin() const override { return tracker.getTauMin(); }
    int getTauMax() const override { return tracker.getTauMax(); }
    const char* name() const override { return "ACF"; }
private:
    AcfPitchTracker tracker;
    float confidence = 0.0f;
};

class YinAdapter final : public IPitchTracker
{
public:
    bool prepare (double sr, float lo, float hi, int, float multiplier) override
    { return tracker.prepare (sr, lo, hi, multiplier); }
    float estimate (const float* frame, int size, float gate, float& conf) override
    { return tracker.estimate (frame, size, gate, conf); }
    int getFrameSize() const override { return tracker.getFrameSize(); }
    int getTauMin() const override { return tracker.getTauMin(); }
    int getTauMax() const override { return tracker.getTauMax(); }
    const char* name() const override { return "YIN"; }
private:
    YinPitchTracker tracker;
};

struct Config
{
    std::string label;
    bool yin;
    double sampleRate;
    int acfFrameSize;
    float minF0;
    float maxF0;
    float periodMultiplier;
};

enum class Signal { Sine, Saw, KarplusStrong, SawAdsr };

const char* signalName (Signal signal)
{
    switch (signal)
    {
        case Signal::Sine: return "sine";
        case Signal::Saw: return "saw";
        case Signal::KarplusStrong: return "karplus_strong";
        case Signal::SawAdsr: return "saw_adsr";
    }
    return "unknown";
}

void synthesize (std::vector<float>& signal, double sr, double hz,
                 Signal kind, double phase, std::mt19937& rng)
{
    const int count = static_cast<int> (signal.size());
    if (kind == Signal::KarplusStrong)
    {
        const int period = std::max (2, static_cast<int> (std::round (sr / hz)));
        std::vector<float> delay (static_cast<std::size_t> (period));
        std::uniform_real_distribution<float> initial (-1.0f, 1.0f);
        for (auto& sample : delay) sample = initial (rng);
        int index = 0;
        for (int i = 0; i < count; ++i)
        {
            const int next = (index + 1) % period;
            const float current = delay[static_cast<std::size_t> (index)];
            const float averaged = 0.5f * (current + delay[static_cast<std::size_t> (next)]);
            delay[static_cast<std::size_t> (index)] = averaged * 0.996f;
            signal[static_cast<std::size_t> (i)] = current;
            index = next;
        }
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        const double cycles = hz * t + phase;
        const double fractional = cycles - std::floor (cycles);
        float value = kind == Signal::Sine
            ? static_cast<float> (std::sin (2.0 * kPi * cycles))
            : static_cast<float> (2.0 * fractional - 1.0);
        if (kind == Signal::SawAdsr)
        {
            const double attack = 0.008, decay = 0.080;
            const double envelope = t < attack ? t / attack
                : (t < attack + decay ? 1.0 - 0.35 * (t - attack) / decay : 0.65);
            value *= static_cast<float> (envelope);
        }
        signal[static_cast<std::size_t> (i)] = value;
    }
}

struct Stats
{
    std::uint64_t trials = 0, voiced = 0, correct = 0, octave = 0, other = 0;
    double confidenceSum = 0.0, microsecondsSum = 0.0;
    std::array<std::uint64_t, 10> confidenceBuckets {};
    std::vector<double> absoluteCents;
};

double percentile (std::vector<double> values, double p)
{
    if (values.empty()) return -1.0;
    const auto index = static_cast<std::size_t> (std::ceil (p * values.size()) - 1.0);
    std::nth_element (values.begin(), values.begin() + static_cast<std::ptrdiff_t> (index), values.end());
    return values[index];
}

void runConfig (const Config& config, bool quick)
{
    std::unique_ptr<IPitchTracker> tracker = config.yin
        ? std::unique_ptr<IPitchTracker> (new YinAdapter())
        : std::unique_ptr<IPitchTracker> (new AcfAdapter());
    if (! tracker->prepare (config.sampleRate, config.minF0, config.maxF0,
                            config.acfFrameSize, config.periodMultiplier))
    {
        std::cerr << "prepare_failed," << config.label << '\n';
        return;
    }

    const int frameSize = tracker->getFrameSize();
    const int preRoll = frameSize;
    const double frameDurationMs = 1000.0 * frameSize / config.sampleRate;
    const std::array<Signal, 4> signals {{
        Signal::Sine, Signal::Saw, Signal::KarplusStrong, Signal::SawAdsr
    }};
    const std::array<double, 2> phases {{ 0.0, 0.371 }};
    const int firstMidi = quick ? 40 : 28;
    const int lastMidi = quick ? 84 : 96;
    const int phaseCount = quick ? 1 : static_cast<int> (phases.size());
    Stats stats[4][5];

    std::vector<float> clean (static_cast<std::size_t> (2 * frameSize));
    std::vector<float> noise (clean.size());
    std::vector<float> mixed (clean.size());

    for (int midi = firstMidi; midi <= lastMidi; ++midi)
    {
        const double hz = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
        for (int wave = 0; wave < static_cast<int> (signals.size()); ++wave)
        {
            for (int phaseIndex = 0; phaseIndex < phaseCount; ++phaseIndex)
            {
                const std::uint32_t seed = static_cast<std::uint32_t> (
                    midi * 7919 + wave * 101 + phaseIndex * 65537);
                std::mt19937 rng (seed);
                synthesize (clean, config.sampleRate, hz, signals[static_cast<std::size_t> (wave)],
                            phases[static_cast<std::size_t> (phaseIndex)], rng);
                std::normal_distribution<float> normal (0.0f, 1.0f);
                double signalPower = 0.0, noisePower = 0.0;
                for (std::size_t i = 0; i < clean.size(); ++i)
                {
                    noise[i] = normal (rng);
                    signalPower += static_cast<double> (clean[i]) * clean[i];
                    noisePower += static_cast<double> (noise[i]) * noise[i];
                }
                signalPower /= clean.size();
                noisePower /= clean.size();

                for (int snrIndex = 0; snrIndex < static_cast<int> (kSnrDb.size()); ++snrIndex)
                {
                    const double ratio = std::pow (10.0, kSnrDb[static_cast<std::size_t> (snrIndex)] / 10.0);
                    const float scale = noisePower > 0.0
                        ? static_cast<float> (std::sqrt (signalPower / (noisePower * ratio))) : 0.0f;
                    for (std::size_t i = 0; i < mixed.size(); ++i)
                        mixed[i] = clean[i] + scale * noise[i];

                    float confidence = 0.0f;
                    const auto begin = std::chrono::steady_clock::now();
                    const float detected = tracker->estimate (mixed.data() + preRoll,
                                                               frameSize, -90.0f, confidence);
                    const auto end = std::chrono::steady_clock::now();
                    auto& s = stats[wave][snrIndex];
                    ++s.trials;
                    s.microsecondsSum += std::chrono::duration<double, std::micro> (end - begin).count();
                    s.confidenceSum += confidence;
                    const int bucket = std::clamp (static_cast<int> (confidence * 10.0f), 0, 9);
                    ++s.confidenceBuckets[static_cast<std::size_t> (bucket)];
                    if (detected > 0.0f)
                    {
                        ++s.voiced;
                        const double cents = std::abs (1200.0 * std::log2 (detected / hz));
                        s.absoluteCents.push_back (cents);
                        if (cents <= 50.0) ++s.correct;
                        else if (std::abs (cents - 1200.0) <= 80.0
                                 || std::abs (cents - 2400.0) <= 80.0)
                            ++s.octave;
                        else ++s.other;
                    }
                }
            }
        }
    }

    for (int wave = 0; wave < static_cast<int> (signals.size()); ++wave)
    {
        for (int snr = 0; snr < static_cast<int> (kSnrDb.size()); ++snr)
        {
            const auto& s = stats[wave][snr];
            const double denominator = std::max<std::uint64_t> (1, s.trials);
            std::cout << config.label << ',' << tracker->name() << ','
                      << config.sampleRate << ',' << frameSize << ','
                      << config.minF0 << ',' << config.maxF0 << ','
                      << config.periodMultiplier << ',' << tracker->getTauMin() << ','
                      << tracker->getTauMax() << ',' << frameDurationMs << ','
                      << signalName (signals[static_cast<std::size_t> (wave)]) << ','
                      << kSnrDb[static_cast<std::size_t> (snr)] << ','
                      << s.trials << ',' << s.voiced / denominator << ','
                      << s.correct / denominator << ',' << s.octave / denominator << ','
                      << s.other / denominator << ','
                      << percentile (s.absoluteCents, 0.50) << ','
                      << percentile (s.absoluteCents, 0.95) << ','
                      << s.confidenceSum / denominator << ','
                      << s.microsecondsSum / denominator << ','
                      << frameDurationMs << '\n';
        }
    }
}
}

int main (int argc, char** argv)
{
    const bool quick = argc > 1 && std::string (argv[1]) == "--quick";
    std::cout << "config,tracker,sample_rate,frame_size,min_f0,max_f0,period_multiplier,tau_min,tau_max,"
                 "frame_duration_ms,signal,snr_db,trials,voiced_rate,correct_rate,octave_error_rate,"
                 "other_error_rate,median_abs_cents,p95_abs_cents,mean_confidence,mean_us_per_estimate,"
                 "first_window_complete_ms\n";

    const std::array<Config, 8> configs {{
        { "ACF-2048-shipped", false, 48000.0, 2048, 55.0f, 1200.0f, 0.0f },
        { "ACF-2048-matched", false, 48000.0, 2048, 55.0f, 2200.0f, 0.0f },
        { "YIN-2048-2.0x", true, 48000.0, 0, 55.0f, 2200.0f, 2.0f },
        { "ACF-4096-experimental", false, 48000.0, 4096, 55.0f, 2200.0f, 0.0f },
        { "YIN-4096-2.5x", true, 48000.0, 0, 55.0f, 2200.0f, 2.5f },
        { "ACF-4096-96k", false, 96000.0, 4096, 55.0f, 2200.0f, 0.0f },
        { "YIN-4096-96k-2.0x", true, 96000.0, 0, 55.0f, 2200.0f, 2.0f },
        { "YIN-8192-96k-2.5x-low40", true, 96000.0, 0, 40.0f, 2200.0f, 2.5f }
    }};
    for (const auto& config : configs) runConfig (config, quick);
    return 0;
}
