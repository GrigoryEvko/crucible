#pragma once

// One iteration is one transaction. It records, then commits once its region
// is built and hashed, then becomes the live one. The transaction it displaced
// is kept rather than discarded, so a regression can restore it.
//
// One thread owns the log, and every state read here is a plain read for that
// reason. The type holds that rule instead of a comment: each member takes a
// proof of type Owner, and only the owning thread can hold one. A second
// thread that wants a change sends it to the owner as a message.

#include <crucible/Types.h>
#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>
#include <fixy/Mutation.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Frame.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible {

// A transaction holds its region by Ref only, so this header does not need
// the definition from MerkleDag.h.
struct RegionNode;

enum class TxStatus : uint8_t {
    RECORDING,  // the ring is accepting operations for this transaction
    CLOSED,  // an iteration boundary was reached and the ring is sealed
    COMMITTED,  // the region is built and its hashes are computed
    ACTIVE,  // this region is the live one
    SUPERSEDED,  // a newer transaction took over, but this one is kept
    ROLLED_BACK,  // quality regressed and the previous transaction was restored
};

struct Transaction {
    // The region of a commit: a Ref of a region in the arena, which is
    // never null.  The log holds a region and gives it to its owner, and it
    // writes no region, so it holds a region of const.
    using ArenaRegion = ::fixy::Tagged<::fixy::Ref<RegionNode const>, ::fixy::tags::source::Arena>;
    // The region of a transaction, or no region before its commit.
    using ArenaRegionLink = ::fixy::Tagged<::fixy::Option<::fixy::Ref<RegionNode const>>, ::fixy::tags::source::Arena>;

    static_assert(sizeof(ArenaRegionLink) == sizeof(::fixy::Option<::fixy::Ref<RegionNode const>>),
                  "the tag adds no byte to the link, and the empty link is the null pointer");

    // Replay determinism rests on this, so the type rejects a write that goes
    // backwards or wraps. Recycling a slot deliberately returns it to zero
    // before the new value is set: per-transaction monotonicity restarts with
    // the slot, and it is the log's own fill counter that keeps the sequence
    // of slots ordered.
    ::fixy::Monotonic<uint64_t> step_id = ::fixy::mint_monotonic<uint64_t>(0);
    // The place of the last displacement of this transaction in the order of
    // the displacements of its log, or zero when no activation displaced it.
    // A rollback restores the superseded transaction with the largest place,
    // so the order of the claims does not choose the rollback target.  A
    // restored transaction that the log displaces again gets a later place.
    ::fixy::Monotonic<uint64_t> displacement_order = ::fixy::mint_monotonic<uint64_t>(0);
    ContentHash content_hash;  // zero until the transaction commits
    MerkleHash merkle_root;  // zero until the transaction commits
    ArenaRegionLink region{};  // no region until the transaction commits
    // The place of the last event of this transaction in the order of the
    // events of its log: a claim, a commit, an activation, a displacement or
    // a rollback.  Zero until the log stamps the transaction.  The log keeps
    // the count, so two runs of one computation give the same stamps.  A time
    // for a person goes to the sink of fixy/os/TimingSink.h, which only a
    // context whose row holds Nondet reads.
    ::fixy::Monotonic<uint64_t> event_order = ::fixy::mint_monotonic<uint64_t>(0);
    // The place of the claim of this transaction in the order of the claims
    // of its log.  A CommittedTx holds it, so the log tells a transaction from
    // a later one in the same slot.
    ::fixy::Monotonic<uint64_t> claim_order = ::fixy::mint_monotonic<uint64_t>(0);
    TxStatus status = TxStatus::RECORDING;
    ::fixy::FixedArray<uint8_t, 7> pad{};
};

// True when the transaction holds the region.  The test compares the
// addresses of the two regions, so a copy of the region is not the region.
// A transaction with no region holds no region.
[[nodiscard]] inline bool holds_region(Transaction const& tx, ::fixy::Ref<const RegionNode> region) noexcept {
    return tx.region.value().is_some_and(
        [region](::fixy::Ref<RegionNode const> const& held) noexcept { return ::fixy::same_object(held, region); });
}

// A proof that the caller is the one thread that owns some state. It is
// empty, so it costs nothing at a call. It cannot be copied or moved, so a
// thread cannot hand it to another thread by value. It has no public default
// constructor, so only its owner can build one. It is not trivially copyable
// and not an implicit-lifetime type, so no bit_cast and no lifetime start over
// bytes can build one either.
//
// This is the ownership half of concurrent separation logic (O'Hearn,
// Resources, Concurrency and Local Reasoning, 2007): one thread owns the
// resource, and the frame rule keeps every other thread out of it.
//
// GCC counts a class whose copy and move are all deleted as trivially
// copyable, so an owner type also needs a destructor that is not trivial.
// An empty user-provided destructor is enough and costs nothing.
template <typename P>
concept OwnerProof =
    std::is_empty_v<P> && !std::is_copy_constructible_v<P> && !std::is_move_constructible_v<P>
    && !std::is_default_constructible_v<P> && !std::is_trivially_copyable_v<P> && !std::is_implicit_lifetime_v<P>;

// The refusal of an activation.
enum class TxRefusal : uint8_t {
    stale = 1,  // a later claim reused the slot of the transaction
    not_committed = 2,  // the transaction is not in the committed state
    not_recording = 3,  // the transaction of the draft is not in the recording state
};

template <uint32_t N, OwnerProof Owner, ::foundation::brand::IsBrand KeyBrand>
class TransactionLog;

namespace detail {

// The key that a draft holds: the LoanKey of a keyed log, and nothing for a
// log of Unkeyed.
struct NoTxKey final {};

template <class KeyBrand>
using tx_key_t =
    std::conditional_t<::foundation::core::IsKeyBrand<KeyBrand>, ::foundation::core::LoanKey<KeyBrand>, NoTxKey>;

}  // namespace detail

// The body of a read of a transaction: it takes the transaction as const, and
// it gives a value that holds no borrow.
template <class Body>
concept TxReadBody =
    ::fixy::CyclicBody<Body, Transaction const> && !std::is_void_v<std::invoke_result_t<Body&&, Transaction const&>>;

// A committed transaction of a log: the slot of the transaction and the place
// of its claim.  It holds no reference, so it outlives each later claim, and
// the log refuses it once a later claim reused its slot.  Only the log builds
// one, so no code builds one from integers.  The brand of a keyed log is in
// its type, so a CommittedTx of one log does not reach a second log of a
// different brand.
template <uint32_t N, class KeyBrand>
class CommittedTx final {
public:
    [[nodiscard]] friend constexpr bool operator==(CommittedTx const&, CommittedTx const&) noexcept = default;

private:
    template <uint32_t, OwnerProof, ::foundation::brand::IsBrand>
    friend class TransactionLog;

    constexpr CommittedTx(::fixy::Index<N> slot, uint64_t claim) noexcept : slot_{slot}, claim_{claim} {}

    ::fixy::Index<N> slot_;
    uint64_t claim_ = 0;
};

// The draft of the transaction of the last claim of a log.  Only a draft
// writes its transaction, and a draft ends only through the commit or the
// abandon of its log, which take it by reference.  It does not move, so no
// code makes a second draft of one claim.  begin_tx gives it as a prvalue.
// A draft of a keyed log holds the key of the log, so no other door of the
// log opens while it lives, and no later claim reuses its slot.
template <uint32_t N, class KeyBrand>
class [[nodiscard]][[= ::foundation::linear::linear{}]] TxDraft final {
public:
    TxDraft(TxDraft const&) = delete("a draft is the one writer of its transaction");
    TxDraft& operator=(TxDraft const&) = delete("a draft is the one writer of its transaction");
    TxDraft(TxDraft&&) = delete("a draft is the one writer of its transaction: it stays where begin_tx built it, and "
                                "commit or abandon takes it by reference");
    TxDraft& operator=(TxDraft&&) = delete("a draft names one transaction for its life");
    ~TxDraft() = default;

private:
    template <uint32_t, OwnerProof, ::foundation::brand::IsBrand>
    friend class TransactionLog;

    constexpr TxDraft(::fixy::Index<N> slot, uint64_t claim, detail::tx_key_t<KeyBrand>&& key) noexcept
        : slot_{slot}, claim_{claim}, key_{std::move(key)} {}

    // The end of the draft: it gives its key out.  Only the log calls it.
    [[nodiscard]] constexpr detail::tx_key_t<KeyBrand> end_() && noexcept { return std::move(key_); }

    ::fixy::Index<N> slot_;
    uint64_t claim_ = 0;
    [[no_unique_address]] detail::tx_key_t<KeyBrand> key_;
};

// The log stamps each event of a transaction with the next place of its own
// count, and it reads no clock.  A clock time in a log makes two runs of one
// computation give two logs.  So no method of the log takes the tag of
// Nondet, and a body whose context holds no Nondet can roll the log back.
//
// THE DOORS
//   begin_tx claims a slot and gives a TxDraft.  commit consumes the draft and
//   gives a CommittedTx, which activate takes, also after later claims and in
//   any order.  A claim never recycles the slot of the active transaction or
//   the slot of the rollback target.  active and previous give the CommittedTx
//   of their transaction, and with_tx lends a transaction as const to a body.
//   No door gives a reference to a transaction out.
//
// THE KEY
//   A log of a key brand comes from build_transaction_log, over a ring of
//   mint_cyclic_buffer, whose LoanKey is the key of the log.
//   begin_tx takes the key by move, the draft holds it, and commit and abandon
//   give it back.  Each other door that writes or reads a transaction borrows
//   the key, so no door opens while a draft lives.  The doors with no key of a
//   log of Unkeyed serve a member log, which has no key until the root doors
//   of the owner-key series give one.  They wait for removal, and only the log
//   of the Vigil uses them.
//
// THE TRANSFER FACT.  The log states sendable{} (foundation/Transfer.h).  Each
// member of the log takes the proof of its owner, and the proof stays on the
// thread that owns the log (OwnerProof).  So a borrow of the log that a job
// uses on the thread of its owner while the caller waits has one user at a
// time.  No field is bound to one thread: the log holds no claim and no cell
// of a thread, and no reference into itself.
template <uint32_t N, OwnerProof Owner, ::foundation::brand::IsBrand KeyBrand = ::foundation::core::Unkeyed>
class[[= ::foundation::transfer::sendable{}]]
     [[= ::foundation::core::loan_key_mark_when<::foundation::core::IsKeyBrand<KeyBrand>>]] TransactionLog {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
    // A claim in a full ring keeps two slots: the slot of the active
    // transaction and the slot of the rollback target.  The ring must hold
    // a third slot for the claim.  N is a power of 2, and the smallest such
    // ring holds 4 slots.  With the fourth slot, a claim also never recycles
    // the slot of the last claim.
    static_assert(N >= 4,
                  "N must be at least 4: a claim keeps the slots of the active transaction and the rollback target, "
                  "and N is a power of 2");

    static constexpr bool is_keyed_ = ::foundation::core::IsKeyBrand<KeyBrand>;
    using key_type = ::foundation::core::LoanKey<KeyBrand>;
    // The key that a door that writes gives to the ring: the key of a keyed
    // log, or an empty value for a log of Unkeyed.
    using write_key = detail::tx_key_t<KeyBrand>;

public:
    // The slots, the write cursor and the fill counter as one composition, so
    // the wrap masking and the saturating fill live in the type rather than
    // being open-coded at each use. The fill saturates at the capacity instead
    // of failing, and never moves backwards.  Only the log reaches its ring,
    // and each door of the log that writes takes the key of a keyed log, so
    // the ring takes no key of its own.
    using Ring = ::fixy::CyclicBuffer<Transaction, N>;
    using Committed = CommittedTx<N, KeyBrand>;
    using Draft = TxDraft<N, KeyBrand>;
    using Slot = ::fixy::Index<N>;

    TransactionLog() noexcept
        requires(!is_keyed_)
    = default;

    // The log that takes the place of a new keyed ring of mint_cyclic_buffer,
    // which build_transaction_log gives.  The log takes the brand of the
    // ring, so the key of the ring opens the log, and it starts empty, as the
    // new ring is.
    explicit TransactionLog(::fixy::CyclicBuffer<Transaction, N, KeyBrand>&& minted) noexcept
        requires is_keyed_
    {
        static_cast<void>(minted);
    }

    TransactionLog(const TransactionLog&) = delete("TransactionLog holds the one active transaction of its ring");
    TransactionLog&
    operator=(const TransactionLog&) = delete("TransactionLog holds the one active transaction of its ring");
    TransactionLog(TransactionLog&&) = delete("a draft and a CommittedTx name a slot of this log");
    TransactionLog& operator=(TransactionLog&&) = delete("a draft and a CommittedTx name a slot of this log");

    // ── The doors with no key of a log of Unkeyed (THE KEY) ────────────────

    [[nodiscard, gnu::cold]] Draft begin_tx(Owner const& /*owner*/, uint64_t step_id) noexcept
        requires(!is_keyed_)
    {
        detail::NoTxKey key{};
        Slot const slot = claim_(key, step_id);
        return Draft{slot, claim_of_(slot), std::move(key)};
    }

    // Commits the transaction of the draft.  A zero root is refused. Zero is
    // what a subtree hash reads before it is computed, and it is also what a
    // freshly recycled slot holds, so a committed entry carrying zero is
    // indistinguishable from an uncommitted one and any search over the log
    // becomes ambiguous. Refusing it here also catches the case where the
    // commit runs before the hash was recomputed.
    //
    // A log of Unkeyed has no key, so a later begin_tx can run while a draft
    // lives, and N claims reuse the slot of the draft.  So the commit compares
    // the claim and the recording state, and refuses a stale draft.  The keyed
    // member form of the consumer series removes the compare, because a keyed
    // draft holds the key and no claim runs while it lives.
    [[nodiscard]] ::fixy::Result<Committed, TxRefusal> commit(Owner const& /*owner*/, Draft&& draft,
                                                              Transaction::ArenaRegion region, ContentHash content_hash,
                                                              MerkleHash merkle_root) noexcept
        requires(!is_keyed_)
    {
        Committed const committed{draft.slot_, draft.claim_};
        detail::NoTxKey key = std::move(draft).end_();
        if (claim_of_(committed.slot_) != committed.claim_) return ::fixy::err(TxRefusal::stale);
        if (!is_recording_(committed.slot_)) return ::fixy::err(TxRefusal::not_recording);
        commit_(key, committed, region, content_hash, merkle_root);
        return committed;
    }

    // Ends the draft with no commit.  The transaction stays in its slot in the
    // recording state, and a later claim reuses the slot.
    void abandon(Owner const& /*owner*/, Draft&& draft) noexcept
        requires(!is_keyed_)
    {
        static_cast<void>(std::move(draft).end_());
    }

    // Makes a committed transaction the active one, and gives the transaction
    // that it displaces, the target of a rollback, or no value.
    [[nodiscard]] ::fixy::Result<::fixy::Option<Committed>, TxRefusal> activate(Owner const& /*owner*/,
                                                                                Committed committed) noexcept
        requires(!is_keyed_)
    {
        detail::NoTxKey key{};
        return activate_(key, committed);
    }

    // Restores the most recently displaced transaction and marks the current
    // one rolled back. Returns false when there is nothing to restore.
    [[nodiscard]] bool rollback(Owner const& /*owner*/) noexcept
        requires(!is_keyed_)
    {
        detail::NoTxKey key{};
        return rollback_(key);
    }

    // The active transaction, or no value when no transaction is active.
    [[nodiscard]] ::fixy::Option<Committed> active(Owner const& /*owner*/) const noexcept
        requires(!is_keyed_)
    {
        return committed_of_(active_);
    }

    // The rollback target, or no value (THE TARGET).
    [[nodiscard]] ::fixy::Option<Committed> previous(Owner const& /*owner*/) const noexcept
        requires(!is_keyed_)
    {
        return committed_of_(previous_slot_());
    }

    // Lends a committed transaction as const to body, or refuses a transaction
    // whose slot a later claim reused.
    template <class Body>
        requires(!is_keyed_) && TxReadBody<Body>
    [[nodiscard]] ::fixy::Result<std::invoke_result_t<Body&&, Transaction const&>, TxRefusal>
    with_tx(Owner const& /*owner*/, Committed committed, Body&& body) const noexcept {
        return with_tx_(committed, static_cast<Body&&>(body));
    }

    // Lends the transaction of a draft as const to body, or refuses a draft
    // whose slot a later claim reused, for the reason of the commit above.
    template <class Body>
        requires(!is_keyed_) && TxReadBody<Body>
    [[nodiscard]] ::fixy::Result<std::invoke_result_t<Body&&, Transaction const&>, TxRefusal>
    with_draft(Owner const& /*owner*/, Draft const& draft, Body&& body) const noexcept {
        return with_tx_(Committed{draft.slot_, draft.claim_}, static_cast<Body&&>(body));
    }

    // ── The doors of a log of a key brand (THE KEY) ────────────────────────

    [[nodiscard, gnu::cold]] Draft begin_tx(key_type&& key, Owner const& /*owner*/, uint64_t step_id) noexcept
        requires is_keyed_
    {
        Slot const slot = claim_(key, step_id);
        return Draft{slot, claim_of_(slot), std::move(key)};
    }

    // The commit of a keyed log, which gives the key back after the
    // CommittedTx.
    [[nodiscard]] ::fixy::Record<Committed, key_type> commit(Owner const& /*owner*/, Draft&& draft,
                                                             Transaction::ArenaRegion region, ContentHash content_hash,
                                                             MerkleHash merkle_root) noexcept
        requires is_keyed_
    {
        Committed const committed{draft.slot_, draft.claim_};
        key_type key = std::move(draft).end_();
        commit_(key, committed, region, content_hash, merkle_root);
        return {committed, std::move(key)};
    }

    [[nodiscard]] key_type abandon(Owner const& /*owner*/, Draft&& draft) noexcept
        requires is_keyed_
    {
        return std::move(draft).end_();
    }

    [[nodiscard]] ::fixy::Result<::fixy::Option<Committed>, TxRefusal> activate(key_type& key, Owner const& /*owner*/,
                                                                                Committed committed) noexcept
        requires is_keyed_
    {
        return activate_(key, committed);
    }

    [[nodiscard]] bool rollback(key_type& key, Owner const& /*owner*/) noexcept
        requires is_keyed_
    {
        return rollback_(key);
    }

    [[nodiscard]] ::fixy::Option<Committed> active(key_type& /*key*/, Owner const& /*owner*/) const noexcept
        requires is_keyed_
    {
        return committed_of_(active_);
    }

    [[nodiscard]] ::fixy::Option<Committed> previous(key_type& /*key*/, Owner const& /*owner*/) const noexcept
        requires is_keyed_
    {
        return committed_of_(previous_slot_());
    }

    template <class Body>
        requires is_keyed_ && TxReadBody<Body> && ::foundation::core::ReachesNoLoanKey<Body, KeyBrand>
    [[nodiscard]] ::fixy::Result<std::invoke_result_t<Body&&, Transaction const&>, TxRefusal>
    with_tx(key_type& /*key*/, Owner const& /*owner*/, Committed committed, Body&& body) const noexcept {
        return with_tx_(committed, static_cast<Body&&>(body));
    }

    // Lends the transaction of a draft as const to body.  A keyed draft holds
    // the key, so no claim reused its slot, and the door refuses nothing.
    template <class Body>
        requires is_keyed_ && TxReadBody<Body> && ::foundation::core::ReachesNoLoanKey<Body, KeyBrand>
    [[nodiscard]] auto with_draft(Owner const& /*owner*/, Draft const& draft, Body&& body) const noexcept {
        return read_slot_(draft.slot_, static_cast<Body&&>(body));
    }

    // A door with no key of a keyed log binds to these overloads, so the
    // refusal gives its reason.
    Draft begin_tx(Owner const& owner, uint64_t step_id) noexcept
        requires is_keyed_
    = delete("a keyed TransactionLog claims a transaction only with its LoanKey: call begin_tx(std::move(key), owner, "
             "step)");
    bool rollback(Owner const& owner) noexcept
        requires is_keyed_
    = delete("a keyed TransactionLog rolls back only with its LoanKey: call rollback(key, owner)");
    ::fixy::Result<::fixy::Option<Committed>, TxRefusal> activate(Owner const& owner, Committed committed) noexcept
        requires is_keyed_
    = delete("a keyed TransactionLog activates a transaction only with its LoanKey: call activate(key, owner, tx)");
    ::fixy::Option<Committed> active(Owner const& owner) const noexcept
        requires is_keyed_
    = delete("a keyed TransactionLog reads its active transaction only with its LoanKey: call active(key, owner)");
    ::fixy::Option<Committed> previous(Owner const& owner) const noexcept
        requires is_keyed_
    = delete("a keyed TransactionLog reads its rollback target only with its LoanKey: call previous(key, owner)");

    // ── The doors of each log ──────────────────────────────────────────────

    // Gives the region of each transaction in the ring to visit.  A rollback
    // can make each such transaction active again, so the owner keeps each of
    // these regions.  Complexity: O(N) in the ring capacity.
    template <class Visit>
    void for_each_region(Owner const&, Visit&& visit) const {
        for (Slot const back : ::fixy::IndexRange<N>{}) {
            if (back.raw() >= ring_.size()) break;
            read_slot_(ring_.slot_back(back), [&visit](Transaction const& entry) noexcept {
                for (const ::fixy::Ref<RegionNode const>& region : entry.region.value())
                    visit(::fixy::some(region));
            });
        }
    }

    [[nodiscard]] uint32_t size(Owner const&) const {
        // The fill saturates at the capacity, which is far below what the
        // narrower type holds, so the conversion loses nothing.
        return static_cast<uint32_t>(ring_.size());
    }

private:
    // True when the slot holds the transaction of the given slot, if any.
    [[nodiscard]] static constexpr bool is_slot_of_(::fixy::Option<Slot> const& held, Slot slot) noexcept {
        return held.is_some_and([slot](Slot const& kept) noexcept { return kept == slot; });
    }

    // The read of the transaction of a slot, through the const door of the
    // ring, which takes no key.
    template <class Body>
    [[nodiscard]] auto read_slot_(Slot slot, Body&& body) const noexcept {
        return ring_.with_slot(slot, static_cast<Body&&>(body));
    }

    // The write of the transaction of a slot, and the write of a fresh claim.
    // Each takes the key of a keyed log, or the empty value of a log of
    // Unkeyed, as the proof that the door that calls it holds the key.
    template <class Body>
    void write_slot_(write_key& /*key*/, Slot slot, Body&& body) noexcept {
        ring_.with_slot(slot, static_cast<Body&&>(body));
    }
    template <class Body>
    void write_fresh_claim_(write_key& /*key*/, Body&& body) noexcept {
        ring_.with_fresh_claim(static_cast<Body&&>(body));
    }

    // The place of the claim of the transaction in a slot.
    [[nodiscard]] uint64_t claim_of_(Slot slot) const noexcept {
        return read_slot_(slot, [](Transaction const& tx) noexcept { return tx.claim_order.get(); });
    }

    // True when the transaction in a slot is in the recording state.
    [[nodiscard]] bool is_recording_(Slot slot) const noexcept {
        return read_slot_(slot, [](Transaction const& tx) noexcept { return tx.status == TxStatus::RECORDING; });
    }

    // The CommittedTx of the transaction in a slot, or no value.
    [[nodiscard]] ::fixy::Option<Committed> committed_of_(::fixy::Option<Slot> const& held) const noexcept {
        for (Slot const& slot : held)
            return ::fixy::some(Committed{slot, claim_of_(slot)});
        return ::fixy::none;
    }

    // Gives the transaction the next place of the count of the events.  The
    // count only grows, so a later event of one transaction gets a larger
    // place.  A slot that a claim recycles starts again at zero.
    void stamp_(Transaction& tx) noexcept {
        events_.bump();
        tx.event_order.advance(events_.get());
    }

    // Claims a slot for a new transaction of the step, and gives the slot.
    // A full ring claims its oldest slot, but two slots must stay.  A
    // rollback can make a transaction of any age the active one.  If a claim
    // recycles that slot, the live transaction ends.  Then activate finds the
    // new transaction in the active slot.  An activation out of order, or a
    // rollback before an activation, can put the rollback target in any slot.
    // If a claim recycles that slot, a rollback restores an older
    // transaction.
    [[nodiscard]] Slot claim_(write_key& key, uint64_t step_id) noexcept {
        ::fixy::Option<Slot> const target = previous_slot_();
        // A pass that finds a kept slot as the oldest claims that slot, which
        // makes it the newest entry.  The claim keeps two slots, and the ring
        // holds at least 4.  After two passes, the oldest slot holds neither.
        for (uint32_t pass = 0; pass < 2 && ring_.full(); ++pass) {
            Slot const oldest = ring_.slot_back(::fixy::index<N - 1>());
            if (!is_slot_of_(active_, oldest) && !is_slot_of_(target, oldest)) break;
            ring_.pass_slot();
        }
        Slot const slot = ring_.next_slot();
        claims_.bump();
        uint64_t const claim = claims_.get();
        // The fresh claim ends the life of the old transaction and starts a
        // new one in the slot.  An assignment would write step_id backward,
        // and its Monotonic refuses an assignment.  The value initialization
        // also sets each padding byte to zero.
        write_fresh_claim_(key, [this, step_id, claim](Transaction& tx) noexcept {
            tx.step_id.advance(step_id);
            tx.claim_order.advance(claim);
            stamp_(tx);
        });
        CRUCIBLE_POST(slot, !is_slot_of_(active_, slot));
        CRUCIBLE_POST(slot, !is_slot_of_(target, slot));
        bool const is_fresh = read_slot_(slot, [this, step_id](Transaction const& tx) noexcept {
            return tx.status == TxStatus::RECORDING && tx.step_id.get() == step_id
                && tx.event_order.get() == events_.get();
        });
        CRUCIBLE_POST(slot, is_fresh);
        return slot;
    }

    // Writes the commit of the transaction of a draft.  Only a draft reaches
    // this, and a draft names the transaction of its own claim in the
    // recording state.
    void commit_(write_key& key, Committed committed, Transaction::ArenaRegion region, ContentHash content_hash,
                 MerkleHash merkle_root) noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(merkle_root));
        write_slot_(key, committed.slot_, [this, region, content_hash, merkle_root](Transaction& tx) noexcept {
            // The region came in under the Arena tag, so the link keeps it.
            tx.region = ::fixy::mint_tagged<::fixy::tags::source::Arena>(
                ::fixy::Option<::fixy::Ref<RegionNode const>>::some(region.value()));
            tx.content_hash = content_hash;
            tx.merkle_root = merkle_root;
            tx.status = TxStatus::COMMITTED;
            stamp_(tx);
            // Dropping any one of these assignments leaves the field at the
            // value the slot was reset to, which reads as an uncommitted
            // transaction.
            CRUCIBLE_POST(true, holds_region(tx, region.value()));
            CRUCIBLE_POST(true, tx.content_hash == content_hash);
            CRUCIBLE_POST(true, tx.merkle_root == merkle_root);
        });
    }

    // The activation of activate.  The compare of the claim is a run-time
    // check, because a later claim reuses a slot after a count of claims
    // that no type of a CommittedTx can hold.
    [[nodiscard]] ::fixy::Result<::fixy::Option<Committed>, TxRefusal> activate_(write_key& key,
                                                                                 Committed committed) noexcept {
        if (claim_of_(committed.slot_) != committed.claim_) return ::fixy::err(TxRefusal::stale);
        bool const is_committed = read_slot_(
            committed.slot_, [](Transaction const& tx) noexcept { return tx.status == TxStatus::COMMITTED; });
        if (!is_committed) return ::fixy::err(TxRefusal::not_committed);

        ::fixy::Option<Committed> const displaced = committed_of_(active_);
        for (Slot const& previous : active_) {
            displacements_.bump();
            write_slot_(key, previous, [this](Transaction& tx) noexcept {
                tx.status = TxStatus::SUPERSEDED;
                tx.displacement_order.advance(displacements_.get());
                stamp_(tx);
            });
        }
        write_slot_(key, committed.slot_, [this](Transaction& tx) noexcept {
            tx.status = TxStatus::ACTIVE;
            stamp_(tx);
        });
        active_ = ::fixy::some(committed.slot_);
        // The displaced transaction must end up in the superseded state, since
        // that is what a rollback searches for.  It must also hold the newest
        // place in the order of displacements, because a rollback restores
        // the superseded transaction with the largest place.
        CRUCIBLE_POST(displaced, is_slot_of_(active_, committed.slot_));
        CRUCIBLE_POST(displaced, displaced.is_none_or([this](Committed const& previous) noexcept {
            return read_slot_(previous.slot_, [this](Transaction const& tx) noexcept {
                return tx.status == TxStatus::SUPERSEDED && tx.displacement_order.get() == displacements_.get();
            });
        }));
        return displaced;
    }

    // The rollback of rollback.
    [[nodiscard]] bool rollback_(write_key& key) noexcept {
        ::fixy::Option<Slot> const target = previous_slot_();
        if (target.is_none()) return false;
        for (Slot const& current : active_) {
            write_slot_(key, current, [this](Transaction& tx) noexcept {
                tx.status = TxStatus::ROLLED_BACK;
                stamp_(tx);
            });
        }
        for (Slot const& restored : target) {
            write_slot_(key, restored, [this](Transaction& tx) noexcept {
                tx.status = TxStatus::ACTIVE;
                stamp_(tx);
            });
        }
        active_ = target;
        return true;
    }

    // THE TARGET: the slot of the superseded transaction with the newest
    // displacement, or no value when no transaction in the ring is
    // superseded.  A caller can activate an older transaction after a newer
    // one, so the order of the claims does not give the target.  The scan is
    // O(N) in the ring capacity.
    [[nodiscard]] ::fixy::Option<Slot> previous_slot_() const noexcept {
        ::fixy::Option<Slot> target{};
        uint64_t target_order = 0;
        for (Slot const back : ::fixy::IndexRange<N>{}) {
            if (back.raw() >= ring_.size()) break;
            Slot const slot = ring_.slot_back(back);
            uint64_t const order = read_slot_(slot, [](Transaction const& entry) noexcept {
                return entry.status == TxStatus::SUPERSEDED ? entry.displacement_order.get() : uint64_t{0};
            });
            // A superseded transaction holds a place above zero, and each
            // displacement takes a new place, so no two superseded
            // transactions share a place, and the scan order cannot change
            // the result.
            if (order <= target_order) continue;
            target = ::fixy::some(slot);
            target_order = order;
            // No place is larger than the count of displacements.  When no
            // rollback undid the last displacement, the scan stops at its
            // transaction.
            if (order == displacements_.get()) break;
        }
        return target;
    }

    // The body of with_tx: the claim of the CommittedTx names the transaction
    // of the slot, or a later claim reused the slot.
    template <class Body>
    [[nodiscard]] ::fixy::Result<std::invoke_result_t<Body&&, Transaction const&>, TxRefusal>
    with_tx_(Committed committed, Body&& body) const noexcept {
        if (claim_of_(committed.slot_) != committed.claim_) return ::fixy::err(TxRefusal::stale);
        return read_slot_(committed.slot_, static_cast<Body&&>(body));
    }

    Ring ring_{};
    // The slot of the active transaction, or no value.  A claim never recycles
    // it.  The empty Option is the niche of the Index, so the slot takes the
    // bytes of one Index.
    ::fixy::Option<Slot> active_{};
    // The count of the claims.  Each claim advances it and gives the new value
    // to the transaction of the claim as its place.
    ::fixy::Monotonic<uint64_t> claims_ = ::fixy::mint_monotonic<uint64_t>(0);
    // The count of displacements.  Each activation that displaces a
    // transaction advances it and gives the new value to the displaced
    // transaction as its place.
    ::fixy::Monotonic<uint64_t> displacements_ = ::fixy::mint_monotonic<uint64_t>(0);
    // The count of the events of the log.  Each stamp advances it and gives
    // the new value to the transaction of the event.
    ::fixy::Monotonic<uint64_t> events_ = ::fixy::mint_monotonic<uint64_t>(0);
};

// A keyed TransactionLog over a keyed ring of mint_cyclic_buffer (THE KEY):
//   auto [ring, key] = ::fixy::mint_cyclic_buffer<Transaction, 16>();
//   auto log = build_transaction_log<Owner>(std::move(ring));
// The log takes the ring and its brand, so the key of the ring is the key of
// the log.  The log does not move, so it comes back as a prvalue.
template <OwnerProof Owner, std::size_t RingCount, ::foundation::brand::IsBrand Brand>
    requires ::foundation::core::IsKeyBrand<Brand>
[[nodiscard]] TransactionLog<static_cast<uint32_t>(RingCount), Owner, Brand>
build_transaction_log(::fixy::CyclicBuffer<Transaction, RingCount, Brand>&& ring) noexcept {
    return TransactionLog<static_cast<uint32_t>(RingCount), Owner, Brand>{std::move(ring)};
}

}  // namespace crucible
