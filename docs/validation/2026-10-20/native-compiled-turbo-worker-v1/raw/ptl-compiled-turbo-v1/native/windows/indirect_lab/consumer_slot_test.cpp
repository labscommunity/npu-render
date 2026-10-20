#include "consumer_slot.hpp"
#include <atomic>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
void check(bool value) { if (!value) throw std::runtime_error("consumer ownership test failed"); }
struct Consumer {
    std::atomic<unsigned> &destroyed;
    std::function<void()> on_destroy;
    explicit Consumer(std::atomic<unsigned> &count) : destroyed(count) {}
    ~Consumer() { ++destroyed; if (on_destroy) on_destroy(); }
};
}
int main() {
    try {
        nr::indirect::ConsumerSlot<Consumer> slot{};
        std::atomic<unsigned> destroyed{0};
        auto first = slot.begin(); auto candidate = std::make_unique<Consumer>(destroyed);
        candidate->on_destroy = [&] {
            // IddCx can reenter teardown while retirement waits for a worker.
            auto reentered = slot.begin(); check(!reentered.previous);
        };
        check(slot.publish(first.generation, candidate) && !candidate);
        auto retired = slot.begin(); check(bool(retired.previous));
        retired.previous.reset(); check(destroyed == 1);
        auto starting = slot.begin(); candidate = std::make_unique<Consumer>(destroyed);
        auto unassigned = slot.begin(); check(!slot.publish(starting.generation, candidate));
        candidate.reset(); check(destroyed == 2);
        auto closing = slot.begin(); candidate = std::make_unique<Consumer>(destroyed);
        auto closed = slot.begin(true); check(!closed.allowed && !slot.publish(closing.generation, candidate));
        candidate.reset(); check(destroyed == 3 && !slot.begin().allowed);

        nr::indirect::ConsumerSlot<Consumer> concurrent{};
        constexpr unsigned threads = 4, rounds = 2000;
        std::vector<std::atomic<unsigned>> counts(threads * rounds);
        for (auto &count : counts) count = 0;
        std::vector<std::thread> workers;
        for (unsigned t = 0; t < threads; ++t) workers.emplace_back([&, t] {
            for (unsigned i = 0; i < rounds; ++i) {
                auto update = concurrent.begin(); update.previous.reset();
                auto current = std::make_unique<Consumer>(counts[t * rounds + i]);
                std::this_thread::yield();
                (void)concurrent.publish(update.generation, current);
            }
        });
        for (auto &worker : workers) worker.join();
        auto final = concurrent.begin(true); final.previous.reset();
        for (const auto &count : counts) check(count == 1);
        std::puts("{\"consumer_ownership\":true,\"reentrant_retirement\":true,\"stale_publication_rejected\":true,\"concurrent_consumers_destroyed_once\":8000,\"desktop_offload_tested\":false}");
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
