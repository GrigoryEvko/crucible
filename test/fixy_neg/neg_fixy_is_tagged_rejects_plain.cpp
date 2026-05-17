// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-IS-Tagged fixture: fixy::is::IsTagged rejects an unrelated
// plain type when used as a `requires` clause gate.
//
// Violation: a function template `requires fixy::is::IsTagged<T>`
// rejects any T that is not `safety::Tagged<U, Source>` for some
// (U, Source).  Routing through the fixy::is alias must reject
// identically.
//
// Expected diagnostic: "constraints not satisfied" / "IsTagged".

#include <crucible/fixy/Is.h>

namespace fis = crucible::fixy::is;

namespace neg_fixy_is_tagged_rejects_plain {

struct TypeIsTaggedRejectsPlain {
    int x = 0;
};

template <typename T>
    requires fis::IsTagged<T>
[[nodiscard]] constexpr int gate(T const&) noexcept { return 1; }

}  // namespace neg_fixy_is_tagged_rejects_plain

int main() {
    namespace tags = neg_fixy_is_tagged_rejects_plain;

    tags::TypeIsTaggedRejectsPlain plain{};
    // Should FAIL: plain is not Tagged<U, Source>.
    [[maybe_unused]] auto bad = tags::gate(plain);
    return 0;
}
