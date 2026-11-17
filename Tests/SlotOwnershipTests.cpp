// Standalone C++17 SPSC protocol stress model. No JUCE dependency.
// Contract: exactly one producer (audio-side publisher) and one consumer
// (worker). Multiple concurrent writers are deliberately out of contract.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <thread>

namespace
{
enum : int { Idle = 0, Writing = 1, Ready = 2, Reading = 3 };
enum : int { Empty = 0, MailWriting = 1, MailReady = 2, MailReading = 3 };

struct Request
{
    std::uint64_t sequence = 0;
    std::uint64_t generation = 0;
    std::uint64_t checksum = 0;
};

std::uint64_t makeChecksum (std::uint64_t sequence, std::uint64_t generation) noexcept
{
    return (sequence * 0x9e3779b185ebca87ULL) ^ (generation * 0xc2b2ae3d27d4eb4fULL);
}

bool coherent (const Request& request) noexcept
{
    return request.checksum == makeChecksum (request.sequence, request.generation);
}

struct Slot
{
    std::atomic<int> state { Idle };
    Request payload {};
    std::atomic<std::uint64_t> generation { 0 };
};

struct Mailbox
{
    std::atomic<int> state { Empty };
    Request payload {};
};

bool publish (Mailbox& mailbox, std::uint64_t sequence) noexcept
{
    int expected = Empty;
    if (! mailbox.state.compare_exchange_strong (expected, MailWriting,
            std::memory_order_acquire, std::memory_order_relaxed))
        return false;

    const std::uint64_t generation = sequence / 997U + 1U;
    mailbox.payload = { sequence, generation, makeChecksum (sequence, generation) };
    mailbox.state.store (MailReady, std::memory_order_release);
    return true;
}
}

int main()
{
    constexpr std::uint64_t target = 250000;
    std::array<Slot, 3> slots;
    Mailbox mailbox;
    slots[0].payload = { 0, 1, makeChecksum (0, 1) };
    slots[0].generation.store (1, std::memory_order_relaxed);
    slots[0].state.store (Ready, std::memory_order_release);

    std::atomic<bool> done { false }, failure { false };
    std::atomic<std::uint64_t> published { 0 }, rejected { 0 };
    std::atomic<std::uint64_t> consumed { 0 }, lastConsumed { 0 };

    // The consumer models the worker: it owns a slot only after Idle -> Writing,
    // and the mailbox payload only after Ready -> Reading.
    std::thread worker ([&]
    {
        std::minstd_rand rng (0x51A7U);
        while (! done.load (std::memory_order_acquire)
               || mailbox.state.load (std::memory_order_acquire) != Empty)
        {
            int slotIndex = -1;
            for (int i = 0; i < static_cast<int> (slots.size()); ++i)
            {
                int expected = Idle;
                if (slots[static_cast<std::size_t> (i)].state.compare_exchange_strong (
                        expected, Writing, std::memory_order_acquire, std::memory_order_relaxed))
                { slotIndex = i; break; }
            }
            if (slotIndex < 0) { std::this_thread::yield(); continue; }

            int expected = MailReady;
            if (! mailbox.state.compare_exchange_strong (expected, MailReading,
                    std::memory_order_acquire, std::memory_order_relaxed))
            {
                slots[static_cast<std::size_t> (slotIndex)].state.store (Idle, std::memory_order_release);
                std::this_thread::yield();
                continue;
            }

            const Request request = mailbox.payload;
            if (! coherent (request)) failure.store (true, std::memory_order_relaxed);
            auto& slot = slots[static_cast<std::size_t> (slotIndex)];
            slot.payload = request;
            slot.generation.fetch_add (1, std::memory_order_relaxed);
            slot.state.store (Ready, std::memory_order_release);
            consumed.fetch_add (1, std::memory_order_relaxed);
            lastConsumed.store (request.sequence, std::memory_order_relaxed);
            mailbox.state.store (Empty, std::memory_order_release);

            // Deterministic asymmetric stalls exercise mailbox back-pressure.
            if ((request.sequence % 10007U) == 0U)
                std::this_thread::sleep_for (std::chrono::milliseconds (2));
            else if ((rng() % 31U) == 0U)
                std::this_thread::sleep_for (std::chrono::microseconds (20));
        }
    });

    // The producer models the audio-side SPSC publisher. Rejected attempts are
    // counted separately; they must never be mistaken for successful publishes.
    std::thread audio ([&]
    {
        int active = -1;
        std::uint64_t next = 1;
        std::minstd_rand rng (0xA0D10U);

        while (next <= target)
        {
            if (publish (mailbox, next))
            {
                published.fetch_add (1, std::memory_order_relaxed);
                ++next;
                if (next == target / 2U)
                    std::this_thread::sleep_for (std::chrono::milliseconds (3)); // producer stall
            }
            else
            {
                rejected.fetch_add (1, std::memory_order_relaxed);
            }

            int replacement = -1;
            for (int i = 0; i < static_cast<int> (slots.size()); ++i)
            {
                int expected = Ready;
                if (slots[static_cast<std::size_t> (i)].state.compare_exchange_strong (
                        expected, Reading, std::memory_order_acquire, std::memory_order_relaxed))
                { replacement = i; break; }
            }
            if (replacement >= 0)
            {
                const int old = active;
                active = replacement;
                const auto& adopted = slots[static_cast<std::size_t> (active)].payload;
                if (! coherent (adopted) || adopted.sequence > target)
                    failure.store (true, std::memory_order_relaxed);
                if (old >= 0 && old != replacement)
                    slots[static_cast<std::size_t> (old)].state.store (Idle, std::memory_order_release);
            }

            if ((rng() % 43U) == 0U)
                std::this_thread::sleep_for (std::chrono::microseconds (10));
            else
                std::this_thread::yield();
        }

        // Drain the final successful request and adopt the last published slot.
        while (consumed.load (std::memory_order_acquire) < published.load (std::memory_order_acquire)
               || mailbox.state.load (std::memory_order_acquire) != Empty)
        {
            int replacement = -1;
            for (int i = 0; i < static_cast<int> (slots.size()); ++i)
            {
                int expected = Ready;
                if (slots[static_cast<std::size_t> (i)].state.compare_exchange_strong (
                        expected, Reading, std::memory_order_acquire, std::memory_order_relaxed))
                { replacement = i; break; }
            }
            if (replacement >= 0)
            {
                const int old = active;
                active = replacement;
                const auto& adopted = slots[static_cast<std::size_t> (active)].payload;
                if (! coherent (adopted)) failure.store (true, std::memory_order_relaxed);
                if (old >= 0 && old != replacement)
                    slots[static_cast<std::size_t> (old)].state.store (Idle, std::memory_order_release);
            }
            std::this_thread::yield();
        }

        // The last successful payload, not the last attempted payload, must
        // reach the consumer. Since rejected attempts do not advance next,
        // the final successful sequence is exactly target.
        if (active >= 0)
        {
            const auto& finalPayload = slots[static_cast<std::size_t> (active)].payload;
            if (! coherent (finalPayload) || finalPayload.sequence != target)
                failure.store (true, std::memory_order_relaxed);
            slots[static_cast<std::size_t> (active)].state.store (Idle, std::memory_order_release);
        }
        done.store (true, std::memory_order_release);
    });

    audio.join();
    worker.join();

    const auto successCount = published.load();
    const auto rejectCount = rejected.load();
    const auto consumeCount = consumed.load();
    if (failure.load() || successCount != target || consumeCount != successCount
        || lastConsumed.load() != target || rejectCount == 0)
    {
        std::cerr << "FAIL: SPSC protocol: published=" << successCount
                  << " rejected=" << rejectCount << " consumed=" << consumeCount
                  << " lastConsumed=" << lastConsumed.load() << '\n';
        return 1;
    }

    std::cout << "PASS: " << successCount << " coherent requests published and consumed; "
              << rejectCount << " rejected attempts counted separately.\n";
    std::cout << "NOTE: standalone SPSC model only; production mailbox, JUCE build, "
                 "ThreadSanitizer, RTSan, pluginval, and DAW tests remain separate gates.\n";
    return 0;
}
