#include <crucible/MerkleDag.h>
#include <crucible/effects/Capabilities.h>
#include "test_assert.h"
#include <cstdio>
#include <cstring>

using crucible::SchemaHash;
using crucible::ContentHash;
using crucible::MerkleHash;

int main() {
  crucible::effects::Test test;
  crucible::Arena arena(1 << 16);

  // Test compute_storage_nbytes
  crucible::TensorMeta m{};
  m.ndim = 2;
  m.sizes[0] = 32;
  m.sizes[1] = 64;
  m.strides[0] = 64;
  m.strides[1] = 1;
  m.dtype = crucible::ScalarType::Float;
  // compute_storage_nbytes returns Saturated<uint64_t> (#1018).  Happy
  // path: clamp flag is false, value matches the bare-arithmetic answer.
  auto nbytes = crucible::compute_storage_nbytes(m);
  // (31*64 + 63*1 + 1) * 4 = 2048 * 4 = 8192 (= 32 * 64 * sizeof(float))
  assert(nbytes.value() == 8192);
  assert(!nbytes.was_clamped() && "well-formed tensor must not saturate");

  // Overflow path: sizes × strides that wrap int64_t.  The pre-fix
  // implementation silently wrapped to a tiny value; the migrated
  // code returns Saturated<uint64_t>{UINT64_MAX, true} so downstream
  // alloc/recording rejects cleanly AND the clamp flag is observable
  // at the call site.
  {
    crucible::TensorMeta huge{};
    huge.ndim = 2;
    huge.sizes[0]   = 1LL << 32;   // 4G elements
    huge.strides[0] = 1LL << 32;   // × 4G stride → 2^64, overflows i64
    huge.sizes[1]   = 1;
    huge.strides[1] = 1;
    huge.dtype = crucible::ScalarType::Float;
    auto nb = crucible::compute_storage_nbytes(huge);
    assert(nb.value() == UINT64_MAX && "huge tensor must saturate value to UINT64_MAX");
    assert(nb.was_clamped() && "huge tensor must carry clamped=true");
  }

  // Test make_region
  crucible::TraceEntry ops[3]{};
  ops[0].schema_hash = SchemaHash{0xAABB};
  ops[1].schema_hash = SchemaHash{0xCCDD};
  ops[2].schema_hash = SchemaHash{0xEEFF};
  auto* region = crucible::make_region(test.alloc, arena, ops, 3);
  assert(region != nullptr);
  assert(region->kind == crucible::TraceNodeKind::REGION);
  assert(region->num_ops == 3);
  assert(region->ops[0].schema_hash == SchemaHash{0xAABB});
  assert(region->ops[2].schema_hash == SchemaHash{0xEEFF});
  assert(region->first_op_schema == SchemaHash{0xAABB});
  assert(static_cast<bool>(region->content_hash));

  // Test compute_content_hash determinism
  ContentHash h1 = region->content_hash;
  auto* region2 = crucible::make_region(test.alloc, arena, ops, 3);
  assert(region2->content_hash == h1);

  // Test make_terminal
  auto* terminal = crucible::make_terminal(test.alloc, arena);
  assert(terminal->kind == crucible::TraceNodeKind::TERMINAL);
  assert(terminal->next == nullptr);

  // Test recompute_merkle
  region->next = terminal;
  crucible::recompute_merkle(region);
  assert(static_cast<bool>(region->merkle_hash));

  // Test KernelCache.  FOUND-I05: lookup/insert keys are now
  // (ContentHash, RowHash) pairs.  RowHash{0} is the bare-type
  // baseline (RowHashFold.h §3) — the migration target for every
  // legacy untyped call site.
  crucible::KernelCache cache;
  using crucible::RowHash;
  assert(cache.lookup(ContentHash{0x1234}, RowHash{0}) == nullptr);
  struct FakeKernel { int x; };
  FakeKernel fk{42};
  assert(cache.insert(ContentHash{0x1234}, RowHash{0},
                      reinterpret_cast<crucible::CompiledKernel*>(&fk)).has_value());
  assert(cache.lookup(ContentHash{0x1234}, RowHash{0})
      == reinterpret_cast<crucible::CompiledKernel*>(&fk));
  // Duplicate insert: overwrites to newer variant under the same row.
  FakeKernel fk2{99};
  assert(cache.insert(ContentHash{0x1234}, RowHash{0},
                      reinterpret_cast<crucible::CompiledKernel*>(&fk2)).has_value());
  assert(cache.lookup(ContentHash{0x1234}, RowHash{0})
      == reinterpret_cast<crucible::CompiledKernel*>(&fk2));

  // FOUND-I05 row-discrimination: identical content_hash with a
  // distinct row_hash MUST cache to a different slot.  Prove that
  // (0x1234, row=0xAA) does not alias the (0x1234, row=0) slot.
  FakeKernel fk_row{777};
  assert(cache.lookup(ContentHash{0x1234}, RowHash{0xAA}) == nullptr);
  assert(cache.insert(ContentHash{0x1234}, RowHash{0xAA},
                      reinterpret_cast<crucible::CompiledKernel*>(&fk_row)).has_value());
  assert(cache.lookup(ContentHash{0x1234}, RowHash{0xAA})
      == reinterpret_cast<crucible::CompiledKernel*>(&fk_row));
  // Original row=0 entry is preserved — no cross-row pollution.
  assert(cache.lookup(ContentHash{0x1234}, RowHash{0})
      == reinterpret_cast<crucible::CompiledKernel*>(&fk2));
  // A third row queries cleanly to a distinct empty slot.
  assert(cache.lookup(ContentHash{0x1234}, RowHash{0xBB}) == nullptr);

  // FOUND-I05 variant-update-pins-row invariant: an insert under
  // (0x1234, RowHash{0xAA}) must REPLACE the kernel at that slot,
  // NOT touch the (0x1234, RowHash{0}) sibling.  Proves the
  // probe correctly distinguishes variant update from sibling row.
  FakeKernel fk_row_v2{888};
  assert(cache.insert(ContentHash{0x1234}, RowHash{0xAA},
                      reinterpret_cast<crucible::CompiledKernel*>(&fk_row_v2)).has_value());
  assert(cache.lookup(ContentHash{0x1234}, RowHash{0xAA})
      == reinterpret_cast<crucible::CompiledKernel*>(&fk_row_v2));   // updated
  assert(cache.lookup(ContentHash{0x1234}, RowHash{0})
      == reinterpret_cast<crucible::CompiledKernel*>(&fk2));         // unchanged

  // Sibling-row insertion under DIFFERENT content_hash also lands
  // in its own slot (sanity check that the row-discrimination is
  // not somehow bound to a specific content_hash slot index).
  FakeKernel fk_other{555};
  assert(cache.insert(ContentHash{0x9999}, RowHash{0xAA},
                      reinterpret_cast<crucible::CompiledKernel*>(&fk_other)).has_value());
  assert(cache.lookup(ContentHash{0x9999}, RowHash{0xAA})
      == reinterpret_cast<crucible::CompiledKernel*>(&fk_other));
  // Cross-content-cross-row queries are clean misses.
  assert(cache.lookup(ContentHash{0x9999}, RowHash{0})    == nullptr);
  assert(cache.lookup(ContentHash{0x1234}, RowHash{0xCC}) == nullptr);

  // ── FOUND-I05-AUDIT — sentinel row + table-full + many-rows ──
  //
  // Sentinel row (RowHash::sentinel() == UINT64_MAX) is a valid
  // lookup target — RowHash is a strong type whose sentinel()
  // factory is reserved for end-of-region markers, but at the
  // KernelCache level it is just another 64-bit row identity.
  // The cache must NOT treat it specially; it is a row like any
  // other.
  FakeKernel fk_sentinel{0xFEED};
  assert(cache.lookup(ContentHash{0x1234}, RowHash::sentinel()) == nullptr);
  assert(cache.insert(ContentHash{0x1234}, RowHash::sentinel(),
                      reinterpret_cast<crucible::CompiledKernel*>(&fk_sentinel)).has_value());
  assert(cache.lookup(ContentHash{0x1234}, RowHash::sentinel())
      == reinterpret_cast<crucible::CompiledKernel*>(&fk_sentinel));
  // Sentinel-row entry does NOT alias any other (C, R).
  assert(cache.lookup(ContentHash{0x1234}, RowHash{0})
      == reinterpret_cast<crucible::CompiledKernel*>(&fk2));         // unchanged
  assert(cache.lookup(ContentHash{0x1234}, RowHash{0xAA})
      == reinterpret_cast<crucible::CompiledKernel*>(&fk_row_v2));   // unchanged

  // Many rows under same content_hash — exercises probe-chain
  // wraparound under row pressure.  Insert N distinct rows
  // sharing one content_hash and verify each is independently
  // retrievable.
  {
    crucible::KernelCache row_pressure_cache(/*capacity=*/16);
    constexpr uint32_t N = 8;  // half the table — leaves room for probes
    FakeKernel row_kernels[N];
    for (uint32_t i = 0; i < N; ++i) {
      row_kernels[i] = FakeKernel{static_cast<int>(0x1000 + i)};
      assert(row_pressure_cache.insert(
          ContentHash{0xABCDEF},                          // ALL share one content
          RowHash{0x100 + i},                             // distinct rows
          reinterpret_cast<crucible::CompiledKernel*>(&row_kernels[i])).has_value());
    }
    for (uint32_t i = 0; i < N; ++i) {
      auto* k = row_pressure_cache.lookup(ContentHash{0xABCDEF}, RowHash{0x100 + i});
      assert(k == reinterpret_cast<crucible::CompiledKernel*>(&row_kernels[i]));
    }
    // A row not in the set queries cleanly to nullptr — proves
    // the probe correctly terminates without false-positive on
    // a foreign-row sibling.
    assert(row_pressure_cache.lookup(ContentHash{0xABCDEF}, RowHash{0xDEAD}) == nullptr);
  }

  // TableFull under row pressure — cap=4, insert 5 distinct
  // (C, R) pairs under SAME content; the 5th must return TableFull.
  {
    crucible::KernelCache tiny_cache(/*capacity=*/4);
    FakeKernel small_kernels[5];
    for (uint32_t i = 0; i < 4; ++i) {
      small_kernels[i] = FakeKernel{static_cast<int>(i)};
      assert(tiny_cache.insert(
          ContentHash{0x42},
          RowHash{0xA00 + i},
          reinterpret_cast<crucible::CompiledKernel*>(&small_kernels[i])).has_value());
    }
    // Fifth insert finds NO empty slot (all 4 occupied by
    // (0x42, R_i) pairs with R_i != lookup_row); returns TableFull.
    small_kernels[4] = FakeKernel{4};
    auto fifth = tiny_cache.insert(
        ContentHash{0x42},
        RowHash{0xA04},                                   // novel row
        reinterpret_cast<crucible::CompiledKernel*>(&small_kernels[4]));
    assert(!fifth.has_value());
    assert(fifth.error() == crucible::KernelCache::InsertError::TableFull);
    // Variant update of an existing slot still succeeds — TableFull
    // is only returned when no (content, row) match exists.
    FakeKernel variant_replacement{0xC};
    auto variant = tiny_cache.insert(
        ContentHash{0x42},
        RowHash{0xA00},                                   // existing row
        reinterpret_cast<crucible::CompiledKernel*>(&variant_replacement));
    assert(variant.has_value());
    assert(tiny_cache.lookup(ContentHash{0x42}, RowHash{0xA00})
        == reinterpret_cast<crucible::CompiledKernel*>(&variant_replacement));
  }

  // ── FOUND-I05-AUDIT-2 — RowHash{0} is a first-class lookup target ──
  //
  // Regression test for the silent-lost-insert bug: prior to the
  // kernel-spin invariant fix, an insert(C, RowHash{0}, K) racing
  // against a CLAIMED-but-mid-publish slot for content C could
  // mistakenly variant-update the still-claiming inserter's slot,
  // causing B's K to be overwritten by A's eventual publish.
  //
  // Single-threaded version exercises the SAME observation that the
  // multi-threaded race produces: insert(C, 0, K1), then insert(C, R, K2)
  // for R != 0, then insert(C, 0, K3) (variant update on the row=0
  // slot).  The fix guarantees K3 lands at the (C, 0) slot, NOT at
  // the (C, R) slot.
  {
    crucible::KernelCache audit2_cache(/*capacity=*/16);
    FakeKernel k_row0_v1{1};
    FakeKernel k_rowR_v1{2};
    FakeKernel k_row0_v2{3};

    // Step 1: insert (C, 0, K1).
    assert(audit2_cache.insert(ContentHash{0x55AA}, RowHash{0},
        reinterpret_cast<crucible::CompiledKernel*>(&k_row0_v1)).has_value());

    // Step 2: insert (C, 0xBEEF, K2) — distinct row, must land at
    // a different slot than the (C, 0) slot.
    assert(audit2_cache.insert(ContentHash{0x55AA}, RowHash{0xBEEF},
        reinterpret_cast<crucible::CompiledKernel*>(&k_rowR_v1)).has_value());

    // Both lookups return the slot-specific kernel.
    assert(audit2_cache.lookup(ContentHash{0x55AA}, RowHash{0})
        == reinterpret_cast<crucible::CompiledKernel*>(&k_row0_v1));
    assert(audit2_cache.lookup(ContentHash{0x55AA}, RowHash{0xBEEF})
        == reinterpret_cast<crucible::CompiledKernel*>(&k_rowR_v1));

    // Step 3: variant-update the (C, 0) slot with K3.  The kernel
    // at the (C, 0) slot must change; the (C, 0xBEEF) slot must
    // be untouched.
    assert(audit2_cache.insert(ContentHash{0x55AA}, RowHash{0},
        reinterpret_cast<crucible::CompiledKernel*>(&k_row0_v2)).has_value());

    // Post-condition: (C, 0) → K3, (C, 0xBEEF) → K2.  If the
    // pre-fix code had instead spun on row_hash and mistakenly
    // landed on the (C, 0xBEEF) slot under a hypothetical
    // CLAIMED-state observation, this assertion would fail.
    assert(audit2_cache.lookup(ContentHash{0x55AA}, RowHash{0})
        == reinterpret_cast<crucible::CompiledKernel*>(&k_row0_v2));
    assert(audit2_cache.lookup(ContentHash{0x55AA}, RowHash{0xBEEF})
        == reinterpret_cast<crucible::CompiledKernel*>(&k_rowR_v1));
  }

  // Test element_size — returns ElementBytes strong type (#129).
  assert(crucible::element_size(crucible::ScalarType::Float)         == crucible::ElementBytes{4});
  assert(crucible::element_size(crucible::ScalarType::Double)        == crucible::ElementBytes{8});
  assert(crucible::element_size(crucible::ScalarType::Half)          == crucible::ElementBytes{2});
  assert(crucible::element_size(crucible::ScalarType::Byte)          == crucible::ElementBytes{1});
  assert(crucible::element_size(crucible::ScalarType::ComplexDouble) == crucible::ElementBytes{16});

  // ═══ LoopNode tests ═════════════════════════════════════════════

  // Build a body sub-DAG: region1 -> region2 -> terminal
  crucible::TraceEntry body_ops1[2]{};
  body_ops1[0].schema_hash = SchemaHash{0x1111};
  body_ops1[1].schema_hash = SchemaHash{0x2222};
  auto* body_r1 = crucible::make_region(test.alloc, arena, body_ops1, 2);

  crucible::TraceEntry body_ops2[1]{};
  body_ops2[0].schema_hash = SchemaHash{0x3333};
  auto* body_r2 = crucible::make_region(test.alloc, arena, body_ops2, 1);

  auto* body_term = crucible::make_terminal(test.alloc, arena);
  body_r1->next = body_r2;
  body_r2->next = body_term;

  // Compute body content hash from the chain
  ContentHash body_ch = crucible::compute_body_content_hash(body_r1);
  assert(static_cast<bool>(body_ch));

  // Body content hash is deterministic
  assert(crucible::compute_body_content_hash(body_r1) == body_ch);

  // make_loop: Repeat(4) with one feedback edge
  crucible::FeedbackEdge fb_edges[1]{};
  fb_edges[0].output_idx = 0;
  fb_edges[0].input_idx = 0;

  auto* loop = crucible::make_loop(
      test.alloc, arena, body_r1, body_ch,
      fb_edges, 1,
      crucible::LoopTermKind::REPEAT, 4);

  assert(loop->kind == crucible::TraceNodeKind::LOOP);
  assert(loop->body == body_r1);
  assert(loop->body_content_hash == body_ch);
  assert(loop->num_feedback == 1);
  assert(loop->feedback_edges[0].output_idx == 0);
  assert(loop->term_kind == crucible::LoopTermKind::REPEAT);
  assert(loop->repeat_count == 4);
  assert(std::bit_cast<uint32_t>(loop->epsilon) == 0);
  static_assert(sizeof(crucible::LoopNode) == 64);
  static_assert(sizeof(crucible::FeedbackEdge) == 4);

  // Wire: loop -> final_region -> terminal2
  crucible::TraceEntry final_ops[1]{};
  final_ops[0].schema_hash = SchemaHash{0x4444};
  auto* final_region = crucible::make_region(test.alloc, arena, final_ops, 1);
  auto* dag_term = crucible::make_terminal(test.alloc, arena);
  final_region->next = dag_term;
  loop->next = final_region;

  // Merkle hash covers loop body, feedback, termination, and continuation
  crucible::recompute_merkle(loop);
  MerkleHash loop_merkle = loop->merkle_hash;
  assert(static_cast<bool>(loop_merkle));

  // Different repeat count → different merkle hash
  auto* loop2 = crucible::make_loop(
      test.alloc, arena, body_r1, body_ch,
      fb_edges, 1,
      crucible::LoopTermKind::REPEAT, 8);
  loop2->next = final_region;
  crucible::recompute_merkle(loop2);
  assert(loop2->merkle_hash != loop_merkle);

  // Different termination kind → different merkle hash
  auto* loop3 = crucible::make_loop(
      test.alloc, arena, body_r1, body_ch,
      fb_edges, 1,
      crucible::LoopTermKind::UNTIL, 4, 0.001f);
  loop3->next = final_region;
  crucible::recompute_merkle(loop3);
  assert(loop3->merkle_hash != loop_merkle);
  assert(loop3->merkle_hash != loop2->merkle_hash);

  // Different feedback → different merkle hash
  crucible::FeedbackEdge fb_edges2[2]{};
  fb_edges2[0] = {.output_idx = 0, .input_idx = 0};
  fb_edges2[1] = {.output_idx = 1, .input_idx = 1};
  auto* loop4 = crucible::make_loop(
      test.alloc, arena, body_r1, body_ch,
      fb_edges2, 2,
      crucible::LoopTermKind::REPEAT, 4);
  loop4->next = final_region;
  crucible::recompute_merkle(loop4);
  assert(loop4->merkle_hash != loop_merkle);

  // feedback_signature: empty → 0, non-empty → nonzero
  assert(crucible::feedback_signature({}) == 0);
  assert(crucible::feedback_signature(loop->feedback_span()) != 0);

  // collect_regions: finds body regions inside LoopNode
  crucible::RegionNode* collected[16]{};
  uint32_t n_collected = crucible::collect_regions(
      loop, std::span{collected, 16});
  // loop body has 2 regions (body_r1, body_r2) + final_region after loop = 3
  assert(n_collected == 3);
  assert(collected[0] == body_r1);
  assert(collected[1] == body_r2);
  assert(collected[2] == final_region);

  // replay: Repeat(4) executes body regions 4 times, then final region
  uint32_t exec_count = 0;
  std::vector<ContentHash> replay_log;
  replay_log.reserve(16);
  bool replay_ok = crucible::replay(
      loop,
      [](const crucible::Guard&) -> int64_t { return 0; },
      [&](crucible::RegionNode* r) {
        replay_log.push_back(r->content_hash);
        exec_count++;
      });
  assert(replay_ok);
  // 4 iterations × 2 body regions + 1 final = 9
  assert(exec_count == 9);
  assert(replay_log.size() == 9);
  // First 8 entries alternate: body_r1, body_r2, body_r1, body_r2, ...
  for (uint32_t i = 0; i < 8; i += 2) {
    assert(replay_log[i] == body_r1->content_hash);
    assert(replay_log[i + 1] == body_r2->content_hash);
  }
  // Last entry is final_region
  assert(replay_log[8] == final_region->content_hash);

  // replay: Repeat(0) skips body entirely, only executes continuation
  auto* loop_zero = crucible::make_loop(
      test.alloc, arena, body_r1, body_ch,
      fb_edges, 1,
      crucible::LoopTermKind::REPEAT, 0);
  loop_zero->next = final_region;
  uint32_t zero_count = 0;
  bool zero_ok = crucible::replay(
      loop_zero,
      [](const crucible::Guard&) -> int64_t { return 0; },
      [&](crucible::RegionNode*) { zero_count++; });
  assert(zero_ok);
  assert(zero_count == 1); // only final_region

  // compute_body_content_hash: different body → different hash
  crucible::TraceEntry alt_body_ops[1]{};
  alt_body_ops[0].schema_hash = SchemaHash{0x9999};
  auto* alt_body = crucible::make_region(test.alloc, arena, alt_body_ops, 1);
  auto* alt_body_term = crucible::make_terminal(test.alloc, arena);
  alt_body->next = alt_body_term;
  ContentHash alt_ch = crucible::compute_body_content_hash(alt_body);
  assert(alt_ch != body_ch);

  // ═══════════════════════════════════════════════════════════════════
  // Recipe-aware content hashing (FORGE.md §18.6)
  //
  // Two regions with byte-identical ops but different NumericalRecipes
  // must produce distinct content_hash values.  Without this, a cached
  // kernel compiled under one recipe would silently serve lookups
  // under another recipe — the replay-determinism invariant break
  // documented in CRUCIBLE.md §10.
  //
  // The FORGE.md §18.6 composition rule: content_hash folds
  // recipe->hash into the accumulator BEFORE the ops stream so that
  // the recipe contribution propagates through every downstream
  // wymix for maximum avalanche.  We verify that property here.
  // ═══════════════════════════════════════════════════════════════════
  {
    // Build two NumericalRecipes with distinct semantic fields.
    // Using crucible::hashed() ensures the `hash` field is populated
    // to the compute_recipe_hash value — compute_content_hash's
    // pre(recipe->hash.raw() != 0) would otherwise fire.
    constexpr crucible::NumericalRecipe recipe_tc = crucible::hashed(
        crucible::NumericalRecipe{
            .accum_dtype    = crucible::ScalarType::Float,
            .out_dtype      = crucible::ScalarType::Half,
            .reduction_algo = crucible::ReductionAlgo::PAIRWISE,
            .rounding       = crucible::RoundingMode::RN,
            .scale_policy   = crucible::ScalePolicy::NONE,
            .softmax        = crucible::SoftmaxRecurrence::ONLINE_LSE,
            .determinism    = crucible::ReductionDeterminism::BITEXACT_TC,
            .flags          = 0,
            .hash           = {},
        });
    constexpr crucible::NumericalRecipe recipe_strict = crucible::hashed(
        crucible::NumericalRecipe{
            .accum_dtype    = crucible::ScalarType::Float,
            .out_dtype      = crucible::ScalarType::Float,
            .reduction_algo = crucible::ReductionAlgo::PAIRWISE,
            .rounding       = crucible::RoundingMode::RN,
            .scale_policy   = crucible::ScalePolicy::NONE,
            .softmax        = crucible::SoftmaxRecurrence::ONLINE_LSE,
            .determinism    = crucible::ReductionDeterminism::BITEXACT_STRICT,
            .flags          = 0,
            .hash           = {},
        });
    assert(recipe_tc.hash != recipe_strict.hash);
    assert(recipe_tc.hash.raw() != 0);
    assert(!recipe_tc.hash.is_sentinel());
    assert(recipe_strict.hash.raw() != 0);
    assert(!recipe_strict.hash.is_sentinel());

    crucible::TraceEntry recipe_ops[2]{};
    recipe_ops[0].schema_hash = SchemaHash{0xDEAD};
    recipe_ops[1].schema_hash = SchemaHash{0xBEEF};
    const std::span<const crucible::TraceEntry> ops_span{recipe_ops, 2};

    // ─── 1. Backward compat: nullptr == default argument ──────────
    //
    // Existing callers that do not pass a recipe must produce the
    // same hash as explicitly passing nullptr.  Any drift here
    // would silently break every existing content-hash golden.
    const ContentHash h_default = crucible::compute_content_hash(ops_span);
    const ContentHash h_null    = crucible::compute_content_hash(ops_span, nullptr);
    assert(h_default == h_null);

    // ─── 2. Recipe disambiguation ────────────────────────────────
    //
    // Same ops + two distinct recipes → two distinct hashes.  This
    // is the primary safety property the integration provides.
    const ContentHash h_tc     = crucible::compute_content_hash(ops_span, &recipe_tc);
    const ContentHash h_strict = crucible::compute_content_hash(ops_span, &recipe_strict);
    assert(h_tc != h_strict);
    assert(h_tc != h_null);
    assert(h_strict != h_null);

    // ─── 3. Recipe-aware determinism ─────────────────────────────
    //
    // Multiple calls with (same ops, same recipe) produce the same
    // hash — pure function, no hidden state.
    const ContentHash h_tc_again = crucible::compute_content_hash(ops_span, &recipe_tc);
    assert(h_tc == h_tc_again);

    // ─── 4. Ops disambiguation preserved under recipe ────────────
    //
    // Same recipe + different ops still produces different hashes.
    // The recipe participation is additive, not overriding.
    crucible::TraceEntry alt_recipe_ops[2]{};
    alt_recipe_ops[0].schema_hash = SchemaHash{0xFACE};
    alt_recipe_ops[1].schema_hash = SchemaHash{0xBEEF};
    const std::span<const crucible::TraceEntry> alt_ops_span{alt_recipe_ops, 2};
    const ContentHash h_alt_tc = crucible::compute_content_hash(alt_ops_span, &recipe_tc);
    assert(h_alt_tc != h_tc);

    // ─── 5. make_region recipe overload ──────────────────────────
    //
    // The recipe-aware make_region stores the recipe-disambiguated
    // hash in the RegionNode's content_hash field, so downstream
    // KernelCache lookups are naturally partitioned by recipe.
    crucible::TraceEntry rops_a[2]{};
    rops_a[0].schema_hash = SchemaHash{0x0A0A};
    rops_a[1].schema_hash = SchemaHash{0x0B0B};
    crucible::TraceEntry rops_b[2]{};
    rops_b[0].schema_hash = SchemaHash{0x0A0A};
    rops_b[1].schema_hash = SchemaHash{0x0B0B};

    auto* r_tc     = crucible::make_region(test.alloc, arena, rops_a, 2, &recipe_tc);
    auto* r_strict = crucible::make_region(test.alloc, arena, rops_b, 2, &recipe_strict);
    auto* r_none   = crucible::make_region(test.alloc, arena, rops_a, 2);

    assert(r_tc != nullptr && r_strict != nullptr && r_none != nullptr);
    assert(r_tc->content_hash != r_strict->content_hash);
    assert(r_tc->content_hash != r_none->content_hash);
    assert(r_strict->content_hash != r_none->content_hash);

    // Recipe-aware hash matches the direct compute_content_hash
    // path (equivalent computation by construction).
    const ContentHash direct_tc =
        crucible::compute_content_hash(std::span<const crucible::TraceEntry>{rops_a, 2},
                                       &recipe_tc);
    assert(r_tc->content_hash == direct_tc);

    // ─── 6. KernelCache disambiguation (end-to-end) ──────────────
    //
    // Register a fake compiled kernel under r_tc's content_hash; a
    // lookup under r_strict's (same ops, different recipe) content
    // hash must miss.  This is the core safety property: recipe
    // divergence at the hash level → no cache collision.
    crucible::KernelCache rcache;
    struct FakeRecipeKernel { int tag; };
    FakeRecipeKernel tc_kernel{1};
    using crucible::RowHash;
    assert(rcache.insert(r_tc->content_hash, RowHash{0},
                         reinterpret_cast<crucible::CompiledKernel*>(&tc_kernel))
               .has_value());
    assert(rcache.lookup(r_tc->content_hash, RowHash{0}) ==
           reinterpret_cast<crucible::CompiledKernel*>(&tc_kernel));
    // Critical: lookup under the different-recipe content_hash MUST miss.
    assert(rcache.lookup(r_strict->content_hash, RowHash{0}) == nullptr);
    // Lookup under the no-recipe content_hash ALSO misses.
    assert(rcache.lookup(r_none->content_hash, RowHash{0}) == nullptr);
  }

  std::printf("test_merkle_dag: all tests passed\n");
  return 0;
}
