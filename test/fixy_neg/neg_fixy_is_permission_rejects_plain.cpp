// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-IS-Permission fixture: fixy::is::IsPermission rejects an
// unrelated plain type when used as a `requires` clause gate.
//
// Violation: a function template `requires fixy::is::IsPermission<T>`
// rejects any T that is not `safety::Permission<Tag>` for some Tag.
// Routing through the fixy::is alias must reject identically.
//
// Expected diagnostic: "constraints not satisfied" / "IsPermission".

#include <crucible/fixy/Is.h>

namespace fis = crucible::fixy::is;

namespace neg_fixy_is_permission_rejects_plain {

struct TypeIsPermissionRejectsPlain {
    int x = 0;
};

template <typename T>
    requires fis::IsPermission<T>
[[nodiscard]] constexpr int gate(T const&) noexcept { return 1; }

}  // namespace neg_fixy_is_permission_rejects_plain

int main() {
    namespace tags = neg_fixy_is_permission_rejects_plain;

    tags::TypeIsPermissionRejectsPlain plain{};
    // Should FAIL: plain is not Permission<Tag>.
    [[maybe_unused]] auto bad = tags::gate(plain);
    return 0;
}
