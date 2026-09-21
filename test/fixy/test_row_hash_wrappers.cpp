// The row hash is one fold in foundation/diag/RowHash.h, and this is the
// cell that proves the fold reaches the wrappers it claims to cover.
//
// Half the wrappers in fixy are aliases for Graded and half are classes
// that inherit graded_facade over it. The old row hash needed a partial
// specialisation per wrapper and got both halves by naming each type.
// The fold names none of them, so the property it owes this tree is that
// every wrapper still lands in a slot of its own. A wrapper that fell
// through would contribute zero and share one slot with every bare type,
// which is how a secret value and a plain one would come to share a
// cache entry.
//
// The coverage question extends to the one carrier that deliberately does
// not reach that fold. A multi-axis binding publishes a grade per axis
// rather than one lattice, so it carries its own fold in fixy/Fn.h, and
// it falls through to the same primary template if that fold goes
// missing. The last cells below are its share of the question.
//
// test/foundation/test_row_hash.cpp owns the algebra of the fold and the
// published wire format. This file owns only the coverage question, and
// it is here rather than there because foundation must not depend on
// fixy.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Bands.h>
#include <fixy/Collision.h>
#include <fixy/Corpus.h>
#include <fixy/Fn.h>
#include <fixy/Mutation.h>
#include <fixy/Qtt.h>
#include <fixy/Role.h>
#include <fixy/Secret.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/diag/RowHash.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace {

namespace fd = ::foundation::diag;

using fd::row_hash_contribution_v;

// One wrapper per shape the tree uses. Tagged, Secret, Qtt, Stale and
// Monotonic are classes over graded_facade. DetSafe and HotPath are
// aliases for Graded. Both halves must land somewhere other than zero.

using TaggedVerified = ::fixy::Tagged<int, ::fixy::tags::trust::Verified>;
using TaggedUnverified = ::fixy::Tagged<int, ::fixy::tags::trust::Unverified>;
using SecretInt = ::fixy::Secret<int>;
using LinearInt = ::fixy::Linear<int>;
using AffineInt = ::fixy::Affine<int>;
using StaleInt = ::fixy::Stale<int>;
using PureInt = ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>;
using HotInt = ::fixy::HotPath<::fixy::HotPathTier_v::Hot, int>;

// Nothing falls through. This is the assertion the old header needed
// fifty-two specialisations to be able to make.
static_assert(row_hash_contribution_v<TaggedVerified> != 0, "Tagged falls through the fold and shares a slot with "
                                                            "every bare type");
static_assert(row_hash_contribution_v<SecretInt> != 0, "Secret falls through the fold and shares a slot with every "
                                                       "bare type");
static_assert(row_hash_contribution_v<LinearInt> != 0, "Linear falls through the fold and shares a slot with every "
                                                       "bare type");
static_assert(row_hash_contribution_v<StaleInt> != 0, "Stale falls through the fold and shares a slot with every bare "
                                                      "type");
static_assert(row_hash_contribution_v<PureInt> != 0);
static_assert(row_hash_contribution_v<HotInt> != 0);

// Two tags of one axis separate, which is the discrimination a trust
// boundary depends on.
static_assert(row_hash_contribution_v<TaggedVerified> != row_hash_contribution_v<TaggedUnverified>);

// Two grades of the usage axis separate.
static_assert(row_hash_contribution_v<LinearInt> != row_hash_contribution_v<AffineInt>);

// Wrappers of different axes separate, across both shapes.
static_assert(row_hash_contribution_v<SecretInt> != row_hash_contribution_v<LinearInt>);
static_assert(row_hash_contribution_v<SecretInt> != row_hash_contribution_v<StaleInt>);
static_assert(row_hash_contribution_v<SecretInt> != row_hash_contribution_v<PureInt>);
static_assert(row_hash_contribution_v<PureInt> != row_hash_contribution_v<HotInt>);
static_assert(row_hash_contribution_v<TaggedVerified> != row_hash_contribution_v<SecretInt>);

// A stack is a fold, so the outer layer does not erase the inner one and
// the two orders are different keys.
static_assert(row_hash_contribution_v<::fixy::Secret<LinearInt>> != row_hash_contribution_v<SecretInt>);
static_assert(row_hash_contribution_v<::fixy::Secret<LinearInt>> != row_hash_contribution_v<LinearInt>);
static_assert(row_hash_contribution_v<::fixy::Secret<LinearInt>>
              != row_hash_contribution_v<::fixy::Linear<SecretInt>>);

// A class-shaped wrapper nests inside an alias-shaped one and stays
// visible, which is the property that makes the two shapes one fold.
static_assert(row_hash_contribution_v<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, SecretInt>>
              != row_hash_contribution_v<PureInt>);

// The payload is the content hash's business, not this one's.
static_assert(row_hash_contribution_v<::fixy::Secret<int>> == row_hash_contribution_v<::fixy::Secret<double>>);

// ---------------------------------------------------------------------
// The one carrier that does not reach the fold above, and must not.
//
// Every wrapper named so far publishes one lattice, one modality and one
// payload, which is what the graded fold reads. A binding publishes a
// grade per axis instead: it IS the resolver across the axes, so there is
// no single lattice for it to name. It carries its own fold in
// fixy/Fn.h, and the coverage question this file owns therefore extends
// to it — with no fold of its own a binding falls through to the primary
// template just as an unrecognised wrapper would, and every binding in
// the tree shares one slot with every bare type.
//
// The two roles below carry the same payload and differ on the Effect and
// Security axes. A pure copy is safe to share between installations; a
// binding that performs IO and emits publicly is not. One slot for both
// serves the first's kernel to the second's caller.

using PureCopyInt = ::fixy::role::PureCopy<int>;
using IoFunctionInt = ::fixy::role::IoFunction<int>;
using BgWorkerInt = ::fixy::role::BgWorker<int>;

static_assert(!::foundation::diag::GradedShaped<PureCopyInt>,
              "a binding must not be made to satisfy the graded shape; it has no single lattice");
static_assert(row_hash_contribution_v<PureCopyInt> != 0, "a binding falls through the fold and shares a slot with "
                                                         "every bare type");
static_assert(row_hash_contribution_v<PureCopyInt> != row_hash_contribution_v<IoFunctionInt>);
static_assert(row_hash_contribution_v<BgWorkerInt> != row_hash_contribution_v<IoFunctionInt>);

// A binding nests a row-bearing payload without erasing it, which is what
// makes the Type axis a recursion rather than a drop.
static_assert(row_hash_contribution_v<::fixy::fn<SecretInt>> != row_hash_contribution_v<::fixy::fn<int>>);
static_assert(row_hash_contribution_v<::fixy::fn<SecretInt>> != row_hash_contribution_v<SecretInt>);

// ---------------------------------------------------------------------
// One axis, two spellings, and which of them share a slot.
//
// An axis can hold an atom that names a level and a strict pole at that
// same level.  They are distinct types, so the walk in fixy/Fn.h folds
// two identities for one claim unless a canonicalisation maps them
// together.  It reads every grade through
// foundation/diag/lattice_canonical_id in order to make that possible.
//
// Both directions of error live here, and they are not symmetric.  Two
// spellings left apart cost a cache miss and a recompile.  Two spellings
// merged with nothing behind the merge serve a kernel compiled under one
// discipline to a caller under another.  So each cell below names the
// consumer that decides its case, and asserts what that consumer does
// rather than quoting it.

using StrictPoleInt = ::fixy::fn<int>;
using ClassifiedInt = ::fixy::fn<int, ::fixy::atom::as_classified>;
using SecretAtomInt = ::fixy::fn<int, ::fixy::atom::as_secret>;
using InternalInt = ::fixy::fn<int, ::fixy::atom::as_internal>;
using PublicInt = ::fixy::fn<int, ::fixy::atom::as_public>;
using UnclassifiedInt = ::fixy::fn<int, ::fixy::atom::as_unclassified>;
using UnverifiedInt = ::fixy::fn<int, ::fixy::atom::trust_unverified>;

// The Security axis merges, because every rule that reads it routes
// through one predicate and that predicate answers alike for the strict
// pole, as_classified and as_secret.  This is the establishment, asserted.
static_assert(
    ::fixy::corpus::detail::is_secret_carrier_<typename ::fixy::axis_traits<::fixy::Axis::Security>::strict>::value);
static_assert(::fixy::corpus::detail::is_secret_carrier_<::fixy::atom::as_classified>::value);
static_assert(::fixy::corpus::detail::is_secret_carrier_<::fixy::atom::as_secret>::value);

// The corpus therefore gives the strict pole and as_classified one
// verdict on every pack, and an IO row is the pack where the verdict
// bites.
static_assert(::fixy::IsAccepted<int>);
static_assert(::fixy::IsAccepted<int, ::fixy::atom::as_classified>);
static_assert(!::fixy::IsAccepted<int, ::fixy::atom::with_io>);
static_assert(!::fixy::IsAccepted<int, ::fixy::atom::as_classified, ::fixy::atom::with_io>);
static_assert(!::fixy::IsAccepted<int, ::fixy::atom::with_bg>);
static_assert(!::fixy::IsAccepted<int, ::fixy::atom::as_classified, ::fixy::atom::with_bg>);

// So the atom whose stated purpose is to write the default out reaches
// the default's slot.  fixy/Atom.h says it "names the strict pole
// explicitly", and before the canonicalisation it was the one spelling
// of the default that moved the key.
static_assert(row_hash_contribution_v<ClassifiedInt> == row_hash_contribution_v<StrictPoleInt>,
              "as_classified names the strict Security pole explicitly, so the two spellings must reach one "
              "cache slot");

// The merge is one pair and not the axis.  Every Security point that sits
// below the classified carrier keeps its own slot, because projecting a
// binding down to one of them is exactly what stops the corpus refusing
// an IO row.  A canonicalisation that swallowed these would serve a
// classified kernel to a public caller.
static_assert(row_hash_contribution_v<InternalInt> != row_hash_contribution_v<StrictPoleInt>);
static_assert(row_hash_contribution_v<PublicInt> != row_hash_contribution_v<StrictPoleInt>);
static_assert(row_hash_contribution_v<UnclassifiedInt> != row_hash_contribution_v<StrictPoleInt>);
static_assert(row_hash_contribution_v<InternalInt> != row_hash_contribution_v<PublicInt>);
static_assert(row_hash_contribution_v<PublicInt> != row_hash_contribution_v<UnclassifiedInt>);
static_assert(row_hash_contribution_v<InternalInt> != row_hash_contribution_v<UnclassifiedInt>);
static_assert(::fixy::IsAccepted<int, ::fixy::atom::as_public, ::fixy::atom::with_io>,
              "as_public is the projection that discharges the IO entry, so it is not the strict pole under "
              "another name");

// as_secret is the third spelling the predicate above accepts, and it is
// deliberately unmapped.  fixy/Atom.h gives it a residual claim the other
// two lack, that no declassification is permitted at all, which two
// points of Conf make coincide with the pole today and tier 4 makes
// unobservable.  No cell here pins its relation to the pole either way:
// asserting a split would block a later author who establishes that the
// residual claim is empty, and asserting a merge is the claim nothing in
// the tree supports.  It has a slot of its own against the points that
// are genuinely other claims, and that much is asserted.
static_assert(row_hash_contribution_v<SecretAtomInt> != row_hash_contribution_v<PublicInt>);
static_assert(row_hash_contribution_v<SecretAtomInt> != row_hash_contribution_v<InternalInt>);

// ---------------------------------------------------------------------
// The load-bearing cell: an axis that reads exactly like Security and
// decides the other way.
//
// atom::trust_unverified names the strict Trust pole, tags::trust::
// Unverified, the same way as_classified names the strict Security pole.
// The naming invites the same merge.  The code refuses it: rule T001 in
// fixy/Collision.h reads the Trust grade as is_same_v against the atom
// alone, so writing the atom out makes the rule fire and taking the
// default leaves it standing down.  Collision.h pins both halves of that
// asymmetry with its own self-tests, so the two spellings are separable
// by a consumer and are therefore two claims, whatever the names suggest.
//
// Whether that asymmetry is right is T001's question and not this fold's.
// Either way the two must not share a slot while it holds, and this cell
// is what stops a later author reading the Security merge as a pattern
// and applying it across the axis table.
static_assert(!::fixy::collision::live_rules<::fixy::atom::capability_usage, ::fixy::atom::trust_unverified>::T001_ok,
              "T001 must fire on a capability at the written-out unverified atom");
static_assert(::fixy::collision::live_rules<::fixy::atom::capability_usage>::T001_ok,
              "T001 must stand down on a capability at the strict Trust pole");
static_assert(
    std::is_same_v<typename ::fixy::axis_traits<::fixy::Axis::Trust>::strict, ::fixy::tags::trust::Unverified>,
    "the cell below is only about the pole while the pole is Unverified");
static_assert(!std::is_same_v<::fixy::atom::trust_unverified, ::fixy::tags::trust::Unverified>,
              "the atom and the pole must stay distinct types for this cell to have content");
static_assert(row_hash_contribution_v<UnverifiedInt> != row_hash_contribution_v<StrictPoleInt>,
              "T001 separates the written-out unverified atom from the strict Trust pole, so the two are two "
              "claims and must not be canonicalised together");

struct TestFailure {};
int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "\n    %s\n", what);
        throw TestFailure{};
    }
}

void test_every_wrapper_reaches_runtime() {
    std::uint64_t const secret = row_hash_contribution_v<SecretInt>;
    std::uint64_t const linear = row_hash_contribution_v<LinearInt>;
    std::uint64_t const tagged = row_hash_contribution_v<TaggedVerified>;
    check(secret != 0, "Secret contributes nothing at run time");
    check(linear != 0, "Linear contributes nothing at run time");
    check(tagged != 0, "Tagged contributes nothing at run time");
    check(secret != linear && secret != tagged && linear != tagged,
          "two wrappers of different axes share one slot at run time");
}

void test_every_binding_reaches_runtime() {
    std::uint64_t const pure_copy = row_hash_contribution_v<PureCopyInt>;
    std::uint64_t const io_function = row_hash_contribution_v<IoFunctionInt>;
    std::uint64_t const bg_worker = row_hash_contribution_v<BgWorkerInt>;
    check(pure_copy != 0, "a pure-copy binding contributes nothing at run time");
    check(io_function != 0, "an IO binding contributes nothing at run time");
    check(bg_worker != 0, "a background-worker binding contributes nothing at run time");
    check(pure_copy != io_function, "a pure-copy binding and an IO binding share one slot at run time");
    check(bg_worker != io_function, "two bindings declaring different effect rows share one slot at run time");
}

// The canonicalisation at run time, in both directions.  A grade folded
// through a consteval path alone can hide a mistake that the constant
// evaluator folds away, so the two answers are read back here as values.
void test_one_claim_reaches_one_slot() {
    std::uint64_t const strict_pole = row_hash_contribution_v<StrictPoleInt>;
    std::uint64_t const classified = row_hash_contribution_v<ClassifiedInt>;
    std::uint64_t const internal_tier = row_hash_contribution_v<InternalInt>;
    std::uint64_t const public_tier = row_hash_contribution_v<PublicInt>;
    check(strict_pole != 0, "the strict-pole binding contributes nothing at run time");
    check(classified == strict_pole, "as_classified names the strict Security pole, and the two spellings take "
                                     "two slots at run time");
    check(internal_tier != strict_pole, "the internal tier shares the classified carrier's slot at run time");
    check(public_tier != strict_pole, "the public tier shares the classified carrier's slot at run time");
    check(internal_tier != public_tier, "two Security points below the carrier share one slot at run time");
}

// The other direction, and the reason it is a separate test: a merge
// applied where no consumer treats the two spellings alike is a wrong
// hit, and a wrong hit is the one way this key must never fail.
void test_two_claims_keep_two_slots() {
    std::uint64_t const strict_pole = row_hash_contribution_v<StrictPoleInt>;
    std::uint64_t const unverified = row_hash_contribution_v<UnverifiedInt>;
    check(unverified != 0, "the unverified binding contributes nothing at run time");
    check(unverified != strict_pole, "T001 separates the written-out unverified atom from the strict Trust pole, "
                                     "and the two share one slot at run time");
}

// The roster is read out of the role namespace, so a role added there is
// covered here without an edit. A role at zero shares a slot with every
// bare payload in the tree.
void test_every_role_is_off_the_zero_slot() {
    check(::fixy::detail::role_self_test::roles_declared() > 0, "the reflected role roster is empty, so this check "
                                                                "proves nothing");
    check(::fixy::detail::role_self_test::roles_off_the_zero_slot() == ::fixy::detail::role_self_test::roles_declared(),
          "a role reaches the zero slot, or has an arity this check cannot instantiate");
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_row_hash_wrappers:\n");
    run_test("test_every_wrapper_reaches_runtime", test_every_wrapper_reaches_runtime);
    run_test("test_every_binding_reaches_runtime", test_every_binding_reaches_runtime);
    run_test("test_one_claim_reaches_one_slot", test_one_claim_reaches_one_slot);
    run_test("test_two_claims_keep_two_slots", test_two_claims_keep_two_slots);
    run_test("test_every_role_is_off_the_zero_slot", test_every_role_is_off_the_zero_slot);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
