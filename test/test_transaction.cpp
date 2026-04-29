#include <crucible/Transaction.h>
#include <crucible/effects/Capabilities.h>
#include "test_assert.h"
#include <cstdio>
#include <cstring>

int main() {
    crucible::effects::Test test;
    crucible::TransactionLog<16> log;
    crucible::Arena arena(1 << 12);

    // ── build a dummy RegionNode for use in transactions ────────────
    crucible::TraceEntry ops[1]{};
    ops[0].schema_hash = crucible::SchemaHash{0xFEED};
    auto* region1 = crucible::make_region(test.alloc, arena, ops, 1);
    assert(region1 != nullptr);

    crucible::TraceEntry ops2[1]{};
    ops2[0].schema_hash = crucible::SchemaHash{0xBEEF};
    auto* region2 = crucible::make_region(test.alloc, arena, ops2, 1);
    assert(region2 != nullptr);

    // ── begin_tx(1) → RECORDING ─────────────────────────────────────
    assert(log.size() == 0);
    assert(log.active()   == nullptr);
    assert(log.previous() == nullptr);

    auto* tx1 = log.begin_tx(1);
    assert(tx1 != nullptr);
    assert(tx1->step_id == 1);
    assert(tx1->status  == crucible::TxStatus::RECORDING);
    assert(log.size() == 1);

    // ── commit(tx1, region, hash, merkle) → COMMITTED ───────────────
    const crucible::ContentHash hash1   = region1->content_hash;
    const crucible::MerkleHash  merkle1 = region1->merkle_hash;
    const bool committed = log.commit(tx1, region1, hash1, merkle1);
    assert(committed);
    assert(tx1->status       == crucible::TxStatus::COMMITTED);
    assert(tx1->content_hash == hash1);
    assert(tx1->merkle_root  == merkle1);
    assert(tx1->region       == region1);

    // Double-commit must fail.
    const bool double_commit = log.commit(tx1, region1, hash1, merkle1);
    assert(!double_commit && "commit on COMMITTED tx must return false");

    // ── activate(tx1) → ACTIVE ──────────────────────────────────────
    auto* prev = log.activate(tx1);
    assert(prev   == nullptr && "no previous ACTIVE on first activation");
    assert(tx1->status == crucible::TxStatus::ACTIVE);
    assert(log.active()   == tx1);
    assert(log.previous() == nullptr);

    // ── begin_tx(2), commit, activate → tx2 ACTIVE, tx1 SUPERSEDED ─
    auto* tx2 = log.begin_tx(2);
    assert(tx2 != nullptr);
    assert(tx2->step_id == 2);
    assert(tx2->status  == crucible::TxStatus::RECORDING);
    assert(log.size() == 2);

    const crucible::ContentHash hash2   = region2->content_hash;
    const crucible::MerkleHash  merkle2 = region2->merkle_hash;
    assert(log.commit(tx2, region2, hash2, merkle2));
    assert(tx2->status == crucible::TxStatus::COMMITTED);

    auto* superseded = log.activate(tx2);
    assert(superseded == tx1 && "tx1 must be returned as the superseded tx");
    assert(tx1->status == crucible::TxStatus::SUPERSEDED);
    assert(tx2->status == crucible::TxStatus::ACTIVE);
    assert(log.active()   == tx2);
    assert(log.previous() == tx1);

    // ── rollback() → tx1 ACTIVE again, tx2 ROLLED_BACK ─────────────
    const bool rolled = log.rollback();
    assert(rolled);
    assert(tx1->status == crucible::TxStatus::ACTIVE);
    assert(tx2->status == crucible::TxStatus::ROLLED_BACK);
    assert(log.active()   == tx1);
    assert(log.previous() == nullptr && "no more SUPERSEDED after rollback");

    // ── Second rollback with no SUPERSEDED → false ───────────────────
    const bool rolled2 = log.rollback();
    assert(!rolled2 && "rollback with no SUPERSEDED must return false");

    // ── Ring wrap: fill more than N-1 additional entries ─────────────
    // Ensure the ring wraps correctly without UB.
    for (uint32_t i = 3; i <= 32; i++) {
        auto* tx = log.begin_tx(i);
        crucible::TraceEntry e{};
        e.schema_hash = crucible::SchemaHash{static_cast<uint64_t>(i)};
        auto* r = crucible::make_region(test.alloc, arena, &e, 1);
        assert(log.commit(tx, r, r->content_hash, r->merkle_hash));
        (void)log.activate(tx);
    }
    // The active tx must be the last one (step_id == 32).
    assert(log.active() != nullptr);
    assert(log.active()->step_id == 32);

    std::printf("test_transaction: all tests passed\n");
    return 0;
}
