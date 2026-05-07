// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for effects::ResourceTag (GAPS-189 #1214).
//
// Premise: ResourceTag<T> demands T::kind name a valid ResourceKind
// atom — checked by the inline `requires IsResourceKind<T::kind>`
// sub-expression in the concept body.  A type that exposes the
// canonical triple (kind / value / name) but with `kind` cast to a
// numeric value OUTSIDE the 0..22 atom range fails IsResourceKind.
//
// Why this is the load-bearing soundness gate:
//
// Without rejection, a future contributor could ship a "fake"
// resource tag — e.g. a Tier-2 wrapper that mimics the resource
// interface but returns a bogus kind value via `static_cast<
// ResourceKind>(0xFF)` — and have it accepted as a ResourceTag.
// That fake would then propagate through GAPS-190 row union
// (silently allocating no budget summation slot) and GAPS-191
// FitsCog (silently passing every Cog capacity check because the
// budget summation found nothing to sum).  The Effect catalog has
// the same FOUND-I04 frozen-value discipline; a fake atom in either
// catalog is a wire-format-breaking forgery.
//
// This fixture witnesses that IsResourceKind catches the membership
// violation, not just the structural-shape violation tested by the
// companion fixture.
//
// Companion fixture: neg_resources_concept_rejects_bare_scalar.cpp
//   * That one tests rejection by ABSENT triple — `int` has no kind
//     / value / name at all (substitution failure on member lookup).
//   * This one tests rejection by BOGUS kind value — the canonical
//     shape is present but the kind value escapes the IsResourceKind
//     0..22 range (concept-body sub-expression failure).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: "constraint not satisfied" /
// "constraints not satisfied" / "IsResourceKind" /
// "ResourceTag" pointing at the static_assert call site.

#include <crucible/effects/Resources.h>

#include <cstdint>
#include <string_view>

namespace eff = crucible::effects;

// ── Forgery: a tag with the right shape but a bogus kind value ──────
//
// 0xFF is well outside the 0..22 atom range.  Casting via static_cast
// produces a valid ResourceKind expression — the cast itself is
// well-formed because enum class admits any underlying-type value —
// but the IsResourceKind concept gate then rejects, since 0xFF doesn't
// satisfy any of the 23 disjuncts in IsResourceKind's body.
struct BogusResourceTag {
    static constexpr eff::ResourceKind kind  =
        static_cast<eff::ResourceKind>(static_cast<std::uint8_t>(0xFF));
    static constexpr std::uint64_t     value = 42;
    static constexpr std::string_view  name  = "BogusResourceTag";
};

// ResourceTag<BogusResourceTag> must NOT be satisfied — even though
// the canonical triple is present, the kind value escapes the atom
// catalog and IsResourceKind rejects.  The static_assert fires.
static_assert(eff::ResourceTag<BogusResourceTag>,
    "ResourceTag concept must reject types whose `kind` falls outside "
    "the IsResourceKind atom range — GAPS-189 forgery defense "
    "compromised.");

int main() { return 0; }
