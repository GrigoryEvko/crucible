// SPDX-License-Identifier: Apache-2.0
//
// test/test_fixy_cheat_probe.cpp
//
// FIXY-A7 — adversarial probe of `crucible::fixy::IsAccepted<>`
// soundness.  Mirrors the test_concept_cheat_probe.cpp pattern for
// the Graded substrate (per GAPS-089 / scripts/check-trait-injection
// CI discipline).  Each cheat is a hand-crafted attempt to make
// IsAccepted return `true` when the engagement check says it should
// not — if any cheat succeeds, the gate is unsound.
//
// Each cheat is one static_assert (the witness that the gate REJECTS
// the attempted bypass).  Build success of this TU means every cheat
// is correctly rejected.
//
// Probe inventory:
//   #1 Specialize EngagedFor concept — concepts are unspecializable
//   #2 Inheritance-base bypass — closed by FIXY-A-PLUS-1 (final)
//   #3 ADL hook on variable templates — vars are not ADL-discoverable
//   #4 Friend-class circumvention — concepts have no friends
//   #5 Reading `relaxes2` instead of `relaxes` — fold pinned to `relaxes`
//   #6 Hidden-friend grant injection — derived_from is structural
//   #7 CRTP base loop bypass — CRTP doesn't synthesize grant_base
//   #8 Alias-template erasure — aliases are transparent
//   #9 Const-qualified grant must still engage (CVR positive case)

#include <crucible/fixy/Reject.h>

#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace cf  = crucible::fixy;
namespace cd  = crucible::fixy::dim;
namespace cg  = crucible::fixy::grant;

namespace probe {

// ═══════════════════════════════════════════════════════════════════
// Cheat 1 — Specialize EngagedFor in user namespace
// ═══════════════════════════════════════════════════════════════════
//
// Threat model: a hostile downstream user attempts to declare
// `template <> concept crucible::fixy::EngagedFor<dim::Usage> = true;`
// to bypass the per-dim check on Usage.
//
// Mitigation: C++ concepts ARE NOT SPECIALIZABLE per [temp]/2.  Any
// attempt to specialize a concept is ill-formed at the declaration
// site.  We probe this via a static_assert that IsAccepted on an
// empty pack remains false — proving no concept-spec hack can
// override the conjunction.

static_assert(!cf::IsAccepted<>,
    "cheat 1: empty pack must remain rejected; concept can't be "
    "specialized to admit it");

// Additionally pin that EngagedFor returns false on an empty pack
// (the load-bearing fold-identity behavior).
static_assert(!cf::EngagedFor<cd::Usage>,
    "cheat 1: empty pack must not satisfy EngagedFor (fold identity)");

// ═══════════════════════════════════════════════════════════════════
// Cheat 2 — Inheritance-base bypass (CLOSED post FIXY-A-PLUS-1)
// ═══════════════════════════════════════════════════════════════════
//
// Threat: `struct fake : accept_default_strict_for<dim::Usage> {};`
// inherits `relaxes == dim::Usage` and forges engagement.
//
// Closure (FIXY-A-PLUS-1): every shipped grant tag is now `final`
// via safety/NotInherited.h.  Derivation hits the `final` keyword
// at the inheritance site, BEFORE IsAccepted ever sees the type.
// The "cheat" therefore CANNOT EVEN COMPILE; the witness is in
// test/fixy_neg/neg_fixy_inheritance_bypass_attempt.cpp.
//
// This probe pins the closure mechanism: NotInherited<T> witnesses
// every grant tag.  A regression that removes `final` fires this
// gate at build time.

static_assert(::crucible::safety::NotInherited<cf::accept_default_strict_for<cd::Usage>>,
    "cheat 2: accept_default_strict_for<D> must be `final` (closes "
    "inheritance-bypass cheat — FIXY-A-PLUS-1)");
static_assert(::crucible::safety::NotInherited<cg::copy>,
    "cheat 2: relaxation tags must be `final`");
static_assert(::crucible::safety::NotInherited<cg::with<::crucible::effects::Effect::IO>>,
    "cheat 2: variadic-template tags must be `final`");

// A genuine bypass attempt — forge a fixy-LIKE struct from scratch
// without inheriting grant_base.  IsGrantTag must reject.
struct ForgedNotDerivedFromBase {
    static constexpr cd::DimAxis relaxes = cd::Usage;
};
static_assert(!cf::IsGrantTag<ForgedNotDerivedFromBase>,
    "cheat 2 (forge variant): a struct with a `relaxes` field but "
    "not derived from grant_base must be rejected by IsGrantTag");

// ═══════════════════════════════════════════════════════════════════
// Cheat 3 — ADL hook on variable templates
// ═══════════════════════════════════════════════════════════════════
//
// Threat: user ships `engaged_for_v` in their namespace and hopes
// ADL redirects.
//
// Mitigation: (a) Reject.h does NOT export a `engaged_for_v` —
// engagement detection lives in the EngagedFor CONCEPT (which fires
// on fold-expression evaluation, NOT via ADL); (b) variable
// templates are not ADL-discoverable in any case.  We probe by
// declaring a local engaged_for_v in this TU's namespace and
// verifying it does not affect EngagedFor's evaluation.

template <cd::DimAxis, typename...>
inline constexpr bool engaged_for_v = true;  // hostile-user attempt

// Despite the user-side `engaged_for_v` being true, EngagedFor on
// an empty pack remains false — the concept doesn't route through
// the variable template.
static_assert(!cf::EngagedFor<cd::Usage>,
    "cheat 3: user-namespace engaged_for_v cannot hijack EngagedFor");

// ═══════════════════════════════════════════════════════════════════
// Cheat 4 — Friend-class circumvention
// ═══════════════════════════════════════════════════════════════════
//
// Threat: declare a friend class of IsAccepted to access private
// state and override the conjunction.
//
// Mitigation: IsAccepted IS A CONCEPT.  Concepts have no friend
// declarations, no member access, no private state.  The cheat is
// structurally inapplicable.  We probe by attempting to require a
// `friend_token` member that doesn't exist — `requires` returns
// false, the probe accepts the rejection.

template <typename T>
inline constexpr bool has_friend_token_v = requires { typename T::friend_token; };

struct ProbeFriend {};  // doesn't matter — concepts have no friends

static_assert(!has_friend_token_v<ProbeFriend>,
    "cheat 4: no friend_token on probe — concepts have no friends");

// Pin: IsAccepted's behavior is unchanged by introducing a struct
// "ProbeFriend" in this TU; the concept is structurally closed.
static_assert(!cf::IsAccepted<ProbeFriend>,
    "cheat 4: ProbeFriend does not satisfy IsAccepted (has no relaxes)");

// ═══════════════════════════════════════════════════════════════════
// Cheat 5 — Reading `relaxes2` instead of `relaxes`
// ═══════════════════════════════════════════════════════════════════
//
// Threat: an attacker defines a grant-LIKE tag with TWO relaxation
// members hoping the engagement check accidentally reads the wrong one
// and engages an unintended dim.
//
// Mitigation: TWO independent closures stack here.
//
// (a) FIXY-AUDIT-LEGITGRANT: EngagedFor requires `derived_from<
//     grant_base>` alongside the `relaxes` field.  A look-alike struct
//     that omits grant_base derivation engages NO dim, period.
//
// (b) The fold reads ONLY `relaxes`, never `relaxes2`.  Even if the
//     attacker DERIVES from grant_base, they can only engage the dim
//     named by `relaxes`; `relaxes2` is unreachable.
//
// Probe: a derived-from-grant_base struct with `relaxes == dim::Type`
// and `relaxes2 == dim::Usage` engages ONLY dim::Type (the canonical
// field), never dim::Usage.  And the prior look-alike form (no
// grant_base derivation) now engages nothing at all.

struct EvilTwoFieldGrant : cf::grant_base {  // legit derivation, hostile fields
    static constexpr cd::DimAxis relaxes  = cd::Type;
    static constexpr cd::DimAxis relaxes2 = cd::Usage;  // hostile field
};

static_assert(cf::EngagedFor<cd::Type, EvilTwoFieldGrant>,
    "cheat 5a: relaxes correctly read (legit grant_base derivation)");
static_assert(!cf::EngagedFor<cd::Usage, EvilTwoFieldGrant>,
    "cheat 5a: relaxes2 is NOT read — fold pinned to `relaxes`");

// Closure (b): non-derived look-alike engages NOTHING.
struct LookalikeTwoFieldNoDeriv {
    static constexpr cd::DimAxis relaxes  = cd::Type;
    static constexpr cd::DimAxis relaxes2 = cd::Usage;
};
static_assert(!cf::EngagedFor<cd::Type, LookalikeTwoFieldNoDeriv>,
    "cheat 5b: a look-alike without grant_base derivation engages no dim");
static_assert(!cf::EngagedFor<cd::Usage, LookalikeTwoFieldNoDeriv>,
    "cheat 5b: a look-alike without grant_base derivation engages no dim");

// ═══════════════════════════════════════════════════════════════════
// Cheat 6 — Hidden-friend grant injection (FIXY-AUDIT-HIDDEN-FRIEND)
// ═══════════════════════════════════════════════════════════════════
//
// Threat: an attacker hopes to inject grant_base derivation through a
// hidden-friend ADL hook on a non-grant type.
//
// Mitigation: derived_from is a structural check on inheritance, not
// ADL.  Hidden friends don't participate in `derived_from`.  We pin
// this with a struct that has hidden friends but doesn't inherit
// grant_base.

struct WithHiddenFriendButNoBase {
    static constexpr cd::DimAxis relaxes = cd::Usage;
    friend bool operator==(WithHiddenFriendButNoBase, WithHiddenFriendButNoBase)
        noexcept { return true; }  // hidden friend
};
static_assert(!cf::EngagedFor<cd::Usage, WithHiddenFriendButNoBase>,
    "cheat 6: hidden friends do not satisfy derived_from<grant_base>");

// ═══════════════════════════════════════════════════════════════════
// Cheat 7 — CRTP base loop bypass (FIXY-AUDIT-CRTP)
// ═══════════════════════════════════════════════════════════════════
//
// Threat: hostile user attempts a CRTP pattern to "synthesize" a
// grant_base derivation without actually inheriting from grant_base.
// E.g., `template <class Derived> struct crtp_grant { ... };`
//
// Mitigation: CRTP introduces no relationship to grant_base — the
// derived class still has to declare grant_base in its base list.

template <typename Derived>
struct CrtpGrantBase {
    static constexpr cd::DimAxis relaxes = cd::Usage;
};
struct CrtpDerivedNoGrantBase : CrtpGrantBase<CrtpDerivedNoGrantBase> {};

static_assert(!cf::EngagedFor<cd::Usage, CrtpDerivedNoGrantBase>,
    "cheat 7: CRTP base does not synthesize grant_base derivation");

// ═══════════════════════════════════════════════════════════════════
// Cheat 8 — Alias-template erasure (FIXY-AUDIT-ALIAS)
// ═══════════════════════════════════════════════════════════════════
//
// Threat: hide the non-grant nature by routing through alias templates.
//
// Mitigation: aliases are transparent; the underlying type still has
// to satisfy derived_from<grant_base>.  Type aliases for non-grant
// types still fold to false.

using AliasedNonGrant = LookalikeTwoFieldNoDeriv;
static_assert(!cf::EngagedFor<cd::Type, AliasedNonGrant>,
    "cheat 8: alias-template wrapper does not change derived_from check");

// ═══════════════════════════════════════════════════════════════════
// Cheat 9 — Const-qualified grant must still engage (CVR companion)
// ═══════════════════════════════════════════════════════════════════
//
// Threat (inverse): a cv-qualified legit grant should STILL engage —
// the cheat-bypass closure must not over-reject.
//
// Mitigation: detail::is_legit_grant uses std::remove_cvref_t, so
// const grant::copy / volatile grant::copy / const& grant::copy all
// engage as expected.

static_assert(cf::EngagedFor<cd::Usage, const cg::copy>,
    "cheat 9: const grant must still engage (cv-strip discipline)");
static_assert(cf::EngagedFor<cd::Usage, volatile cg::copy>,
    "cheat 9: volatile grant must still engage (cv-strip discipline)");
// References fold to false per FIXY-AUDIT-CVR (separate closure).
static_assert(!cf::EngagedFor<cd::Usage, cg::copy&>,
    "cheat 9: reference does not engage (CVR closure)");

}  // namespace probe

int main() {
    // Runtime self-check that the cheat-probe TU built and the
    // static_asserts above hold.  If any cheat had succeeded, the
    // build would have failed; reaching main means the gate is sound.
    //
    // Post FIXY-A-PLUS-1: cheat #2 (inheritance-base bypass) is
    // structurally CLOSED — every shipped grant tag is `final`, and
    // the bypass attempt is a compile error caught by
    // test/fixy_neg/neg_fixy_inheritance_bypass_attempt.cpp.  No
    // architectural-limit admissions remain in this probe.
    std::fprintf(stderr,
        "test_fixy_cheat_probe: 9 cheat attempts rejected at compile "
        "time (0 architectural-limit admissions; cheats 6-9 closed by "
        "FIXY-AUDIT-LEGITGRANT/HIDDEN-FRIEND/CRTP/ALIAS/CVR audit)\n");
    return 0;
}
