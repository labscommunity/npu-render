#include <cstdlib>
#include <cstdio>
extern "C" void *nr_host_allocate(size_t bytes) { return std::malloc(bytes); }
extern "C" void nr_host_free(void *p) { std::free(p); }
extern "C" int nr_kernel_host_test();
int main() {
    const int failed_line = nr_kernel_host_test();
    if (failed_line) { std::printf("DDI host test failed at line %d\n", failed_line); return 1; }
    std::puts("DDI host validation: start gate, owned segment model, deferred paging model, failure unwind, bounds, duplicate destruction pass");
    return 0;
}
