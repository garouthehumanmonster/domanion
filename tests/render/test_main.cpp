#include "test_framework.hpp"
#include "core/clock.hpp"
#include "render/gpu_preference.hpp"

RON_EXPORT_GPU_PREFERENCE()

int main() {
    ron::clock_init();
    return ron::test::run_all_tests();
}
