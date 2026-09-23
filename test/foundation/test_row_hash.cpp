// The row hash is a header of constants, so almost everything worth
// checking is a static assertion. This translation unit exists to make
// those assertions run: a header-only assertion block is checked only
// where some translation unit includes the header, and nothing else in
// the new tree includes this one yet.
//
// Ported from the self-test block inside
// include/crucible/safety/diag/RowHashFold.h, with the per-wrapper cells
// replaced by cells over the one Graded fold.
//
// The literals in the wire-format section are the on-the-wire contract.
// Every published cache entry whose key includes one of these rows
// carries that exact 64-bit value in its serialized form. Changing an
// underlying effect value, changing the mixer or the offset basis, or
// changing the seed and fold invalidates every entry already published,
// everywhere.
//
// Do not edit a literal to make the build pass. A literal changes only
// as part of a deliberate wire-format break, announced the way that
// procedure requires. A drift without it corrupts every peer's cache
// silently. Adding a pin for a row not covered here is always safe.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/algebra/lattices/AllocClassLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Capability.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace {

namespace fd = ::foundation::diag;
namespace fe = ::foundation::effects;
namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;

using fe::Effect;
using fe::EmptyRow;
using fe::Row;

using fd::row_hash_contribution_v;

// ── A bare type carries no row ───────────────────────────────────────

static_assert(row_hash_contribution_v<int> == 0);
static_assert(row_hash_contribution_v<float> == 0);
static_assert(row_hash_contribution_v<double> == 0);
static_assert(row_hash_contribution_v<void> == 0);
static_assert(row_hash_contribution_v<unsigned> == 0);

static_assert(fd::row_hash_of_v<int> == 0);
static_assert(fd::row_hash_of_v<float> == 0);

// ── The row is a set: permutation and repetition do not move it ──────

static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::IO>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::Block>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::Bg>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::Init>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::Test>> != 0);

static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
              == row_hash_contribution_v<Row<Effect::IO, Effect::Alloc>>);
static_assert(row_hash_contribution_v<Row<Effect::Block, Effect::Bg>>
              == row_hash_contribution_v<Row<Effect::Bg, Effect::Block>>);

using FullRow_canonical = Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>;
using FullRow_reversed = Row<Effect::Test, Effect::Init, Effect::Bg, Effect::Block, Effect::IO, Effect::Alloc>;
using FullRow_shuffled = Row<Effect::Block, Effect::Alloc, Effect::Test, Effect::IO, Effect::Init, Effect::Bg>;

static_assert(row_hash_contribution_v<FullRow_canonical> == row_hash_contribution_v<FullRow_reversed>);
static_assert(row_hash_contribution_v<FullRow_canonical> == row_hash_contribution_v<FullRow_shuffled>);

// Nothing canonicalizes a pack before it reaches the hash, so these
// cases are the only thing keeping a row written with repeats out of a
// slot of its own.
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::Alloc>>
              == row_hash_contribution_v<Row<Effect::Alloc>>);
static_assert(row_hash_contribution_v<Row<Effect::IO, Effect::IO, Effect::IO>>
              == row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Bg, Effect::IO, Effect::Bg, Effect::IO>>
              == row_hash_contribution_v<Row<Effect::IO, Effect::Bg>>);

// A pack of four with two distinct atoms must seed on two, which pins
// that the seed reads the unique count and not the pack size.
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::Alloc, Effect::IO, Effect::IO>>
              == row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>);

// ── Cardinality participates ─────────────────────────────────────────

static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
              != row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>);

// ── The empty row is a row, not the absence of one ───────────────────

static_assert(row_hash_contribution_v<EmptyRow> != 0);
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<int>);

// The atom whose underlying value is zero is the dangerous one. Folding
// zero into a seed leaves the seed alone, which is how a singleton over
// that atom would alias the empty row. Every singleton is pinned apart
// from the empty row for that reason.
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<Row<Effect::Alloc>>);
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<Row<Effect::Test>>);

// Exhaustive over pairs of atoms, up to symmetry. A renumbering of the
// effect enum that made two atoms share a value would show up here.
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>> != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>> != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>> != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>> != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::Block>> != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::Block>> != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::Block>> != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::Bg>> != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::Bg>> != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::Init>> != row_hash_contribution_v<Row<Effect::Test>>);

// The all-ones value marks an empty cache slot. A real row that landed
// on it would claim that slot.
static_assert(row_hash_contribution_v<EmptyRow> != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<FullRow_canonical> != static_cast<std::uint64_t>(-1));

// ── The computation carrier ──────────────────────────────────────────

// A carrier over the empty row is not a bare payload, and it is not its
// own row either.
static_assert(row_hash_contribution_v<fe::Computation<EmptyRow, int>> != row_hash_contribution_v<int>);
static_assert(row_hash_contribution_v<fe::Computation<EmptyRow, int>> != 0);
static_assert(row_hash_contribution_v<fe::Computation<EmptyRow, int>> != row_hash_contribution_v<EmptyRow>);
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Alloc>, int>>
              != row_hash_contribution_v<Row<Effect::Alloc>>);

// Payload identity belongs to the content hash, so two kernels that
// return different scalar types share a row signature and separate on
// the other half of the key.
static_assert(row_hash_contribution_v<fe::Computation<EmptyRow, int>>
              == row_hash_contribution_v<fe::Computation<EmptyRow, double>>);
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Alloc>, int>>
              == row_hash_contribution_v<fe::Computation<Row<Effect::Alloc>, char>>);

// Two kernels that compute the same value under different effect rows
// must not share a slot. This is the property the whole key exists for.
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Alloc>, int>>
              != row_hash_contribution_v<fe::Computation<Row<Effect::IO>, int>>);
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Alloc>, int>>
              != row_hash_contribution_v<fe::Computation<EmptyRow, int>>);

// Permutation invariance and cardinality both lift through the carrier.
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Alloc, Effect::IO>, int>>
              == row_hash_contribution_v<fe::Computation<Row<Effect::IO, Effect::Alloc>, int>>);
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Alloc>, int>>
              != row_hash_contribution_v<fe::Computation<Row<Effect::Alloc, Effect::IO>, int>>);

// A carrier nested inside a carrier keeps the inner row visible in the
// outer hash, so it cannot alias the flattened form.
static_assert(row_hash_contribution_v<fe::Computation<EmptyRow, fe::Computation<Row<Effect::IO>, int>>>
              != row_hash_contribution_v<fe::Computation<EmptyRow, int>>);
static_assert(row_hash_contribution_v<fe::Computation<EmptyRow, fe::Computation<Row<Effect::IO>, int>>>
              != row_hash_contribution_v<fe::Computation<Row<Effect::IO>, int>>);

// ── The one fold, over Graded ────────────────────────────────────────
//
// These cells are what replaces fifty-two per-wrapper cells in the old
// header. Each one is a property of the fold rather than of a named
// wrapper, so a wrapper added later is covered without a cell of its
// own.

template <fa::ModalityKind M, typename L, typename T>
using G = fa::Graded<M, L, T>;

using DetPure = fl::DetSafeLattice::At<fl::DetSafeTier::Pure>;
using DetEntropyRead = fl::DetSafeLattice::At<fl::DetSafeTier::EntropyRead>;
using HotHot = fl::HotPathLattice::At<fl::HotPathTier::Hot>;

// A graded carrier contributes something, which is the whole difference
// between a wrapper the cache can see and one it cannot. Under the old
// header a wrapper with no specialization fell through to the primary
// and contributed zero; nothing can fall through here.
static_assert(row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, int>> != 0);

// Two tiers of one axis are different slots.
static_assert(row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, int>>
              != row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetEntropyRead, int>>);

// Two axes are different slots. This is what the per-wrapper salt byte
// used to buy, and it now comes from the lattice type itself.
static_assert(row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, int>>
              != row_hash_contribution_v<G<fa::ModalityKind::Absolute, HotHot, int>>);

// The modality participates. The same lattice under a comonadic carrier
// and an absolute one are different claims about the value.
static_assert(row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, int>>
              != row_hash_contribution_v<G<fa::ModalityKind::Comonad, DetPure, int>>);

// The payload recurses, which is what makes this a fold. A stack of two
// is not either of its layers.
static_assert(row_hash_contribution_v<G<fa::ModalityKind::Absolute, HotHot, G<fa::ModalityKind::Absolute, DetPure, int>>>
              != row_hash_contribution_v<G<fa::ModalityKind::Absolute, HotHot, int>>);
static_assert(row_hash_contribution_v<G<fa::ModalityKind::Absolute, HotHot, G<fa::ModalityKind::Absolute, DetPure, int>>>
              != row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, int>>);

// Nesting order carries meaning, so the combiner must not commute.
static_assert(row_hash_contribution_v<G<fa::ModalityKind::Absolute, HotHot, G<fa::ModalityKind::Absolute, DetPure, int>>>
              != row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, G<fa::ModalityKind::Absolute, HotHot, int>>>);

// A graded carrier is payload-blind for the same reason the computation
// carrier is: which scalar sits underneath belongs to the content hash.
static_assert(row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, int>>
              == row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, double>>);

// A row reaching the fold through a graded payload stays visible.
static_assert(row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, fe::Computation<Row<Effect::IO>, int>>>
              != row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, fe::Computation<EmptyRow, int>>>);

// No graded carrier lands on the empty-slot marker.
static_assert(row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, int>> != static_cast<std::uint64_t>(-1));

// ── The class-shaped wrapper ─────────────────────────────────────────
//
// Half the wrappers in fixy are aliases for Graded and half are classes
// that inherit a facade over it. A fold that matched the template rather
// than the shape would hand every class-shaped wrapper the primary
// template's zero, which is the fail-open the old header needed
// fifty-two specialisations to avoid.
//
// This probe publishes the three names the facade publishes and nothing
// else, so it stands for that whole half without this test depending on
// fixy.

template <fa::ModalityKind M, typename L, typename T>
struct FacadeProbe {
    static constexpr fa::ModalityKind modality = M;
    using lattice_type = L;
    using value_type = T;
};

static_assert(row_hash_contribution_v<FacadeProbe<fa::ModalityKind::Absolute, DetPure, int>> != 0,
              "a class-shaped wrapper falls through to the primary template, so the fold reads the Graded "
              "template rather than the shape every wrapper publishes");

// It agrees with the alias-shaped carrier over the same three
// arguments, which is the property that lets a wrapper change between
// the two spellings without moving its cache slot.
static_assert(row_hash_contribution_v<FacadeProbe<fa::ModalityKind::Absolute, DetPure, int>>
              == row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, int>>);

// The payload recurses through the class shape too.
static_assert(row_hash_contribution_v<FacadeProbe<fa::ModalityKind::Absolute, HotHot,
                                                  FacadeProbe<fa::ModalityKind::Absolute, DetPure, int>>>
              != row_hash_contribution_v<FacadeProbe<fa::ModalityKind::Absolute, HotHot, int>>);

// The canonical-id hook is what a rename goes through. Two lattices with
// no mapping between them are separate; the mapping is what makes one
// reach the other's entries.
static_assert(fd::lattice_canonical_id_v<DetPure> != fd::lattice_canonical_id_v<DetEntropyRead>);
static_assert(fd::lattice_canonical_id_v<DetPure> != fd::lattice_canonical_id_v<HotHot>);
static_assert(fd::lattice_canonical_id_v<DetPure> != 0);

// ── A graded wrapper with a claim its lattice does not see ───────────
//
// The sealed refinement is the case in the tree. Publishing
// row_discipline beside the graded shape folds one more identity in, and
// a wrapper that publishes none keeps the hash it had.

struct sealed_probe_identity;

template <fa::ModalityKind M, typename L, typename T>
struct SealedFacadeProbe : FacadeProbe<M, L, T> {
    using row_discipline = sealed_probe_identity;
};

static_assert(row_hash_contribution_v<SealedFacadeProbe<fa::ModalityKind::Absolute, DetPure, int>>
              != row_hash_contribution_v<FacadeProbe<fa::ModalityKind::Absolute, DetPure, int>>);
static_assert(row_hash_contribution_v<SealedFacadeProbe<fa::ModalityKind::Absolute, DetPure, int>>
              == row_hash_contribution_v<SealedFacadeProbe<fa::ModalityKind::Absolute, DetPure, double>>);

// ── The discipline carrier ───────────────────────────────────────────

struct probe_discipline;
struct other_probe_discipline;

template <typename Identity, typename Payload>
struct DisciplineProbe {
    using row_discipline = Identity;
    using row_payload = Payload;
};

using Disc = DisciplineProbe<probe_discipline, int>;

static_assert(fd::DisciplineShaped<Disc>);
static_assert(!fd::DisciplineShaped<FacadeProbe<fa::ModalityKind::Absolute, DetPure, int>>);
static_assert(row_hash_contribution_v<Disc> != 0);

// The published shape and the specialisation helper are one fold.
static_assert(row_hash_contribution_v<Disc> == fd::discipline_row_hash_v<probe_discipline, int>);

// Two identities are two slots, a bare payload is blind, and a payload
// carrying a row stays visible.
static_assert(row_hash_contribution_v<Disc> != row_hash_contribution_v<DisciplineProbe<other_probe_discipline, int>>);
static_assert(row_hash_contribution_v<Disc> == row_hash_contribution_v<DisciplineProbe<probe_discipline, double>>);
static_assert(row_hash_contribution_v<DisciplineProbe<probe_discipline, fe::Computation<Row<Effect::IO>, int>>>
              != row_hash_contribution_v<Disc>);

// A payload list folds in order, and an empty list is not a zero payload.
static_assert(fd::discipline_row_hash_v<probe_discipline, fd::row_payloads<Row<Effect::IO>, EmptyRow>>
              != fd::discipline_row_hash_v<probe_discipline, fd::row_payloads<EmptyRow, Row<Effect::IO>>>);
static_assert(fd::discipline_row_hash_v<probe_discipline, fd::row_payloads<>>
              != fd::discipline_row_hash_v<probe_discipline, int>);

// A discipline identity does not alias the lattice of the same name.
static_assert(row_hash_contribution_v<DisciplineProbe<DetPure, int>>
              != row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, int>>);

// ── The stepping carrier ─────────────────────────────────────────────

struct probe_protocol_end;
struct probe_protocol_send;

template <typename Proto, typename Resource>
struct SteppingProbe {
    using protocol_type = Proto;
    using resource_type = Resource;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Stepping;
};

static_assert(fd::SteppingShaped<SteppingProbe<probe_protocol_end, int>>);
static_assert(row_hash_contribution_v<SteppingProbe<probe_protocol_end, int>> != 0);
static_assert(row_hash_contribution_v<SteppingProbe<probe_protocol_end, int>>
              != row_hash_contribution_v<SteppingProbe<probe_protocol_send, int>>);
static_assert(row_hash_contribution_v<SteppingProbe<probe_protocol_end, int>>
              == row_hash_contribution_v<SteppingProbe<probe_protocol_end, double>>);
static_assert(row_hash_contribution_v<SteppingProbe<probe_protocol_end, int>>
              != row_hash_contribution_v<DisciplineProbe<probe_protocol_end, int>>);

// ── Half a shape is an error, not a zero ─────────────────────────────
//
// The primary template refuses a type that publishes a lattice or a
// modality without a whole shape. These cells pin the detector the
// refusal reads; test/fixy/test_row_hash_wrappers.cpp shows a real
// half-shaped type, the session handle base, caught by it.

struct HalfGraded {
    using lattice_type = DetPure;
    using value_type = int;
};

struct HalfStepping {
    using protocol_type = probe_protocol_end;
    static constexpr fa::ModalityKind modality = fa::ModalityKind::Stepping;
};

static_assert(fd::PublishesGradedMember<HalfGraded> && !fd::GradedShaped<HalfGraded>);
static_assert(fd::PublishesGradedMember<HalfStepping> && !fd::SteppingShaped<HalfStepping>);
static_assert(!fd::PublishesGradedMember<int>);

// ── The effect layer's carriers ──────────────────────────────────────

static_assert(row_hash_contribution_v<fe::Bg> != 0);
static_assert(row_hash_contribution_v<fe::Bg> != row_hash_contribution_v<fe::Init>);
static_assert(row_hash_contribution_v<fe::ExecCtx<>> != 0);
static_assert(row_hash_contribution_v<fe::ExecCtx<>> != row_hash_contribution_v<EmptyRow>);
static_assert(row_hash_contribution_v<fe::ExecCtx<fe::Bg, Row<Effect::Bg>>>
              != row_hash_contribution_v<fe::ExecCtx<fe::Bg, Row<Effect::Bg, Effect::IO>>>);
static_assert(row_hash_contribution_v<fe::ExecCtx<fe::Init, Row<Effect::Alloc>>>
              != row_hash_contribution_v<fe::ExecCtx<fe::Test, Row<Effect::Alloc>>>);
static_assert(row_hash_contribution_v<fe::Capability<Effect::IO, fe::Bg>>
              != row_hash_contribution_v<fe::Capability<Effect::IO, fe::Init>>);

// ── The fold primitives ──────────────────────────────────────────────

static_assert(fd::detail::sorted_uints(std::array<std::uint64_t, 0>{}) == std::array<std::uint64_t, 0>{});
static_assert(fd::detail::sorted_uints(std::array<std::uint64_t, 3>{3, 1, 2}) == std::array<std::uint64_t, 3>{1, 2, 3});
static_assert(fd::detail::sorted_uints(std::array<std::uint64_t, 4>{1, 1, 1, 1})
              == std::array<std::uint64_t, 4>{1, 1, 1, 1});

static_assert(fd::detail::unique_count_sorted(std::array<std::uint64_t, 0>{}) == 0);
static_assert(fd::detail::unique_count_sorted(std::array<std::uint64_t, 4>{1, 1, 1, 1}) == 1);
static_assert(fd::detail::unique_count_sorted(std::array<std::uint64_t, 5>{1, 1, 2, 3, 3}) == 3);
static_assert(fd::detail::unique_count_sorted(std::array<std::uint64_t, 6>{0, 0, 1, 1, 2, 2}) == 3);

// Comparing the dedup fold against the plain fold over the canonical
// array is what pins the skip branch to the right side of its test.
static_assert(fd::detail::fmix64_fold_unique_sorted(std::array<std::uint64_t, 1>{7}, 0xAA)
              == fd::detail::fmix64_fold(std::array<std::uint64_t, 1>{7}, 0xAA));
static_assert(fd::detail::fmix64_fold_unique_sorted(std::array<std::uint64_t, 2>{7, 7}, 0xAA)
              == fd::detail::fmix64_fold(std::array<std::uint64_t, 1>{7}, 0xAA));
static_assert(fd::detail::fmix64_fold_unique_sorted(std::array<std::uint64_t, 3>{1, 1, 2}, 0xBB)
              == fd::detail::fmix64_fold(std::array<std::uint64_t, 2>{1, 2}, 0xBB));
static_assert(fd::detail::fmix64_fold_unique_sorted(std::array<std::uint64_t, 0>{}, 0xCC) == 0xCC);

// ── The federation key ───────────────────────────────────────────────
//
// The toolchain tag is not a row hash. It only ever enters a key that
// spans toolchains, and folding it in leaves every published row hash
// and every pin below untouched.

static_assert(fd::detail::FEDERATION_TOOLCHAIN_TAG != 0);
static_assert(fd::federation_toolchain_tag() == fd::detail::FEDERATION_TOOLCHAIN_TAG);

// A key carrying the tag is disjoint from the bare hash, which is what
// keeps the two kinds of peer from reading each other's slots.
static_assert(fd::federation_key_with_toolchain_v<EmptyRow> != row_hash_contribution_v<EmptyRow>);
static_assert(fd::federation_key_with_toolchain_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::Alloc>>);

// It keeps every property the bare hash has: row-discriminating,
// payload-blind, and clear of the empty-slot marker.
static_assert(fd::federation_key_with_toolchain_v<Row<Effect::Alloc>>
              != fd::federation_key_with_toolchain_v<Row<Effect::IO>>);
static_assert(fd::federation_key_with_toolchain_v<Row<Effect::Alloc>> != fd::federation_key_with_toolchain_v<EmptyRow>);
static_assert(fd::federation_key_with_toolchain_v<fe::Computation<EmptyRow, int>>
              == fd::federation_key_with_toolchain_v<fe::Computation<EmptyRow, double>>);
static_assert(fd::federation_key_with_toolchain_v<EmptyRow> != static_cast<std::uint64_t>(-1));

// ── The wire format ──────────────────────────────────────────────────
//
// Read the file header before touching a literal below.

static_assert(row_hash_contribution_v<EmptyRow> == 0xEFD01F60BA992926ULL,
              "the empty row hash drifted, which breaks the federation wire format");
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> == 0x436DAF9EDCB565C3ULL,
              "Row<Alloc> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::IO>> == 0x6FBFD0F707B63BECULL,
              "Row<IO> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Block>> == 0x3117F06B828C9247ULL,
              "Row<Block> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Bg>> == 0x008A519814C8FC81ULL,
              "Row<Bg> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Init>> == 0x9E23FC5AC81DA675ULL,
              "Row<Init> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Test>> == 0x26A9EB08E748D58FULL,
              "Row<Test> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>> == 0x6CC046F52E6D7663ULL,
              "Row<Alloc, IO> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<FullRow_canonical> == 0x1C9D0E4F548FAAD6ULL,
              "Full-Universe row row_hash drifted — federation wire-format break.");

// The carrier pins catch a drift in the combiner, in the carrier
// specialization, or in a row contribution, and they carry the same
// severity as the row pins above.

static_assert(row_hash_contribution_v<fe::Computation<EmptyRow, int>> == 0x49A55BE1CFC23FB0ULL,
              "Computation<EmptyRow, int> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Bg>, int>> == 0x3ACE35615F0F9243ULL,
              "Computation<Row<Bg>, int> row_hash drifted — wire-format break.");
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Alloc, Effect::IO>, int>> == 0x83D432DE6CDEACA7ULL,
              "Computation<Row<Alloc, IO>, int> row_hash drifted — break.");
static_assert(row_hash_contribution_v<fe::Computation<EmptyRow, fe::Computation<Row<Effect::IO>, int>>>
                  == 0x94EC56B861A6B8FDULL,
              "Nested Computation<EmptyRow, Computation<Row<IO>, int>> row_hash drifted — wire-format break.");

// One anchor per effect atom, so that a routing regression on any single
// atom reddens the build instead of moving the cache slot for every
// kernel that declares it.

static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Alloc>, int>> == 0x058CA6EFB434D439ULL,
              "Computation<Row<Alloc>, int> row_hash drifted — wire-format break.");
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::IO>, int>> == 0xCCFE717213BBA49CULL,
              "Computation<Row<IO>, int> row_hash drifted — wire-format break.");
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Block>, int>> == 0x6D28A236D0E146C7ULL,
              "Computation<Row<Block>, int> row_hash drifted — wire-format break.");
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Init>, int>> == 0x64EF4D0126C4A4E3ULL,
              "Computation<Row<Init>, int> row_hash drifted — wire-format break.");
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Test>, int>> == 0xF4060D16B464EFDEULL,
              "Computation<Row<Test>, int> row_hash drifted — wire-format break.");

// The reverse arrangement, with the row on the outside and the empty row
// within, must hash differently from the forms above. The combiner does
// not commute, and this pins that.
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Bg>, fe::Computation<EmptyRow, int>>>
                  == 0x40D0E7791202A526ULL,
              "Computation<Row<Bg>, Computation<EmptyRow, int>> drifted — federation wire-format break.");

// One row nested inside itself must not collapse to the single form.
static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Bg>, fe::Computation<Row<Effect::Bg>, int>>>
                  == 0xAFCB34F7B12A2F95ULL,
              "Computation<Row<Bg>, Computation<Row<Bg>, int>> drifted — a row nested in itself must stay distinct "
              "from the single form.");

static_assert(row_hash_contribution_v<fe::Computation<Row<Effect::Alloc>, fe::Computation<Row<Effect::IO>, int>>>
                  == 0xB25AFEA0CE322A7EULL,
              "Computation<Row<Alloc>, Computation<Row<IO>, int>> drifted.");

static_assert(
    row_hash_contribution_v<
        fe::Computation<Row<Effect::Alloc>,
                        fe::Computation<Row<Effect::IO>, fe::Computation<Row<Effect::Block>, int>>>>
        == 0xAC3F22322B23C1FEULL,
    "the triple-nested carrier row_hash drifted — the chained fold must stay bit-stable");

// ── The runtime half ─────────────────────────────────────────────────
//
// Only one thing here is not a constant: whether the values the compiler
// folded are the values that reach a running program. Re-deriving two of
// them through the same functions at run time is what a header of
// constants cannot check on its own.

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

void test_constants_reach_runtime() {
    std::uint64_t const empty = row_hash_contribution_v<EmptyRow>;
    check(empty == 0xEFD01F60BA992926ULL, "the empty row hash read at run time is not the published literal");

    std::uint64_t const alloc = row_hash_contribution_v<Row<Effect::Alloc>>;
    check(alloc != empty, "Row<Alloc> and the empty row share a slot at run time");
}

void test_fold_reaches_runtime() {
    std::uint64_t const pure = row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetPure, int>>;
    std::uint64_t const impure = row_hash_contribution_v<G<fa::ModalityKind::Absolute, DetEntropyRead, int>>;
    check(pure != 0, "a graded carrier contributes nothing at run time, so the fold is not reached");
    check(pure != impure, "two tiers of one axis share a slot at run time");
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_row_hash:\n");
    run_test("test_constants_reach_runtime", test_constants_reach_runtime);
    run_test("test_fold_reaches_runtime", test_fold_reaches_runtime);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
