#include <cstdlib>
#include <cstdio>
#include <windows.h>
extern "C" void *nr_host_allocate(size_t bytes) { return std::malloc(bytes); }
extern "C" void nr_host_free(void *p) { std::free(p); }
extern "C" void nr_host_probe(void *data, size_t bytes, unsigned long alignment) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto address = reinterpret_cast<size_t>(data);
    const auto limit = reinterpret_cast<size_t>(info.lpMaximumApplicationAddress);
    if (alignment != 1 || !bytes || address > limit || bytes - 1 > limit - address)
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    // The actual guarded copy tests inaccessible user pages separately.
}
extern "C" int nr_kernel_host_test();
int main() {
    const int failed_line = nr_kernel_host_test();
    if (failed_line) { std::printf("DDI host test failed at line %d\n", failed_line); return 1; }
    std::puts("DDI host validation: start gate, owned segment model, deferred paging model, failure unwind, bounds, duplicate destruction pass");
    return 0;
}
