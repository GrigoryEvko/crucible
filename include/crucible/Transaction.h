#pragma once

// One iteration is one transaction. It records, then commits once its region
// is built and hashed, then becomes the live one. The transaction it displaced
// is kept rather than discarded, so a regression can restore it.
//
// One thread owns the log, and every state read here is a plain read for that
// reason. The type holds that rule instead of a comment: each member takes a
// proof of type Owner, and only the owning thread can hold one. A second
// thread that wants a change sends it to the owner as a message.

#include <crucible/DagArena.h>
#include <crucible/Types.h>
#include <fixy/Core.h>
#include <fixy/CyclicBuffer.h>
#include <fixy/Mutation.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/session/StepId.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Identity.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible {

// The log holds the region of a transaction by Ref only, so this header does
// not need the definition from MerkleDag.h.
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
    // The region of a commit: a Ref of a region in a DAG arena, with the
    // brand of the arena, which is never null.  The log holds a region and
    // gives it to its owner, and it writes no region, so it holds a region of
    // const.
    using ArenaRegion = ::fixy::Tagged<::fixy::Ref<RegionNode const, DagArena>, ::fixy::tags::source::Arena>;
    // The region of a transaction, or no region before its commit.  The log
    // keeps it beside its ring, by the slot of the transaction, so a
    // transaction holds plain values and no borrow (region_of of the log).
    using ArenaRegionLink =
        ::fixy::Tagged<::fixy::Option<::fixy::Ref<RegionNode const, DagArena>>, ::fixy::tags::source::Arena>;

    static_assert(sizeof(ArenaRegionLink) == sizeof(::fixy::Option<::fixy::Ref<RegionNode const, DagArena>>),
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
    ::fixy::FixedArray<uint8_t, 15> pad{};
};

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
    stale = 1,  // a later claim reused the slot of the transaction, or the token names a different log
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

// A committed transaction of a log: the slot of the transaction, the place of
// its claim and the Identity of its log.  It holds no reference, so it
// outlives each later claim, and the log refuses it once a later claim reused
// its slot.  Only the log builds one, so no code builds one from integers.
//
// The brand of a keyed log is in its type, so a CommittedTx of one log does
// not reach a log of a different brand.  But a brand names the mint site of a
// log, and not one log: two runs of one site, such as two turns of a loop,
// give two logs of one brand, and each log of Unkeyed has the one brand
// Unkeyed.  The slot and the claim of a token then name a transaction of the
// second log too.  So the token carries the Identity of its log, and each door
// of the log compares it at run time with the claim.
template <uint32_t N, class KeyBrand>
class CommittedTx final {
public:
    [[nodiscard]] friend constexpr bool operator==(CommittedTx const&, CommittedTx const&) noexcept = default;

private:
    template <uint32_t, OwnerProof, ::foundation::brand::IsBrand>
    friend class TransactionLog;

    constexpr CommittedTx(::fixy::Index<N> slot, uint64_t claim, ::foundation::core::Identity log) noexcept
        : slot_{slot}, claim_{claim}, log_{log} {}

    ::fixy::Index<N> slot_;
    uint64_t claim_ = 0;
    ::foundation::core::Identity log_;
};

// The draft of the transaction of the last claim of a log.  Only a draft
// writes its transaction, and a draft ends only through the commit or the
// abandon of its log, which take it by reference.  It does not move, so no
// code makes a second draft of one claim.  begin_tx gives it as a prvalue.
// A draft of a keyed log holds the key of the log, so no door that takes that
// key opens while it lives.  A key of another log of the mint site has the
// same type, and it opens the doors of this log, so its claims can reuse the
// slot of the draft (the brand of a key of foundation/core/Frame.h).  A draft
// carries the Identity of its log, for the reason of a CommittedTx, and each
// door that takes a draft requires the Identity of its own log.  Each door
// that writes or reads the transaction of a keyed draft also requires the
// claim of the draft.
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

    constexpr TxDraft(::fixy::Index<N> slot, uint64_t claim, ::foundation::core::Identity log,
                      detail::tx_key_t<KeyBrand>&& key) noexcept
        : slot_{slot}, claim_{claim}, log_{log}, key_{std::move(key)} {}

    // The end of the draft: it gives its key out.  Only the log calls it.
    [[nodiscard]] constexpr detail::tx_key_t<KeyBrand> end_() && noexcept { return std::move(key_); }

    ::fixy::Index<N> slot_;
    uint64_t claim_ = 0;
    ::foundation::core::Identity log_;
    [[no_unique_address]] detail::tx_key_t<KeyBrand> key_;
};

// The log stamps each event of a transaction with the next place of its own
// count, and it reads no clock.  A clock time in a log makes two runs of one
// computation give two logs.  So no method of the log takes the tag of
// Nondet, and a body whose context holds no Nondet can roll the log back.
//
// Doors
//   begin_tx claims a slot and gives a TxDraft.  commit consumes the draft and
//   gives a CommittedTx, which activate takes, also after later claims and in
//   any order.  A claim never recycles the slot of the active transaction or
//   the slot of the rollback target.  active and previous give the CommittedTx
//   of their transaction, and with_tx lends a transaction as const to a body.
//   No door gives a reference to a transaction out.  region_of gives the region
//   of a committed transaction, which the log keeps beside the ring.
//
// Regions
//   A transaction holds plain values.  The log keeps the region of each slot
//   in its own array, and a fresh claim clears the region of its slot.  So a
//   body of a door of the ring reaches no borrow, and no body can keep a
//   borrow of a slot.  A key of the region store with a generation replaces
//   each region link when the store gets one.
//
// Identity
//   Each log holds an Identity that its constructor mints, so no caller gives
//   a log the Identity of a different log.  Each CommittedTx and each draft
//   carries the Identity of its log, and each door that takes one compares
//   it.  A stored CommittedTx can go stale in correct code, so activate,
//   with_tx and region_of refuse a token of a different log as stale.  A draft
//   is linear, and only the door of its own log makes one, so a draft of a
//   different log at commit, abandon or with_draft breaks the precondition of
//   the door.
//
// Key
//   A log of a key brand comes from the mint of foundation/core/KeyedMint.h
//   through the maker transaction_log_owner, with the LoanKey of the log.
//   begin_tx takes the key by move, the draft holds it, and commit and abandon
//   give it back.  Each other door that writes or reads a transaction borrows
//   the key, so no door that takes the key of the draft opens while it lives.
//   A key of another log of the mint site opens a door of this log too, so the
//   commit and the read of a keyed draft compare the claim of the draft.  The
//   doors with no key of a log of Unkeyed serve a member log, which has no key
//   until the root doors of the owner-key series give one.  They wait for
//   removal, and only the log of the Vigil uses them.
//
// Transfer fact.  The log states sendable{} (foundation/Transfer.h).  Each
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
    // The region of a committed transaction: a Ref of a region of the DAG,
    // which borrows the DAG and not the ring.
    using RegionRef = ::fixy::Ref<RegionNode const, DagArena>;

    TransactionLog() noexcept
        requires(!is_keyed_)
    = default;

    // The empty log of a key brand, with the proof of the mint of the brand
    // that only the mint of foundation/core/KeyedMint.h makes.  The maker
    // transaction_log_owner builds it, and the mint gives the key of the log
    // beside it.
    explicit TransactionLog(::foundation::core::MintedBrand<KeyBrand> /*proof*/) noexcept
        requires is_keyed_
    {}

    TransactionLog(const TransactionLog&) = delete("TransactionLog holds the one active transaction of its ring");
    TransactionLog&
    operator=(const TransactionLog&) = delete("TransactionLog holds the one active transaction of its ring");
    TransactionLog(TransactionLog&&) = delete("a draft and a CommittedTx name a slot of this log");
    TransactionLog& operator=(TransactionLog&&) = delete("a draft and a CommittedTx name a slot of this log");

    // ── The doors with no key of a log of Unkeyed (the key) ────────────────

    [[nodiscard, gnu::cold]] Draft begin_tx(Owner const& /*owner*/, ::fixy::session::StepId step) noexcept
        requires(!is_keyed_)
    {
        detail::NoTxKey key{};
        Slot const slot = claim_(key, step);
        return Draft{slot, claim_of_(slot), identity_, std::move(key)};
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
    // the claim and the recording state, and refuses a stale draft.  A log of
    // a key brand requires the claim in place of a refusal: a draft holds the
    // key of its log, so only a key of another log of the mint site runs a
    // claim while it lives.
    [[nodiscard]] ::fixy::Result<Committed, TxRefusal> commit(Owner const& /*owner*/, Draft&& draft,
                                                              Transaction::ArenaRegion region, ContentHash content_hash,
                                                              MerkleHash merkle_root) noexcept
        requires(!is_keyed_)
    {
        CRUCIBLE_PRE_MSG(draft.log_ == identity_, "a draft goes back only to the log that made it");
        Committed const committed{draft.slot_, draft.claim_, draft.log_};
        detail::NoTxKey key = std::move(draft).end_();
        if (!names_live_(committed)) return ::fixy::err(TxRefusal::stale);
        if (!is_recording_(committed.slot_)) return ::fixy::err(TxRefusal::not_recording);
        commit_(key, committed, region, content_hash, merkle_root);
        return committed;
    }

    // Ends the draft with no commit.  The transaction stays in its slot in the
    // recording state, and a later claim reuses the slot.
    void abandon(Owner const& /*owner*/, Draft&& draft) noexcept
        requires(!is_keyed_)
    {
        CRUCIBLE_PRE_MSG(draft.log_ == identity_, "a draft goes back only to the log that made it");
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

    // The rollback target, or no value (the target).
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
        CRUCIBLE_PRE_MSG(draft.log_ == identity_, "a draft goes back only to the log that made it");
        return with_tx_(Committed{draft.slot_, draft.claim_, draft.log_}, static_cast<Body&&>(body));
    }

    // The region of a committed transaction, or a refusal of a token that
    // names a different log or a slot that a later claim reused (the regions,
    // the Identity).
    [[nodiscard]] ::fixy::Result<::fixy::Option<RegionRef>, TxRefusal> region_of(Owner const& /*owner*/,
                                                                                 Committed committed) const noexcept
        requires(!is_keyed_)
    {
        return region_of_(committed);
    }

    // ── The doors of a log of a key brand (the key) ────────────────────────

    [[nodiscard, gnu::cold]] Draft begin_tx(key_type&& key, Owner const& /*owner*/,
                                            ::fixy::session::StepId step) noexcept
        requires is_keyed_
    {
        Slot const slot = claim_(key, step);
        return Draft{slot, claim_of_(slot), identity_, std::move(key)};
    }

    // The commit of a keyed log, which gives the key back after the
    // CommittedTx.
    [[nodiscard]] ::fixy::Record<Committed, key_type> commit(Owner const& /*owner*/, Draft&& draft,
                                                             Transaction::ArenaRegion region, ContentHash content_hash,
                                                             MerkleHash merkle_root) noexcept
        requires is_keyed_
    {
        CRUCIBLE_PRE_MSG(draft.log_ == identity_, "a draft goes back only to the log that made it");
        CRUCIBLE_PRE_MSG(holds_claim_(draft), "a key of another log of the site reused the slot of the draft");
        Committed const committed{draft.slot_, draft.claim_, draft.log_};
        key_type key = std::move(draft).end_();
        commit_(key, committed, region, content_hash, merkle_root);
        return {committed, std::move(key)};
    }

    [[nodiscard]] key_type abandon(Owner const& /*owner*/, Draft&& draft) noexcept
        requires is_keyed_
    {
        CRUCIBLE_PRE_MSG(draft.log_ == identity_, "a draft goes back only to the log that made it");
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

    [[nodiscard]] ::fixy::Result<::fixy::Option<RegionRef>, TxRefusal>
    region_of(key_type& /*key*/, Owner const& /*owner*/, Committed committed) const noexcept
        requires is_keyed_
    {
        return region_of_(committed);
    }

    // Lends the transaction of a draft as const to body.  The claim of the
    // draft names the transaction of the slot, so the door refuses nothing.
    template <class Body>
        requires is_keyed_ && TxReadBody<Body> && ::foundation::core::ReachesNoLoanKey<Body, KeyBrand>
    [[nodiscard]] auto with_draft(Owner const& /*owner*/, Draft const& draft, Body&& body) const noexcept {
        CRUCIBLE_PRE_MSG(draft.log_ == identity_, "a draft goes back only to the log that made it");
        CRUCIBLE_PRE_MSG(holds_claim_(draft), "a key of another log of the site reused the slot of the draft");
        return read_slot_(draft.slot_, static_cast<Body&&>(body));
    }

    // A door with no key of a keyed log binds to these overloads, so the
    // refusal gives its reason.
    Draft begin_tx(Owner const& owner, ::fixy::session::StepId step) noexcept
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
    template <class Body>
        requires is_keyed_ && std::is_invocable_v<Body&&, Transaction const&>
    void with_tx(Owner const& owner, Committed committed, Body&& body) const noexcept =
        delete("a keyed TransactionLog lends a committed transaction only with its LoanKey: call with_tx(key, owner, "
               "committed, body)");
    ::fixy::Result<::fixy::Option<RegionRef>, TxRefusal> region_of(Owner const& owner,
                                                                   Committed committed) const noexcept
        requires is_keyed_
    = delete("a keyed TransactionLog reads the region of a transaction only with its LoanKey: call region_of(key, "
             "owner, committed)");

    // ── The doors of each log ──────────────────────────────────────────────

    // Gives the region of each transaction in the ring to visit.  A rollback
    // can make each such transaction active again, so the owner keeps each of
    // these regions.  Complexity: O(N) in the ring capacity.
    template <class Visit>
    void for_each_region(Owner const&, Visit&& visit) const {
        for (Slot const back : ::fixy::IndexRange<N>{}) {
            if (back.raw() >= ring_.size()) break;
            regions_.at(ring_.slot_back(back))
                .value()
                .match([&visit](RegionRef region) { visit(::fixy::some(region)); }, [] {});
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

    // True when the transaction in the slot of the draft is the one of the
    // claim of the draft.
    [[nodiscard]] bool holds_claim_(Draft const& draft) const noexcept {
        return claim_of_(draft.slot_) == draft.claim_;
    }

    // True when the token names a transaction of this log that no later claim
    // replaced: the Identity of this log and the claim of the transaction in
    // its slot.
    [[nodiscard]] bool names_live_(Committed const& committed) const noexcept {
        return committed.log_ == identity_ && claim_of_(committed.slot_) == committed.claim_;
    }

    // The CommittedTx of the transaction in a slot, or no value.
    [[nodiscard]] ::fixy::Option<Committed> committed_of_(::fixy::Option<Slot> const& held) const noexcept {
        for (Slot const& slot : held)
            return ::fixy::some(Committed{slot, claim_of_(slot), identity_});
        return ::fixy::none;
    }

    // The next place of the count of the events, for the transaction of one
    // event.  The count only grows, so a later event of one transaction gets
    // a larger place.  A slot that a claim recycles starts again at zero.  The
    // door takes the place before it lends the slot, so the body of the slot
    // captures the place by value and reaches no member of the log.
    [[nodiscard]] uint64_t next_stamp_() noexcept {
        events_.bump();
        return events_.get();
    }

    // The body of region_of: the region of the transaction that the token
    // names, or a refusal of a token that names a different log or a slot
    // that a later claim reused.
    [[nodiscard]] ::fixy::Result<::fixy::Option<RegionRef>, TxRefusal> region_of_(Committed committed) const noexcept {
        if (!names_live_(committed)) return ::fixy::err(TxRefusal::stale);
        return regions_.at(committed.slot_).value();
    }

    // The region link of a slot: no region, for a fresh claim, or the region
    // of a commit.  Each takes the place of the old link with no read of it.
    [[nodiscard]] static Transaction::ArenaRegionLink no_region_() noexcept { return Transaction::ArenaRegionLink{}; }
    [[nodiscard]] static Transaction::ArenaRegionLink region_link_(Transaction::ArenaRegion region) noexcept {
        // The region came in under the Arena tag, so the link keeps it.
        return ::fixy::mint_tagged<::fixy::tags::source::Arena>(::fixy::Option<RegionRef>::some(region.value()));
    }

    // Claims a slot for a new transaction of the step, and gives the slot.
    // A full ring claims its oldest slot, but two slots must stay.  A
    // rollback can make a transaction of any age the active one.  If a claim
    // recycles that slot, the live transaction ends.  Then activate finds the
    // new transaction in the active slot.  An activation out of order, or a
    // rollback before an activation, can put the rollback target in any slot.
    // If a claim recycles that slot, a rollback restores an older
    // transaction.
    [[nodiscard]] Slot claim_(write_key& key, ::fixy::session::StepId step) noexcept {
        uint64_t const step_id = step.raw();
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
        uint64_t const stamp = next_stamp_();
        write_fresh_claim_(key, [step_id, claim, stamp](Transaction& tx) noexcept {
            tx.step_id.advance(step_id);
            tx.claim_order.advance(claim);
            tx.event_order.advance(stamp);
        });
        regions_.build_at(slot, no_region_);
        CRUCIBLE_POST(slot, !is_slot_of_(active_, slot));
        CRUCIBLE_POST(slot, !is_slot_of_(target, slot));
        bool const is_fresh = read_slot_(slot, [step_id, stamp](Transaction const& tx) noexcept {
            return tx.status == TxStatus::RECORDING && tx.step_id.get() == step_id && tx.event_order.get() == stamp;
        });
        CRUCIBLE_POST(slot, is_fresh && regions_.at(slot).value().is_none());
        return slot;
    }

    // Writes the commit of the transaction of a draft.  Only a draft reaches
    // this, and a draft names the transaction of its own claim in the
    // recording state.
    void commit_(write_key& key, Committed committed, Transaction::ArenaRegion region, ContentHash content_hash,
                 MerkleHash merkle_root) noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(merkle_root));
        uint64_t const stamp = next_stamp_();
        write_slot_(key, committed.slot_, [content_hash, merkle_root, stamp](Transaction& tx) noexcept {
            tx.content_hash = content_hash;
            tx.merkle_root = merkle_root;
            tx.status = TxStatus::COMMITTED;
            tx.event_order.advance(stamp);
            // Dropping any one of these assignments leaves the field at the
            // value the slot was reset to, which reads as an uncommitted
            // transaction.
            CRUCIBLE_POST(true, tx.content_hash == content_hash);
            CRUCIBLE_POST(true, tx.merkle_root == merkle_root);
        });
        regions_.build_at(committed.slot_, region_link_, region);
        CRUCIBLE_POST(true, regions_.at(committed.slot_).value().is_some_and([region](RegionRef held) noexcept {
            return ::fixy::same_object(held, region.value());
        }));
    }

    // The activation of activate.  The compare of the claim is a run-time
    // check, because a later claim reuses a slot after a count of claims
    // that no type of a CommittedTx can hold.  The compare of the Identity is
    // a run-time check, because a brand names a mint site and not one log.
    [[nodiscard]] ::fixy::Result<::fixy::Option<Committed>, TxRefusal> activate_(write_key& key,
                                                                                 Committed committed) noexcept {
        if (!names_live_(committed)) return ::fixy::err(TxRefusal::stale);
        bool const is_committed = read_slot_(
            committed.slot_, [](Transaction const& tx) noexcept { return tx.status == TxStatus::COMMITTED; });
        if (!is_committed) return ::fixy::err(TxRefusal::not_committed);

        ::fixy::Option<Committed> const displaced = committed_of_(active_);
        for (Slot const& previous : active_) {
            displacements_.bump();
            uint64_t const displacement = displacements_.get();
            uint64_t const stamp = next_stamp_();
            write_slot_(key, previous, [displacement, stamp](Transaction& tx) noexcept {
                tx.status = TxStatus::SUPERSEDED;
                tx.displacement_order.advance(displacement);
                tx.event_order.advance(stamp);
            });
        }
        uint64_t const stamp = next_stamp_();
        write_slot_(key, committed.slot_, [stamp](Transaction& tx) noexcept {
            tx.status = TxStatus::ACTIVE;
            tx.event_order.advance(stamp);
        });
        active_ = ::fixy::some(committed.slot_);
        // The displaced transaction must end up in the superseded state, since
        // that is what a rollback searches for.  It must also hold the newest
        // place in the order of displacements, because a rollback restores
        // the superseded transaction with the largest place.
        uint64_t const displacements = displacements_.get();
        CRUCIBLE_POST(displaced, is_slot_of_(active_, committed.slot_));
        CRUCIBLE_POST(displaced, displaced.is_none_or([this, displacements](Committed previous) noexcept {
            return read_slot_(previous.slot_, [displacements](Transaction const& tx) noexcept {
                return tx.status == TxStatus::SUPERSEDED && tx.displacement_order.get() == displacements;
            });
        }));
        return displaced;
    }

    // The rollback of rollback.
    [[nodiscard]] bool rollback_(write_key& key) noexcept {
        ::fixy::Option<Slot> const target = previous_slot_();
        if (target.is_none()) return false;
        for (Slot const& current : active_) {
            uint64_t const stamp = next_stamp_();
            write_slot_(key, current, [stamp](Transaction& tx) noexcept {
                tx.status = TxStatus::ROLLED_BACK;
                tx.event_order.advance(stamp);
            });
        }
        for (Slot const& restored : target) {
            uint64_t const stamp = next_stamp_();
            write_slot_(key, restored, [stamp](Transaction& tx) noexcept {
                tx.status = TxStatus::ACTIVE;
                tx.event_order.advance(stamp);
            });
        }
        active_ = target;
        return true;
    }

    // Target: the slot of the superseded transaction with the newest
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

    // The body of with_tx: the CommittedTx names the transaction of the slot
    // of this log, or it names a different log, or a later claim reused the
    // slot.
    template <class Body>
    [[nodiscard]] ::fixy::Result<std::invoke_result_t<Body&&, Transaction const&>, TxRefusal>
    with_tx_(Committed committed, Body&& body) const noexcept {
        if (!names_live_(committed)) return ::fixy::err(TxRefusal::stale);
        return read_slot_(committed.slot_, static_cast<Body&&>(body));
    }

    Ring ring_{};
    // The region of the transaction of each slot (the regions).  A key of the
    // region store with a generation replaces each link when the store gets
    // one.
    ::fixy::FixedArray<Transaction::ArenaRegionLink, N> regions_{};
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
    // The Identity of this log, which each constructor mints.  Each token and
    // each draft of the log carries it.  Its value depends on the order of the
    // mints of the process, so the log only compares it, and no stamp, no
    // order and no hash reads it.
    ::foundation::core::Identity identity_ = ::foundation::core::mint_identity();
};

// The maker of a keyed TransactionLog of N slots, for the mint of
// foundation/core/KeyedMint.h (the key):
//   auto [log, key] = ::fixy::mint_keyed_owner(transaction_log_owner<16, Owner>);
// The log does not move, so it comes back as a prvalue part of the Record.
template <uint32_t N, OwnerProof Owner>
struct TransactionLogOwner final {
    template <::foundation::core::IsKeyBrand Brand>
    [[nodiscard]] TransactionLog<N, Owner, Brand>
    operator()(::foundation::core::MintedBrand<Brand> proof) const noexcept {
        return TransactionLog<N, Owner, Brand>{proof};
    }
};

template <uint32_t N, OwnerProof Owner>
inline constexpr TransactionLogOwner<N, Owner> transaction_log_owner{};

}  // namespace crucible
