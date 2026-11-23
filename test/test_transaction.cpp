#include <crucible/MerkleDag.h>
#include <crucible/Transaction.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>
#include <fixy/Core.h>
#include <fixy/check/Check.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <system_error>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using Bytes = ::fixy::Quantity<::fixy::unit::Byte, size_t>;

}  // namespace

using crucible::DagArena;

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

// The log stamps each event with a place of its own count and reads no
// clock.  So a rollback takes the owner proof and no tag of Nondet, and the
// log needs no context.
static_assert(requires(Log& log, SoloOwner const& owner) { log.rollback(owner); });
static_assert(std::is_default_constructible_v<Log>);

// A draft is the one writer of its transaction: it does not copy or move, and
// only the log ends it.  A CommittedTx holds no reference, so it copies.
static_assert(!std::is_copy_constructible_v<Log::Draft>);
static_assert(!std::is_move_constructible_v<Log::Draft>);
static_assert(std::is_copy_constructible_v<Log::Committed>);
static_assert(!std::is_constructible_v<Log::Committed, ::fixy::Index<16>, std::uint64_t>);

[[nodiscard]] crucible::Transaction::ArenaRegion arena_region(crucible::RegionNode& region) {
    return ::fixy::mint_tagged<::fixy::tags::source::Arena>(
        crucible::DagRef<crucible::RegionNode const>{::fixy::to(region)});
}

// A commit takes a Ref, so it cannot take a null region.  A slot that no
// commit wrote holds no region, and the link of no region takes one pointer.
static_assert(!std::is_constructible_v<crucible::Transaction::ArenaRegion, std::nullptr_t>);
static_assert(sizeof(crucible::Transaction::ArenaRegionLink) == sizeof(void*));

// The facts of one transaction that a check reads: its status, its step, the
// place of its last event, and the region that it holds or none.
struct TxFacts {
    crucible::TxStatus status = crucible::TxStatus::RECORDING;
    std::uint64_t step = 0;
    std::uint64_t stamp = 0;
    bool has_region = false;
};

[[nodiscard]] TxFacts facts_of_tx(crucible::Transaction const& tx) noexcept {
    return TxFacts{.status = tx.status,
                   .step = tx.step_id.get(),
                   .stamp = tx.event_order.get(),
                   .has_region = tx.region.value().is_some()};
}

// The facts of a committed transaction of a log.  The handle must still name
// its transaction.
template <class LogType>
[[nodiscard]] TxFacts facts_of(LogType const& log, SoloOwner const& owner, typename LogType::Committed committed) {
    return log.with_tx(owner, committed, facts_of_tx).expect("no claim reused the slot of the transaction");
}

// True when a committed transaction of a log holds the region.
template <class LogType>
[[nodiscard]] bool holds(LogType const& log, SoloOwner const& owner, typename LogType::Committed committed,
                         crucible::RegionNode& region) {
    crucible::DagRef<crucible::RegionNode const> const wanted = ::fixy::to(region);
    return log
        .with_tx(owner, committed,
                 [wanted](crucible::Transaction const& tx) noexcept { return crucible::holds_region(tx, wanted); })
        .expect("no claim reused the slot of the transaction");
}

// One region for each step, so that each transaction holds a region that no
// other transaction holds.  The region and a copy of its op are in the arena.
[[nodiscard]] crucible::RegionNode* region_of_step(::foundation::effects::Alloc alloc,
                                                   ::fixy::BumpArena<DagArena>& arena, uint32_t step) {
    crucible::TraceEntry entry{};
    entry.schema_hash = crucible::SchemaHash{0x5000u + step};
    const auto ops_run = arena.array<crucible::TraceEntry>(alloc, 1, entry);
    const crucible::DagRef<crucible::RegionNode> region = crucible::make_region(alloc, arena, ops_run);
    crucible::recompute_merkle(*region);
    return &*region;
}

// Begins and commits the transaction of one step.
template <class LogType>
[[nodiscard]] typename LogType::Committed commit_step(LogType& log, const SoloOwner& owner, uint32_t step,
                                                      crucible::RegionNode* region) {
    return log
        .commit(owner, log.begin_tx(owner, ::fixy::session::StepId{step}), arena_region(*region), region->content_hash,
                region->merkle_hash)
        .expect("no claim runs between the claim and its commit");
}

// Begins, commits and activates the transaction of one step, as the publish
// stage of a Vigil does for each region.  Returns the transaction that the
// activation displaced.
template <class LogType>
::fixy::Option<typename LogType::Committed> publish(LogType& log, const SoloOwner& owner, uint32_t step,
                                                    crucible::RegionNode* region) {
    return log.activate(owner, commit_step(log, owner, step, region)).expect("a fresh commit activates");
}

// The log holds the transaction of `step` as the active one, with its region.
template <class LogType>
void expect_active(LogType const& log, const SoloOwner& owner, uint32_t step, crucible::RegionNode* region) {
    const auto active = log.active(owner).expect("the log holds an active transaction");
    const TxFacts facts = facts_of(log, owner, active);
    CRUCIBLE_TEST_ASSERT(kTest, facts.status == crucible::TxStatus::ACTIVE);
    CRUCIBLE_TEST_ASSERT(kTest, facts.step == step);
    CRUCIBLE_TEST_ASSERT(kTest, holds(log, owner, active, *region));
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
void claim_passes_over_the_active_slot(::foundation::effects::Alloc alloc, const SoloOwner& owner,
                                       RollbackOrder order) {
    constexpr uint32_t kSlots = 16;
    Log log{};
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 14});
    std::array<crucible::RegionNode*, kSlots + 2> regions{};
    for (uint32_t step = 1; step <= kSlots + 1; ++step)
        regions[step] = region_of_step(alloc, arena, step);

    if (order == RollbackOrder::InARow) {
        for (uint32_t step = 1; step <= kSlots; ++step)
            static_cast<void>(publish(log, owner, step, regions[step]));
        for (uint32_t step = kSlots; step > 1; --step)
            CRUCIBLE_TEST_ASSERT(kTest, log.rollback(owner));
    } else {
        static_cast<void>(publish(log, owner, 1, regions[1]));
        for (uint32_t step = 2; step <= kSlots; ++step) {
            static_cast<void>(publish(log, owner, step, regions[step]));
            CRUCIBLE_TEST_ASSERT(kTest, log.rollback(owner));
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, log.size(owner) == kSlots);
    expect_active(log, owner, 1, regions[1]);

    const auto displaced = publish(log, owner, kSlots + 1, regions[kSlots + 1]).expect("the first one is displaced");
    const TxFacts facts = facts_of(log, owner, displaced);
    CRUCIBLE_TEST_ASSERT(kTest, facts.status == crucible::TxStatus::SUPERSEDED);
    CRUCIBLE_TEST_ASSERT(kTest, facts.step == 1);
    CRUCIBLE_TEST_ASSERT(kTest, holds(log, owner, displaced, *regions[1]));
    expect_active(log, owner, kSlots + 1, regions[kSlots + 1]);
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner) == ::fixy::some(displaced));

    CRUCIBLE_TEST_ASSERT(kTest, log.rollback(owner));
    expect_active(log, owner, 1, regions[1]);
}

// The rollback target is the transaction that the last activation displaced.
// The activations here do not follow the order of the claims, so the
// rollback target is not the superseded transaction with the newest claim.
void previous_follows_the_order_of_displacements(::foundation::effects::Alloc alloc, const SoloOwner& owner) {
    Log log{};
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 12});
    std::array<crucible::RegionNode*, 5> regions{};
    for (uint32_t step = 1; step <= 4; ++step)
        regions[step] = region_of_step(alloc, arena, step);
    const auto first = commit_step(log, owner, 1, regions[1]);
    const auto second = commit_step(log, owner, 2, regions[2]);
    const auto third = commit_step(log, owner, 3, regions[3]);
    const auto fourth = commit_step(log, owner, 4, regions[4]);

    // The third transaction displaces nothing, the first displaces the
    // third, and the second displaces the first.
    CRUCIBLE_TEST_ASSERT(kTest, log.activate(owner, third).expect("fresh").is_none());
    CRUCIBLE_TEST_ASSERT(kTest, log.activate(owner, first).expect("fresh") == ::fixy::some(third));
    CRUCIBLE_TEST_ASSERT(kTest, log.activate(owner, second).expect("fresh") == ::fixy::some(first));
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner) == ::fixy::some(first));

    CRUCIBLE_TEST_ASSERT(kTest, log.rollback(owner));
    expect_active(log, owner, 1, regions[1]);
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner) == ::fixy::some(third));

    // A second displacement of the restored first transaction comes after
    // the displacement of the third, although its claim comes before.
    CRUCIBLE_TEST_ASSERT(kTest, log.activate(owner, fourth).expect("fresh") == ::fixy::some(first));
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner) == ::fixy::some(first));

    CRUCIBLE_TEST_ASSERT(kTest, log.rollback(owner));
    expect_active(log, owner, 1, regions[1]);
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner) == ::fixy::some(third));
    CRUCIBLE_TEST_ASSERT(kTest, log.rollback(owner));
    expect_active(log, owner, 3, regions[3]);
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, !log.rollback(owner));
}

// A full ring recycles its oldest slot, but a claim must keep the rollback
// target.  Activations out of order put the target in the oldest slot.  The
// second transaction displaces nothing, the first displaces the second, and
// the third displaces the first.  The claim must not recycle the slot of the
// first, and a rollback after the claim restores the first with its region.
// The fourth transaction stays committed, and its slot does not change.
void claim_keeps_the_target_of_out_of_order_activations(::foundation::effects::Alloc alloc, const SoloOwner& owner) {
    SmallestLog log{};
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 12});
    std::array<crucible::RegionNode*, 6> regions{};
    for (uint32_t step = 1; step <= 5; ++step)
        regions[step] = region_of_step(alloc, arena, step);
    const auto first = commit_step(log, owner, 1, regions[1]);
    const auto second = commit_step(log, owner, 2, regions[2]);
    const auto third = commit_step(log, owner, 3, regions[3]);
    const auto fourth = commit_step(log, owner, 4, regions[4]);
    CRUCIBLE_TEST_ASSERT(kTest, log.activate(owner, second).expect("fresh").is_none());
    CRUCIBLE_TEST_ASSERT(kTest, log.activate(owner, first).expect("fresh") == ::fixy::some(second));
    CRUCIBLE_TEST_ASSERT(kTest, log.activate(owner, third).expect("fresh") == ::fixy::some(first));
    CRUCIBLE_TEST_ASSERT(kTest, log.size(owner) == 4);
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner) == ::fixy::some(first));

    // The claim of the fifth step reuses the slot of the second, and each
    // other handle still names its transaction.
    const auto fifth = commit_step(log, owner, 5, regions[5]);
    CRUCIBLE_TEST_ASSERT(kTest, fifth != first && fifth != third && fifth != fourth);
    CRUCIBLE_TEST_ASSERT(kTest, log.with_tx(owner, second, facts_of_tx).is_err());
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner) == ::fixy::some(first));
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, first).step == 1);
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, fourth).status == crucible::TxStatus::COMMITTED);
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, fourth).step == 4);

    CRUCIBLE_TEST_ASSERT(kTest, log.rollback(owner));
    expect_active(log, owner, 1, regions[1]);
}

// A rollback between a claim and its activation moves the rollback target
// to an older slot.  Four publications in sequence fill the ring, the fifth
// claim recycles the slot of the first, and a rollback restores the third.
// The second is then the rollback target, in the oldest slot.  The caller
// does not commit the fifth transaction, and it claims a sixth.  That claim
// must not recycle the slot of the second or the slot of the third.
void claim_keeps_the_target_after_a_rollback_before_activation(::foundation::effects::Alloc alloc,
                                                               const SoloOwner& owner) {
    SmallestLog log{};
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 12});
    std::array<crucible::RegionNode*, 5> regions{};
    for (uint32_t step = 1; step <= 4; ++step) {
        regions[step] = region_of_step(alloc, arena, step);
        static_cast<void>(publish(log, owner, step, regions[step]));
    }
    auto fifth = log.begin_tx(owner, ::fixy::session::StepId{5});
    CRUCIBLE_TEST_ASSERT(kTest, log.rollback(owner));
    expect_active(log, owner, 3, regions[3]);
    const auto target = log.previous(owner).expect("the second is the rollback target");
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, target).step == 2);
    log.abandon(owner, std::move(fifth));

    auto sixth = log.begin_tx(owner, ::fixy::session::StepId{6});
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner) == ::fixy::some(target));
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, target).step == 2);
    expect_active(log, owner, 3, regions[3]);
    CRUCIBLE_TEST_ASSERT(kTest, log.with_draft(owner, sixth, facts_of_tx).expect("fresh").step == 6);
    log.abandon(owner, std::move(sixth));

    CRUCIBLE_TEST_ASSERT(kTest, log.rollback(owner));
    expect_active(log, owner, 2, regions[2]);
}

// A CommittedTx outlives each later claim, and the log refuses it once a
// claim reused its slot.  The count of claims that reuses a slot is a fact of
// run time, so the refusal is a run-time compare of the claim.
void committed_goes_stale_after_the_ring_wraps(::foundation::effects::Alloc alloc, const SoloOwner& owner) {
    SmallestLog log{};
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 12});
    crucible::RegionNode* const region = region_of_step(alloc, arena, 1);
    const auto kept = commit_step(log, owner, 1, region);
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, kept).status == crucible::TxStatus::COMMITTED);
    // Four more claims wrap the ring of four slots onto the slot of the kept
    // transaction, which is neither active nor a rollback target.
    for (uint32_t step = 2; step <= 5; ++step)
        log.abandon(owner, log.begin_tx(owner, ::fixy::session::StepId{step}));
    CRUCIBLE_TEST_ASSERT(kTest, log.with_tx(owner, kept, facts_of_tx).is_err());
    const auto refused = log.activate(owner, kept);
    CRUCIBLE_TEST_ASSERT(kTest, refused.err() == ::fixy::some(crucible::TxRefusal::stale));
    CRUCIBLE_TEST_ASSERT(kTest, log.active(owner).is_none());
}

// A log of Unkeyed has no key, so a claim can run while a draft lives.  A
// draft of step 100 stays open while four more claims wrap the ring of four
// slots onto its slot, and the log refuses the read and the commit of the
// draft: they would name the transaction of a later claim.
void draft_goes_stale_on_a_log_of_unkeyed(::foundation::effects::Alloc alloc, const SoloOwner& owner) {
    SmallestLog log{};
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 12});
    crucible::RegionNode* const region = region_of_step(alloc, arena, 1);
    auto held = log.begin_tx(owner, ::fixy::session::StepId{100});
    CRUCIBLE_TEST_ASSERT(kTest, log.with_draft(owner, held, facts_of_tx).expect("fresh").step == 100);
    for (uint32_t step = 101; step <= 104; ++step)
        log.abandon(owner, log.begin_tx(owner, ::fixy::session::StepId{step}));
    CRUCIBLE_TEST_ASSERT(kTest, log.with_draft(owner, held, facts_of_tx).is_err());
    const auto refused =
        log.commit(owner, std::move(held), arena_region(*region), region->content_hash, region->merkle_hash);
    CRUCIBLE_TEST_ASSERT(kTest, refused.err() == ::fixy::some(crucible::TxRefusal::stale));
    CRUCIBLE_TEST_ASSERT(kTest, log.active(owner).is_none());
}

// A keyed log takes its key at each door.  The draft holds the key, and the
// commit gives it back after the CommittedTx.
void keyed_log_passes_its_key(::foundation::effects::Alloc alloc, const SoloOwner& owner) {
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 12});
    crucible::RegionNode* const region = region_of_step(alloc, arena, 1);
    auto minted = ::fixy::mint_cyclic_buffer<crucible::Transaction, 4>();
    auto& ring = minted.at(::fixy::index<0>());
    auto& key = minted.at(::fixy::index<1>());
    auto log = crucible::build_transaction_log<SoloOwner>(std::move(ring));

    auto draft = log.begin_tx(std::move(key), owner, ::fixy::session::StepId{1});
    auto committed =
        log.commit(owner, std::move(draft), arena_region(*region), region->content_hash, region->merkle_hash);
    const auto first = committed.at(::fixy::index<0>());
    auto& key_back = committed.at(::fixy::index<1>());
    CRUCIBLE_TEST_ASSERT(kTest, log.activate(key_back, owner, first).expect("fresh").is_none());
    CRUCIBLE_TEST_ASSERT(kTest, log.active(key_back, owner) == ::fixy::some(first));
    const std::uint64_t step =
        log.with_tx(key_back, owner, first, [](crucible::Transaction const& tx) noexcept { return tx.step_id.get(); })
            .expect("fresh");
    CRUCIBLE_TEST_ASSERT(kTest, step == 1);
    CRUCIBLE_TEST_ASSERT(kTest, !log.rollback(key_back, owner));
}

// The stamps of a log are places of its own count, so two runs of one
// computation give two logs with the same stamps.  A clock time in a stamp
// made two such logs differ.
constexpr uint32_t kRunSteps = 6;
using RunStamps = ::fixy::FixedArray<uint64_t, kRunSteps>;

// Publishes kRunSteps regions into a log of its own, rolls back after each
// third, and gives the stamp of each transaction at the end.
[[nodiscard]] RunStamps publish_and_stamp(::foundation::effects::Alloc alloc, ::fixy::BumpArena<DagArena>& arena,
                                          const SoloOwner& owner) {
    Log log{};
    auto minted = ::foundation::core::mint_fixed_array<::fixy::Option<Log::Committed>, kRunSteps>();
    auto& txs = minted.at(::fixy::index<0>());
    auto& key = minted.at(::fixy::index<1>());
    for (const ::fixy::Index<kRunSteps> slot : ::fixy::IndexRange<kRunSteps>{}) {
        const uint32_t step = static_cast<uint32_t>(slot.raw()) + 1;
        crucible::RegionNode& region = *region_of_step(alloc, arena, step);
        const Log::Committed committed = commit_step(log, owner, step, &region);
        static_cast<void>(log.activate(owner, committed).expect("a fresh commit activates"));
        if (step % 3 == 0) CRUCIBLE_TEST_ASSERT(kTest, log.rollback(owner));
        txs.with_element(key, slot, [committed](::fixy::Option<Log::Committed>& element) noexcept {
            element = ::fixy::some(committed);
        });
    }
    RunStamps stamps{};
    for (const ::fixy::Index<kRunSteps> slot : stamps.indices()) {
        ::fixy::Option<Log::Committed> held = txs.at(slot);
        const uint64_t stamp = facts_of(log, owner, std::move(held).expect("each step holds a transaction")).stamp;
        stamps.with_element(slot, [stamp](uint64_t& element) noexcept { element = stamp; });
    }
    return stamps;
}

void two_runs_of_one_computation_give_one_log(::foundation::effects::Alloc alloc, const SoloOwner& owner) {
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 13});
    const RunStamps first = publish_and_stamp(alloc, arena, owner);
    const RunStamps second = publish_and_stamp(alloc, arena, owner);
    CRUCIBLE_TEST_ASSERT(kTest, first == second);
    for (const uint64_t stamp : first)
        CRUCIBLE_TEST_ASSERT(kTest, stamp != 0);
}

}  // namespace

[[gnu::cold]] int main() {
    auto test = ::foundation::effects::testing::test();
    const SoloOwner owner = SoloOwner::claim();
    Log log{};
    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 12});

    auto const ops_run = arena.array(test.alloc, 1, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::TraceEntry> const ops = ops_run.view();
    ops.get(0).expect("the run holds one op")->schema_hash = crucible::SchemaHash{0xFEED};
    const crucible::DagRef<crucible::RegionNode> region1 = crucible::make_region(test.alloc, arena, ops_run);
    // A commit requires a non-zero merkle root, so the hash is recomputed
    // first. The production path does the same thing in the same order.
    // Synthesizing a hash instead would take the test off that path.
    crucible::recompute_merkle(*region1);
    CRUCIBLE_TEST_ASSERT(kTest, region1->merkle_hash.raw() != 0);

    auto const ops2_run = arena.array(test.alloc, 1, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::TraceEntry> const ops2 = ops2_run.view();
    ops2.get(0).expect("the run holds one op")->schema_hash = crucible::SchemaHash{0xBEEF};
    const crucible::DagRef<crucible::RegionNode> region2 = crucible::make_region(test.alloc, arena, ops2_run);
    crucible::recompute_merkle(*region2);
    CRUCIBLE_TEST_ASSERT(kTest, region2->merkle_hash.raw() != 0);

    CRUCIBLE_TEST_ASSERT(kTest, log.size(owner) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, log.active(owner).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner).is_none());

    auto draft1 = log.begin_tx(owner, ::fixy::session::StepId{1});
    const TxFacts claimed1 = log.with_draft(owner, draft1, facts_of_tx).expect("fresh");
    CRUCIBLE_TEST_ASSERT(kTest, claimed1.step == 1);
    CRUCIBLE_TEST_ASSERT(kTest, claimed1.status == crucible::TxStatus::RECORDING);
    CRUCIBLE_TEST_ASSERT(kTest, log.size(owner) == 1);
    // The log stamped the claim with the first place of its count.
    CRUCIBLE_TEST_ASSERT(kTest, claimed1.stamp == 1);
    // A transaction that the log began and did not commit holds no region.
    CRUCIBLE_TEST_ASSERT(kTest, !claimed1.has_region);

    const crucible::ContentHash hash1 = region1->content_hash;
    const crucible::MerkleHash merkle1 = region1->merkle_hash;
    const auto tx1 = log.commit(owner, std::move(draft1), arena_region(*region1), hash1, merkle1).expect("fresh");
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, tx1).status == crucible::TxStatus::COMMITTED);
    const auto reads_the_hashes = [hash1, merkle1](crucible::Transaction const& tx) noexcept {
        return tx.content_hash == hash1 && tx.merkle_root == merkle1;
    };
    const bool holds_hashes = log.with_tx(owner, tx1, reads_the_hashes).expect("fresh");
    CRUCIBLE_TEST_ASSERT(kTest, holds_hashes);
    CRUCIBLE_TEST_ASSERT(kTest, holds(log, owner, tx1, *region1));
    CRUCIBLE_TEST_ASSERT(kTest, !holds(log, owner, tx1, *region2));
    // Each event takes the next place, so the commit comes after the claim.
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, tx1).stamp == 2);

    CRUCIBLE_TEST_ASSERT(kTest, log.activate(owner, tx1).expect("fresh").is_none());
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, tx1).status == crucible::TxStatus::ACTIVE);
    CRUCIBLE_TEST_ASSERT(kTest, log.active(owner) == ::fixy::some(tx1));
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner).is_none());
    // An active transaction is not committed, so a second activation is refused.
    CRUCIBLE_TEST_ASSERT(kTest, log.activate(owner, tx1).err() == ::fixy::some(crucible::TxRefusal::not_committed));

    auto draft2 = log.begin_tx(owner, ::fixy::session::StepId{2});
    CRUCIBLE_TEST_ASSERT(kTest, log.with_draft(owner, draft2, facts_of_tx).expect("fresh").step == 2);
    CRUCIBLE_TEST_ASSERT(kTest, log.size(owner) == 2);

    const crucible::ContentHash hash2 = region2->content_hash;
    const crucible::MerkleHash merkle2 = region2->merkle_hash;
    const auto tx2 = log.commit(owner, std::move(draft2), arena_region(*region2), hash2, merkle2).expect("fresh");
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, tx2).status == crucible::TxStatus::COMMITTED);

    CRUCIBLE_TEST_ASSERT(kTest, log.activate(owner, tx2).expect("fresh") == ::fixy::some(tx1));
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, tx1).status == crucible::TxStatus::SUPERSEDED);
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, tx2).status == crucible::TxStatus::ACTIVE);
    CRUCIBLE_TEST_ASSERT(kTest, log.active(owner) == ::fixy::some(tx2));
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner) == ::fixy::some(tx1));

    // The activation of tx1 took place 3, the claim and the commit of tx2
    // took 4 and 5, the displacement of tx1 took 6 and the activation of tx2
    // took 7.
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, tx1).stamp == 6 && facts_of(log, owner, tx2).stamp == 7);

    CRUCIBLE_TEST_ASSERT(kTest, log.rollback(owner));
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, tx1).status == crucible::TxStatus::ACTIVE);
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, tx2).status == crucible::TxStatus::ROLLED_BACK);
    // The rollback stamps the current transaction first, then the restored one.
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, tx2).stamp == 8 && facts_of(log, owner, tx1).stamp == 9);
    CRUCIBLE_TEST_ASSERT(kTest, log.active(owner) == ::fixy::some(tx1));
    CRUCIBLE_TEST_ASSERT(kTest, log.previous(owner).is_none() && "no more SUPERSEDED after rollback");
    CRUCIBLE_TEST_ASSERT(kTest, !log.rollback(owner) && "rollback with no SUPERSEDED must return false");

    // More entries than the ring holds, so it wraps at least once.
    for (uint32_t i = 3; i <= 32; i++) {
        crucible::TraceEntry e{};
        e.schema_hash = crucible::SchemaHash{static_cast<uint64_t>(i)};
        const auto entry_run = arena.array<crucible::TraceEntry>(test.alloc, 1, e);
        const crucible::DagRef<crucible::RegionNode> r = crucible::make_region(test.alloc, arena, entry_run);
        // Recomputed before the commit, for the reason given above.
        crucible::recompute_merkle(*r);
        static_cast<void>(publish(log, owner, i, &*r));
    }
    // The loop ends at 32, so the last transaction is the active one.
    const auto last = log.active(owner).expect("the last publication is active");
    CRUCIBLE_TEST_ASSERT(kTest, facts_of(log, owner, last).step == 32);

    claim_passes_over_the_active_slot(test.alloc, owner, RollbackOrder::AfterEachPublication);
    claim_passes_over_the_active_slot(test.alloc, owner, RollbackOrder::InARow);
    previous_follows_the_order_of_displacements(test.alloc, owner);
    claim_keeps_the_target_of_out_of_order_activations(test.alloc, owner);
    claim_keeps_the_target_after_a_rollback_before_activation(test.alloc, owner);
    committed_goes_stale_after_the_ring_wraps(test.alloc, owner);
    draft_goes_stale_on_a_log_of_unkeyed(test.alloc, owner);
    keyed_log_passes_its_key(test.alloc, owner);
    two_runs_of_one_computation_give_one_log(test.alloc, owner);

    ::fixy::check::pass(kTest, "test_transaction: all tests passed\n");
    return 0;
}
