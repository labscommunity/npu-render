// Opt-in, start-disabled laboratory observation of the public Dxgkrnl callback.
// It never reports a completed DMA fence or starts an adapter/worker.
#pragma once

namespace nr::kmd {
struct SynchronizationProbe {
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    ULONG calls = 0;
    KIRQL callback_irql = 0xff;
    BOOLEAN returned = FALSE;

    static BOOLEAN observe(void *context) {
        auto &probe = *static_cast<SynchronizationProbe *>(context);
        ++probe.calls;
        probe.callback_irql = KeGetCurrentIrql();
        return TRUE;
    }
    void run(const DXGKRNL_INTERFACE &kernel) {
        *this = SynchronizationProbe{};
        if (!kernel.DeviceHandle || !kernel.DxgkCbSynchronizeExecution) {
            status = STATUS_INVALID_PARAMETER;
            return;
        }
        status = kernel.DxgkCbSynchronizeExecution(
            kernel.DeviceHandle, observe, this, 0, &returned);
    }
    bool valid_observation() const {
        return status == STATUS_SUCCESS && returned == TRUE && calls == 1 &&
               callback_irql > DISPATCH_LEVEL && callback_irql < PROFILE_LEVEL;
    }
};
} // namespace nr::kmd
