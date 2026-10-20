#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
#include <utility>
namespace nr::indirect {
// WDF zero-initializes context storage. Keep this slot trivially destructible;
// teardown explicitly closes it before the framework destroys the context.
template<class T> struct ConsumerSlot {
    SRWLOCK lock = SRWLOCK_INIT;
    T *value = nullptr;
    uint64_t generation = 0;
    bool closed = false;
    struct Update { uint64_t generation; std::unique_ptr<T> previous; bool allowed; };
    Update begin(bool close = false) noexcept {
        AcquireSRWLockExclusive(&lock);
        T *previous = std::exchange(value, nullptr);
        const auto ticket = ++generation;
        closed = closed || close;
        const bool allowed = !closed;
        ReleaseSRWLockExclusive(&lock);
        // Destruction may wait for a worker and reenter IddCx. Ownership must
        // already be detached, and no slot lock may be held while destroying.
        return {ticket, std::unique_ptr<T>(previous), allowed};
    }
    bool publish(uint64_t ticket, std::unique_ptr<T> &candidate) noexcept {
        AcquireSRWLockExclusive(&lock);
        const bool accepted = !closed && ticket == generation && !value && bool(candidate);
        if (accepted) value = candidate.release();
        ReleaseSRWLockExclusive(&lock);
        return accepted;
    }
};
}
