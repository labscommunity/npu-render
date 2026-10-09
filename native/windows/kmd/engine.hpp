// Serialized WDDM software paging engine. The owner preallocates queue/segment
// storage, and must stop/join this engine before releasing either. This does not
// implement rendering or establish that Windows accepts a started adapter.
#pragma once

namespace nr::kmd {
class Engine {
public:
    Engine() = default;
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    NTSTATUS start(const DXGKRNL_INTERFACE &kernel, Scheduler &queue,
                   Submissions &submissions, Segment &segment) {
        if (KeGetCurrentIrql() != PASSIVE_LEVEL || thread_ || alive() ||
            queue.running() || queue.active() || !submissions.ready() || !segment.ready())
            return STATUS_INVALID_DEVICE_STATE;
        if (!kernel.DeviceHandle || !kernel.DxgkCbSynchronizeExecution ||
            !kernel.DxgkCbNotifyInterrupt || !kernel.DxgkCbQueueDpc || !kernel.DxgkCbNotifyDpc)
            return STATUS_INVALID_PARAMETER;
        kernel_ = kernel;
        queue_ = &queue;
        submissions_ = &submissions;
        segment_ = &segment;
        inflight_ = {};
        aborted_fence_ = queue.completed();
        worker_irql_ = 0xff;
        InterlockedExchange(&quit_, 0);
        InterlockedExchange(&worker_exited_, 0);
        InterlockedExchange64(&progress_time_, 0);
        KeInitializeEvent(&wake_, SynchronizationEvent, FALSE);
        if (queue.start() != QueueResult::ok) return STATUS_INVALID_DEVICE_STATE;
        InterlockedExchange(&alive_, 1);
        OBJECT_ATTRIBUTES attributes{};
        InitializeObjectAttributes(&attributes, nullptr, OBJ_KERNEL_HANDLE, nullptr, nullptr);
        const auto status = PsCreateSystemThread(&thread_, SYNCHRONIZE, &attributes,
                                                nullptr, nullptr, thread_main, this);
        if (status != STATUS_SUCCESS) {
            queue.stop();
            InterlockedExchange(&alive_, 0);
            thread_ = nullptr;
            clear();
        }
        return status;
    }

    NTSTATUS submit(const DXGKARG_SUBMITCOMMAND &args) {
        Action action{this, Operation::submit};
        action.submission = &args;
        const auto result = synchronize(action);
        if (result == STATUS_SUCCESS) KeSetEvent(&wake_, IO_NO_INCREMENT, FALSE);
        return result;
    }
    NTSTATUS preempt(const DXGKARG_PREEMPTCOMMAND &args) {
        if (args.NodeOrdinal || args.EngineOrdinal || args.Flags.Value) return STATUS_NOT_SUPPORTED;
        Action action{this, Operation::preempt};
        action.fence = args.PreemptionFenceId;
        const auto result = synchronize(action);
        if (result == STATUS_SUCCESS) KeSetEvent(&wake_, IO_NO_INCREMENT, FALSE);
        return result;
    }
    NTSTATUS query(UINT &fence) {
        Action action{this, Operation::query};
        const auto result = synchronize(action);
        if (result == STATUS_SUCCESS) fence = action.fence;
        return result;
    }
    bool alive() const { return InterlockedCompareExchange(&alive_, 0, 0) != 0; }
    KIRQL worker_irql_after_join() const { return worker_irql_; }
    UINT aborted_fence_after_join() const { return aborted_fence_; }
    NTSTATUS health(DXGK_ENGINESTATUS &health) {
        health.Value = 0;
        if (!alive()) return STATUS_SUCCESS;
        Action action{this, Operation::health};
        // A failed synchronization is an unresponsive engine, not a successful
        // progress observation. Valid status queries always return success.
        if (synchronize(action) == STATUS_SUCCESS) health.Responsive = action.fence ? 1 : 0;
        return STATUS_SUCCESS;
    }
    struct DebugInfo {
        ULONG magic = 0x4e524544, version = 1, bytes = sizeof(DebugInfo), reason = 0;
        LONG alive = 0, quit = 0, worker_exited = 0, reserved = 0;
        ULONGLONG last_progress_time = 0;
    };
    DebugInfo debug_snapshot(ULONG reason) {
        // Individually atomic observations, not a transaction across fields.
        // No allocation, lock or Dxgkrnl callback at synchronization level zero.
        DebugInfo result{};
        result.reason = reason;
        result.alive = InterlockedCompareExchange(&alive_, 0, 0);
        result.quit = InterlockedCompareExchange(&quit_, 0, 0);
        result.worker_exited = InterlockedCompareExchange(&worker_exited_, 0, 0);
        result.last_progress_time = ULONGLONG(InterlockedCompareExchange64(&progress_time_, 0, 0));
        return result;
    }
    // Crash-time reset must not wait or enter Dxgkrnl synchronization. Normal
    // reset/stop calls stop() at PASSIVE_LEVEL and joins the worker separately.
    void request_shutdown() { InterlockedExchange(&quit_, 1); }
    void observe_dpc() {
        if (alive() && KeGetCurrentIrql() == DISPATCH_LEVEL)
            kernel_.DxgkCbNotifyDpc(kernel_.DeviceHandle);
    }

    NTSTATUS stop() {
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
        if (!thread_) return STATUS_SUCCESS;
        InterlockedExchange(&quit_, 1);
        Action stop_action{this, Operation::stop};
        synchronize(stop_action);
        KeSetEvent(&wake_, IO_NO_INCREMENT, FALSE);
        // The private kernel handle cannot be released while the worker can
        // still touch a borrowed MDL. An unexpected wait error is not a join.
        while (ZwWaitForSingleObject(thread_, FALSE, nullptr) != STATUS_SUCCESS) {
            LARGE_INTEGER retry{};
            retry.QuadPart = -10000000;
            KeDelayExecutionThread(KernelMode, FALSE, &retry);
        }
        // A failed synchronization may have left a claimed ticket. The worker
        // has joined, so it is now safe to acknowledge its separate drain.
        Action drain{this, Operation::drain};
        const auto drained = synchronize(drain);
        if (drained != STATUS_SUCCESS) return drained; // Retain all storage/handle.
        const auto closed = ZwClose(thread_);
        if (closed != STATUS_SUCCESS) return closed;
        thread_ = nullptr;
        // No producer remains; queued callbacks may still hold our context.
        KeFlushQueuedDpcs();
        InterlockedExchange(&alive_, 0);
        clear();
        return STATUS_SUCCESS;
    }

    // Exactly one PASSIVE_LEVEL worker may call this. Host tests step it with
    // a deferred thread shim; they are not evidence of real concurrency.
    NTSTATUS process_one() {
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
        Action claim{this, Operation::claim};
        auto status = synchronize(claim);
        if (status != STATUS_SUCCESS || !claim.work.ticket.serial) return status;
        progress();
        const auto packet = submissions_->packet(claim.work.ticket);
        status = packet ? STATUS_SUCCESS : STATUS_DATA_ERROR;
        if (packet) {
            for (UINT i = 0; i < packet->commands; ++i) {
                if (InterlockedCompareExchange(&quit_, 0, 0)) { status = STATUS_CANCELLED; break; }
                status = Paging::execute(*segment_, packet->paging[i]);
                if (status != STATUS_SUCCESS) break;
                progress();
            }
        }
        // No access to the packet or its MDLs is allowed after this call.
        Action finish{this, Operation::finish};
        finish.work = claim.work;
        finish.execution = status;
        return synchronize(finish);
    }

private:
    enum class Operation { submit, preempt, query, health, claim, finish, stop, drain };
    struct Action {
        Engine *engine;
        Operation operation;
        const DXGKARG_SUBMITCOMMAND *submission = nullptr;
        Work work{};
        UINT fence = 0;
        NTSTATUS execution = STATUS_NOT_SUPPORTED;
        NTSTATUS status = STATUS_UNSUCCESSFUL;
    };
    static NTSTATUS translate(QueueResult result) {
        switch (result) {
        case QueueResult::ok: return STATUS_SUCCESS;
        case QueueResult::busy: return STATUS_DEVICE_BUSY;
        case QueueResult::stopped: return STATUS_DEVICE_NOT_READY;
        default: return STATUS_INVALID_DEVICE_STATE;
        }
    }
    NTSTATUS synchronize(Action &action) {
        if (!alive() || KeGetCurrentIrql() > DISPATCH_LEVEL) return STATUS_DEVICE_NOT_READY;
        BOOLEAN returned = FALSE;
        const auto status = kernel_.DxgkCbSynchronizeExecution(
            kernel_.DeviceHandle, synchronized, &action, 0, &returned);
        if (status != STATUS_SUCCESS) return status;
        return returned ? action.status : STATUS_UNSUCCESSFUL;
    }
    void notify(DXGKARGCB_NOTIFY_INTERRUPT_DATA &data) {
        // Called only while public SynchronizeExecution holds DIRQL. FALSE
        // from QueueDpc means a DPC is already queued, not a failed command.
        kernel_.DxgkCbNotifyInterrupt(kernel_.DeviceHandle, &data);
        kernel_.DxgkCbQueueDpc(kernel_.DeviceHandle);
    }
    static BOOLEAN synchronized(void *context) {
        auto &action = *static_cast<Action *>(context);
        const auto irql = KeGetCurrentIrql();
        if (irql <= DISPATCH_LEVEL || irql >= PROFILE_LEVEL) return FALSE;
        auto &self = *action.engine;
        auto &queue = *self.queue_;
        switch (action.operation) {
        case Operation::submit: {
            Ticket ticket{};
            action.status = self.submissions_->capture(*action.submission, ticket);
            break;
        }
        case Operation::preempt:
            action.status = translate(queue.request_preempt(action.fence));
            break;
        case Operation::query:
            action.fence = queue.completed();
            action.status = STATUS_SUCCESS;
            break;
        case Operation::health: {
            const auto now = KeQueryInterruptTime();
            const auto last = ULONGLONG(InterlockedCompareExchange64(&self.progress_time_, 0, 0));
            const bool idle = !queue.active() && !queue.pending() && !queue.preempting();
            // One second without progress while work is outstanding is not a
            // responsive engine. Merely owning a thread handle proves nothing.
            const bool recent = last && now >= last && now - last < 10000000;
            action.fence = queue.running() && !InterlockedCompareExchange(&self.quit_, 0, 0) &&
                !InterlockedCompareExchange(&self.worker_exited_, 0, 0) && (idle || recent) ? 1 : 0;
            action.status = STATUS_SUCCESS;
            break;
        }
        case Operation::stop:
            if (queue.running())
                self.aborted_fence_ = queue.active() ? self.inflight_.fence : queue.completed();
            action.status = translate(queue.stop());
            break;
        case Operation::drain:
            if (queue.running())
                self.aborted_fence_ = queue.active() ? self.inflight_.fence : queue.completed();
            action.status = queue.running() ? translate(queue.stop()) : STATUS_SUCCESS;
            if (action.status == STATUS_SUCCESS && queue.active())
                action.status = translate(queue.drain(self.inflight_.ticket));
            break;
        case Operation::claim:
            if (!queue.running() || InterlockedCompareExchange(&self.quit_, 0, 0)) {
                action.status = STATUS_CANCELLED;
                break;
            }
            if (queue.preempting()) {
                DXGKARGCB_NOTIFY_INTERRUPT_DATA data{};
                data.InterruptType = DXGK_INTERRUPT_DMA_PREEMPTED;
                action.status = translate(queue.finish_preempt(
                    data.DmaPreempted.PreemptionFenceId, data.DmaPreempted.LastCompletedFenceId));
                if (action.status == STATUS_SUCCESS) self.notify(data);
            } else {
                const auto taken = queue.take(action.work);
                action.status = taken == QueueResult::invalid ? STATUS_NO_MORE_ENTRIES : translate(taken);
                if (action.status == STATUS_SUCCESS) self.inflight_ = action.work;
            }
            break;
        case Operation::finish: {
            if (!queue.running() || InterlockedCompareExchange(&self.quit_, 0, 0)) {
                if (queue.running()) {
                    self.aborted_fence_ = action.work.fence;
                    queue.stop();
                }
                action.status = translate(queue.drain(action.work.ticket));
                if (action.status == STATUS_SUCCESS) action.status = STATUS_CANCELLED;
                break;
            }
            DXGKARGCB_NOTIFY_INTERRUPT_DATA data{};
            if (action.execution == STATUS_SUCCESS) {
                action.status = translate(queue.complete(action.work.ticket));
                if (action.status != STATUS_SUCCESS) break;
                data.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
                data.DmaCompleted.SubmissionFenceId = action.work.fence;
            } else {
                self.aborted_fence_ = action.work.fence;
                queue.stop();
                action.status = translate(queue.drain(action.work.ticket));
                if (action.status != STATUS_SUCCESS) break;
                data.InterruptType = DXGK_INTERRUPT_DMA_FAULTED;
                data.DmaFaulted.FaultedFenceId = action.work.fence;
                data.DmaFaulted.Status = action.execution;
                action.status = action.execution;
            }
            self.notify(data);
            break;
        }
        }
        return TRUE;
    }
    static void thread_main(void *context) {
        auto &self = *static_cast<Engine *>(context);
        self.worker_irql_ = KeGetCurrentIrql();
        self.progress();
        while (!InterlockedCompareExchange(&self.quit_, 0, 0)) {
            const auto wait = KeWaitForSingleObject(&self.wake_, Executive, KernelMode, FALSE, nullptr);
            if (wait != STATUS_SUCCESS) break;
            NTSTATUS result;
            do { result = self.process_one(); } while (result == STATUS_SUCCESS);
            if (result != STATUS_NO_MORE_ENTRIES) break;
        }
        InterlockedExchange(&self.worker_exited_, 1);
        PsTerminateSystemThread(STATUS_SUCCESS);
    }
    void progress() { InterlockedExchange64(&progress_time_, LONGLONG(KeQueryInterruptTime())); }
    void clear() {
        RtlZeroMemory(&kernel_, sizeof(kernel_));
        queue_ = nullptr;
        submissions_ = nullptr;
        segment_ = nullptr;
    }
    DXGKRNL_INTERFACE kernel_{};
    Scheduler *queue_ = nullptr;
    Submissions *submissions_ = nullptr;
    Segment *segment_ = nullptr;
    Work inflight_{};
    HANDLE thread_ = nullptr;
    KEVENT wake_{};
    volatile LONG quit_ = 0;
    mutable volatile LONG alive_ = 0;
    KIRQL worker_irql_ = 0xff;
    UINT aborted_fence_ = 0;
    volatile LONG worker_exited_ = 0;
    volatile LONG64 progress_time_ = 0;
};
} // namespace nr::kmd
