// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-026 fixture, mismatch class B:
//   per-domain grant tag claiming an ENUM-VALUED substrate axis
//   (Security) without a corresponding `project<G>` specialization.
//
// Companion to neg_fixy_project_per_domain_type_axis.cpp, which
// exercises mismatch class A (type-valued substrate axes — `::type`
// resolution).  This fixture exercises the parallel failure mode on
// enum-valued substrate axes (Security uses `::value` + `::value_type`
// to surface a `safety::fn::SecLevel` enumerator).  Both shapes route
// through the same primary-template static_assert; the FOUND-026
// diagnostic must name the tag in either case.
//
// HS14 mandates ≥2 distinct mismatch-class fixtures per new mint /
// new diagnostic surface; this pair satisfies the floor by covering
// the two arms of `project<>`'s value-discriminator structure (type
// vs value).
//
// Expected diagnostic: `project<G> reached for a grant tag with no
// specialization` OR `FIXY-FOUND-026` OR `per-domain tag` (the
// message body language).

#include <crucible/fixy/Fn.h>

// Per-domain grant tag.  Final, derives grant_base — passes the
// structural IsGrantTag_v gate.
struct DomainSecurityTag final : ::crucible::fixy::grant::grant_base {};

// which_dim specialization routes the tag to substrate axis Security
// (enum-valued: SecLevel).  Living in the originating namespace per
// Grant.h reopened-namespace discipline.
// fixy-CR-09: known residual gap — this neg-compile fixture intentionally
// reopens crucible::fixy::grant to manufacture a structurally-valid but
// unprojected per-domain tag; the reopen IS the FOUND-026 test surface.
namespace crucible::fixy::grant {
template <>
struct which_dim<::DomainSecurityTag> {
    static constexpr ::crucible::fixy::dim::DimensionAxis value =
        ::crucible::fixy::dim::DimensionAxis::Security;
};
}  // namespace crucible::fixy::grant

// Probe the FOUND-026 diagnostic surface — Security-axis specs would
// supply `::value` + `::value_type`; with no specialization the
// primary template fires its structured static_assert naming the tag.
inline constexpr auto kProbeFailure =
    ::crucible::fixy::detail::resolve::project<DomainSecurityTag>::value;

int main() {
    static_cast<void>(kProbeFailure);
    return 0;
}
