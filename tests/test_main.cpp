#include "TestHarness.h"

int main() {
    int run = 0;
    for (const auto& c : ::test::Registry()) {
        run++;
        std::printf("[ RUN ] %s\n", c.name);
        c.fn();
    }
    int failures = ::test::FailureCount();
    std::printf("\n%d test(s) run, %d failure(s)\n", run, failures);
    return failures == 0 ? 0 : 1;
}
