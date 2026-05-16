// SPDX-License-Identifier: Apache-2.0
//
// test/test_fixy_theory.cpp
//
// FIXY-D Phase D sentinel — pins the §30.14 Known-Unsoundness corpus
// in include/crucible/fixy/Theory.h.  Acceptance per
// misc/16_05_2026_fixy.md §4 Phase D + §9 R6:
//
//   - Catalog cardinality stable at 15 entries (10 academic + 5
//     Crucible-audit) — adding entries is monotonic; removing is
//     forbidden.
//   - Every entry's pattern tag is a unique type.
//   - Every entry's cite literal is non-empty.
//
// This file is the consumer-side load-bearing test: a future
// maintainer who accidentally removes a corpus entry breaks the
// build here, before it reaches review.

#include <crucible/fixy/Theory.h>

#include <cstdio>
#include <tuple>
#include <type_traits>

namespace {

namespace ct = crucible::fixy::theory;
namespace cp = crucible::fixy::theory::pattern;
namespace cs = crucible::safety::fn;
namespace cfx = crucible::effects;

// ─── 1. Catalog cardinality ──────────────────────────────────────────

static_assert(ct::corpus_size_v == 15,
    "Theory corpus regression: expected 15 seeded entries "
    "(10 academic + 5 Crucible-audit).");

static_assert(std::tuple_size_v<ct::Catalog> == 15);

// ─── 2. Per-entry citation non-empty + pattern tag uniqueness ────────

// Spot-check a few academic citations carry author/year/paper.
static_assert(ct::cite_atkey_2018_lam.find("Atkey 2018") != std::string_view::npos);
static_assert(ct::cite_caires_pfenning_2010.find("Caires & Pfenning 2010")
              != std::string_view::npos);
static_assert(ct::cite_bsyz22.find("CONCUR") != std::string_view::npos);

// Spot-check Crucible-audit citations carry the GAPS-* tag.
static_assert(ct::cite_gaps_001.find("GAPS-001") != std::string_view::npos);
static_assert(ct::cite_gaps_003.find("GAPS-003") != std::string_view::npos);
static_assert(ct::cite_gaps_010.find("GAPS-010") != std::string_view::npos);
static_assert(ct::cite_gaps_013.find("GAPS-013") != std::string_view::npos);
static_assert(ct::cite_gaps_017.find("GAPS-017") != std::string_view::npos);

// Pattern tag uniqueness — sampled cross-pairs.  A full N² matrix
// would balloon the test; the spot check catches accidental copy-
// paste tag-name collisions.
static_assert(!std::is_same_v<cp::atkey_2018_lam_double_use,
                              cp::caires_pfenning_2010_implicit_flow>);
static_assert(!std::is_same_v<cp::bsyz22_crash_stop_partial_view,
                              cp::ccdr14_fractional_overallocation>);
static_assert(!std::is_same_v<cp::gpsy23_async_subtype_unbounded,
                              cp::honda_yoshida_2008_proj_drop_role>);
static_assert(!std::is_same_v<cp::gay_hole_2005_subtype_branching,
                              cp::atkey_lindley_2009_effect_row_leak>);
static_assert(!std::is_same_v<cp::krishnaswami_2014_capability_replay,
                              cp::krishnaswami_2017_staleness_ct_channel>);
static_assert(!std::is_same_v<cp::gaps_001_session_global_stopg_proj,
                              cp::gaps_003_crashwatched_perm_leak>);
static_assert(!std::is_same_v<cp::gaps_010_monotonic_concurrent_no_atomic,
                              cp::gaps_013_decimal_overflow_wrap>);

// ─── 3. theory_entry round-trip ──────────────────────────────────────
//
// A theory_entry instance's pattern_tag and cite must round-trip via
// std::tuple_element / decltype.

using FirstEntry = std::tuple_element_t<0, ct::Catalog>;
static_assert(std::is_same_v<FirstEntry::pattern_tag,
                             cp::atkey_2018_lam_double_use>);
static_assert(FirstEntry::cite.find("Atkey 2018") != std::string_view::npos);

using LastEntry = std::tuple_element_t<14, ct::Catalog>;
static_assert(std::is_same_v<LastEntry::pattern_tag,
                             cp::gaps_017_capability_replay_session>);
static_assert(LastEntry::cite.find("GAPS-017") != std::string_view::npos);

// ─── 4. Cite hash stability + uniqueness ─────────────────────────────
//
// Each entry hashes to a non-zero 64-bit identifier; adjacent entries
// produce distinct hashes.  A federation cache slot keyed on the
// hash inherits stability from this static_assert family.

static_assert(ct::theory_cite_hash_v<std::tuple_element_t<0, ct::Catalog>> != 0,
    "Theory cite hash must be non-zero — fnv1a + fmix64 distortion.");

static_assert(ct::theory_cite_hash_v<std::tuple_element_t<0, ct::Catalog>> !=
              ct::theory_cite_hash_v<std::tuple_element_t<1, ct::Catalog>>,
    "Theory cite hash collision between two distinct corpus entries — "
    "investigate fmix64 stability.");

// Hash is structural — same Entry type produces the same hash twice.
static_assert(ct::theory_cite_hash_v<FirstEntry> ==
              ct::theory_cite_hash_v<FirstEntry>,
    "theory_cite_hash_v must be a pure function of Entry type.");

// ─── 5. Pattern matcher behaviour ────────────────────────────────────
//
// `which_pattern_matches<F>()` returns the first matching corpus
// entry's cite, or empty if no pattern matches.  The matcher is
// purely structural: it inspects Fn axes (mutation_v, effect_row_t,
// usage_v, security_v, ...) without re-running ValidComposition.

// (a) DefaultFn — substrate's healthiest baseline.  No pattern matches.
using DefaultFn = cs::Fn<int>;
static_assert(ct::which_pattern_matches<DefaultFn>().empty(),
    "DefaultFn must not match any corpus entry.");

// (b) Monotonic+Bg+Atomic — the M012 FIX shape.  Matcher fires
// (neighborhood classification) and returns gaps_010 cite.
using MonotonicBgAtomic = cs::Fn<int,
    cs::pred::True,
    cs::UsageMode::Linear,
    cfx::Row<cfx::Effect::Bg>,
    cs::SecLevel::Classified,
    cs::proto::None,
    cs::lifetime::Static,
    crucible::safety::source::FromInternal,
    crucible::safety::trust::Verified,
    cs::ReprKind::Atomic,
    cs::cost::Unstated,
    cs::precision::Exact,
    cs::space::Zero,
    cs::OverflowMode::Trap,
    cs::MutationMode::Monotonic,
    cs::ReentrancyMode::NonReentrant,
    cs::size_pol::Unstated,
    1,
    cs::stale::Fresh>;

static_assert(ct::matches<cp::gaps_010_monotonic_concurrent_no_atomic,
                          MonotonicBgAtomic>::value,
    "Monotonic+Bg shape must match gaps_010 pattern.");

static_assert(ct::which_pattern_matches<MonotonicBgAtomic>() == ct::cite_gaps_010,
    "First-match classifier must return gaps_010 cite on Monotonic+Bg shape.");

// (c) Non-matching shapes do not lie: the 10 academic patterns that
// require flow-sensitive analysis return false on every well-formed
// Fn — they're documented as guidebook entries.
static_assert(!ct::matches<cp::atkey_2018_lam_double_use, DefaultFn>::value,
    "Flow-sensitive pattern matchers must return false (not lie about coverage).");
static_assert(!ct::matches<cp::bsyz22_crash_stop_partial_view, DefaultFn>::value);
static_assert(!ct::matches<cp::honda_yoshida_2008_proj_drop_role, DefaultFn>::value);

}  // namespace

int main() {
    std::printf("fixy theory sentinel: corpus_size=%zu first_hash=0x%016lx\n",
                ct::corpus_size_v,
                static_cast<unsigned long>(
                    ct::theory_cite_hash_v<std::tuple_element_t<0, ct::Catalog>>));
    return 0;
}
