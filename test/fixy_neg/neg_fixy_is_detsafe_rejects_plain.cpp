// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-IS-DetSafe fixture: fixy::is::IsDetSafe rejects an unrelated
// plain type when used as a `requires` clause gate.
//
// Violation: a function template `requires fixy::is::IsDetSafe<T>`
// rejects any T that is not `safety::DetSafe<Tier, U>` for some
// (Tier, U).  Routing through the fixy::is alias must reject
// identically.
//
// Expected diagnostic: "constraints not satisfied" / "IsDetSafe".

#include <crucible/fixy/Is.h>

namespace fis = crucible::fixy::is;

namespace neg_fixy_is_detsafe_rejects_plain {

struct TypeIsDetSafeRejectsPlain {
    int x = 0;
};

template <typename T>
    requires fis::IsDetSafe<T>
[[nodiscard]] constexpr int gate(T const&) noexcept { return 1; }

}  // namespace neg_fixy_is_detsafe_rejects_plain

int main() {
    namespace tags = neg_fixy_is_detsafe_rejects_plain;

    tags::TypeIsDetSafeRejectsPlain plain{};
    // Should FAIL: plain is not DetSafe<Tier, U>.
    [[maybe_unused]] auto bad = tags::gate(plain);
    return 0;
}
