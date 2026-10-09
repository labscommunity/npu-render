#include "../windows/kmd/scheduler.hpp"
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
using namespace nr::kmd;
void check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
void exercise(Scheduler &q) {
    Ticket rejected{99, 99};
    check(q.submit(1, 4, rejected) == QueueResult::stopped, "stopped queue accepted work");
    check(rejected.serial == 99, "failed submission changed ticket");
    check(q.start() == QueueResult::ok, "start failed");
    check(q.submit(1, Scheduler::max_bytes + 1, rejected) == QueueResult::invalid,
          "oversized command accepted");
    std::deque<Work> reference;
    std::mt19937 random(0x4e5055);
    bool active = false, running = true, preempting = false;
    size_t cancel_count = 0;
    uint32_t preempt_fence = 0;
    Work worker{};
    uint32_t completed = q.completed();
    uint32_t submitted = q.submitted();
    for (unsigned step = 0; step < 200000; ++step) {
        switch (random() % 9) {
        case 0: {
            // Include repeated and wrapped OS fence IDs. Ordering is queue
            // ordering, never an ordinary unsigned comparison of fence IDs.
            uint32_t fence = step % 7 == 0 ? 0xffffffffu : random();
            Ticket ticket{};
            auto result = q.submit(fence, step % 65537, ticket);
            if (!running) check(result == QueueResult::stopped, "submission after stop");
            else if (reference.size() + size_t(active) == q.limit())
                check(result == QueueResult::full, "capacity exceeded");
            else {
                check(result == QueueResult::ok, "unexpected submission failure");
                reference.push_back({ticket, fence, step % 65537});
                submitted = fence;
            }
            break;
        }
        case 1: {
            Work out{};
            auto result = q.take(out);
            if (!running) check(result == QueueResult::stopped, "take after stop");
            else if (active || preempting) check(result == QueueResult::busy, "blocked worker accepted");
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
            preempting = false;
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
        case 6: {
            const uint32_t requested = random() % 4;
            const auto result = q.request_preempt(requested);
            if (!running) check(result == QueueResult::stopped, "preempt after stop");
            else if (preempting) check(result == QueueResult::busy, "preempt request overwritten");
            else {
                check(result == QueueResult::ok, "preemption request rejected");
                preempting = true;
                cancel_count = reference.size();
                preempt_fence = requested;
            }
            break;
        }
        case 7: {
            uint32_t fence = 99, last = 99;
            const auto result = q.finish_preempt(fence, last);
            if (!running) check(result == QueueResult::stopped, "preemption completed after stop");
            else if (!preempting) check(result == QueueResult::invalid, "phantom preemption");
            else if (active) check(result == QueueResult::busy, "preempted before worker finished");
            else {
                check(result == QueueResult::ok && fence == preempt_fence && last == completed,
                      "wrong preemption completion");
                for (size_t i = 0; i < cancel_count; ++i) reference.pop_front();
                preempting = false;
            }
            if (result != QueueResult::ok)
                check(fence == 99 && last == 99, "rejected preemption changed output");
            break;
        }
        case 8: {
            const auto result = q.reset_adapter_fence();
            if (running || active || !reference.empty())
                check(result == QueueResult::busy, "reset baseline advanced before drain");
            else {
                check(result == QueueResult::ok, "drained reset rejected");
                completed = submitted;
            }
            break;
        }
        }
        check(q.running() == running && q.active() == active &&
              q.pending() == reference.size() && q.completed() == completed &&
              q.preempting() == preempting && q.submitted() == submitted,
              "scheduler diverged from independent lifecycle model");
    }
    // Exercise a full queue, including the active worker consuming a slot.
    q.stop();
    if (active) q.drain(worker.ticket);
    check(q.start() == QueueResult::ok, "capacity test start failed");
    for (size_t i = 0; i < q.limit(); ++i) {
        Ticket ticket{};
        check(q.submit(uint32_t(i), 0, ticket) == QueueResult::ok, "queue filled early");
    }
    check(q.take(worker) == QueueResult::ok, "full queue take failed");
    check(q.submit(65, 0, rejected) == QueueResult::full, "active slot not counted");
    check(q.set_storage(nullptr, 0) == QueueResult::busy, "running queue storage released");
    check(q.stop() == QueueResult::ok, "final stop failed");
    check(q.set_storage(nullptr, 0) == QueueResult::busy, "active worker storage released");
    check(q.drain(worker.ticket) == QueueResult::ok, "final drain failed");
}
int main() {
    try {
        Scheduler q;
        check(q.set_storage(nullptr, 1) == QueueResult::invalid, "null external storage accepted");
        Work unusable{};
        check(q.set_storage(&unusable, 0) == QueueResult::invalid, "zero external capacity accepted");
        exercise(q);
        for (const size_t entries : {size_t(1), size_t(3), size_t(257)}) {
            std::vector<Work> memory(entries + 2);
            memory.front().ticket.serial = 0x12345678;
            memory.back().ticket.serial = 0x87654321;
            check(q.set_storage(memory.data() + 1, entries) == QueueResult::ok,
                  "external queue setup failed");
            check(q.limit() == entries, "external capacity ignored");
            exercise(q);
            check(memory.front().ticket.serial == 0x12345678 &&
                  memory.back().ticket.serial == 0x87654321, "queue guard overwritten");
            check(q.set_storage(nullptr, 0) == QueueResult::ok, "drained storage retained");
            check(q.limit() == Scheduler::capacity, "embedded capacity not restored");
        }
        std::cout << "800000 lifecycle operations across four capacities; FIFO, preemption boundaries, stop/drain, stale fences and storage lifetime pass\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
