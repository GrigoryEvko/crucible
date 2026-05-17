// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-IS-Secret fixture #2: fixy::is::IsSecret rejects an unrelated
// plain type when used as a `requires` clause gate.
//
// Violation: a function template `requires fixy::is::IsSecret<T>`
// rejects any T that is not `safety::Secret<U>` for some U.
// Routing through the fixy::is alias must reject identically.
//
// Expected diagnostic: "constraints not satisfied" / "IsSecret".

#include <crucible/fixy/Is.h>

namespace fis = crucible::fixy::is;

namespace neg_fixy_is_secret_rejects_plain {

struct TypeIsSecretRejectsPlain {
    int x = 0;
};

template <typename T>
    requires fis::IsSecret<T>
[[nodiscard]] constexpr int gate(T const&) noexcept { return 1; }

}  // namespace neg_fixy_is_secret_rejects_plain

int main() {
    namespace tags = neg_fixy_is_secret_rejects_plain;

    tags::TypeIsSecretRejectsPlain plain{};
    // Should FAIL: plain is not a Secret<U>.
    [[maybe_unused]] auto bad = tags::gate(plain);
    return 0;
}
