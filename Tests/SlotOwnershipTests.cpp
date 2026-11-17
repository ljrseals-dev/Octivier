// Standalone C++17 stress test for the coefficient-slot and request-mailbox protocol.
// It intentionally has no JUCE dependency; it does not replace plugin/DAW tests.
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

namespace
{
enum : int { Idle = 0, Writing = 1, Ready = 2, Reading = 3 };
enum : int { Empty = 0, MailWriting = 1, MailReady = 2, MailReading = 3 };
struct Request { std::uint64_t value = 0; };
struct Slot { std::atomic<int> state { Idle }; std::uint64_t value = 0; std::atomic<std::uint64_t> generation { 0 }; };
struct Mailbox { std::atomic<int> state { Empty }; Request payload {}; };

bool publish(Mailbox& m, std::uint64_t value)
{
    int expected = Empty;
    if (!m.state.compare_exchange_strong(expected, MailWriting, std::memory_order_acquire, std::memory_order_relaxed)) return false;
    m.payload.value = value;
    m.state.store(MailReady, std::memory_order_release);
    return true;
}
}

int main()
{
    constexpr std::uint64_t target = 250000;
    std::array<Slot, 3> slots;
    Mailbox mailbox;
    slots[0].value = 0;
    slots[0].generation.store(1);
    slots[0].state.store(Ready, std::memory_order_release);
    std::atomic<bool> done { false };
    std::atomic<std::uint64_t> published { 0 }, consumed { 0 }, readCount { 0 };
    std::atomic<bool> failure { false };

    std::thread worker([&]
    {
        while (!done.load(std::memory_order_acquire) || mailbox.state.load(std::memory_order_acquire) != Empty)
        {
            int slotIndex = -1;
            for (int i = 0; i < static_cast<int>(slots.size()); ++i)
            {
                int expected = Idle;
                if (slots[static_cast<std::size_t>(i)].state.compare_exchange_strong(expected, Writing,
                        std::memory_order_acquire, std::memory_order_relaxed)) { slotIndex = i; break; }
            }
            if (slotIndex < 0) { std::this_thread::yield(); continue; }
            int expected = MailReady;
            if (!mailbox.state.compare_exchange_strong(expected, MailReading,
                    std::memory_order_acquire, std::memory_order_relaxed))
            {
                slots[static_cast<std::size_t>(slotIndex)].state.store(Idle, std::memory_order_release);
                std::this_thread::yield();
                continue;
            }
            const auto req = mailbox.payload;
            auto& slot = slots[static_cast<std::size_t>(slotIndex)];
            slot.value = req.value; // exclusive Writing ownership
            slot.generation.fetch_add(1, std::memory_order_relaxed);
            slot.state.store(Ready, std::memory_order_release);
            consumed.fetch_add(1, std::memory_order_relaxed);
            mailbox.state.store(Empty, std::memory_order_release);
        }
    });

    std::thread audio([&]
    {
        int active = -1;
        std::uint64_t next = 1;
        while (next <= target)
        {
            // Publish one request at a time; if the mailbox is busy, retry.
            if (publish(mailbox, next)) { published.fetch_add(1, std::memory_order_relaxed); ++next; }
            int replacement = -1;
            for (int i = 0; i < static_cast<int>(slots.size()); ++i)
            {
                int expected = Ready;
                if (slots[static_cast<std::size_t>(i)].state.compare_exchange_strong(expected, Reading,
                        std::memory_order_acquire, std::memory_order_relaxed)) { replacement = i; break; }
            }
            if (replacement >= 0)
            {
                const int old = active;
                active = replacement;
                if (old >= 0 && old != replacement) slots[static_cast<std::size_t>(old)].state.store(Idle, std::memory_order_release);
            }
            if (active >= 0)
            {
                const auto value = slots[static_cast<std::size_t>(active)].value;
                if (value > target) failure.store(true, std::memory_order_relaxed);
                readCount.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
        // Keep adopting until the final request has reached a slot and mailbox drains.
        for (int spins = 0; spins < 5000000 && (consumed.load() < published.load() || mailbox.state.load() != Empty); ++spins)
        {
            int replacement = -1;
            for (int i = 0; i < static_cast<int>(slots.size()); ++i)
            {
                int expected = Ready;
                if (slots[static_cast<std::size_t>(i)].state.compare_exchange_strong(expected, Reading,
                        std::memory_order_acquire, std::memory_order_relaxed)) { replacement = i; break; }
            }
            if (replacement >= 0)
            {
                const int old = active; active = replacement;
                if (old >= 0 && old != replacement) slots[static_cast<std::size_t>(old)].state.store(Idle, std::memory_order_release);
            }
            std::this_thread::yield();
        }
        if (active >= 0) slots[static_cast<std::size_t>(active)].state.store(Idle, std::memory_order_release);
        done.store(true, std::memory_order_release);
    });

    audio.join();
    worker.join();
    if (failure.load() || consumed.load() != published.load() || published.load() != target)
    {
        std::cerr << "FAIL: protocol stress: published=" << published << " consumed=" << consumed << '\n';
        return 1;
    }
    std::cout << "PASS: " << target << " requests published and consumed; mailbox payload handoff and slot ownership stress completed.\n";
    std::cout << "NOTE: standalone protocol stress only; JUCE compilation, ThreadSanitizer, pluginval, and DAW tests remain separate gates.\n";
    return 0;
}
