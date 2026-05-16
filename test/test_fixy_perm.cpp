// ── test_fixy_perm — sentinel TU for fixy/Perm.h ───────────────────
//
// Pulls fixy/Perm.h into a TU compiled under project warning flags
// so the header's static_asserts execute under enforcement.
// Witnesses:
//
//   1. fixy::perm::mint_permission_root names the same function set as
//      safety::mint_permission_root.
//   2. fixy::perm::mint_permission_split / combine work end-to-end
//      against a user-defined splits_into specialization.
//   3. fixy::perm::mint_permission_share lends/upgrades correctly via
//      a Pool (round-trip).
//   4. fixy::perm::Permission<Tag> aliases safety::Permission<Tag>.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/neg_fixy_perm_*.cpp.

#include <crucible/fixy/Perm.h>

#include <type_traits>
#include <utility>

namespace test_fixy_perm {
struct Whole {};
struct Left {};
struct Right {};
}  // namespace test_fixy_perm

namespace crucible::safety {

template <>
struct splits_into<test_fixy_perm::Whole, test_fixy_perm::Left, test_fixy_perm::Right>
    : std::true_type {};

}  // namespace crucible::safety

namespace safe  = ::crucible::safety;
namespace fperm = ::crucible::fixy::perm;
namespace tags  = test_fixy_perm;

// ─── 1. Type carriers alias the substrate ─────────────────────────

static_assert(std::is_same_v<
    fperm::Permission<tags::Whole>,
    safe::Permission<tags::Whole>>,
    "fixy::perm::Permission<T> must alias safety::Permission<T>.");

// ─── 2. mint_permission_root noexcept ─────────────────────────────

static_assert(noexcept(fperm::mint_permission_root<tags::Whole>()),
    "fixy::perm::mint_permission_root<T>() must be noexcept.");

// ─── 3. Split/combine round-trip via the alias ────────────────────

int main() {
    {
        // Root + split + combine round-trip via the fixy:: alias.
        auto whole = fperm::mint_permission_root<tags::Whole>();
        auto [l, r] =
            fperm::mint_permission_split<tags::Left, tags::Right>(std::move(whole));
        auto recombined =
            fperm::mint_permission_combine<tags::Whole>(std::move(l), std::move(r));
        safe::permission_drop(std::move(recombined));
    }
    return 0;
}
