// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-IS-HotPath fixture: fixy::is::IsHotPath rejects an unrelated
// plain type when used as a `requires` clause gate.
//
// Violation: a function template `requires fixy::is::IsHotPath<T>`
// rejects any T that is not `safety::HotPath<Tier, U>` for some
// (Tier, U).  Routing through the fixy::is alias must reject
// identically.
//
// Expected diagnostic: "constraints not satisfied" / "IsHotPath".

#include <crucible/fixy/Is.h>

namespace fis = crucible::fixy::is;

namespace neg_fixy_is_hotpath_rejects_plain {

struct TypeIsHotPathRejectsPlain {
    int x = 0;
};

template <typename T>
    requires fis::IsHotPath<T>
[[nodiscard]] constexpr int gate(T const&) noexcept { return 1; }

}  // namespace neg_fixy_is_hotpath_rejects_plain

int main() {
    namespace tags = neg_fixy_is_hotpath_rejects_plain;

    tags::TypeIsHotPathRejectsPlain plain{};
    // Should FAIL: plain is not HotPath<Tier, U>.
    [[maybe_unused]] auto bad = tags::gate(plain);
    return 0;
}
