// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `category_of_v<UserTag>` where UserTag IS a
// valid diagnostic tag (inherits tag_base) but is NOT registered in
// the foundation Catalog.  The Category enum is CLOSED to the
// foundation's 22 wrapper-axis categories; user extensions
// participate in the type-level diagnostic surface
// (diagnostic_name_v, Diagnostic<UserTag, Ctx...>) but DO NOT occupy
// a Category slot.
//
// The routed `static_assert` in `detail::category_of_impl<UserTag>`
// fires with the message documenting the closed-catalog discipline.

#include <crucible/safety/Diagnostic.h>

namespace {

struct user_local_tag : ::crucible::safety::diag::tag_base {
    static constexpr std::string_view name        = "UserLocalTag";
    static constexpr std::string_view description = "user-defined";
    static constexpr std::string_view remediation = "see project docs";
};

}  // namespace

// Force instantiation: this should fire the "Tag is not registered
// in the foundation Catalog" routed assert.
constexpr auto bogus_cat = ::crucible::safety::diag::category_of_v<user_local_tag>;
auto const& g_ref = bogus_cat;

int main() { return 0; }
