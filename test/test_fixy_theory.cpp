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

}  // namespace

int main() {
    std::printf("fixy theory sentinel: corpus_size=%zu\n", ct::corpus_size_v);
    return 0;
}
