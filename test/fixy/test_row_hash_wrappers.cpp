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

#include <fixy/Aliases.h>
#include <fixy/Atom.h>
#include <fixy/atoms/Barrier.h>
#include <fixy/atoms/Ctrl.h>
#include <fixy/atoms/Dispatch.h>
#include <fixy/atoms/Fp.h>
#include <fixy/atoms/Global.h>
#include <fixy/atoms/Hw.h>
#include <fixy/atoms/Observe.h>
#include <fixy/atoms/Os.h>
#include <fixy/atoms/Regime.h>
#include <fixy/atoms/Scope.h>
#include <fixy/atoms/Simd.h>
#include <fixy/atoms/Stack.h>
#include <fixy/atoms/Stdio.h>
#include <fixy/atoms/Sync.h>
#include <fixy/Axis.h>
#include <fixy/Bands.h>
#include <fixy/Bits.h>
#include <fixy/Borrowed.h>
#include <fixy/Checked.h>
#include <fixy/Collision.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/MpscRing.h>
#include <fixy/concurrent/PayloadRow.h>
#include <fixy/concurrent/PermissionedMpscChannel.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/RingValue.h>
#include <fixy/concurrent/SpscRing.h>
#include <fixy/concurrent/Stage.h>
#include <fixy/concurrent/StageShape.h>
#include <fixy/concurrent/Topology.h>
#include <fixy/concurrent/WorkingSet.h>
#include <fixy/ConstantTime.h>
#include <fixy/Corpus.h>
#include <fixy/Ctx.h>
#include <fixy/Cyclic.h>
#include <fixy/FixedArray.h>
#include <fixy/Fn.h>
#include <fixy/fp/Canonicalize.h>
#include <fixy/fp/Polynomial.h>
#include <fixy/GradedFacade.h>
#include <fixy/handle/Once.h>
#include <fixy/handle/PublishOnce.h>
#include <fixy/Insights.h>
#include <fixy/Machine.h>
#include <fixy/Mutation.h>
#include <fixy/os/CipherDurable.h>
#include <fixy/os/ClockSource.h>
#include <fixy/os/CpuPinned.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Io.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/Sched.h>
#include <fixy/os/SchedClass.h>
#include <fixy/os/Spawn.h>
#include <fixy/os/SpinLock.h>
#include <fixy/os/ThreadName.h>
#include <fixy/os/Time.h>
#include <fixy/OwnedFile.h>
#include <fixy/OwnedMmap.h>
#include <fixy/OwnedRegion.h>
#include <fixy/Path.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Reject.h>
#include <fixy/Role.h>
#include <fixy/Saturate.h>
#include <fixy/Saturated.h>
#include <fixy/ScopedView.h>
#include <fixy/Secret.h>
#include <fixy/session/Handle.h>
#include <fixy/session/MachineBridge.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Stepping.h>
#include <fixy/session/VigilMode.h>
#include <fixy/SharedRegion.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/Throws.h>
#include <fixy/Witnessed.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/algebra/lattices/AllocClassLattice.h>
#include <foundation/algebra/lattices/BarrierStrengthLattice.h>
#include <foundation/algebra/lattices/BoolLattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/algebra/lattices/CipherTierLattice.h>
#include <foundation/algebra/lattices/ClockSourceLattice.h>
#include <foundation/algebra/lattices/ConfLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/EnumValuePins.h>
#include <foundation/algebra/lattices/FractionalLattice.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/algebra/lattices/LifetimeLattice.h>
#include <foundation/algebra/lattices/MemoryScopeLattice.h>
#include <foundation/algebra/lattices/MonotoneLattice.h>
#include <foundation/algebra/lattices/PinningRequirementLattice.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/algebra/lattices/RecipeFamilyLattice.h>
#include <foundation/algebra/lattices/SchedulerPolicyLattice.h>
#include <foundation/algebra/lattices/SeqPrefixLattice.h>
#include <foundation/algebra/lattices/StalenessSemiring.h>
#include <foundation/algebra/lattices/SuspendBehaviorLattice.h>
#include <foundation/algebra/lattices/ToleranceLattice.h>
#include <foundation/algebra/lattices/TrustLattice.h>
#include <foundation/algebra/lattices/VendorLattice.h>
#include <foundation/algebra/lattices/WaitLattice.h>
#include <foundation/algebra/Modality.h>
#include <foundation/Brand.h>
#include <foundation/contracts/Armed.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/DecideOracle.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/diag/Catalog.h>
#include <foundation/diag/FailClosed.h>
#include <foundation/diag/Insights.h>
#include <foundation/diag/JsonEmitter.h>
#include <foundation/diag/RowHash.h>
#include <foundation/diag/RowMismatch.h>
#include <foundation/diag/Runtime.h>
#include <foundation/effects/Capability.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Fwd.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/ReadView.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/reflect/Enumerate.h>
#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/Hash.h>
#include <foundation/reflect/Instance.h>
#include <foundation/reflect/RawEscape.h>
#include <foundation/reflect/Signature.h>
#include <foundation/Saturate.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <meta>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

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

// ---------------------------------------------------------------------
// Every carrier in the tree is off the zero slot, or says why it is not.
//
// Zero is what the primary template answers for a type that carries no
// row, and it is also the value the live kernel-cache lookup passes as
// its row.  So a carrier that folds to zero does not merely share a slot
// with another carrier.  It shares the most-used slot in the cache with
// every bare payload in the tree, and the probe that finds it returns on
// row equality with no second check.  Before this census, some sixty
// carriers sat there: session handles, permissions, stages, pipelines,
// contexts, capabilities, and the mutation carriers beside the two that
// happened to be graded.
//
// The roster is reflected out of the namespaces that hold carriers, so a
// carrier added to one of them is in the census the moment it is
// declared.  Every roster member needs exactly one disposition: a
// witness instance whose row hash is not zero, or a stated reason why no
// row belongs to it.  There is no third outcome.  A member with no
// disposition, with two, or with a witness that folds to zero counts as
// unproven, and the proven count then falls short of the roster.  An
// entry that names nothing in the roster is stale, and a stale table is
// how this census would rot, so it reddens too.
//
// The namespace list is held to the same rule one level up.  Every
// namespace under fixy and foundation that declares a class is either
// censused or named as vocabulary with a reason.  A new namespace is
// therefore a red build until someone decides which it is.
//
// The census sees what this translation unit includes, which is every
// public header of both layers.  A header left out of that list is the
// one gap, and the include block above is where to close it.

namespace census {

namespace fa = ::foundation::algebra;
namespace fe = ::foundation::effects;
namespace fp = ::foundation::permissions;

struct CarrierWitness {
    std::meta::info entity;
    std::meta::info witness;
};

struct StatedZero {
    std::meta::info entity;
    std::string_view reason;
};

struct Verdict {
    std::size_t roster = 0;
    std::size_t proven = 0;
    std::size_t stale = 0;
};

[[nodiscard]] consteval bool is_roster_member(std::meta::info member) {
    if (std::meta::is_class_template(member)) return true;
    return std::meta::is_type(member) && !std::meta::is_type_alias(member) && std::meta::is_class_type(member)
           && std::meta::has_identifier(member);
}

// A witness must be an instance of the member it stands for, so that a
// table row cannot prove one carrier with the hash of another.
[[nodiscard]] consteval bool witness_stands_for(std::meta::info witness, std::meta::info entity) {
    std::meta::info const dealiased = std::meta::dealias(witness);
    if (std::meta::is_class_template(entity)) {
        return std::meta::has_template_arguments(dealiased) && std::meta::template_of(dealiased) == entity;
    }
    return dealiased == entity;
}

// The row hash of each witness, in table order.  It is the one step that
// needs a splice, so it is the one step that is an expansion.
template <auto const& Carriers>
[[nodiscard]] consteval auto witness_hashes() {
    std::array<std::uint64_t, std::size(Carriers)> hashes{};
    std::size_t index = 0;
    template for (constexpr CarrierWitness row : Carriers) {
        hashes[index++] = ::foundation::diag::row_hash_contribution_v<typename[:row.witness:]>;
    }
    return hashes;
}

template <auto const& Namespaces, auto const& Carriers, auto const& Zeros>
[[nodiscard]] consteval Verdict run() {
    Verdict verdict{};
    auto const hashes = witness_hashes<Carriers>();
    std::vector<std::meta::info> roster;
    for (std::meta::info ns : Namespaces) {
        for (std::meta::info member : std::meta::members_of(ns, std::meta::access_context::current())) {
            if (is_roster_member(member)) roster.push_back(member);
        }
    }
    verdict.roster = roster.size();
    for (std::meta::info member : roster) {
        std::size_t dispositions = 0;
        bool witnessed = false;
        for (std::size_t i = 0; i < std::size(Carriers); ++i) {
            if (Carriers[i].entity != member) continue;
            ++dispositions;
            witnessed = hashes[i] != 0 && witness_stands_for(Carriers[i].witness, member);
        }
        bool reasoned = false;
        for (StatedZero const& zero : Zeros) {
            if (zero.entity != member) continue;
            ++dispositions;
            reasoned = !zero.reason.empty();
        }
        if (dispositions == 1 && (witnessed || reasoned)) ++verdict.proven;
    }
    auto const in_roster = [&](std::meta::info entity) {
        for (std::meta::info member : roster)
            if (member == entity) return true;
        return false;
    };
    for (CarrierWitness const& row : Carriers)
        if (!in_roster(row.entity)) ++verdict.stale;
    for (StatedZero const& zero : Zeros)
        if (!in_roster(zero.entity)) ++verdict.stale;
    return verdict;
}

// ── The stand-in roster ──────────────────────────────────────────────
//
// The census is only worth its green if it can go red.  This roster is
// small enough to know the answer for: two carriers that fold, one whose
// witness folds to zero, one with no disposition at all, and one table
// row that names something outside the roster.  The verdict must count
// exactly two proven members and one stale row.

namespace standin {

struct GradedProbe {
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;
    using lattice_type = fa::lattices::DetSafeLattice::At<fa::lattices::DetSafeTier::Pure>;
    using value_type = int;
};

struct discipline_identity;

struct DisciplineProbe {
    using row_discipline = discipline_identity;
    using row_payload = int;
};

template <typename T>
struct SilentCarrier {};

struct Undisposed {};

}  // namespace standin

inline constexpr std::meta::info kStandinNamespaces[] = {^^standin};

inline constexpr CarrierWitness kStandinCarriers[] = {
    {^^standin::GradedProbe, ^^standin::GradedProbe},
    {^^standin::DisciplineProbe, ^^standin::DisciplineProbe},
    {^^standin::SilentCarrier, ^^standin::SilentCarrier<int>},
    {^^::fixy::Bits, ^^int},
};

inline constexpr StatedZero kStandinZeros[] = {
    {^^standin::discipline_identity, "an identity, declared and never defined"},
};

inline constexpr Verdict kStandinVerdict = run<kStandinNamespaces, kStandinCarriers, kStandinZeros>();

static_assert(kStandinVerdict.roster == 5, "the stand-in roster did not reflect the five classes it declares");
static_assert(kStandinVerdict.proven == 3,
              "the census proved a member it must not.  Of five stand-in members, the graded probe, the "
              "discipline probe and the reasoned identity are proven; a carrier whose witness folds to zero "
              "and a carrier with no disposition must both stay unproven");
static_assert(kStandinVerdict.stale == 1, "the census did not count the one table row that names a type outside "
                                          "the stand-in roster");

// ── The carrier namespaces ───────────────────────────────────────────

inline constexpr std::meta::info kCensusNamespaces[] = {
    ^^::foundation,
    ^^::foundation::algebra,
    ^^::foundation::permissions,
    ^^::foundation::effects,
    ^^::fixy,
    ^^::fixy::session,
    ^^::fixy::session::vigil_mode,
    ^^::fixy::concurrent,
    ^^::fixy::handle,
    ^^::fixy::io,
    ^^::fixy::fs,
    ^^::fixy::mmap,
    ^^::fixy::fp,
    ^^::fixy::sched,
    ^^::fixy::spin,
    ^^::fixy::time,
    ^^::fixy::witness,
    ^^::fixy::cipher::durable,
};

// Namespaces that declare classes and hold no carrier.  Each holds the
// vocabulary a grade is spelled in, or the machinery that reads grades,
// and none of their types is a value that crosses a kernel signature.
struct StatedVocabulary {
    std::meta::info ns;
    std::string_view reason;
};

inline constexpr std::string_view kGradeVocabulary =
    "grade vocabulary: the atoms, poles, tags and lattice points a grade is spelled in, never a value";
inline constexpr std::string_view kMachinery =
    "machinery that computes over grades at compile time; nothing here is a value in a signature";

inline constexpr StatedVocabulary kVocabularyNamespaces[] = {
    {^^::fixy::tags::source, kGradeVocabulary},
    {^^::fixy::tags::trust, kGradeVocabulary},
    {^^::fixy::tags::access, kGradeVocabulary},
    {^^::fixy::tags::version, kGradeVocabulary},
    {^^::fixy::tags::vessel_trust, kGradeVocabulary},
    {^^::fixy::tags::secret_policy, kGradeVocabulary},
    {^^::fixy::tags::hash_family, kGradeVocabulary},
    {^^::fixy::pole, kGradeVocabulary},
    {^^::fixy::pole::pred, kGradeVocabulary},
    {^^::fixy::pole::proto, kGradeVocabulary},
    {^^::fixy::pole::lifetime, kGradeVocabulary},
    {^^::fixy::pole::cost, kGradeVocabulary},
    {^^::fixy::pole::precision, kGradeVocabulary},
    {^^::fixy::pole::space, kGradeVocabulary},
    {^^::fixy::pole::size_pol, kGradeVocabulary},
    {^^::fixy::pole::stale, kGradeVocabulary},
    {^^::fixy::atom, kGradeVocabulary},
    {^^::fixy::atom::barrier, kGradeVocabulary},
    {^^::fixy::atom::ctrl, kGradeVocabulary},
    {^^::fixy::atom::dispatch, kGradeVocabulary},
    {^^::fixy::atom::fp, kGradeVocabulary},
    {^^::fixy::atom::global, kGradeVocabulary},
    {^^::fixy::atom::hw, kGradeVocabulary},
    {^^::fixy::atom::observe, kGradeVocabulary},
    {^^::fixy::atom::io, kGradeVocabulary},
    {^^::fixy::atom::fs, kGradeVocabulary},
    {^^::fixy::atom::mmap, kGradeVocabulary},
    {^^::fixy::atom::leak, kGradeVocabulary},
    {^^::fixy::atom::regime, kGradeVocabulary},
    {^^::fixy::atom::scope, kGradeVocabulary},
    {^^::fixy::atom::simd, kGradeVocabulary},
    {^^::fixy::atom::stack, kGradeVocabulary},
    {^^::fixy::atom::stdio, kGradeVocabulary},
    {^^::fixy::atom::stdio::streams, kGradeVocabulary},
    {^^::fixy::atom::sync, kGradeVocabulary},
    {^^::fixy::atom::spawn, kGradeVocabulary},
    {^^::fixy::io::engine, kGradeVocabulary},
    {^^::fixy::io::zerocopy, kGradeVocabulary},
    {^^::fixy::io::ring_flag, kGradeVocabulary},
    {^^::fixy::fs::open_mode, kGradeVocabulary},
    {^^::fixy::fs::flag, kGradeVocabulary},
    {^^::fixy::fs::sync_op, kGradeVocabulary},
    {^^::fixy::fs::atomicity, kGradeVocabulary},
    {^^::fixy::mmap::prot, kGradeVocabulary},
    {^^::fixy::mmap::share, kGradeVocabulary},
    {^^::fixy::mmap::advice, kGradeVocabulary},
    {^^::fixy::concurrent::mpsc_tag, kGradeVocabulary},
    {^^::fixy::concurrent::spsc_tag, kGradeVocabulary},
    {^^::fixy::spawn::join, kGradeVocabulary},
    {^^::fixy::sanitize::path_traversal, kGradeVocabulary},
    {^^::fixy::session::check, "abandonment policies, a property of the build and not of the claim"},
    {^^::fixy::session::detach_reason, kGradeVocabulary},
    {^^::fixy::refined, kMachinery},
    {^^::fixy::refined::admitted_implications, kMachinery},
    {^^::fixy::refined_algebra, "refinement predicate combinators, which are grade vocabulary"},
    {^^::fixy::collision, kMachinery},
    {^^::fixy::corpus, kMachinery},
    {^^::fixy::spin::spinlock_size_probe_, "a layout probe for a static assertion"},
    {^^::fixy::row_discipline, "discipline identities, declared and never defined; they name claims"},
    {^^::fixy::refined::row_discipline, "discipline identities, declared and never defined; they name claims"},
    {^^::foundation::algebra::modality, kGradeVocabulary},
    {^^::foundation::algebra::lattices, kGradeVocabulary},
    {^^::foundation::effects::cap, kGradeVocabulary},
    {^^::foundation::effects::ctx_cap, kGradeVocabulary},
    {^^::foundation::effects::host, "the owners a context is minted for, never passed as values"},
    {^^::foundation::effects::testing, "the test context's witness, never passed as a value"},
    {^^::foundation::permissions::tag, kGradeVocabulary},
    {^^::foundation::permissions::host, "the issuer a borrow is minted for, never passed as a value"},
    {^^::foundation::permissions::row_discipline, "discipline identities, declared and never defined"},
    {^^::foundation::brand, "brands, which are identities of instances and never fold"},
    {^^::foundation::decide, kMachinery},
    {^^::foundation::decide::oracle, kMachinery},
    {^^::foundation::diag, "the row-hash fold and the diagnostic surface themselves"},
    {^^::foundation::diag::row_discipline, "discipline identities, declared and never defined"},
    {^^::foundation::fail_closed, kMachinery},
    {^^::foundation::reflect, kMachinery},
};

[[nodiscard]] consteval bool is_skipped_namespace(std::meta::info ns) {
    if (!std::meta::has_identifier(ns)) return true;
    std::string_view const name = std::meta::identifier_of(ns);
    return name == "detail" || name.ends_with("_self_test") || name.ends_with("_test");
}

[[nodiscard]] consteval bool declares_a_class(std::meta::info ns) {
    for (std::meta::info member : std::meta::members_of(ns, std::meta::access_context::current()))
        if (is_roster_member(member)) return true;
    return false;
}

consteval void collect_namespaces(std::meta::info ns, std::vector<std::meta::info>& out) {
    for (std::meta::info member : std::meta::members_of(ns, std::meta::access_context::current())) {
        if (!std::meta::is_namespace(member) || std::meta::is_namespace_alias(member)) continue;
        if (is_skipped_namespace(member)) continue;
        if (declares_a_class(member)) out.push_back(member);
        collect_namespaces(member, out);
    }
}

struct NamespaceVerdict {
    std::size_t declaring = 0;
    std::size_t disposed = 0;
    std::size_t stale = 0;
};

[[nodiscard]] consteval NamespaceVerdict namespace_verdict() {
    std::vector<std::meta::info> declaring;
    for (std::meta::info root : {^^::foundation, ^^::fixy}) {
        if (declares_a_class(root)) declaring.push_back(root);
        collect_namespaces(root, declaring);
    }
    NamespaceVerdict verdict{};
    verdict.declaring = declaring.size();
    for (std::meta::info ns : declaring) {
        std::size_t dispositions = 0;
        for (std::meta::info censused : kCensusNamespaces)
            if (censused == ns) ++dispositions;
        for (StatedVocabulary const& vocabulary : kVocabularyNamespaces)
            if (vocabulary.ns == ns && !vocabulary.reason.empty()) ++dispositions;
        if (dispositions == 1) ++verdict.disposed;
    }
    auto const declared = [&](std::meta::info ns) {
        for (std::meta::info candidate : declaring)
            if (candidate == ns) return true;
        return false;
    };
    for (std::meta::info censused : kCensusNamespaces)
        if (!declared(censused)) ++verdict.stale;
    for (StatedVocabulary const& vocabulary : kVocabularyNamespaces)
        if (!declared(vocabulary.ns)) ++verdict.stale;
    return verdict;
}

inline constexpr NamespaceVerdict kNamespaceVerdict = namespace_verdict();

static_assert(kNamespaceVerdict.declaring > 0, "no namespace under fixy or foundation declares a class, so the "
                                               "namespace census proves nothing");
static_assert(kNamespaceVerdict.disposed == kNamespaceVerdict.declaring,
              "a namespace under fixy or foundation declares a class and is neither censused nor named as "
              "vocabulary, or is named twice.  Add it to kCensusNamespaces if it holds carriers, or to "
              "kVocabularyNamespaces with the reason it holds none.");
static_assert(kNamespaceVerdict.stale == 0, "a namespace listed in the census tables no longer declares a class, "
                                            "or no longer exists.  Remove the stale row.");

// ── Witnesses ────────────────────────────────────────────────────────

struct PureRegionTag {
    using permission_row = fe::Row<>;
};

struct IoRegionTag {
    using permission_row = fe::Row<fe::Effect::IO>;
};

struct SplitSiteName {};

struct MachineState {};

using PureDet = fa::lattices::DetSafeLattice::At<fa::lattices::DetSafeTier::Pure>;
using BorrowedInt = ::fixy::Borrowed<int, PureRegionTag>;

namespace stage_probe = ::fixy::concurrent::detail::stage_self_test;
namespace pipeline_probe = ::fixy::concurrent::detail::pipeline_self_test;

inline constexpr CarrierWitness kCarriers[] = {
    {^^fa::Graded, ^^fa::Graded<fa::ModalityKind::Absolute, PureDet, int>},

    {^^fp::Permission, ^^fp::Permission<PureRegionTag>},
    {^^fp::SharedPermission, ^^fp::SharedPermission<PureRegionTag>},
    {^^fp::SharedPermissionGuard, ^^fp::SharedPermissionGuard<PureRegionTag, ::foundation::brand::DefaultBrand>},
    {^^fp::SharedPermissionPool, ^^fp::SharedPermissionPool<PureRegionTag, ::foundation::brand::DefaultBrand>},
    {^^fp::PermSet, ^^fp::PermSet<PureRegionTag>},
    {^^fp::ReadView, ^^fp::ReadView<PureRegionTag>},

    {^^fe::Bg, ^^fe::Bg},
    {^^fe::Init, ^^fe::Init},
    {^^fe::Test, ^^fe::Test},
    {^^fe::Row, ^^fe::Row<>},
    {^^fe::ExecCtx, ^^fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>},
    {^^fe::Computation, ^^fe::Computation<fe::Row<>, int>},
    {^^fe::Capability, ^^fe::Capability<fe::Effect::Alloc, fe::Bg>},

    {^^::fixy::BorrowedRef, ^^::fixy::BorrowedRef<int>},
    {^^::fixy::Borrowed, ^^BorrowedInt},
    {^^::fixy::OwnedRegion, ^^::fixy::OwnedRegion<int, PureRegionTag>},
    {^^::fixy::WeakRef, ^^::fixy::WeakRef<int>},
    {^^::fixy::fn, ^^::fixy::role::PureCopy<int>},
    {^^::fixy::graded_facade, ^^::fixy::graded_facade<fa::ModalityKind::Absolute, PureDet, int>},
    {^^::fixy::WriteOnce, ^^::fixy::WriteOnce<int>},
    {^^::fixy::WriteOnceNonNull, ^^::fixy::WriteOnceNonNull<int*>},
    {^^::fixy::AppendOnly, ^^::fixy::AppendOnly<int>},
    {^^::fixy::OrderedAppendOnly, ^^::fixy::OrderedAppendOnly<int>},
    {^^::fixy::Monotonic, ^^::fixy::Monotonic<std::uint64_t>},
    {^^::fixy::BoundedMonotonic, ^^::fixy::BoundedMonotonic<std::uint32_t, 1024U>},
    {^^::fixy::AtomicMonotonic, ^^::fixy::AtomicMonotonic<std::uint64_t>},
    {^^::fixy::Qtt, ^^::fixy::Linear<int>},
    {^^::fixy::Refinement, ^^::fixy::Refined<::fixy::positive, int>},
    {^^::fixy::Saturated, ^^::fixy::Saturated<int>},
    {^^::fixy::Secret, ^^::fixy::Secret<int>},
    {^^::fixy::Stale, ^^::fixy::Stale<int>},
    {^^::fixy::Tagged, ^^::fixy::Tagged<int, ::fixy::tags::trust::Verified>},
    {^^::fixy::CpuPinned, ^^::fixy::detail::cpu_pinned_invariants::PinnedC0},
    {^^::fixy::SchedClass, ^^::fixy::sched_class::Batch<int>},
    {^^::fixy::ThreadNamed, ^^::fixy::ThreadNamed<"census">},
    {^^::fixy::Machine, ^^::fixy::Machine<MachineState>},
    {^^::fixy::ClockSource, ^^::fixy::MonotonicClockBytes<int>},
    {^^::fixy::OwnedMmap, ^^::fixy::detail::owned_mmap_self_test::SmokeOwnedMmap},
    {^^::fixy::Disjoint, ^^::fixy::Disjoint<PureRegionTag, ::foundation::brand::DefaultBrand, SplitSiteName, 2>},
    {^^::fixy::SharedRegion, ^^::fixy::SharedRegion<int, PureRegionTag>},
    {^^::fixy::ScopedView, ^^::fixy::ScopedView<::fixy::detail::sv_test_carrier, ::fixy::detail::sv_test_tag>},
    {^^::fixy::SharedRead, ^^::fixy::SharedRead<int, PureRegionTag>},
    {^^::fixy::Witnessed, ^^::fixy::Witnessed<BorrowedInt, ::fixy::witness::UnderRow<PureRegionTag>>},

    {^^::fixy::session::SessionHandle, ^^::fixy::session::SessionHandle<::fixy::session::End, int>},
    {^^::fixy::session::SessionFromMachine,
     ^^::fixy::session::SessionFromMachine<MachineState, ::fixy::session::End>},

    {^^::fixy::concurrent::MpscRing, ^^::fixy::concurrent::MpscRing<int, 8>},
    {^^::fixy::concurrent::SpscRing, ^^::fixy::concurrent::SpscRing<int, 8>},
    {^^::fixy::concurrent::PermissionedMpscChannel, ^^::fixy::concurrent::PermissionedMpscChannel<int, 8>},
    {^^::fixy::concurrent::PermissionedSpscChannel, ^^::fixy::concurrent::PermissionedSpscChannel<int, 8>},
    {^^::fixy::concurrent::Stage, ^^stage_probe::S1},
    {^^::fixy::concurrent::MpmcStage, ^^stage_probe::M1},
    {^^::fixy::concurrent::SwmrStage, ^^stage_probe::W1},
    {^^::fixy::concurrent::Pipeline, ^^pipeline_probe::P1},
    {^^::fixy::concurrent::PipelineDag, ^^pipeline_probe::PDiamond},

    {^^::fixy::handle::SetOnce, ^^::fixy::handle::SetOnce<int>},
    {^^::fixy::handle::Lazy, ^^::fixy::handle::Lazy<int>},
    {^^::fixy::handle::PublishOnce, ^^::fixy::handle::PublishOnce<int>},
    {^^::fixy::handle::PublishSlot, ^^::fixy::handle::PublishSlot<int>},

    {^^::fixy::sched::SchedPriority, ^^::fixy::sched::SchedPriority<0>},
    {^^::fixy::spin::SpinLock, ^^::fixy::spin::SpinLock<PureRegionTag>},
    {^^::fixy::spin::SpinGuard, ^^::fixy::spin::SpinGuard<PureRegionTag>},
    {^^::fixy::cipher::durable::CipherDurableHandle,
     ^^::fixy::cipher::durable::CipherDurableHandle<::fixy::cipher::durable::warm_writer_stance>},
};

// ── Stated zeros ─────────────────────────────────────────────────────

inline constexpr std::string_view kMetafunction =
    "a compile-time metafunction: it answers a question about types and is never instantiated as a value";
inline constexpr std::string_view kPasskey =
    "a passkey: it exists only inside the one mint that may construct it and never outlives the call";
inline constexpr std::string_view kVocabulary =
    "grade vocabulary: it is the grade another carrier is spelled at, not a carrier of one";
inline constexpr std::string_view kProtocol =
    "a protocol combinator: it is the grade a session handle steps through, and the handle folds it";
inline constexpr std::string_view kPayload = "a bare payload: it holds a value and makes no claim about it";
inline constexpr std::string_view kDescriptor =
    "a single non-template runtime descriptor or cell: it makes one fixed claim, is never a template "
    "argument of a kernel signature, and has no tier to separate";
inline constexpr std::string_view kFactory =
    "a stateless factory: it hands out readings and holds nothing a key could discriminate";
inline constexpr std::string_view kGraphShape =
    "the type-level shape of a stage graph; the pipeline that runs it folds the shape as its identity";

inline constexpr StatedZero kZeros[] = {
    {^^::foundation::Pinned, "a CRTP marker base: it forbids moves on its deriver and is never a value"},

    {^^fa::is_graded_specialization, kMetafunction},
    {^^fa::graded_modality, kMetafunction},
    {^^fa::value_type_decoupled, kMetafunction},

    {^^fp::splits_into, kMetafunction},
    {^^fp::splits_into_pack, kMetafunction},
    {^^fp::splits_into_authoring_witness, kMetafunction},
    {^^fp::splits_into_pack_authoring_witness, kMetafunction},
    {^^fp::perm_mint_key, kPasskey},
    {^^fp::perm_set_insert, kMetafunction},
    {^^fp::perm_set_remove, kMetafunction},
    {^^fp::perm_set_union, kMetafunction},
    {^^fp::perm_set_difference, kMetafunction},
    {^^fp::perm_set_canonicalize, kMetafunction},

    {^^fe::is_effect_row, kMetafunction},
    {^^fe::canonical_row, kMetafunction},
    {^^fe::is_subrow, kMetafunction},
    {^^fe::EffectRowLattice, "the lattice over effect rows: a grade, where the row it grades is what folds"},
    {^^fe::is_cap_type, kMetafunction},
    {^^fe::cap_permitted_row, kMetafunction},
    {^^fe::is_exec_ctx, kMetafunction},
    {^^fe::cap_mint_key, kPasskey},

    {^^::fixy::axis_traits, kMetafunction},
    {^^::fixy::Bits, kPayload},
    {^^::fixy::is_writeonce, kMetafunction},
    {^^::fixy::is_writeoncenonnull, kMetafunction},
    {^^::fixy::is_already_linear, kMetafunction},
    {^^::fixy::is_already_consume_disciplined, kMetafunction},
    {^^::fixy::Aligned, kVocabulary},
    {^^::fixy::InRange, kVocabulary},
    {^^::fixy::BoundedAbove, kVocabulary},
    {^^::fixy::LengthGe, kVocabulary},
    {^^::fixy::ExactSize, kVocabulary},
    {^^::fixy::BoundedBelow, kVocabulary},
    {^^::fixy::DivisibleBy, kVocabulary},
    {^^::fixy::retag_policy, kMetafunction},
    {^^::fixy::ThreadNameLiteral, "a string literal usable as a template argument; ThreadNamed folds it"},
    {^^::fixy::Cyclic, kPayload},
    {^^::fixy::FixedArray, kPayload},
    {^^::fixy::duplicate_atom_on, kMetafunction},
    {^^::fixy::malformed_atom, kMetafunction},
    {^^::fixy::unholdable_payload, kMetafunction},
    {^^::fixy::is_fn, kMetafunction},
    {^^::fixy::PathTraversal, kVocabulary},
    {^^::fixy::Unsplit, kVocabulary},
    {^^::fixy::Slice, kVocabulary},
    {^^::fixy::SplitParts, "a return bundle of a Disjoint witness and its shards, destructured at the call "
                           "site; each member folds on its own"},
    {^^::fixy::split_parts_for_, kMetafunction},
    {^^::fixy::OwnedFile, kDescriptor},
    {^^::fixy::is_scoped_view, kMetafunction},

    {^^::fixy::session::Send, kProtocol},
    {^^::fixy::session::Recv, kProtocol},
    {^^::fixy::session::Select, kProtocol},
    {^^::fixy::session::Sender, kProtocol},
    {^^::fixy::session::AnonymousPeer, kProtocol},
    {^^::fixy::session::Offer, kProtocol},
    {^^::fixy::session::offer_sender, kMetafunction},
    {^^::fixy::session::Loop, kProtocol},
    {^^::fixy::session::Continue, kProtocol},
    {^^::fixy::session::End, kProtocol},
    {^^::fixy::session::VendorPinned, kProtocol},
    {^^::fixy::session::session_loop_ctx_traits, kMetafunction},
    {^^::fixy::session::session_loop_ctx_rebind_inner, kMetafunction},
    {^^::fixy::session::is_send, kMetafunction},
    {^^::fixy::session::is_recv, kMetafunction},
    {^^::fixy::session::is_select, kMetafunction},
    {^^::fixy::session::is_offer, kMetafunction},
    {^^::fixy::session::is_loop, kMetafunction},
    {^^::fixy::session::is_end, kMetafunction},
    {^^::fixy::session::is_continue, kMetafunction},
    {^^::fixy::session::is_vendor_pinned, kMetafunction},
    {^^::fixy::session::is_empty_choice, kMetafunction},
    {^^::fixy::session::dual_of, kMetafunction},
    {^^::fixy::session::is_dual_involutive, kMetafunction},
    {^^::fixy::session::compose, kMetafunction},
    {^^::fixy::session::compose_at_branch, kMetafunction},
    {^^::fixy::session::is_terminal_state, kMetafunction},
    {^^::fixy::session::is_well_formed, kMetafunction},
    {^^::fixy::session::SessionHandleBase,
     "the base every session handle derives from: it is never held by value, and the handle folds.  It "
     "publishes the Stepping modality without a resource, so hashing it directly is a hard error"},

    {^^::fixy::session::vigil_mode::mode_tag, kVocabulary},
    {^^::fixy::session::vigil_mode::ModeTransition, kProtocol},
    {^^::fixy::session::vigil_mode::ModeCell, kDescriptor},

    {^^::fixy::concurrent::payload_row, kMetafunction},
    {^^::fixy::concurrent::StageArity, kMetafunction},
    {^^::fixy::concurrent::Topology, "a measured description of the host, read by the scheduler and never a "
                                     "template argument of a kernel signature"},
    {^^::fixy::concurrent::StagePack, kGraphShape},
    {^^::fixy::concurrent::EdgePack, kGraphShape},
    {^^::fixy::concurrent::StageEdge, kGraphShape},
    {^^::fixy::concurrent::StageGraph, kGraphShape},
    {^^::fixy::concurrent::stage_inline_safe, kMetafunction},

    {^^::fixy::handle::Once, kDescriptor},

    {^^::fixy::io::IoUringRing, kDescriptor},
    {^^::fixy::io::engine_is_io_uring, kMetafunction},
    {^^::fixy::io::zerocopy_is_simple_transfer, kMetafunction},
    {^^::fixy::io::ring_flag_bits, kMetafunction},
    {^^::fixy::fs::open_mode_flags, kMetafunction},
    {^^::fixy::fs::flag_bits, kMetafunction},
    {^^::fixy::fs::OwnedFd, kDescriptor},
    {^^::fixy::fs::Dirfd, kDescriptor},
    {^^::fixy::mmap::prot_bits, kMetafunction},
    {^^::fixy::mmap::share_flags, kMetafunction},
    {^^::fixy::mmap::advice_value, kMetafunction},
    {^^::fixy::fp::CanonicalizeRecipeSpec, kPayload},
    {^^::fixy::fp::ReduceResult, kPayload},

    {^^::fixy::spin::UnwitnessedSpinLock, kDescriptor},
    {^^::fixy::time::ClockReader, kFactory},
    {^^::fixy::time::TscReader, kFactory},
    {^^::fixy::time::BoundedSleeper, kFactory},
    {^^::fixy::witness::AtProtocol, kVocabulary},
    {^^::fixy::witness::UnderRow, kVocabulary},
    {^^::fixy::cipher::durable::warm_writer_stance, kVocabulary},
    {^^::fixy::cipher::durable::cold_writer_stance, kVocabulary},
    {^^::fixy::cipher::durable::head_advance_stance, kVocabulary},
};

inline constexpr Verdict kVerdict = run<kCensusNamespaces, kCarriers, kZeros>();

static_assert(kVerdict.roster > 0, "the reflected carrier roster is empty, so the census proves nothing");
static_assert(kVerdict.proven == kVerdict.roster,
              "a carrier in a censused namespace is unproven.  Either it folds to the zero slot that every bare "
              "payload shares, or it has no row in kCarriers or kZeros, or it has two.  Give the carrier a "
              "shape foundation/diag/RowHash.h reads and a witness in kCarriers, or, if it genuinely carries "
              "no row, a row in kZeros that says why.");
static_assert(kVerdict.stale == 0, "a row in kCarriers or kZeros names a type that is no longer in the census "
                                   "roster.  Remove the stale row.");

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

// A permission set is a set, so its spelling order does not move it, and
// its size does.
static_assert(row_hash_contribution_v<fp::PermSet<PureRegionTag, IoRegionTag>>
              == row_hash_contribution_v<fp::PermSet<IoRegionTag, PureRegionTag>>);
static_assert(row_hash_contribution_v<fp::PermSet<PureRegionTag>>
              != row_hash_contribution_v<fp::PermSet<PureRegionTag, IoRegionTag>>);
static_assert(row_hash_contribution_v<fp::PermSet<>> != 0);

// A session handle's grade is its protocol position.
static_assert(row_hash_contribution_v<::fixy::session::SessionHandle<::fixy::session::End, int>>
              != row_hash_contribution_v<
                  ::fixy::session::SessionHandle<::fixy::session::Send<int, ::fixy::session::End>, int>>);
static_assert(::foundation::diag::SteppingShaped<::fixy::session::SessionHandle<::fixy::session::End, int>>);
static_assert(::foundation::diag::PublishesGradedMember<::fixy::session::SessionHandleBase<::fixy::session::End>>
                  && !::foundation::diag::SteppingShaped<::fixy::session::SessionHandleBase<::fixy::session::End>>,
              "the handle base publishes part of the Stepping contract, which is exactly what the primary "
              "template's hard error exists to catch");

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
static_assert(row_hash_contribution_v<pipeline_probe::P1>
              != row_hash_contribution_v<pipeline_probe::S_int_to_int>);
static_assert(row_hash_contribution_v<stage_probe::S1> != row_hash_contribution_v<stage_probe::W1>);
static_assert(row_hash_contribution_v<::fixy::concurrent::PermissionedSpscChannel<int, 8>::ProducerHandle>
              != row_hash_contribution_v<::fixy::concurrent::PermissionedSpscChannel<int, 8>::ConsumerHandle>);
static_assert(row_hash_contribution_v<::fixy::concurrent::PermissionedMpscChannel<int, 8>::ProducerHandle>
              != row_hash_contribution_v<::fixy::concurrent::PermissionedSpscChannel<int, 8>::ProducerHandle>);

}  // namespace census

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

// The census verdicts are constants, so the build already proved them.
// Reading them back as values is what shows the constants reach a
// running program, and it prints the roster size so a shrinking census
// is visible in the log rather than silent.
void test_every_carrier_is_off_the_zero_slot() {
    std::size_t const roster = census::kVerdict.roster;
    std::size_t const proven = census::kVerdict.proven;
    std::fprintf(stderr, "(%zu carriers and stated zeros across %zu class-declaring namespaces) ", roster,
                 census::kNamespaceVerdict.declaring);
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
