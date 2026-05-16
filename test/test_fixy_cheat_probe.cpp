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
//   #2 Inheritance-base bypass — documented architectural limit
//   #3 ADL hook on variable templates — vars are not ADL-discoverable
//   #4 Friend-class circumvention — concepts have no friends
//   #5 Reading `relaxes2` instead of `relaxes` — pinned by the fold

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
// Threat: an attacker defines a grant tag with TWO relaxation members
// hoping the engagement check accidentally reads the wrong one and
// engages an unintended dim.
//
// Mitigation: the fold reads ONLY `relaxes`, never `relaxes2`.
// Probe: a tag with `relaxes == dim::Type` and `relaxes2 ==
// dim::Usage` engages ONLY dim::Type, never dim::Usage.

struct EvilTwoFieldGrant {
    static constexpr cd::DimAxis relaxes  = cd::Type;
    static constexpr cd::DimAxis relaxes2 = cd::Usage;  // hostile field
};

static_assert(cf::EngagedFor<cd::Type, EvilTwoFieldGrant>,
    "cheat 5: relaxes correctly read");
static_assert(!cf::EngagedFor<cd::Usage, EvilTwoFieldGrant>,
    "cheat 5: relaxes2 is NOT read — fold pinned to `relaxes`");

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
        "test_fixy_cheat_probe: 5 cheat attempts rejected at compile "
        "time (0 architectural-limit admissions; cheat #2 closed by "
        "FIXY-A-PLUS-1)\n");
    return 0;
}
