// ── test_fixy_call_with_perm — FIXY-G4 positive test ──────────────────
//
// Pin call_with_perm + PermissionMatchesLifetime + R013 trait for:
//   * Binding with lifetime_region<TestArenaTag> + Mutation::Mutable;
//     invocation with Permission<TestArenaTag> succeeds.
//   * R013_requires_permission_v fires `true` for the above binding.
//   * Strict-default Lifetime::Static binding accepts ANY tag perm.

#include <crucible/fixy/Fixy.h>

#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;
namespace cs = crucible::safety;

namespace {

struct TestArenaTag {};

inline constexpr TestArenaTag kArena{};

using WriterPtr = void(*)(int);

void writer_impl(int) noexcept {}

using MutableArenaFn = cf::fn<WriterPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cg::lifetime_region<kArena>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cg::mutable_in_place,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

using StrictStaticFn = cf::fn<WriterPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// Compile-time invariants.
static_assert(cf::R013_requires_permission_v<MutableArenaFn>);
static_assert(!cf::R013_requires_permission_v<StrictStaticFn>);

// PermissionMatchesLifetime gate accepts only the right tag.
static_assert(cf::PermissionMatchesLifetime<MutableArenaFn, TestArenaTag>);
static_assert(!cf::PermissionMatchesLifetime<MutableArenaFn, struct OtherTag>);

// Strict-default Static lifetime accepts any tag (review hint: the
// binding didn't claim a region; perm is purely informative).
static_assert(cf::PermissionMatchesLifetime<StrictStaticFn, TestArenaTag>);
static_assert(cf::PermissionMatchesLifetime<StrictStaticFn, struct AnyTag>);

}  // namespace

int main() {
    MutableArenaFn bound{&writer_impl};

    // Mint a Permission<TestArenaTag> via the substrate's factory.
    auto perm = cs::mint_permission_root<TestArenaTag>();
    cf::call_with_perm<MutableArenaFn>(bound, std::move(perm), 42);

    // Strict-default lifetime accepts ANY permission tag.
    StrictStaticFn s_bound{&writer_impl};
    auto p2 = cs::mint_permission_root<TestArenaTag>();
    cf::call_with_perm<StrictStaticFn>(s_bound, std::move(p2), 7);

    std::fputs("test_fixy_call_with_perm: OK\n", stdout);
    return 0;
}
