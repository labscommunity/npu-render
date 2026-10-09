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
    Scheduler() = default;
    Scheduler(const Scheduler &) = delete;
    Scheduler &operator=(const Scheduler &) = delete;
    // The kernel owner preallocates storage using RequiredDmaQueueEntry and
    // keeps it alive until stop/drain. Null plus zero restores embedded storage.
    QueueResult set_storage(Work *storage, size_t entries) {
        if (running_ || active_ || count_) return QueueResult::busy;
        if ((!storage) != (!entries)) return QueueResult::invalid;
        storage_ = storage ? storage : pending_;
        entries_ = storage ? entries : capacity;
        head_ = 0;
        return QueueResult::ok;
    }
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
        if (count_ + size_t(active_) == entries_) return QueueResult::full;
        ticket = {++serial_, epoch_};
        submitted_ = fence;
        // Avoid overflowing head + count even for a caller's large capacity.
        const auto tail = count_ >= entries_ - head_ ? count_ - (entries_ - head_) : head_ + count_;
        storage_[tail] = {ticket, fence, bytes};
        ++count_;
        return QueueResult::ok;
    }
    QueueResult take(Work &work) {
        if (!running_) return QueueResult::stopped;
        if (active_ || preempting_) return QueueResult::busy;
        if (!count_) return QueueResult::invalid;
        current_ = storage_[head_];
        head_ = head_ + 1 == entries_ ? 0 : head_ + 1;
        --count_;
        active_ = true;
        work = current_;
        return QueueResult::ok;
    }
    // DMA-buffer boundary preemption. Finish an already active buffer, then
    // cancel only work submitted before this request. Later submissions retain
    // their order. Keep all slots occupied until the active worker finishes.
    QueueResult request_preempt(uint32_t fence) {
        if (!running_) return QueueResult::stopped;
        if (preempting_) return QueueResult::busy;
        preempting_ = true;
        preempt_fence_ = fence;
        preempt_cut_ = serial_;
        return QueueResult::ok;
    }
    QueueResult finish_preempt(uint32_t &fence, uint32_t &completed) {
        if (!running_) return QueueResult::stopped;
        if (!preempting_) return QueueResult::invalid;
        if (active_) return QueueResult::busy;
        while (count_ && storage_[head_].ticket.serial <= preempt_cut_) {
            head_ = head_ + 1 == entries_ ? 0 : head_ + 1;
            --count_;
        }
        fence = preempt_fence_;
        completed = completed_;
        preempting_ = false;
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
        preempting_ = false;
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
    bool preempting() const { return preempting_; }
    size_t pending() const { return count_; }
    size_t limit() const { return entries_; }
    uint32_t completed() const { return completed_; }
    uint32_t submitted() const { return submitted_; }
    // WDDM adapter recovery advances the scheduling baseline to the last
    // submitted fence after all users drain. This is not a DMA completion and
    // must never generate a successful execution notification.
    QueueResult reset_adapter_fence() {
        if (running_ || active_ || count_) return QueueResult::busy;
        completed_ = submitted_;
        return QueueResult::ok;
    }
private:
    Work pending_[capacity]{};
    Work *storage_ = pending_;
    size_t entries_ = capacity;
    Work current_{};
    size_t head_ = 0, count_ = 0;
    uint64_t epoch_ = 0, serial_ = 0;
    uint64_t preempt_cut_ = 0;
    uint32_t completed_ = 0;
    uint32_t submitted_ = 0;
    uint32_t preempt_fence_ = 0;
    bool running_ = false, active_ = false, preempting_ = false;
};
} // namespace nr::kmd
