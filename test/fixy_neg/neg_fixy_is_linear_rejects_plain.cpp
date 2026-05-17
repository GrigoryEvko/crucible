// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-IS-Linear fixture #1: fixy::is::IsLinear rejects an
// unrelated plain type when used as a `requires` clause gate.
//
// Violation: a function template `requires fixy::is::IsLinear<T>`
// rejects any T that is not `safety::Linear<U>` for some U.
// Routing through the fixy::is alias must reject identically.
//
// Expected diagnostic: "constraints not satisfied" / "IsLinear".

#include <crucible/fixy/Is.h>

namespace fis = crucible::fixy::is;

namespace neg_fixy_is_linear_rejects_plain {

// Unique per-fixture carrier.  PlainStruct is NOT safety::Linear<U>.
struct TypeIsLinearRejectsPlain {
    int x = 0;
};

template <typename T>
    requires fis::IsLinear<T>
[[nodiscard]] constexpr int gate(T const&) noexcept { return 1; }

}  // namespace neg_fixy_is_linear_rejects_plain

int main() {
    namespace tags = neg_fixy_is_linear_rejects_plain;

    tags::TypeIsLinearRejectsPlain plain{};
    // Should FAIL: plain is not a Linear<U>.
    [[maybe_unused]] auto bad = tags::gate(plain);
    return 0;
}
