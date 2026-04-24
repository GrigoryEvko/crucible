// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `Secret<T>::transform(f)` invoked with an `f` whose
// return type is a reference.  Per #151 the static_assert fires
// `[Capture_Leak_Reference_Return]` because a reference return
// aliases either the moved-from secret storage or a member of f's
// closure — both are silent declassifications bypassing the
// declassify<Policy> audit trail.

#include <crucible/safety/Secret.h>

#include <cstdint>

using crucible::safety::Secret;

int main() {
    // Shared state `leak` is not classified.  If the transform
    // below compiled, `rebound` would be `Secret<std::uint64_t&>`
    // holding a reference to `leak` — the caller can then mutate
    // `leak` to observe or substitute the classified payload
    // without ever calling declassify<Policy>.
    std::uint64_t leak = 0;

    Secret<std::uint64_t> s{0xDEADBEEFCAFEBABEULL};

    auto rebound = std::move(s).transform(
        [&leak](std::uint64_t v) -> std::uint64_t& {
            leak = v;
            return leak;  // REFERENCE RETURN — captured-reference leak.
        });

    (void)rebound;
    return 0;
}
