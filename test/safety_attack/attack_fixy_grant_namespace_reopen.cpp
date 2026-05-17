// POSITIVE-ATTACK REGRESSION TEST.  This file MUST COMPILE TODAY and
// MUST FAIL TO COMPILE when the residual fixy-CR-09 gap closes
// (closed-world enumeration of grant tags via reflection,
// namespace-private specialization access control, or any other
// mechanism that prevents foreign translation units from registering
// `which_dim<T>` specializations).
//
// ── fixy-CR-09 — IsGrantTag defense is comment-only — user-side ───
//                which_dim specialization slips into the namespace
//
// Threat model: C++ has no namespace-scoped specialization access
// control.  Any translation unit can write:
//
//   namespace crucible::fixy::grant {
//     template <>
//     struct which_dim<::attacker::foreign>
//         : std::integral_constant<dim::DimensionAxis,
//                                  dim::DimensionAxis::Usage> {};
//   }
//
// The doc-block on `crucible::fixy::grant::IsGrantTag_v` historically
// implied this namespace was a closed authoring boundary; it is not.
// The structural defense is not the namespace — it is the
// `IsGrantTag_v<T>` gate (`is_base_of_v<grant_base, T> && is_final_v<T>`)
// which fires BEFORE `which_dim_v<T>` is consulted in
// `crucible::fixy::engaged_for` / `count_engagements_for`.
//
// What this fixture proves (two attack patterns, both succeed
// today — compile cleanly; the residual gap is real):
//
//   Pattern A — foreign-type which_dim injection: the attacker
//               opens `namespace crucible::fixy::grant` and
//               registers a which_dim specialization for a non-
//               grant type that does NOT inherit grant_base.  The
//               specialization compiles; `which_dim_v<foreign>` is
//               accessible and reports the engineered axis.  The
//               IsGrantTag gate downstream is the load-bearing
//               defense, NOT the namespace.
//
//   Pattern B — wrapper-type which_dim injection: the attacker
//               builds a `final` struct that DOES inherit
//               grant_base (so it IS a structural grant tag), then
//               specializes which_dim for it under an axis the
//               attacker did not intend to engage.  The wrapper
//               passes IsGrantTag; the engagement-axis routing
//               obeys the attacker-supplied specialization.  This
//               is documented architectural behavior — novel
//               user-defined grant tags engage at the resolver
//               layer — but the namespace reopen mechanism is the
//               same.
//
// fixy-CR-09: known residual gap — this fixture INTENTIONALLY
// reopens `namespace crucible::fixy::grant` to lock the attack
// surface into CI regression coverage.  Per the CR-05 attack
// pattern, the script
// `scripts/check-fixy-grant-namespace-purity.sh` excepts this file
// (matching `test/safety_attack/attack_fixy_grant_*.cpp`) ONLY when
// the explicit acknowledgement comment is present.  Remove this
// comment and the CI guard reds — that is the discipline.
//
// WHEN THIS TEST REDDENS (i.e., the build itself fails because the
// reopen + specialization no longer compile):
//   1. Confirm fixy-M-29-equivalent (closed-world enum via
//      reflection, namespace-private specialization access, or
//      friend-only `which_dim` registration) has landed.
//   2. Rewrite THIS fixture as a NEGATIVE regression
//      (test/fixy_neg/neg_fixy_grant_namespace_reopen.cpp): the
//      reopen must produce a documented diagnostic naming the
//      closure mechanism.
//   3. Update the "Defense-surface honesty (fixy-CR-09)" block in
//      `include/crucible/fixy/Grant.h` to remove the residual-gap
//      caveat — the namespace IS now closed-world.
//   4. Update the discipline in
//      `scripts/check-fixy-grant-namespace-purity.sh` — the
//      attack-fixture exception is no longer required.

#include <crucible/fixy/Fn.h>

#include <type_traits>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

// ─── Pattern A — foreign-type which_dim injection ──────────────────

namespace attack_fixy_grant_namespace_reopen::pattern_a {
    // Non-grant — does NOT inherit grant_base, NOT final.
    struct foreign {};
}

// fixy-CR-09: known residual gap — reopen the closed-world namespace
// from a foreign translation unit and register a which_dim
// specialization for a type that is not a grant.  This compiles
// today; the IsGrantTag gate is what defends.
namespace crucible::fixy::grant {
    template <>
    struct which_dim<::attack_fixy_grant_namespace_reopen::pattern_a::foreign>
        : std::integral_constant<dim::DimensionAxis,
                                 dim::DimensionAxis::Usage> {};
}

namespace attack_fixy_grant_namespace_reopen::pattern_a {
    // The injected specialization compiled — the namespace is open.
    static_assert(gr::which_dim_v<foreign> == D::Usage,
                  "Pattern A setup: which_dim specialization was "
                  "successfully injected from a foreign TU.  This "
                  "compiles today because C++ has no namespace-scoped "
                  "specialization access control.");

    // BUT the structural gate rejects foreign types regardless of
    // which_dim's contents — that is the load-bearing defense.
    static_assert(!gr::IsGrantTag<foreign>,
                  "Pattern A defense: `foreign` does NOT inherit "
                  "grant_base and is NOT final.  IsGrantTag rejects "
                  "it BEFORE which_dim is consulted in any pack "
                  "evaluation, so the injected specialization can "
                  "never reach IsAcceptedGrants.");
}

// ─── Pattern B — wrapper-type which_dim injection ──────────────────

namespace attack_fixy_grant_namespace_reopen::pattern_b {
    // `final` + inherits grant_base — IS a structural grant tag.
    struct wrapper final : gr::grant_base {};
}

// fixy-CR-09: known residual gap — register an attacker-chosen
// engagement axis for a user-defined wrapper that DOES satisfy
// IsGrantTag.  This is the same namespace reopen mechanism as
// Pattern A; the only difference is the type passes the structural
// gate.  Documented as expected behavior at the resolver layer —
// novel user grant tags route per their which_dim specialization —
// but the reopen mechanism is identical and reviewer- / CI-grep-
// dependent.
namespace crucible::fixy::grant {
    template <>
    struct which_dim<::attack_fixy_grant_namespace_reopen::pattern_b::wrapper>
        : std::integral_constant<dim::DimensionAxis,
                                 dim::DimensionAxis::Security> {};
}

namespace attack_fixy_grant_namespace_reopen::pattern_b {
    static_assert(gr::IsGrantTag<wrapper>,
                  "Pattern B setup: the wrapper IS a structural grant "
                  "tag (final + inherits grant_base).");
    static_assert(gr::which_dim_v<wrapper> == D::Security,
                  "Pattern B setup: the attacker-supplied "
                  "engagement axis routes via the injected "
                  "which_dim specialization.  The namespace reopen "
                  "is the same mechanism as Pattern A; only the "
                  "structural gate distinguishes them.");
}

// ─── The fixture must COMPILE today.  When it red's, see header. ──
int main() { return 0; }
