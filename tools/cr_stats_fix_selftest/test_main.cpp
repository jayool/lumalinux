#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
extern "C" int TryHandle_test(uint32_t appId);
int main() {
    int init = -1;
    if (auto fn = reinterpret_cast<int (*)()>(dlsym(RTLD_DEFAULT, "cr_stats_fix_test_init"))) init = fn();
    std::printf("init=%d app1=%d app2=%d app3=%d app4=%d\n", init,
                TryHandle_test(1), TryHandle_test(2), TryHandle_test(3), TryHandle_test(4));
    // second round: the once-per-app log must not repeat, results must be stable
    std::printf("again app1=%d app2=%d\n", TryHandle_test(1), TryHandle_test(2));
    return 0;
}
