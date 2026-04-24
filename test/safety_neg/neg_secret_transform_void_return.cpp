// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `Secret<T>::transform(f)` invoked with an `f` whose
// return type is void.  Per #151 the static_assert fires
// `[Capture_Leak_Void_Return]` — a void return produces
// `Secret<void>`, which is meaningless.  The likely intent is a
// side-effecting observation on the classified payload; that
// belongs in declassify<Policy>() where an audit trail survives
// review, not in transform().

#include <crucible/safety/Secret.h>

#include <cstdint>
#include <cstdio>

using crucible::safety::Secret;

int main() {
    Secret<std::uint64_t> s{0xDEADBEEFCAFEBABEULL};

    // Side-effect-only transform: logs the classified value and
    // returns nothing.  The return type deduces to void, which
    // would yield `Secret<void>` — a meaningless type and a
    // silent declassification sink (the data has already flowed
    // to stderr before the compiler notices).
    auto sink = std::move(s).transform(
        [](std::uint64_t v) -> void {
            std::fprintf(stderr, "%llu\n",
                         static_cast<unsigned long long>(v));
        });

    (void)sink;
    return 0;
}
