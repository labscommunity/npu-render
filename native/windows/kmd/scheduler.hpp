// Bounded software queue shared by the WDDM miniport and its host tests.
// The caller serializes all accesses. Tickets, unlike WDDM fence IDs, cannot
// be reused after preemption/reset: late worker completions must be rejected.
#pragma once

namespace nr::kmd {
using uint32_t = unsigned int;
using uint64_t = unsigned long long;
using size_t = decltype(sizeof(0));
static_assert(sizeof(uint32_t) == 4 && sizeof(uint64_t) == 8);
struct Ticket { uint64_t serial = 0; uint64_t epoch = 0; };
struct Work { Ticket ticket; uint32_t fence = 0; uint32_t bytes = 0; };
enum class QueueResult { ok, stopped, full, invalid, stale, busy };
class Scheduler {
public:
    static constexpr size_t capacity = 64;
    static constexpr uint32_t max_bytes = 65536;
    QueueResult start() {
        if (running_ || active_ || count_) return QueueResult::busy;
        if (epoch_ == ~uint64_t(0)) return QueueResult::invalid;
        ++epoch_;
        running_ = true;
        return QueueResult::ok;
    }
    QueueResult submit(uint32_t fence, uint32_t bytes, Ticket &ticket) {
        if (!running_) return QueueResult::stopped;
        if (bytes > max_bytes || serial_ == ~uint64_t(0)) return QueueResult::invalid;
        if (count_ + size_t(active_) == capacity) return QueueResult::full;
        ticket = {++serial_, epoch_};
        pending_[(head_ + count_) % capacity] = {ticket, fence, bytes};
        ++count_;
        return QueueResult::ok;
    }
    QueueResult take(Work &work) {
        if (!running_) return QueueResult::stopped;
        if (active_) return QueueResult::busy;
        if (!count_) return QueueResult::invalid;
        current_ = pending_[head_];
        head_ = (head_ + 1) % capacity;
        --count_;
        active_ = true;
        work = current_;
        return QueueResult::ok;
    }
    QueueResult complete(Ticket ticket) {
        if (!active_ || ticket.epoch != epoch_ ||
            ticket.serial != current_.ticket.serial) return QueueResult::stale;
        completed_ = current_.fence;
        active_ = false;
        return QueueResult::ok;
    }
    // Invalidate outstanding completions, but do NOT free worker memory or
    // claim the device is quiescent. stop/reset callers must join the worker
    // before releasing resources. drain() is the separate acknowledgement.
    QueueResult stop() {
        if (epoch_ == ~uint64_t(0)) return QueueResult::invalid;
        running_ = false;
        ++epoch_;
        count_ = 0;
        head_ = 0;
        return QueueResult::ok;
    }
    QueueResult drain(Ticket ticket) {
        if (running_ || !active_ || ticket.serial != current_.ticket.serial ||
            ticket.epoch != current_.ticket.epoch) return QueueResult::stale;
        active_ = false;
        return QueueResult::ok;
    }
    bool running() const { return running_; }
    bool active() const { return active_; }
    size_t pending() const { return count_; }
    uint32_t completed() const { return completed_; }
private:
    Work pending_[capacity]{};
    Work current_{};
    size_t head_ = 0, count_ = 0;
    uint64_t epoch_ = 0, serial_ = 0;
    uint32_t completed_ = 0;
    bool running_ = false, active_ = false;
};
} // namespace nr::kmd
