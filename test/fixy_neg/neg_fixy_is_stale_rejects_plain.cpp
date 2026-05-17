// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-IS-Stale fixture: fixy::is::IsStale rejects an unrelated
// plain type when used as a `requires` clause gate.
//
// Violation: a function template `requires fixy::is::IsStale<T>`
// rejects any T that is not `safety::Stale<U>` for some U.
// Routing through the fixy::is alias must reject identically.
//
// Expected diagnostic: "constraints not satisfied" / "IsStale".

#include <crucible/fixy/Is.h>

namespace fis = crucible::fixy::is;

namespace neg_fixy_is_stale_rejects_plain {

struct TypeIsStaleRejectsPlain {
    int x = 0;
};

template <typename T>
    requires fis::IsStale<T>
[[nodiscard]] constexpr int gate(T const&) noexcept { return 1; }

}  // namespace neg_fixy_is_stale_rejects_plain

int main() {
    namespace tags = neg_fixy_is_stale_rejects_plain;

    tags::TypeIsStaleRejectsPlain plain{};
    // Should FAIL: plain is not Stale<U>.
    [[maybe_unused]] auto bad = tags::gate(plain);
    return 0;
}
