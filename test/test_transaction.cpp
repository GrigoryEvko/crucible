#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>
#include <crucible/Transaction.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>
#include "test_assert.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <system_error>
#include <type_traits>

namespace {

// The owner proof of a log that this one thread drives.  The factory is
// the only door, so the test shows what an owner must supply: an empty
// type that cannot be copied, moved, default-built or built from bytes.
class SoloOwner {
public:
    [[nodiscard]] static SoloOwner claim() noexcept { return SoloOwner{}; }
    SoloOwner(const SoloOwner&) = delete("the owner proof stays with its thread");
    SoloOwner(SoloOwner&&) = delete("the owner proof stays with its thread");
    SoloOwner& operator=(const SoloOwner&) = delete("the owner proof stays with its thread");
    SoloOwner& operator=(SoloOwner&&) = delete("the owner proof stays with its thread");
    ~SoloOwner() {}

private:
    SoloOwner() noexcept {}
};

static_assert(crucible::OwnerProof<SoloOwner>);

// Each shape that a thread could hand to another thread, that any code
// could build, or that bytes could build, is refused as an owner.  The
// last one is the GCC case: deleted copies with a trivial destructor still
// read as trivially copyable.
class DeletedCopiesOnly {
    DeletedCopiesOnly() noexcept {}

public:
    DeletedCopiesOnly(const DeletedCopiesOnly&) = delete;
    DeletedCopiesOnly(DeletedCopiesOnly&&) = delete;
};
static_assert(!crucible::OwnerProof<DeletedCopiesOnly>);
struct CopyableOwner {};
struct MovableOwner {
    MovableOwner(MovableOwner&&) = default;

private:
    MovableOwner() noexcept {}
};
class PublicOwner {
public:
    PublicOwner() noexcept {}
    PublicOwner(const PublicOwner&) = delete;
};
class TrivialOwner {
    TrivialOwner() = default;

public:
    TrivialOwner(const TrivialOwner&) = delete;
};
static_assert(!crucible::OwnerProof<CopyableOwner>);
static_assert(!crucible::OwnerProof<MovableOwner>);
static_assert(!crucible::OwnerProof<PublicOwner>);
static_assert(!crucible::OwnerProof<TrivialOwner>);
static_assert(!crucible::OwnerProof<int>);

using Log = crucible::TransactionLog<16, SoloOwner>;
// The smallest ring that the log admits.
using SmallestLog = crucible::TransactionLog<4, SoloOwner>;

// The log reads the monotonic clock, so only a context off the replay-bound
// foreground path builds one.
static_assert(crucible::CtxFitsTransactionLog<::fixy::TestRunnerCtx>);
static_assert(crucible::CtxFitsTransactionLog<::fixy::ColdInitCtx>);
static_assert(crucible::CtxFitsTransactionLog<::fixy::BgDrainCtx>);
static_assert(!crucible::CtxFitsTransactionLog<::fixy::HotFgCtx>);
static_assert(!std::is_constructible_v<Log, ::fixy::HotFgCtx const&>);
static_assert(!std::is_default_constructible_v<Log>);

// A slot that the log never stamped holds no reading, and nothing builds a
// reading from a raw integer.
static_assert(!std::is_constructible_v<crucible::Transaction::Timestamp, std::uint64_t>);
static_assert(!std::is_trivially_copyable_v<crucible::Transaction>);

[[nodiscard]] crucible::Transaction::ArenaRegion arena_region(crucible::RegionNode* region) {
    return ::fixy::mint_tagged<::fixy::tags::source::Arena>(region);
}

// One region for each step, so that each transaction holds a region that no
// other transaction holds.
[[nodiscard]] crucible::RegionNode* region_of_step(::foundation::effects::Alloc alloc, crucible::Arena& arena,
                                                   uint32_t step) {
    crucible::TraceEntry entry{};
    entry.schema_hash = crucible::SchemaHash{0x5000u + step};
    auto* region = crucible::make_region(alloc, arena, &entry, 1);
    crucible::recompute_merkle(region);
    return region;
}

// Begins, commits and activates the transaction of one step, as the publish
// stage of a Vigil does for each region.  Returns the transaction that the
// activation displaced.
crucible::Transaction* publish(auto& log, const SoloOwner& owner, uint32_t step, crucible::RegionNode* region) {
    auto* tx = log.begin_tx(owner, step);
    assert(log.commit(owner, tx, arena_region(region), region->content_hash, region->merkle_hash));
    return log.activate(owner, tx);
}

// The log holds the transaction of `step` as the active one, with its region.
void expect_active(auto& log, const SoloOwner& owner, uint32_t step, crucible::RegionNode* region) {
    const crucible::Transaction* active = log.active(owner);
    assert(active != nullptr);
    assert(active->status == crucible::TxStatus::ACTIVE);
    assert(active->step_id.get() == step);
    assert(active->region.value() == region);
}

// The two orders of publications and rollbacks that make the oldest
// transaction of a full ring the active one.
enum class RollbackOrder : uint8_t {
    // Each rollback restores the transaction that the publication before it
    // displaced, so the first transaction stays the active one while each
    // publication takes a new slot.
    AfterEachPublication,
    // The publish stage publishes a full ring while no rollback comes, and
    // then a run of rollbacks walks the active transaction back to the first
    // one.  A loaded host gives this order when the rollback jobs of a Vigil
    // reach the stage before the regions of their rounds.
    InARow,
};

// A full ring claims its oldest slot.  When a rollback made the oldest
// transaction the active one, the next claim must not recycle it: the
// activation then displaces the transaction that was live, and a rollback
// restores it with its region.  Each order fills the ring so that the next
// claim lands on the slot of the active transaction.
void claim_passes_over_the_active_slot(::foundation::effects::Alloc alloc, const ::fixy::TestRunnerCtx& ctx,
                                       const SoloOwner& owner, RollbackOrder order) {
    constexpr uint32_t kSlots = 16;
    Log log{ctx};
    crucible::Arena arena(1 << 14);
    std::array<crucible::RegionNode*, kSlots + 2> regions{};
    for (uint32_t step = 1; step <= kSlots + 1; ++step)
        regions[step] = region_of_step(alloc, arena, step);

    if (order == RollbackOrder::InARow) {
        for (uint32_t step = 1; step <= kSlots; ++step)
            (void)publish(log, owner, step, regions[step]);
        for (uint32_t step = kSlots; step > 1; --step)
            assert(log.rollback(owner));
    } else {
        (void)publish(log, owner, 1, regions[1]);
        for (uint32_t step = 2; step <= kSlots; ++step) {
            (void)publish(log, owner, step, regions[step]);
            assert(log.rollback(owner));
        }
    }
    assert(log.size(owner) == kSlots);
    expect_active(log, owner, 1, regions[1]);

    const crucible::Transaction* displaced = publish(log, owner, kSlots + 1, regions[kSlots + 1]);
    assert(displaced != nullptr);
    assert(displaced->status == crucible::TxStatus::SUPERSEDED);
    assert(displaced->step_id.get() == 1);
    assert(displaced->region.value() == regions[1]);
    expect_active(log, owner, kSlots + 1, regions[kSlots + 1]);
    assert(log.previous(owner) == displaced);

    assert(log.rollback(owner));
    expect_active(log, owner, 1, regions[1]);
}

// The rollback target is the transaction that the last activation displaced.
// The activations here do not follow the order of the claims, so the
// rollback target is not the superseded transaction with the newest claim.
void previous_follows_the_order_of_displacements(::foundation::effects::Alloc alloc,
                                                 const ::fixy::TestRunnerCtx& ctx, const SoloOwner& owner) {
    constexpr uint32_t kSteps = 4;
    Log log{ctx};
    crucible::Arena arena(1 << 12);
    std::array<crucible::RegionNode*, kSteps + 1> regions{};
    std::array<crucible::Transaction*, kSteps + 1> txs{};
    for (uint32_t step = 1; step <= kSteps; ++step) {
        regions[step] = region_of_step(alloc, arena, step);
        txs[step] = log.begin_tx(owner, step);
        assert(log.commit(owner, txs[step], arena_region(regions[step]), regions[step]->content_hash,
                          regions[step]->merkle_hash));
    }

    // The third transaction displaces nothing, the first displaces the
    // third, and the second displaces the first.
    assert(log.activate(owner, txs[3]) == nullptr);
    assert(log.activate(owner, txs[1]) == txs[3]);
    assert(log.activate(owner, txs[2]) == txs[1]);
    assert(log.previous(owner) == txs[1]);

    assert(log.rollback(owner));
    expect_active(log, owner, 1, regions[1]);
    assert(log.previous(owner) == txs[3]);

    // A second displacement of the restored first transaction comes after
    // the displacement of the third, although its claim comes before.
    assert(log.activate(owner, txs[4]) == txs[1]);
    assert(log.previous(owner) == txs[1]);

    assert(log.rollback(owner));
    expect_active(log, owner, 1, regions[1]);
    assert(log.previous(owner) == txs[3]);
    assert(log.rollback(owner));
    expect_active(log, owner, 3, regions[3]);
    assert(log.previous(owner) == nullptr);
    assert(!log.rollback(owner));
}

// A full ring recycles its oldest slot, but a claim must keep the rollback
// target.  Activations out of order put the target in the oldest slot.  The
// second transaction displaces nothing, the first displaces the second, and
// the third displaces the first.  The claim must not recycle the slot of the
// first, and a rollback after the claim restores the first with its region.
// The fourth transaction stays committed, and its slot does not change.
void claim_keeps_the_target_of_out_of_order_activations(::foundation::effects::Alloc alloc,
                                                        const ::fixy::TestRunnerCtx& ctx, const SoloOwner& owner) {
    constexpr uint32_t kSlots = 4;
    SmallestLog log{ctx};
    crucible::Arena arena(1 << 12);
    std::array<crucible::RegionNode*, kSlots + 1> regions{};
    std::array<crucible::Transaction*, kSlots + 1> txs{};
    for (uint32_t step = 1; step <= kSlots; ++step) {
        regions[step] = region_of_step(alloc, arena, step);
        txs[step] = log.begin_tx(owner, step);
        assert(log.commit(owner, txs[step], arena_region(regions[step]), regions[step]->content_hash,
                          regions[step]->merkle_hash));
    }
    assert(log.activate(owner, txs[2]) == nullptr);
    assert(log.activate(owner, txs[1]) == txs[2]);
    assert(log.activate(owner, txs[3]) == txs[1]);
    assert(log.size(owner) == kSlots);
    assert(log.previous(owner) == txs[1]);

    const crucible::Transaction* claimed = log.begin_tx(owner, kSlots + 1);
    assert(claimed != txs[1] && claimed != txs[3] && claimed != txs[4]);
    assert(log.previous(owner) == txs[1]);
    assert(txs[1]->step_id.get() == 1);
    assert(txs[4]->status == crucible::TxStatus::COMMITTED);
    assert(txs[4]->step_id.get() == 4);

    assert(log.rollback(owner));
    expect_active(log, owner, 1, regions[1]);
}

// A rollback between a claim and its activation moves the rollback target
// to an older slot.  Four publications in sequence fill the ring, the fifth
// claim recycles the slot of the first, and a rollback restores the third.
// The second is then the rollback target, in the oldest slot.  The caller
// does not activate the fifth transaction, and it claims a sixth.  That
// claim must not recycle the slot of the second or the slot of the third.
void claim_keeps_the_target_after_a_rollback_before_activation(::foundation::effects::Alloc alloc,
                                                               const ::fixy::TestRunnerCtx& ctx,
                                                               const SoloOwner& owner) {
    constexpr uint32_t kSlots = 4;
    SmallestLog log{ctx};
    crucible::Arena arena(1 << 12);
    std::array<crucible::RegionNode*, kSlots + 1> regions{};
    for (uint32_t step = 1; step <= kSlots; ++step) {
        regions[step] = region_of_step(alloc, arena, step);
        (void)publish(log, owner, step, regions[step]);
    }
    (void)log.begin_tx(owner, kSlots + 1);
    assert(log.rollback(owner));
    expect_active(log, owner, 3, regions[3]);
    const crucible::Transaction* target = log.previous(owner);
    assert(target != nullptr);
    assert(target->step_id.get() == 2);

    const crucible::Transaction* claimed = log.begin_tx(owner, kSlots + 2);
    assert(claimed != target && claimed != log.active(owner));
    assert(log.previous(owner) == target);
    assert(target->step_id.get() == 2);

    assert(log.rollback(owner));
    expect_active(log, owner, 2, regions[2]);
}

}  // namespace

[[gnu::cold]] int main() {
    auto test = ::foundation::effects::testing::test();
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    const SoloOwner owner = SoloOwner::claim();
    Log log{ctx};
    crucible::Arena arena(1 << 12);

    crucible::TraceEntry ops[1]{};
    ops[0].schema_hash = crucible::SchemaHash{0xFEED};
    auto* region1 = crucible::make_region(test.alloc, arena, ops, 1);
    assert(region1 != nullptr);
    // A commit requires a non-zero merkle root, so the hash is recomputed
    // first. The production path does the same thing in the same order.
    // Synthesizing a hash instead would take the test off that path.
    crucible::recompute_merkle(region1);
    assert(region1->merkle_hash.raw() != 0);

    crucible::TraceEntry ops2[1]{};
    ops2[0].schema_hash = crucible::SchemaHash{0xBEEF};
    auto* region2 = crucible::make_region(test.alloc, arena, ops2, 1);
    assert(region2 != nullptr);
    crucible::recompute_merkle(region2);
    assert(region2->merkle_hash.raw() != 0);

    assert(log.size(owner) == 0);
    assert(log.active(owner) == nullptr);
    assert(log.previous(owner) == nullptr);

    auto* tx1 = log.begin_tx(owner, 1);
    assert(tx1 != nullptr);
    assert(tx1->step_id.get() == 1);
    assert(tx1->status == crucible::TxStatus::RECORDING);
    assert(log.size(owner) == 1);

    const crucible::ContentHash hash1 = region1->content_hash;
    const crucible::MerkleHash merkle1 = region1->merkle_hash;
    // The log stamped the transaction when it began it, and the stamp is a
    // reading of the monotonic clock.
    assert(tx1->ts_ns.has_value());
    const std::uint64_t begun_at = tx1->ts_ns->peek();

    const bool committed = log.commit(owner, tx1, arena_region(region1), hash1, merkle1);
    assert(committed);
    assert(tx1->status == crucible::TxStatus::COMMITTED);
    assert(tx1->content_hash == hash1);
    assert(tx1->merkle_root == merkle1);
    assert(tx1->region.value() == region1);
    // The reader clamps, so a later stamp through the same log never reads
    // earlier than a former one.
    assert(tx1->ts_ns.has_value() && tx1->ts_ns->peek() >= begun_at);

    const bool double_commit = log.commit(owner, tx1, arena_region(region1), hash1, merkle1);
    assert(!double_commit && "commit on COMMITTED tx must return false");

    auto* prev = log.activate(owner, tx1);
    assert(prev == nullptr && "no previous ACTIVE on first activation");
    assert(tx1->status == crucible::TxStatus::ACTIVE);
    assert(log.active(owner) == tx1);
    assert(log.previous(owner) == nullptr);

    auto* tx2 = log.begin_tx(owner, 2);
    assert(tx2 != nullptr);
    assert(tx2->step_id.get() == 2);
    assert(tx2->status == crucible::TxStatus::RECORDING);
    assert(log.size(owner) == 2);

    const crucible::ContentHash hash2 = region2->content_hash;
    const crucible::MerkleHash merkle2 = region2->merkle_hash;
    assert(log.commit(owner, tx2, arena_region(region2), hash2, merkle2));
    assert(tx2->status == crucible::TxStatus::COMMITTED);

    auto* superseded = log.activate(owner, tx2);
    assert(superseded == tx1 && "tx1 must be returned as the superseded tx");
    assert(tx1->status == crucible::TxStatus::SUPERSEDED);
    assert(tx2->status == crucible::TxStatus::ACTIVE);
    assert(log.active(owner) == tx2);
    assert(log.previous(owner) == tx1);

    const bool rolled = log.rollback(owner);
    assert(rolled);
    assert(tx1->status == crucible::TxStatus::ACTIVE);
    assert(tx2->status == crucible::TxStatus::ROLLED_BACK);
    assert(log.active(owner) == tx1);
    assert(log.previous(owner) == nullptr && "no more SUPERSEDED after rollback");

    const bool rolled2 = log.rollback(owner);
    assert(!rolled2 && "rollback with no SUPERSEDED must return false");

    // More entries than the ring holds, so it wraps at least once.
    for (uint32_t i = 3; i <= 32; i++) {
        auto* tx = log.begin_tx(owner, i);
        crucible::TraceEntry e{};
        e.schema_hash = crucible::SchemaHash{static_cast<uint64_t>(i)};
        auto* r = crucible::make_region(test.alloc, arena, &e, 1);
        // Recomputed before the commit, for the reason given above.
        crucible::recompute_merkle(r);
        assert(log.commit(owner, tx, arena_region(r), r->content_hash, r->merkle_hash));
        (void)log.activate(owner, tx);
    }
    // The loop ends at 32, so the last transaction is the active one.
    assert(log.active(owner) != nullptr);
    assert(log.active(owner)->step_id.get() == 32);

    claim_passes_over_the_active_slot(test.alloc, ctx, owner, RollbackOrder::AfterEachPublication);
    claim_passes_over_the_active_slot(test.alloc, ctx, owner, RollbackOrder::InARow);
    previous_follows_the_order_of_displacements(test.alloc, ctx, owner);
    claim_keeps_the_target_of_out_of_order_activations(test.alloc, ctx, owner);
    claim_keeps_the_target_after_a_rollback_before_activation(test.alloc, ctx, owner);

    // A failed clock read leaves the transaction with no reading.  The
    // result of a failed read stands in for a clock that fails, because a
    // working clock cannot be made to fail here.  The stamp before it is a
    // real reading, so the test shows that a failed read clears it rather
    // than keeping a reading of an earlier event.
    {
        crucible::Transaction stamped{};
        const auto reader = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx);
        crucible::stamp_transaction(stamped, reader.read());
        assert(stamped.ts_ns.has_value());
        const std::expected<crucible::Transaction::Timestamp, std::error_code> failed_read{
            std::unexpect, std::make_error_code(std::errc::io_error)};
        crucible::stamp_transaction(stamped, failed_read);
        assert(!stamped.ts_ns.has_value() && "a failed clock read must leave the timestamp empty");
    }

    std::printf("test_transaction: all tests passed\n");
    return 0;
}
