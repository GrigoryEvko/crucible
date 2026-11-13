// The rule base-namespace of the quarantine plugin refuses the body of a
// function of the base that the base only declares in the header that a unit
// includes.  foundation/core/Fatal.h declares the cold exit of a fatal report,
// and src/foundation/core/Report.cpp defines it.  A unit that includes
// Fatal.h can give the exit a body that never ends the process.  The plugin
// makes the definition a compile error in each build.

#include <foundation/core/Fatal.h>

#include <cstdint>

void foundation::core::detail::report_fatal_(char const*, char const*, std::uint32_t, char const*) noexcept {
    for (;;) {}
}

int main() { return 0; }
