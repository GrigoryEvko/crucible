// Cross-build row-hash witness for the foundation fold.
//
// Prints one `<label> 0x%016lx` line per entry of the matrix below. CI
// runs the binary and diffs stdout against
// tools/row_hash_foundation_golden.txt. A mismatch means either a
// specialisation moved on purpose, and then the golden is recaptured in
// the same commit, or a reflected name moved underneath the fold, and
// then the cache is silently broken.
//
// ── Why a separate binary ──────────────────────────────────────────
//
// Within one build every row_hash_contribution<W>::value is a constant,
// and test/foundation/test_row_hash.cpp pins the algebra of the fold with
// static_asserts. Those asserts fire in the same translation unit that
// computed the hashes. A regression in display_string_of, which is the
// upstream of stable_type_id and therefore of every lattice identity, can
// move a hash in one TU context and leave every in-TU assert green. Only
// a second binary whose output is captured under version control catches
// that.
//
// ── Why this is a peer of tools/dump_row_hashes.cpp, not an extension ──
//
// That tool witnesses the old per-wrapper fold at
// crucible/safety/diag/_RowHashFold.h. One tool covering both folds would
// be better than two tools of different shapes, so this one keeps that
// tool's shape exactly: the same rationale block, the same anchor
// cross-link, the same golden format, the same architecture gate.
//
// What it cannot share is the translation unit. Each tree ships its own
// Platform.h and its own CRUCIBLE_PRE family, and one TU that names both
// redefines five macros: CRUCIBLE_INVARIANT, CRUCIBLE_FATAL_INVARIANT,
// CRUCIBLE_PRE, CRUCIBLE_PRE_MSG and CRUCIBLE_DIAG_ASSERT. Which
// definition wins is an include-order race that no production TU runs. A
// witness against TU-context fragility must not introduce a TU context of
// its own, so the two folds take two binaries. This one links fixy and
// names no crucible header at all.
//
// ── The exposure this covers is wider than the old tool's ──────────
//
// foundation/diag/RowHash.h replaced fifty-two per-wrapper salts with one
// fold over the graded shape. Under the old salts a handful of wrapper
// kinds folded a reflected name. Under one fold every graded wrapper
// does, because the lattice identity is one of the fold's three inputs.
// The multi-axis binding in fixy/Fn.h folds such an identity for every
// axis but two. So there are more toolchain-dependent folds here than the
// old tool witnesses, which is the reason this file exists.
//
// The row entries are the exception and they are in the matrix for that
// reason. Row<Es...> folds the underlying values of an append-only enum
// through pure arithmetic and reaches no reflected name. Those five lines
// are portable across toolchains, and a change that makes them move is a
// change that broke the one half of the federation key that peers on
// different compilers can still share.
//
// ── Ceremony discipline ────────────────────────────────────────────
//
// When an entry is added, removed or reordered:
//   1. Extend kEntries.
//   2. Re-roll kFoldAnchor to the new fold_anchor() value.
//   3. Re-run the binary and replace tools/row_hash_foundation_golden.txt
//      with its stdout.
//   4. Commit the tool and the golden together.
//
// A partial migration reddens the build twice. The anchor static_assert
// below fires at compile time when the matrix and the anchor disagree.
// The CTest entry row_hash_foundation_golden_diff fires when the matrix
// and the golden disagree.
//
// ── Reading a diff ─────────────────────────────────────────────────
//
// The header carries a toolchain tag, and that is deliberate even though
// it moves on a libstdc++ point release that renames nothing. A golden
// claims only that these values hold on one toolchain, and without the
// tag a reader who meets moved hashes cannot tell a regression from a
// different compiler. So the first line of the diff answers the question
// the rest of it raises.
//
// A diff in the tag alone, with all forty-one payload lines unchanged,
// means the toolchain moved and the fold did not. Recapture the golden
// and say which toolchain in the commit message. Nothing is broken.
//
// A diff in the payload lines is the case this guard exists for. If the
// R** lines moved, the portable half of the federation key stopped being
// portable, and that is a defect rather than a ceremony.

#include <fixy/Budgeted.h>
#include <fixy/EpochVersioned.h>
#include <fixy/Fn.h>
#include <fixy/Mutation.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Role.h>
#include <fixy/Secret.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/HappensBefore.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace fa = ::foundation::algebra;
namespace fd = ::foundation::diag;
namespace fe = ::foundation::effects;
namespace fl = ::foundation::algebra::lattices;

using fd::row_hash_contribution_v;

// A clock tag is part of the clock's identity.  The tag lives in a named
// namespace, because its reflected name enters the hash and an anonymous
// namespace has no name to print.
namespace row_hash_witness {
struct ReplayClock {};
}  // namespace row_hash_witness

namespace {

// ── The graded fold ────────────────────────────────────────────────
//
// Every one of these folds a lattice identity, which is a reflected name
// under the default trait. Both wrapper shapes appear: an alias for
// Graded and a class over the graded facade reach the same fold, because
// the fold matches on the published shape rather than on the template.
using G01_Linear = ::fixy::Linear<int>;
using G02_Affine = ::fixy::Affine<int>;
using G03_TaggedVerified = ::fixy::Tagged<int, ::fixy::tags::trust::Verified>;
using G04_TaggedUnverified = ::fixy::Tagged<int, ::fixy::tags::trust::Unverified>;
using G05_Secret = ::fixy::Secret<int>;
using G06_Stale = ::fixy::Stale<int>;
using G07_Monotonic = ::fixy::Monotonic<std::uint64_t>;

// A refinement carries its predicate as a template argument of its
// lattice, so these two lines witness that a predicate rename moves a
// hash. That is the case the old tree needed a separate pred_canonical_id
// trait to express.
using G08_RefinedPositive = ::fixy::Refined<::fixy::positive, int>;
using G09_RefinedNonNegative = ::fixy::Refined<::fixy::non_negative, int>;

// The two product-graded wrappers.  Each folds a product lattice, and the
// product's identity is built from the identities of its two components.
using G10_EpochVersioned = ::fixy::EpochVersioned<int>;
using G11_Budgeted = ::fixy::Budgeted<int>;

// ── The counter axes ───────────────────────────────────────────────
//
// The four axes are one lattice template under four tags, so the tag is
// the only input that separates them.  They must print four values.
template <typename L>
using OnAxis = fa::Graded<fa::ModalityKind::Absolute, L, int>;
using K01_OnEpoch = OnAxis<fl::EpochLattice>;
using K02_OnGeneration = OnAxis<fl::GenerationLattice>;
using K03_OnPeakBytes = OnAxis<fl::PeakBytesLattice>;
using K04_OnBitsBudget = OnAxis<fl::BitsBudgetLattice>;

// A version grades by the order dual, which is a different lattice from
// the counter it turns over and must take a different slot.
using K05_OnDualEpoch = OnAxis<fl::DualLattice<fl::EpochLattice>>;

// ── The vector clock ───────────────────────────────────────────────
//
// The width and the tag are both part of the key.  H01 and H02 differ
// only in the tag, and H01 and H03 differ only in the width.
using H01_ClockOfFour = OnAxis<fl::HappensBeforeLattice<4>>;
using H02_ClockOfFourTagged = OnAxis<fl::HappensBeforeLattice<4, row_hash_witness::ReplayClock>>;
using H03_ClockOfEight = OnAxis<fl::HappensBeforeLattice<8>>;

// ── Nesting order ──────────────────────────────────────────────────
//
// The combiner is order-sensitive, so a wrapper's position in the stack
// is part of the key. These two entries hold opposite nesting of one
// pair and must never print the same value.
using N01_StaleOfTagged = ::fixy::Stale<::fixy::Tagged<int, ::fixy::tags::trust::Verified>>;
using N02_TaggedOfStale = ::fixy::Tagged<::fixy::Stale<int>, ::fixy::tags::trust::Verified>;

// ── The row specialisation, which is the portable one ──────────────
//
// R04 and R05 spell one row in two orders and must print one value,
// because a row denotes a set. R01 is the empty row, which is a real row
// and so is seeded rather than zero.
using R01_RowEmpty = fe::Row<>;
using R02_RowBg = fe::Row<fe::Effect::Bg>;
using R03_RowIo = fe::Row<fe::Effect::IO>;
using R04_RowBgIo = fe::Row<fe::Effect::Bg, fe::Effect::IO>;
using R05_RowIoBg = fe::Row<fe::Effect::IO, fe::Effect::Bg>;

// ── The carrier ────────────────────────────────────────────────────
//
// C03 nests a carrier inside a carrier. The inner row must survive into
// the outer hash, so C03 must differ from the flattened forms above it.
using C01_CompEmpty = fe::Computation<fe::Row<>, int>;
using C02_CompBg = fe::Computation<fe::Row<fe::Effect::Bg>, int>;
using C03_CompNested = fe::Computation<fe::Row<fe::Effect::Bg>, fe::Computation<fe::Row<fe::Effect::IO>, int>>;

// ── The multi-axis binding ─────────────────────────────────────────
//
// B01 names no axis and means the strict pole on all thirty-three. B02
// spells the Security pole out, and the two are one claim under two
// spellings, so they must print one value. B03 pins the top of the same
// lattice and keeps a residual claim the pole does not carry, so it must
// differ. B06 moves a different axis.
//
// B07 moves the payload and must NOT move the hash. The Type axis folds
// the payload through row_hash_contribution, and a bare type contributes
// zero, so the binding is blind to it. Payload identity belongs to the
// content half of the cache key, which folds a stable_type_id per
// argument. B07 prints B01's value on purpose, and an assert below says
// so, because an unexplained repeat in the golden reads as a collision.
using B01_FnStrictPole = ::fixy::fn<int>;
using B02_FnClassified = ::fixy::fn<int, ::fixy::atom::as_classified>;
using B03_FnSecret = ::fixy::fn<int, ::fixy::atom::as_secret>;
using B04_FnInternal = ::fixy::fn<int, ::fixy::atom::as_internal>;
using B05_FnPublic = ::fixy::fn<int, ::fixy::atom::as_public>;
using B06_FnUnverified = ::fixy::fn<int, ::fixy::atom::trust_unverified>;
using B07_FnDoublePayload = ::fixy::fn<double>;

// ── The role compositions ──────────────────────────────────────────
//
// A role is a named stack of atoms over the same binding, so these
// entries witness the walk over several axes at once.
//
// Two of these repeat a value above, and both repeats are the point
// rather than a collision. S01 is the binding with no atom at all, so it
// prints B01. S05 spells an empty effect set and the Security top, and an
// empty effect set is that axis's own pole, so it prints B03. The asserts
// below hold both, and a reader who diffs the golden by hand needs them.
using S01_PureLinear = ::fixy::role::PureLinear<int>;
using S02_PureCopy = ::fixy::role::PureCopy<int>;
using S03_IoFunction = ::fixy::role::IoFunction<int>;
using S04_BgWorker = ::fixy::role::BgWorker<int>;
using S05_CtCrypto = ::fixy::role::CtCrypto<int>;

struct LabeledEntry {
    const char* label;
    std::uint64_t value;
};

inline constexpr std::array<LabeledEntry, 41> kEntries = {{
    {"G01_Linear", row_hash_contribution_v<G01_Linear>},
    {"G02_Affine", row_hash_contribution_v<G02_Affine>},
    {"G03_TaggedVerified", row_hash_contribution_v<G03_TaggedVerified>},
    {"G04_TaggedUnverified", row_hash_contribution_v<G04_TaggedUnverified>},
    {"G05_Secret", row_hash_contribution_v<G05_Secret>},
    {"G06_Stale", row_hash_contribution_v<G06_Stale>},
    {"G07_Monotonic", row_hash_contribution_v<G07_Monotonic>},
    {"G08_RefinedPositive", row_hash_contribution_v<G08_RefinedPositive>},
    {"G09_RefinedNonNegative", row_hash_contribution_v<G09_RefinedNonNegative>},
    {"N01_StaleOfTagged", row_hash_contribution_v<N01_StaleOfTagged>},
    {"N02_TaggedOfStale", row_hash_contribution_v<N02_TaggedOfStale>},
    {"R01_RowEmpty", row_hash_contribution_v<R01_RowEmpty>},
    {"R02_RowBg", row_hash_contribution_v<R02_RowBg>},
    {"R03_RowIo", row_hash_contribution_v<R03_RowIo>},
    {"R04_RowBgIo", row_hash_contribution_v<R04_RowBgIo>},
    {"R05_RowIoBg", row_hash_contribution_v<R05_RowIoBg>},
    {"C01_CompEmpty", row_hash_contribution_v<C01_CompEmpty>},
    {"C02_CompBg", row_hash_contribution_v<C02_CompBg>},
    {"C03_CompNested", row_hash_contribution_v<C03_CompNested>},
    {"B01_FnStrictPole", row_hash_contribution_v<B01_FnStrictPole>},
    {"B02_FnClassified", row_hash_contribution_v<B02_FnClassified>},
    {"B03_FnSecret", row_hash_contribution_v<B03_FnSecret>},
    {"B04_FnInternal", row_hash_contribution_v<B04_FnInternal>},
    {"B05_FnPublic", row_hash_contribution_v<B05_FnPublic>},
    {"B06_FnUnverified", row_hash_contribution_v<B06_FnUnverified>},
    {"B07_FnDoublePayload", row_hash_contribution_v<B07_FnDoublePayload>},
    {"S01_PureLinear", row_hash_contribution_v<S01_PureLinear>},
    {"S02_PureCopy", row_hash_contribution_v<S02_PureCopy>},
    {"S03_IoFunction", row_hash_contribution_v<S03_IoFunction>},
    {"S04_BgWorker", row_hash_contribution_v<S04_BgWorker>},
    {"S05_CtCrypto", row_hash_contribution_v<S05_CtCrypto>},
    {"G10_EpochVersioned", row_hash_contribution_v<G10_EpochVersioned>},
    {"G11_Budgeted", row_hash_contribution_v<G11_Budgeted>},
    {"K01_OnEpoch", row_hash_contribution_v<K01_OnEpoch>},
    {"K02_OnGeneration", row_hash_contribution_v<K02_OnGeneration>},
    {"K03_OnPeakBytes", row_hash_contribution_v<K03_OnPeakBytes>},
    {"K04_OnBitsBudget", row_hash_contribution_v<K04_OnBitsBudget>},
    {"K05_OnDualEpoch", row_hash_contribution_v<K05_OnDualEpoch>},
    {"H01_ClockOfFour", row_hash_contribution_v<H01_ClockOfFour>},
    {"H02_ClockOfFourTagged", row_hash_contribution_v<H02_ClockOfFourTagged>},
    {"H03_ClockOfEight", row_hash_contribution_v<H03_ClockOfEight>},
}};

inline constexpr std::size_t kEntryCount = kEntries.size();

// ── The anchor ─────────────────────────────────────────────────────
//
// One fold over every entry in order. A change in the count, in the
// order, or in any single hash moves this value and reddens the build
// before the golden diff runs, with the ceremony named in the message.
inline constexpr std::uint64_t kFoldSeed = 0xF0117A11EDA11A5EULL;
inline constexpr std::uint64_t kFoldAnchor = 0x398b036ee8547c85ULL;

[[nodiscard]] consteval std::uint64_t fold_anchor() noexcept {
    std::uint64_t acc = kFoldSeed;
    for (auto const& entry : kEntries) {
        acc = ::foundation::reflect::combine_ids(acc, entry.value);
    }
    return acc;
}

static_assert(fold_anchor() == kFoldAnchor,
              "the matrix and the anchor disagree. An entry was added, removed or "
              "reordered, or a row_hash_contribution specialisation moved. Re-roll "
              "kFoldAnchor to the new fold_anchor() value, recapture "
              "tools/row_hash_foundation_golden.txt from this binary, and commit both "
              "together. The ceremony is at the top of this file.");

// ── The properties a reader of the golden should not have to re-derive ──
//
// These hold within this build, and the golden holds them across builds.
// They are here rather than only in the test tree because a reader who
// diffs the golden by hand needs to know which lines are allowed to
// agree and which are not.

// One row, two spellings, one slot. A row denotes a set of atoms.
static_assert(row_hash_contribution_v<R04_RowBgIo> == row_hash_contribution_v<R05_RowIoBg>,
              "two spellings of one row must take one slot");

// The empty row is a real row and is not the zero a bare type carries.
static_assert(row_hash_contribution_v<R01_RowEmpty> != 0, "the empty row must not alias a bare payload");
static_assert(row_hash_contribution_v<int> == 0, "a bare type carries no row");

// Nesting order is part of the key.
static_assert(row_hash_contribution_v<N01_StaleOfTagged> != row_hash_contribution_v<N02_TaggedOfStale>,
              "opposite nesting of one pair must take two slots");

// A predicate rename moves a hash, which is why a rename needs a
// lattice_canonical_id specialisation to keep reaching cached entries.
static_assert(row_hash_contribution_v<G08_RefinedPositive> != row_hash_contribution_v<G09_RefinedNonNegative>,
              "two refinements must take two slots");

// The Security pole and the atom that spells it out are one claim.
static_assert(row_hash_contribution_v<B01_FnStrictPole> == row_hash_contribution_v<B02_FnClassified>,
              "the strict Security pole and as_classified must take one slot");
static_assert(row_hash_contribution_v<B01_FnStrictPole> != row_hash_contribution_v<B03_FnSecret>,
              "as_secret keeps a residual claim the pole does not carry");

// A carrier does not collapse over a payload that carries a row.
static_assert(row_hash_contribution_v<C03_CompNested> != row_hash_contribution_v<C02_CompBg>,
              "a nested carrier must keep the inner row in the outer hash");

// The three intended repeats in the golden. Each one is a property of the
// fold, and naming it here is what keeps a later reader from filing a
// collision against a line that is doing its job.
static_assert(row_hash_contribution_v<B07_FnDoublePayload> == row_hash_contribution_v<B01_FnStrictPole>,
              "the binding is blind to a bare payload, which belongs to the content "
              "half of the cache key");
static_assert(row_hash_contribution_v<S01_PureLinear> == row_hash_contribution_v<B01_FnStrictPole>,
              "PureLinear names no atom, so it is the strict pole on every axis");
static_assert(row_hash_contribution_v<S05_CtCrypto> == row_hash_contribution_v<B03_FnSecret>,
              "an empty effect set is the Effect pole, so CtCrypto resolves to the "
              "Security top alone");

// Two values are reserved, and no entry of this matrix may take either.
//
// Zero is the bare-type baseline. crucible/Types.h calls RowHash{0} a
// valid key and the row value with the most traffic in the tree, so zero
// is not forbidden in general. It is forbidden here, because every entry
// below is a wrapper, a row, a carrier or a binding rather than a bare
// type. An entry at zero means the fold failed to reach that type and
// handed it the primary template, and that type then shares one slot with
// every plain payload. A secret value and a plain one would come to share
// a cache entry.
//
// UINT64_MAX is the strong-hash sentinel, and Types.h says it is the one
// value guaranteed never to be produced. An entry there is
// indistinguishable from an unaddressed slot.
[[nodiscard]] consteval bool no_reserved_values() noexcept {
    for (auto const& entry : kEntries) {
        if (entry.value == 0) return false;
        if (entry.value == static_cast<std::uint64_t>(-1)) return false;
    }
    return true;
}

static_assert(no_reserved_values(), "an entry took a reserved row_hash value. Zero means the fold missed that "
                                    "type and handed it the bare-type baseline, so it shares one slot with every "
                                    "plain payload. UINT64_MAX is the EMPTY-slot sentinel, so the entry is "
                                    "indistinguishable from an unaddressed cache slot.");

// The tag is the only input that separates the four counter axes, and the
// tag and the width are both inputs of a clock.  A value graded on one of
// these must never reach a cache entry filed under another.
[[nodiscard]] consteval bool counters_and_clocks_are_distinct() noexcept {
    std::uint64_t const values[] = {
        row_hash_contribution_v<K01_OnEpoch>,     row_hash_contribution_v<K02_OnGeneration>,
        row_hash_contribution_v<K03_OnPeakBytes>, row_hash_contribution_v<K04_OnBitsBudget>,
        row_hash_contribution_v<K05_OnDualEpoch>,
        row_hash_contribution_v<H01_ClockOfFour>, row_hash_contribution_v<H02_ClockOfFourTagged>,
        row_hash_contribution_v<H03_ClockOfEight>,
    };
    for (std::size_t i = 0; i < std::size(values); ++i) {
        for (std::size_t j = i + 1; j < std::size(values); ++j) {
            if (values[i] == values[j]) return false;
        }
    }
    return true;
}

static_assert(counters_and_clocks_are_distinct(), "two counter axes, or two clocks that differ in tag or width, "
                                                  "took one slot");

// Every entry is distinct except the repeats named above.
[[nodiscard]] consteval std::size_t distinct_value_count() noexcept {
    std::size_t distinct = 0;
    for (std::size_t i = 0; i < kEntryCount; ++i) {
        bool seen_before = false;
        for (std::size_t j = 0; j < i; ++j) {
            if (kEntries[j].value == kEntries[i].value) seen_before = true;
        }
        if (!seen_before) ++distinct;
    }
    return distinct;
}

// Forty-one entries carry thirty-six distinct values. Five entries repeat
// one that stands above them: R05 repeats R04, B02 and B07 and S01 each
// repeat B01, and S05 repeats B03. Every one of those five has its own
// assert above, with the property that makes the repeat correct.
static_assert(distinct_value_count() == 36,
              "the number of distinct values moved. Every repeat in this matrix is "
              "named by an assert above, so a new one is a collision between two "
              "claims that must not share a cache slot.");

}  // namespace

int main() {
    // The header is a block of `#`-prefixed lines, so a consumer can
    // ignore comments and diff the payload alone. The toolchain tag is
    // printed because it is the discriminator a cross-toolchain
    // federation keys through, and a reader comparing two goldens needs
    // to know whether the two builds shared a toolchain.
    std::printf("# foundation row_hash cross-build witness\n");
    std::printf("# format-version:  1\n");
    std::printf("# matrix-size:     %zu\n", kEntryCount);
    std::printf("# fold-seed:       0x%016" PRIx64 "\n", kFoldSeed);
    std::printf("# fold-anchor:     0x%016" PRIx64 "\n", kFoldAnchor);
    std::printf("# toolchain-tag:   0x%016" PRIx64 "\n", fd::federation_toolchain_tag());
    std::printf("# (a diff means a row_hash_contribution specialisation moved. "
                "Name the ceremony in the commit message and recapture this golden.)\n");
    std::printf("# (the R** lines fold no reflected name and are portable across "
                "toolchains. Every other line is not.)\n");
    for (auto const& entry : kEntries) {
        std::printf("%-24s 0x%016" PRIx64 "\n", entry.label, entry.value);
    }
    return 0;
}
