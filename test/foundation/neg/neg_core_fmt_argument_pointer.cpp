// A pointer has no formatter in the Report family: its value is an address,
// which differs from run to run, and a pointer to char carries no count.
// So a report refuses a pointer argument at the call.

#include <foundation/core/Report.h>

int main() {
    int value = 3;
    ::foundation::core::report(::foundation::core::Sink::Out, "the value is at {}\n", &value);
}
