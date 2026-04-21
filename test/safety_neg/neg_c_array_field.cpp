// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: storing ScopedViews in a C array field inside a struct.
// Crucible uses C arrays heavily (e.g., SchemaEntry entries[CAP],
// CKernelEntry entries[CAP], DyingInfo d_info[MAX_PER_OP]); the
// audit must recurse into fixed-size C arrays via sv_unwrap_single<T[N]>.

#include <crucible/safety/ScopedView.h>

struct Carrier { int v = 0; };
struct Tag     {};

constexpr bool view_ok(Carrier const&, std::type_identity<Tag>) noexcept {
    return true;
}

using View = crucible::safety::ScopedView<Carrier, Tag>;

struct Bad {
    View views[4];  // C array of views in a struct field — forbidden
};

static_assert(crucible::safety::no_scoped_view_field_check<Bad>());

int main() { return 0; }
