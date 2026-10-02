// The row hash is one fold in foundation/diag/RowHash.h, and this is the
// cell that proves the fold reaches the wrappers it claims to cover.
//
// Half the wrappers in fixy are aliases for Graded and half are classes
// that inherit graded_facade over it. The fold names none of them, so
// the property it owes this tree is that every wrapper lands in a slot
// of its own. A wrapper that fell
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
//
// The carrier census is in row_hash_census.h.  The fixture
// neg_row_hash_census_undisposed_carrier includes that header and not this
// file, so the fixture does not compile the cells below.

#include "row_hash_census.h"

// The build writes this header from the tree.  It defines
// CRUCIBLE_CENSUS_PUBLIC_HEADER_COUNT (test/fixy/CMakeLists.txt).
#include <census_public_headers.h>
#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Bands.h>
#include <fixy/Collision.h>
#include <fixy/Corpus.h>
#include <fixy/Fn.h>
#include <fixy/Mutation.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Reject.h>
#include <fixy/Role.h>
#include <fixy/Secret.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Protocol.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Capability.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/ReadView.h>

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

// Nothing falls through.
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
static_assert(row_hash_contribution_v<::fixy::Secret<LinearInt>> != row_hash_contribution_v<::fixy::Linear<SecretInt>>);

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

// The Security axis merges, because every corpus entry that reads it
// routes through one predicate, and that predicate answers alike on every
// channel for the strict pole, as_classified and as_secret.  This is the
// establishment, asserted.
template <class Grade>
inline constexpr bool classified_on_every_channel =
    ::fixy::corpus::detail::is_classified_on_<::fixy::corpus::DischargeAxis::IO, Grade>::value
    && ::fixy::corpus::detail::is_classified_on_<::fixy::corpus::DischargeAxis::Bg, Grade>::value
    && ::fixy::corpus::detail::is_classified_on_<::fixy::corpus::DischargeAxis::Staleness, Grade>::value;
static_assert(classified_on_every_channel<typename ::fixy::axis_traits<::fixy::Axis::Security>::strict>);
static_assert(classified_on_every_channel<::fixy::atom::as_classified>);
static_assert(classified_on_every_channel<::fixy::atom::as_secret>);

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
// explicitly".  Without the canonicalisation it would be the one
// spelling of the default that moves the key.
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
// The Trust axis merges one pair too, for the same reason.
//
// atom::trust_unverified names the strict Trust pole, tags::trust::
// Unverified, the same way as_classified names the strict Security pole.
// Rule T001 in fixy/Collision.h, the one rule that reads the Trust axis,
// reads the grade through the closed relation trust_class_of_, which
// gives the two spellings the class Unverified.  So T001 gives them one
// verdict on every pack, and this is the establishment, asserted.
static_assert(::fixy::collision::trust_class_of_v<::fixy::atom::trust_unverified>
              == ::fixy::collision::trust_class_of_v<typename ::fixy::axis_traits<::fixy::Axis::Trust>::strict>);
static_assert(!::fixy::collision::live_rules<::fixy::atom::capability_usage, ::fixy::atom::trust_unverified>::T001_ok,
              "T001 must fire on a capability at the written-out unverified atom");
static_assert(!::fixy::collision::live_rules<::fixy::atom::capability_usage>::T001_ok,
              "T001 must fire on a capability at the strict Trust pole");
static_assert(!std::is_same_v<::fixy::atom::trust_unverified, ::fixy::tags::trust::Unverified>,
              "the atom and the pole must stay distinct types for this cell to have content");
static_assert(row_hash_contribution_v<UnverifiedInt> == row_hash_contribution_v<StrictPoleInt>,
              "trust_unverified names the strict Trust pole, so the two spellings must reach one cache slot");

// The merge is one pair and not the axis.  A Trust grade that T001 reads
// as verified admits a capability that the pole refuses, so it keeps its
// own slot.  A merge there gives a caller under one verdict the kernel
// that the other verdict admitted.
using VerifiedInt = ::fixy::fn<int, ::fixy::atom::trust_verified>;
static_assert(::fixy::collision::live_rules<::fixy::atom::capability_usage, ::fixy::atom::trust_verified>::T001_ok);
static_assert(row_hash_contribution_v<VerifiedInt> != row_hash_contribution_v<StrictPoleInt>,
              "T001 separates trust_verified from the strict Trust pole, so the two are two claims");

}  // namespace

namespace census {

// ── One pair per family ──────────────────────────────────────────────
//
// The census proves each carrier is off the zero slot.  These prove that
// the claims that differ inside a family take different slots, which is
// what a fold that dropped the discriminating half would collapse.

using ::foundation::diag::row_hash_contribution_v;

// A sealed refinement cannot be mutated in place and an open one can.
// They grade on one lattice, so only the sealed identity separates them.
static_assert(row_hash_contribution_v<::fixy::Refined<::fixy::positive, int>>
                  != row_hash_contribution_v<::fixy::SealedRefined<::fixy::positive, int>>,
              "a sealed refinement and an open one over the same predicate share a cache slot");

// A token over an IO region is not a token over a pure one, and an
// exclusive token is not a shared one.
static_assert(row_hash_contribution_v<fp::Permission<PureRegionTag>>
              != row_hash_contribution_v<fp::Permission<IoRegionTag>>);
static_assert(row_hash_contribution_v<fp::Permission<PureRegionTag>>
              != row_hash_contribution_v<fp::SharedPermission<PureRegionTag>>);
static_assert(row_hash_contribution_v<fp::Permission<PureRegionTag>>
              != row_hash_contribution_v<fp::ReadView<PureRegionTag>>);

// A share folds its tag's row as its payload, as the exclusive token
// does.  Its published value type is the tag, a bare type that folds to
// zero, so the published shape alone gave shares over an IO region and
// over a pure region one slot.
static_assert(row_hash_contribution_v<fp::SharedPermission<PureRegionTag>>
                  != row_hash_contribution_v<fp::SharedPermission<IoRegionTag>>,
              "a share over an IO region and a share over a pure region take one slot");
static_assert(row_hash_contribution_v<fp::SharedPermission<IoRegionTag>>
                  == ::foundation::diag::graded_row_hash_v<fp::SharedPermission<IoRegionTag>::modality,
                                                           fp::SharedPermission<IoRegionTag>::lattice_type,
                                                           fe::Row<fe::Effect::IO>>,
              "a share must fold through the graded fold with its tag's row as the payload");

// A permission set is a set, so its spelling order does not move it, and
// its size does.
static_assert(row_hash_contribution_v<fp::PermSet<PureRegionTag, IoRegionTag>>
              == row_hash_contribution_v<fp::PermSet<IoRegionTag, PureRegionTag>>);
static_assert(row_hash_contribution_v<fp::PermSet<PureRegionTag>>
              != row_hash_contribution_v<fp::PermSet<PureRegionTag, IoRegionTag>>);
static_assert(row_hash_contribution_v<fp::PermSet<>> != 0);

// A session handle's grade is its protocol position.
static_assert(
    row_hash_contribution_v<::fixy::session::SessionHandle<::fixy::session::End, int>>
    != row_hash_contribution_v<::fixy::session::SessionHandle<::fixy::session::Send<int, ::fixy::session::End>, int>>);
static_assert(::foundation::diag::SteppingShaped<::fixy::session::SessionHandle<::fixy::session::End, int>>);
static_assert(::foundation::diag::PublishesGradedMember<::fixy::session::SessionHandleBase<::fixy::session::End>>
                  && !::foundation::diag::SteppingShaped<::fixy::session::SessionHandleBase<::fixy::session::End>>,
              "the handle base publishes part of the Stepping contract, which is exactly what the primary "
              "template's hard error exists to catch");

// A decorator makes a claim that the handle it wraps does not make.  A
// crash-watched handle can meet a crashed peer, a recorded handle writes
// each step to a log, and a checkpoint handle can roll back.  So each
// takes a slot apart from its inner handle, and a stack keeps the claim
// of each layer.
static_assert(row_hash_contribution_v<CrashWatchedHandle> != row_hash_contribution_v<PlainHandle>,
              "a crash-watched handle and the plain handle that it wraps take one slot");
static_assert(row_hash_contribution_v<RecordedHandle> != row_hash_contribution_v<PlainHandle>,
              "a recorded handle and the plain handle that it wraps take one slot");
static_assert(row_hash_contribution_v<CheckpointAtEnd> != row_hash_contribution_v<PlainHandle>,
              "a checkpoint handle and the plain handle that it wraps take one slot");
static_assert(row_hash_contribution_v<RecordedCrashWatchedHandle> != row_hash_contribution_v<RecordedHandle>,
              "a recorder over a crash-watched handle and a recorder over a plain handle take one slot");
static_assert(row_hash_contribution_v<RecordedCrashWatchedHandle> != row_hash_contribution_v<CrashWatchedHandle>);

// Contexts, execution contexts and capabilities each read the row they
// carry.
static_assert(row_hash_contribution_v<fe::Bg> != row_hash_contribution_v<fe::Init>);
static_assert(row_hash_contribution_v<fe::Bg> != row_hash_contribution_v<fe::Test>);
static_assert(row_hash_contribution_v<fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>>
              != row_hash_contribution_v<fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg>>>);
static_assert(row_hash_contribution_v<fe::Capability<fe::Effect::Alloc, fe::Bg>>
              != row_hash_contribution_v<fe::Capability<fe::Effect::IO, fe::Bg>>);

// The mutation carriers make different claims about the same payload.
static_assert(row_hash_contribution_v<::fixy::WriteOnce<int*>>
              != row_hash_contribution_v<::fixy::WriteOnceNonNull<int*>>);
static_assert(row_hash_contribution_v<::fixy::OrderedAppendOnly<int>>
              != row_hash_contribution_v<::fixy::AppendOnly<int>>);
static_assert(row_hash_contribution_v<::fixy::BoundedMonotonic<std::uint32_t, 1024U>>
              != row_hash_contribution_v<::fixy::Monotonic<std::uint32_t>>);
static_assert(row_hash_contribution_v<::fixy::AtomicMonotonic<std::uint64_t>>
              != row_hash_contribution_v<::fixy::Monotonic<std::uint64_t>>);

// A pipeline is not the stage it runs, and the two ends of a channel are
// two claims.
static_assert(row_hash_contribution_v<PipelineWitness> != row_hash_contribution_v<StageWitness>);
static_assert(row_hash_contribution_v<StageWitness> != row_hash_contribution_v<SwmrStageWitness>);
static_assert(row_hash_contribution_v<SpscWitness::ProducerHandle>
              != row_hash_contribution_v<SpscWitness::ConsumerHandle>);
static_assert(row_hash_contribution_v<MpscWitness::ProducerHandle>
              != row_hash_contribution_v<SpscWitness::ProducerHandle>);

}  // namespace census

namespace {

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
    std::uint64_t const unverified = row_hash_contribution_v<UnverifiedInt>;
    check(unverified == strict_pole, "trust_unverified names the strict Trust pole, and the two spellings take two "
                                     "slots at run time");
}

// The other direction, and the reason it is a separate test: a merge
// applied where no consumer treats the two spellings alike is a wrong
// hit, and a wrong hit is the one way this key must never fail.
void test_two_claims_keep_two_slots() {
    std::uint64_t const strict_pole = row_hash_contribution_v<StrictPoleInt>;
    std::uint64_t const verified = row_hash_contribution_v<VerifiedInt>;
    check(verified != 0, "the verified binding contributes nothing at run time");
    check(verified != strict_pole, "T001 separates trust_verified from the strict Trust pole, and the two share one "
                                   "slot at run time");
}

// The roster is read out of the role namespace, so a role added there is
// covered here without an edit. A role at zero shares a slot with every
// bare payload in the tree.
void test_every_role_is_off_the_zero_slot() {
    check(::fixy::detail::roles_declared() > 0, "the reflected role roster is empty, so this check proves nothing");
    check(::fixy::detail::roles_off_the_zero_slot() == ::fixy::detail::roles_declared(),
          "a role reaches the zero slot, or has an arity this check cannot instantiate");
}

// The census verdicts are constants, so the build already proved them.
// Reading them back as values is what shows the constants reach a
// running program, and it prints the roster size so a shrinking census
// is visible in the log rather than silent.
void test_every_carrier_is_off_the_zero_slot() {
    std::size_t const roster = census::kVerdict.roster;
    std::size_t const proven = census::kVerdict.proven;
    std::fprintf(stderr,
                 "(%zu carriers and stated zeros across %zu class-declaring namespaces, from %d public headers) ",
                 roster, census::kNamespaceVerdict.declaring, CRUCIBLE_CENSUS_PUBLIC_HEADER_COUNT);
    check(CRUCIBLE_CENSUS_PUBLIC_HEADER_COUNT > 0, "the census includes no public header, so it proves nothing");
    check(roster > 0, "the reflected carrier roster is empty, so the census proves nothing");
    check(proven == roster, "a carrier in a censused namespace is unproven at run time");
    check(census::kStandinVerdict.proven == 3 && census::kStandinVerdict.roster == 5,
          "the stand-in census no longer separates proven members from unproven ones");
    std::uint64_t const open_refined = row_hash_contribution_v<::fixy::Refined<::fixy::positive, int>>;
    std::uint64_t const sealed_refined = row_hash_contribution_v<::fixy::SealedRefined<::fixy::positive, int>>;
    check(open_refined != sealed_refined, "a sealed refinement and an open one share a slot at run time");
    std::uint64_t const handle = row_hash_contribution_v<::fixy::session::SessionHandle<::fixy::session::End, int>>;
    std::uint64_t const token = row_hash_contribution_v<::foundation::permissions::Permission<census::PureRegionTag>>;
    check(handle != 0 && token != 0, "a session handle or a permission token reaches the zero slot at run time");
    std::uint64_t const pure_share =
        row_hash_contribution_v<::foundation::permissions::SharedPermission<census::PureRegionTag>>;
    std::uint64_t const io_share =
        row_hash_contribution_v<::foundation::permissions::SharedPermission<census::IoRegionTag>>;
    check(pure_share != io_share, "a share over an IO region and a share over a pure region share a slot at run time");
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_row_hash_wrappers:\n");
    run_test("test_every_wrapper_reaches_runtime", test_every_wrapper_reaches_runtime);
    run_test("test_every_binding_reaches_runtime", test_every_binding_reaches_runtime);
    run_test("test_one_claim_reaches_one_slot", test_one_claim_reaches_one_slot);
    run_test("test_two_claims_keep_two_slots", test_two_claims_keep_two_slots);
    run_test("test_every_role_is_off_the_zero_slot", test_every_role_is_off_the_zero_slot);
    run_test("test_every_carrier_is_off_the_zero_slot", test_every_carrier_is_off_the_zero_slot);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
