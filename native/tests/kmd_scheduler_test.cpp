#include "../windows/kmd/scheduler.hpp"
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace nr::kmd;
void check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
int main() {
    try {
        Scheduler q;
        Ticket rejected{99, 99};
        check(q.submit(1, 4, rejected) == QueueResult::stopped, "stopped queue accepted work");
        check(rejected.serial == 99, "failed submission changed ticket");
        check(q.start() == QueueResult::ok, "start failed");
        check(q.submit(1, Scheduler::max_bytes + 1, rejected) == QueueResult::invalid,
              "oversized command accepted");
        std::deque<Work> reference;
        std::mt19937 random(0x4e5055);
        bool active = false, running = true;
        Work worker{};
        uint32_t completed = 0;
        for (unsigned step = 0; step < 200000; ++step) {
            switch (random() % 6) {
            case 0: {
                // Include repeated and wrapped OS fence IDs. Ordering is queue
                // ordering, never an ordinary unsigned comparison of fence IDs.
                uint32_t fence = step % 7 == 0 ? 0xffffffffu : random();
                Ticket ticket{};
                auto result = q.submit(fence, step % 65537, ticket);
                if (!running) check(result == QueueResult::stopped, "submission after stop");
                else if (reference.size() + size_t(active) == Scheduler::capacity)
                    check(result == QueueResult::full, "capacity exceeded");
                else {
                    check(result == QueueResult::ok, "unexpected submission failure");
                    reference.push_back({ticket, fence, step % 65537});
                }
                break;
            }
            case 1: {
                Work out{};
                auto result = q.take(out);
                if (!running) check(result == QueueResult::stopped, "take after stop");
                else if (active) check(result == QueueResult::busy, "concurrent workers accepted");
                else if (reference.empty()) check(result == QueueResult::invalid, "phantom work");
                else {
                    check(result == QueueResult::ok, "take failed");
                    check(out.fence == reference.front().fence &&
                          out.bytes == reference.front().bytes &&
                          out.ticket.serial == reference.front().ticket.serial, "FIFO corrupted");
                    worker = out;
                    reference.pop_front();
                    active = true;
                }
                break;
            }
            case 2:
                if (active) {
                    auto result = q.complete(worker.ticket);
                    if (running) {
                        check(result == QueueResult::ok, "valid completion rejected");
                        completed = worker.fence;
                        active = false;
                        check(q.complete(worker.ticket) == QueueResult::stale,
                              "duplicate completion accepted");
                    } else check(result == QueueResult::stale, "late completion accepted");
                }
                break;
            case 3:
                check(q.stop() == QueueResult::ok, "stop failed");
                running = false;
                reference.clear();
                break;
            case 4:
                if (!running && active) {
                    check(q.start() == QueueResult::busy, "restart before worker drained");
                    check(q.drain({worker.ticket.serial + 1, worker.ticket.epoch}) ==
                              QueueResult::stale, "unrelated worker drained");
                    check(q.drain(worker.ticket) == QueueResult::ok, "drain failed");
                    active = false;
                }
                break;
            case 5: {
                auto result = q.start();
                if (running || active) check(result == QueueResult::busy, "unsafe restart");
                else { check(result == QueueResult::ok, "restart failed"); running = true; }
                break;
            }
            }
            check(q.running() == running && q.active() == active &&
                  q.pending() == reference.size() && q.completed() == completed,
                  "scheduler diverged from independent lifecycle model");
        }
        // Exercise a full queue, including the active worker consuming a slot.
        q.stop();
        if (active) q.drain(worker.ticket);
        check(q.start() == QueueResult::ok, "capacity test start failed");
        for (size_t i = 0; i < Scheduler::capacity; ++i) {
            Ticket ticket{};
            check(q.submit(uint32_t(i), 0, ticket) == QueueResult::ok, "queue filled early");
        }
        check(q.take(worker) == QueueResult::ok, "full queue take failed");
        check(q.submit(65, 0, rejected) == QueueResult::full, "active slot not counted");
        std::cout << "200000 lifecycle operations; FIFO, stop/drain, stale fences and bounds pass\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
