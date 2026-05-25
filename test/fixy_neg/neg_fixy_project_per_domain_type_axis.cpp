// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-026 fixture, mismatch class A:
//   per-domain grant tag claiming a TYPE-VALUED substrate axis
//   (Effect) without a corresponding `project<G>` specialization.
//
// Before FOUND-026 the primary template `template <typename G>
// struct project;` was undefined; reaching it surfaced as an opaque
// "incomplete type project<...>" compile error that didn't tell the
// reader the tag itself was the problem.  After FOUND-026 the primary
// template is a structured static_assert that NAMES G in the
// compiler's instantiation context and enumerates the two repair
// paths (specialize project<G> or harden IsGrantTag_v).
//
// This fixture mints a `final` per-domain grant tag deriving
// `grant_base` with a `which_dim` specialization on DimensionAxis::Effect
// (a type-valued substrate axis — `using type = effects::Row<...>`
// is the shipped projection shape) but no `project<>` specialization.
// Instantiating `project<DomainEffectTag>::type` must red at the
// FOUND-026 static_assert, naming the tag.
//
// Expected diagnostic: `project<G> reached for a grant tag with no
// specialization` OR `FIXY-FOUND-026` OR `per-domain tag` (the
// message body language).

#include <crucible/fixy/Fn.h>

// Per-domain grant tag.  Final, derives grant_base — passes the
// structural IsGrantTag_v gate.
struct DomainEffectTag final : ::crucible::fixy::grant::grant_base {};

// which_dim specialization routes the tag to substrate axis Effect.
// This MUST live in the originating namespace (per Grant.h
// reopened-namespace discipline).
namespace crucible::fixy::grant {
template <>
struct which_dim<::DomainEffectTag> {
    static constexpr ::crucible::fixy::dim::DimensionAxis value =
        ::crucible::fixy::dim::DimensionAxis::Effect;
};
}  // namespace crucible::fixy::grant

// Probe the FOUND-026 diagnostic surface directly — bypass the
// IsAccepted full-engagement gate and reach project<> on the domain
// tag.  This MUST red at the new structured static_assert.
using ProbeFailure =
    typename ::crucible::fixy::detail::resolve::project<DomainEffectTag>::type;

int main() { return sizeof(ProbeFailure); }
