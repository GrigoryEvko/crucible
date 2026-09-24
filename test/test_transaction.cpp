#include <crucible/Transaction.h>
#include <crucible/effects/_Capabilities.h>
#include "test_assert.h"
#include <cstdio>
#include <cstring>

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

}  // namespace

[[gnu::cold]] int main() {
    auto test = crucible::effects::testing::test();
    const SoloOwner owner = SoloOwner::claim();
    Log log;
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
    const bool committed = log.commit(owner, tx1, crucible::Transaction::ArenaRegion{region1}, hash1, merkle1);
    assert(committed);
    assert(tx1->status == crucible::TxStatus::COMMITTED);
    assert(tx1->content_hash == hash1);
    assert(tx1->merkle_root == merkle1);
    assert(tx1->region.value() == region1);

    const bool double_commit = log.commit(owner, tx1, crucible::Transaction::ArenaRegion{region1}, hash1, merkle1);
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
    assert(log.commit(owner, tx2, crucible::Transaction::ArenaRegion{region2}, hash2, merkle2));
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
        assert(log.commit(owner, tx, crucible::Transaction::ArenaRegion{r}, r->content_hash, r->merkle_hash));
        (void)log.activate(owner, tx);
    }
    // The loop ends at 32, so the last transaction is the active one.
    assert(log.active(owner) != nullptr);
    assert(log.active(owner)->step_id.get() == 32);

    std::printf("test_transaction: all tests passed\n");
    return 0;
}
