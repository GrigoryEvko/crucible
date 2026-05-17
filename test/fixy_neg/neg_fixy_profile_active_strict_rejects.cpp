// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-A4 fixture: `fixy::IsAcceptedActive` under STRICT mode
// rejects a partially-specified Grants pack identically to
// `fixy::IsAccepted`.
//
// Profile.h ships the toggle infrastructure: `IsAcceptedActive`
// aliases `IsAccepted` (strict, default) or `IsAcceptedSketch`
// (always-true, opt-out).  The default preset compiles with
// `CRUCIBLE_FIXY_STRICT=1` (see top-level CMakeLists.txt §367), so
// `IsAcceptedActive` in this TU resolves to the strict gate.
//
// Violation: a binding with NO engaged dims must reject under strict.
// Routing through `fixy::IsAcceptedActive` must reject identically to
// `fixy::IsAccepted` to keep the toggle behaviorally faithful when
// production wrappers eventually rewire (deferred — see Profile.h
// §"Scope note (B2 ship)").
//
// Expected diagnostic: static assertion failure / IsAcceptedActive
// constraints not satisfied / FixyNotEngaged_Type.

#include <crucible/fixy/Profile.h>

namespace fixy = crucible::fixy;

namespace neg_fixy_profile_active_strict_rejects {

struct TypeNoEngagement {
    int x = 0;
};

template <typename Type, typename... Grants>
    requires fixy::IsAcceptedActive<Type, Grants...>
[[nodiscard]] constexpr int gate(Type const&) noexcept { return 1; }

}  // namespace neg_fixy_profile_active_strict_rejects

int main() {
    namespace tags = neg_fixy_profile_active_strict_rejects;

    // Witness: assert the toggle is strict in this TU (preset default).
    static_assert(fixy::fixy_is_strict,
        "Default preset must compile with CRUCIBLE_FIXY_STRICT=1.");

    // Should FAIL under STRICT: empty grants pack engages no dim, so
    // IsAccepted rejects → IsAcceptedActive rejects.
    tags::TypeNoEngagement plain{};
    [[maybe_unused]] auto bad = tags::gate(plain);
    return 0;
}
