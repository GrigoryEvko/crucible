// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-IS-Refined fixture: fixy::is::IsRefined rejects an unrelated
// plain type when used as a `requires` clause gate.
//
// Violation: a function template `requires fixy::is::IsRefined<T>`
// rejects any T that is not `safety::Refined<Pred, U>` for some
// (Pred, U).  Routing through the fixy::is alias must reject
// identically.
//
// Expected diagnostic: "constraints not satisfied" / "IsRefined".

#include <crucible/fixy/Is.h>

namespace fis = crucible::fixy::is;

namespace neg_fixy_is_refined_rejects_plain {

struct TypeIsRefinedRejectsPlain {
    int x = 0;
};

template <typename T>
    requires fis::IsRefined<T>
[[nodiscard]] constexpr int gate(T const&) noexcept { return 1; }

}  // namespace neg_fixy_is_refined_rejects_plain

int main() {
    namespace tags = neg_fixy_is_refined_rejects_plain;

    tags::TypeIsRefinedRejectsPlain plain{};
    // Should FAIL: plain is not Refined<Pred, U>.
    [[maybe_unused]] auto bad = tags::gate(plain);
    return 0;
}
