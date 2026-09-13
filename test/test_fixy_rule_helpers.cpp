// Every helper below is exercised twice, once on a pack it must accept
// and once on a pack it must reject, so both branches are witnessed.

#include <crucible/fixy/Rules.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fixy = crucible::fixy;
namespace sfn = crucible::safety::fn;

// A linear binding inside a region counts as unprotected by default, and
// a per-function rule then refuses it.  Each function below is opted out
// through a trait specialization further down, which is what lets these
// packs reach the pack-level helpers at all.
//
// The functions differ from each other in exactly one template argument.
// The comment beside that argument is what separates them.

using LinearA = sfn::Fn<
    int, sfn::pred::True, sfn::UsageMode::Linear, crucible::effects::Row<>, sfn::SecLevel::Classified, sfn::proto::None,
    sfn::lifetime::In<7>,  // region tag 7
    crucible::safety::source::FromInternal, crucible::safety::trust::Verified, sfn::ReprKind::Opaque,
    sfn::cost::Unstated, sfn::precision::Exact, sfn::space::Zero, sfn::OverflowMode::Trap, sfn::MutationMode::Immutable,
    sfn::ReentrancyMode::NonReentrant, sfn::size_pol::Unstated, 1u, sfn::stale::Fresh>;

using LinearB = sfn::Fn<
    int, sfn::pred::True, sfn::UsageMode::Linear, crucible::effects::Row<>, sfn::SecLevel::Classified, sfn::proto::None,
    sfn::lifetime::In<11>,  // a different region tag
    crucible::safety::source::FromInternal, crucible::safety::trust::Verified, sfn::ReprKind::Opaque,
    sfn::cost::Unstated, sfn::precision::Exact, sfn::space::Zero, sfn::OverflowMode::Trap, sfn::MutationMode::Immutable,
    sfn::ReentrancyMode::NonReentrant, sfn::size_pol::Unstated, 1u, sfn::stale::Fresh>;

// Shares region 7 with LinearA, so the pair of them aliases.
using LinearAClone = sfn::Fn<
    long,  // a different type, the same region
    sfn::pred::True, sfn::UsageMode::Linear, crucible::effects::Row<>, sfn::SecLevel::Classified, sfn::proto::None,
    sfn::lifetime::In<7>,  // the same region tag as LinearA
    crucible::safety::source::FromInternal, crucible::safety::trust::Verified, sfn::ReprKind::Opaque,
    sfn::cost::Unstated, sfn::precision::Exact, sfn::space::Zero, sfn::OverflowMode::Trap, sfn::MutationMode::Immutable,
    sfn::ReentrancyMode::NonReentrant, sfn::size_pol::Unstated, 1u, sfn::stale::Fresh>;

// Sits at a different security level from LinearA, so the pair of them
// disagrees on that axis.
using PublicFn = sfn::Fn<int, sfn::pred::True, sfn::UsageMode::Linear, crucible::effects::Row<>,
                         sfn::SecLevel::Public,  // LinearA is classified
                         sfn::proto::None, sfn::lifetime::In<13>, crucible::safety::source::FromInternal,
                         crucible::safety::trust::Verified, sfn::ReprKind::Opaque, sfn::cost::Unstated,
                         sfn::precision::Exact, sfn::space::Zero, sfn::OverflowMode::Trap, sfn::MutationMode::Immutable,
                         sfn::ReentrancyMode::NonReentrant, sfn::size_pol::Unstated, 1u, sfn::stale::Fresh>;

namespace crucible::safety::fn::collision {
template <>
struct marks_lifetime_region_unprotected<::LinearA> : std::false_type {};
template <>
struct marks_lifetime_region_unprotected<::LinearB> : std::false_type {};
template <>
struct marks_lifetime_region_unprotected<::LinearAClone> : std::false_type {};
template <>
struct marks_lifetime_region_unprotected<::PublicFn> : std::false_type {};
}  // namespace crucible::safety::fn::collision

static_assert(fixy::rule::pack::no_linear_region_alias_v<>, "Empty pack must accept — no aliasing possible.");

static_assert(fixy::rule::pack::no_linear_region_alias_v<LinearA>,
              "Single-binding pack must accept — no aliasing possible.");

static_assert(fixy::rule::pack::no_linear_region_alias_v<LinearA, LinearB>,
              "Two Linears in DIFFERENT regions must accept.");

static_assert(!fixy::rule::pack::no_linear_region_alias_v<LinearA, LinearAClone>,
              "Two Linears in the SAME region (tag 7) must reject — alias.");

static_assert(fixy::rule::pack::frame_axis_consistent_v<>, "Empty pack must accept — vacuously consistent.");

static_assert(fixy::rule::pack::frame_axis_consistent_v<LinearA>, "Single-binding pack is consistent with itself.");

static_assert(fixy::rule::pack::frame_axis_consistent_v<LinearA, LinearB>,
              "LinearA + LinearB agree on Security (both Classified).");

static_assert(!fixy::rule::pack::frame_axis_consistent_v<LinearA, PublicFn>,
              "LinearA (Classified) + PublicFn (Public) disagree on Security.");

static_assert(fixy::rule::pack::is_linear_in_region_v<LinearA>,
              "LinearA is Linear in lifetime::In<7> — is_linear_in_region must accept.");

using TagA = fixy::rule::pack::region_tag_of_t<sfn::lifetime::In<7>>;
using TagAClone = fixy::rule::pack::region_tag_of_t<sfn::lifetime::In<7>>;
using TagB = fixy::rule::pack::region_tag_of_t<sfn::lifetime::In<11>>;

static_assert(fixy::rule::pack::same_region_tag_v<TagA, TagAClone>, "Identical region tags must compare equal.");

static_assert(!fixy::rule::pack::same_region_tag_v<TagA, TagB>, "Distinct region tags must compare unequal.");

static_assert(std::is_void_v<fixy::rule::pack::region_tag_of_t<sfn::lifetime::Static>>,
              "region_tag_of_t<lifetime::Static> must be void (non-region sentinel).");

static_assert(!std::is_void_v<fixy::rule::pack::region_tag_of_t<sfn::lifetime::In<7>>>,
              "region_tag_of_t<lifetime::In<7>> must be non-void.");

int main() { return 0; }
