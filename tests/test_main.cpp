#include "test_framework.hpp"
#include "core/clock.hpp"

int main() {
    ron::clock_init();
    return ron::test::run_all_tests();
}
