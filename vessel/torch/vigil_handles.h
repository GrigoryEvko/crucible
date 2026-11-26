#pragma once

// The table of the handles of the Vigils of the vessel.
//
// A C caller names a Vigil by a handle: a word of 64 bits that crucible_create
// gives (CrucibleHandle of vessel_api.h).  The handle is outside data, so each
// C entry checks it here, and a handle that names no live Vigil gives a
// refusal, never an end of the process.  A destroyed, a forged and a zero
// handle give stale.  A call on a thread that does not hold the Vigil gives
// wrong_thread.
//
// The word
//   Bits 0 to 5 hold the index of a slot, bits 32 to 63 hold the generation of
//   the slot, and each other bit is zero.  A generation is 1 or more, so the
//   word 0 names no Vigil.  The end of a Vigil gives its slot the next
//   generation, so the word of an ended Vigil never names the next Vigil of
//   its slot.  After 2^32 - 1 Vigils in one slot, the generation wraps to 1.
//
// The slot
//   A slot holds four atomic words: the word of its live Vigil (live, 0 when
//   the slot is free), the thread that holds the slot (holder, a
//   ThreadClaimCell of foundation/effects/ThreadClaim.h), the generation of the
//   next Vigil, and the owner of the Vigil (owner).  The owner is the bits of a
//   BoxWord of the slot: the Vigil belongs to the foreign caller of the
//   handle, and the rule process-wide-state refuses each static object whose
//   type reaches a Vigil, because each thread reaches a static.  So the owner
//   keeps no type, and the opt-out regions of the foreign ABI below turn it
//   into a BoxWord and back.  The protocol of the slot, and no type, makes each
//   use of the owner safe (the threads, the orders and the end below).  The
//   words are trivially destructible, so no Vigil ends at the exit of the
//   process.  A Vigil that no C caller destroys stays, as before the table.
//
// The threads
//   The thread that holds a slot is the one thread that reads its Vigil
//   through the table.  The create claims the slot for the thread of the
//   create.  Each other door claims a free slot for its caller, and a slot that
//   another thread holds gives wrong_thread.  So a thread that does not hold a
//   slot never reads the memory of its Vigil, and only the holder ends the
//   Vigil.  The claim of a slot also claims the producer role of its Vigil
//   (try_claim_producer of crucible/Vigil.h), and a release gives up the two.
//   So the slot and its Vigil have one holder.  A claim of the producer role
//   that another thread holds, also through another Vigil, gives wrong_thread
//   too.
//
// The orders of the memory
//   - publish writes the owner, and then stores the word of the handle into
//     live with release order.  Each read of live has acquire order, so a
//     thread that reads the word reads the owner that publish wrote.
//   - The claim of the holder is an exchange of acquire and release order.  It
//     pairs with the release store of the last holder (release, withdraw), so
//     each write of the last holder, such as the next generation, happens
//     before each access of the next holder.
//   - A thread that read the word before it claimed the slot reads live again
//     after the claim.  A withdraw between the two reads stored 0 into live
//     before it freed the holder, so the second read gives 0, and the claim
//     gives stale and frees the slot.
//
// The end of a Vigil
//   withdraw stores 0 into live with release order, then it takes the owner
//   back, gives the slot the next generation and frees the holder.  The store
//   of 0 ends each use of the Ref of the owner: no door gives the Vigil after
//   it, and the holder, the one thread that read the Vigil, ended each use
//   before the call (busy of the table).  Only then does the owner become a
//   Box again, whose end ends the Vigil.
//
// The table of the process
//   vigil_handles is one table for the process.  CRUCIBLE_PROCESS_WIDE makes it
//   one object for libcrucible_vessel.so, which makes and ends each Vigil, and
//   libcrucible_dispatch.so, which reads the handle of the TLS state of
//   PyTorch for each op.  Python loads the two with RTLD_LOCAL.

#include "vessel_api.h"

#include <crucible/ValidatedOp.h>
#include <crucible/Vigil.h>
#include <fixy/Core.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/Transfer.h>
#include <foundation/effects/ThreadClaim.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::vessel {

// The word of a handle of a Vigil.
using VigilHandle = ::fixy::Id<struct VigilHandleTag, std::uint64_t>;

// The refusal of a door of the table, as the C entries give it
// (CRUCIBLE_HANDLE_* of vessel_api.h).
enum class HandleRefusal : std::uint8_t {
    // No live Vigil has the handle: it ended, it never lived, or it is forged.
    stale,
    // Another thread holds the Vigil, or holds the producer role of another
    // live Vigil.
    wrong_thread,
    // An op of the Vigil runs on this thread, so the Vigil cannot end or let
    // its thread go.
    busy,
    // The table holds kVigilSlots live Vigils.
    full,
};

// The code of a refusal in the C ABI.
[[nodiscard]] constexpr std::uint32_t handle_code_of(HandleRefusal refused) noexcept {
    switch (refused) {
        case HandleRefusal::stale:
            return CRUCIBLE_HANDLE_STALE;
        case HandleRefusal::wrong_thread:
            return CRUCIBLE_HANDLE_WRONG_THREAD;
        case HandleRefusal::busy:
            return CRUCIBLE_HANDLE_BUSY;
        case HandleRefusal::full:
            return CRUCIBLE_HANDLE_FULL;
        default:
            ::fixy::unreachable();
    }
}

// The most live Vigils of the process, and the bits of a word below.
inline constexpr std::size_t kVigilSlots = CRUCIBLE_VIGIL_SLOTS;
inline constexpr std::uint64_t kSlotIndexMask = kVigilSlots - 1U;
inline constexpr std::uint32_t kGenerationShift = 32;

using VigilSlotIndex = ::fixy::Index<kVigilSlots>;

// The word of a slot and a generation.
[[nodiscard]] constexpr VigilHandle handle_of(VigilSlotIndex index, std::uint32_t generation) noexcept {
    return VigilHandle{(std::uint64_t{generation} << kGenerationShift) | std::uint64_t{index.raw()}};
}

// The generation after a generation.  0 names no Vigil, so a wrap skips it.
[[nodiscard]] constexpr std::uint32_t next_generation(std::uint32_t generation) noexcept {
    return generation == ::fixy::max_of<std::uint32_t> ? 1U : generation + 1U;
}

// One slot of the table (the slot above).  Each field is an atomic word, so
// two threads can use one slot, and the protocol of the slot orders the
// writes.
class VigilSlot {
public:
    // The proof that the calling thread holds the slot, for the time of one
    // call: the handle of the live Vigil, or of the Vigil that a reserved slot
    // takes.  Only a claim door of the slot makes one, and it belongs to the
    // thread of the claim.  A proof that a call drops leaves the slot held:
    // the holder keeps the slot until release or withdraw.
    class[[= ::foundation::transfer::thread_bound{}]] Held {
    public:
        Held(Held&&) noexcept = default;
        Held(const Held&) = delete("a proof of a slot names one claim of one thread");
        Held& operator=(const Held&) = delete("a proof of a slot names one claim of one thread");
        Held& operator=(Held&&) = delete("a proof of a slot names one claim of one thread");
        ~Held() = default;

        [[nodiscard]] constexpr VigilHandle handle() const noexcept { return handle_; }

    private:
        constexpr explicit Held(VigilHandle handle) noexcept : handle_{handle} {}

        VigilHandle handle_;

        friend class VigilSlot;
    };

    // The owner of a Vigil on its way into a slot or out of one: the bits of
    // a BoxWord of the slot, which hold no borrow.  A move leaves 0 in the
    // source, and only claim_box ends an owner that holds a Vigil.
    class Owner {
    public:
        Owner(Owner&& other) noexcept : bits_{::fixy::exchange(other.bits_, std::uint64_t{0})} {}
        Owner(const Owner&) = delete("an owner names one Vigil");
        Owner& operator=(const Owner&) = delete("an owner names one Vigil");
        Owner& operator=(Owner&&) = delete("an owner names one Vigil");
        ~Owner() { CRUCIBLE_FATAL_INVARIANT(bits_ == 0U); }

    private:
        constexpr explicit Owner(std::uint64_t bits) noexcept : bits_{bits} {}

        std::uint64_t bits_;

        friend class VigilSlot;
    };

    constexpr VigilSlot() noexcept = default;
    VigilSlot(const VigilSlot&) = delete("a slot holds one Vigil at one place");
    VigilSlot& operator=(const VigilSlot&) = delete("a slot holds one Vigil at one place");
    VigilSlot(VigilSlot&&) = delete("a slot holds one Vigil at one place");
    VigilSlot& operator=(VigilSlot&&) = delete("a slot holds one Vigil at one place");
    ~VigilSlot() = default;

    // The owner of a new Vigil, for publish.  The Box gives its object to a
    // BoxWord of the slot, and the owner keeps the bits of the word.
    [[nodiscard]] static Owner owner_of(::fixy::Box<Vigil>&& vigil) noexcept {
        const ::fixy::BoxWord<Vigil, VigilSlot> word =
            std::move(vigil).release_to_word(::fixy::box_word_key<VigilSlot>{});
        // The C caller owns the Vigil through its handle.  The slot keeps the
        // word as bits that no type ties to the Vigil, and the protocol of the
        // slot (live, holder, generation and the end of a Vigil above) is the
        // one rule of each use of them.
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the C caller owns the Vigil, so a static slot keeps its BoxWord as bits")
        return Owner{std::bit_cast<std::uint64_t>(word)};
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    }

    // The Box of the Vigil of an owner, whose end ends the Vigil.  The owner
    // came from owner_of or from withdraw, so its bits are the bits of a
    // BoxWord of the slot, and no other owner holds them (the end above).
    [[nodiscard]] static ::fixy::Box<Vigil> claim(Owner&& owner) noexcept {
        const std::uint64_t bits = ::fixy::exchange(owner.bits_, std::uint64_t{0});
        // owner_of made the bits, and withdraw took them out of a slot after
        // its store of 0 into live, so no other owner and no Ref of a borrow
        // holds them (the end of a Vigil above).
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the bits of an owner are the bits of a BoxWord of the slot")
        const auto word = std::bit_cast<::fixy::BoxWord<Vigil, VigilSlot>>(bits);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
        return ::fixy::claim_box(word, ::fixy::box_word_key<VigilSlot>{});
    }

    // The claim door of a live handle.  The proof for the calling thread when
    // the slot holds the Vigil of the handle and the thread holds the slot or
    // claims it, and claims the producer role of the Vigil.  stale when the
    // slot holds no Vigil of the handle, and wrong_thread when another thread
    // holds the slot or the producer role.  A refused claim leaves the slot as
    // it was.  The fast path is two loads and three compares.
    [[nodiscard]] ::fixy::Result<Held, HandleRefusal> try_hold(VigilHandle handle) noexcept {
        // The word 0 names no Vigil, also for a free slot, whose live is 0.
        if (handle.raw() == 0U || live_.load_acquire() != handle.raw()) return ::fixy::err(HandleRefusal::stale);
        if (holder_.is_held_by_caller()) return Held{handle};
        if (!holder_.try_claim()) return ::fixy::err(HandleRefusal::wrong_thread);
        // The second read of live (the orders above).
        if (live_.load_acquire() != handle.raw()) {
            static_cast<void>(holder_.release());
            return ::fixy::err(HandleRefusal::stale);
        }
        if (!vigil_()->try_claim_producer()) {
            static_cast<void>(holder_.release());
            return ::fixy::err(HandleRefusal::wrong_thread);
        }
        return Held{handle};
    }

    // The claim door of a free slot, for a create.  The proof for the calling
    // thread with the handle of the next Vigil of the slot, or no value when
    // the slot holds a Vigil or another thread holds it.
    [[nodiscard]] ::fixy::Option<Held> try_reserve(VigilSlotIndex index) noexcept {
        if (live_.load_acquire() != 0U) return ::fixy::none;
        if (!holder_.try_claim()) return ::fixy::none;
        // A slot that a Vigil took between the two reads stays as it is.
        if (live_.load_acquire() != 0U) {
            static_cast<void>(holder_.release());
            return ::fixy::none;
        }
        return ::fixy::Option<Held>::some(Held{handle_of(index, generation_.load_acquire())});
    }

    // Gives the Vigil of the owner to the slot that the calling thread
    // reserved, claims its producer role, and gives its handle.  The thread
    // keeps the slot.  wrong_thread when another thread holds the producer
    // role of another live Vigil: the slot is then free, and the Vigil ends.
    [[nodiscard]] ::fixy::Result<VigilHandle, HandleRefusal> publish(Held&& reserved, Owner&& owner) noexcept {
        owner_.store_release(::fixy::exchange(owner.bits_, std::uint64_t{0}));
        if (!vigil_()->try_claim_producer()) {
            const ::fixy::Box<Vigil> refused = claim(Owner{owner_.exchange_acq_rel(0U)});
            static_cast<void>(holder_.release());
            return ::fixy::err(HandleRefusal::wrong_thread);
        }
        live_.store_release(reserved.handle().raw());
        return reserved.handle();
    }

    // Takes the owner of the Vigil out of the slot of the calling thread,
    // gives the slot the next generation and frees it (the end of a Vigil
    // above).  The caller turns the owner into the Box that ends the Vigil.
    [[nodiscard]] Owner withdraw(Held&& held) noexcept {
        static_cast<void>(held);
        live_.store_release(0U);
        Owner owner{owner_.exchange_acq_rel(0U)};
        generation_.store_release(next_generation(generation_.load_acquire()));
        static_cast<void>(holder_.release());
        return owner;
    }

    // Frees the slot of the calling thread.  The Vigil stays in the slot, and
    // the next claim of a thread takes it.
    void release(Held&& held) noexcept {
        static_cast<void>(held);
        static_cast<void>(holder_.release());
    }

    // The Vigil of the slot of the proof.
    [[nodiscard]] ::fixy::Ref<Vigil> vigil_of(Held const& held) const noexcept {
        static_cast<void>(held);
        return vigil_();
    }

    // The Vigil of the handle when the calling thread holds the slot, or no
    // value.  It claims nothing: the cold path of a caller claims through
    // try_hold.  Two loads and three compares, as in try_hold.
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::Option<::fixy::Ref<Vigil>> held_vigil(VigilHandle handle) const noexcept {
        if (handle.raw() == 0U || live_.load_acquire() != handle.raw() || !holder_.is_held_by_caller())
            return ::fixy::none;
        return ::fixy::Option<::fixy::Ref<Vigil>>::some(vigil_());
    }

    // True when the slot holds no Vigil.  The answer can be old when the call
    // returns.
    [[nodiscard]] bool is_free() const noexcept { return live_.load_acquire() == 0U; }

private:
    // The Vigil of the owner of the slot.  The caller read the word of the
    // handle in live and holds the slot, or published the owner itself, so the
    // owner holds the Vigil, and no other thread ends it (the threads above).
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::Ref<Vigil> vigil_() const noexcept {
        // The caller holds a live slot, so the bits are the bits of the
        // BoxWord that publish wrote, and only withdraw on this thread takes
        // them back (the threads and the end of a Vigil above).
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the holder of a live slot borrows the Vigil of its owner bits")
        const auto word = std::bit_cast<::fixy::BoxWord<Vigil, VigilSlot>>(owner_.load_acquire());
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
        return ::fixy::erase_brand(word.borrow(::fixy::box_word_key<VigilSlot>{}));
    }

    // The word of the live Vigil, or 0.
    ::fixy::Atomic<std::uint64_t> live_{std::uint64_t{0}};
    // The thread that holds the slot.
    ::foundation::effects::ThreadClaimCell holder_{};
    // The generation of the next Vigil of the slot.
    ::fixy::Atomic<std::uint32_t> generation_{1U};
    // The bits of the BoxWord of the Vigil of the slot, or 0.
    ::fixy::Atomic<std::uint64_t> owner_{std::uint64_t{0}};
};

// The proof that the calling thread holds the Vigil of a handle.
using HeldVigil = VigilSlot::Held;

// True when an op of the Vigil runs on the calling thread: a nest or a frame
// of its record stack is open.  The caller holds the slot, so it holds the
// producer role of the Vigil, and the mint takes its fast path.
[[nodiscard]] inline bool is_busy(Vigil& vigil) noexcept {
    const VigilFgCtx fg = vigil.mint_producer_context();
    const OpRecordStack& stack = vigil.op_record_stack(fg);
    return !stack.depth().is_top() || stack.held() != 0U;
}

// The table of SlotCount slots.  The process holds one table of kVigilSlots
// slots (vigil_handles below), and a test makes a small one.
template <std::size_t SlotCount>
    requires(SlotCount > 0 && SlotCount <= kVigilSlots)
class VigilHandleTable {
public:
    using SlotIndex = ::fixy::Index<SlotCount>;

    constexpr VigilHandleTable() noexcept = default;
    VigilHandleTable(const VigilHandleTable&) = delete("the handles name the slots of one table");
    VigilHandleTable& operator=(const VigilHandleTable&) = delete("the handles name the slots of one table");
    VigilHandleTable(VigilHandleTable&&) = delete("the handles name the slots of one table");
    VigilHandleTable& operator=(VigilHandleTable&&) = delete("the handles name the slots of one table");
    ~VigilHandleTable() = default;

    // True when a slot is free.  The answer can be old when the call returns,
    // so a create asks it only to skip the start of a Vigil that admit would
    // end.  Linear in the slots.
    [[nodiscard]] bool has_free_slot() const noexcept {
        for (std::size_t position = 0; position < SlotCount; ++position) {
            if (slots_.at(slot_index_(position)).is_free()) return true;
        }
        return false;
    }

    // Puts the Vigil into the first free slot, claims the slot and the
    // producer role of the Vigil for the calling thread, and gives its handle.
    // full when each slot holds a Vigil, and wrong_thread when another thread
    // holds the producer role of another live Vigil.  The Vigil then ends
    // here.  Linear in the slots.
    [[nodiscard]] ::fixy::Result<VigilHandle, HandleRefusal> admit(::fixy::Box<Vigil>&& vigil) noexcept {
        VigilSlot::Owner owner = VigilSlot::owner_of(std::move(vigil));
        for (std::size_t position = 0; position < SlotCount; ++position) {
            const SlotIndex at = slot_index_(position);
            ::fixy::Option<HeldVigil> reserved =
                slots_.with_element(at, [at](VigilSlot& slot) noexcept { return slot.try_reserve(at); });
            if (reserved.is_none()) continue;
            return slots_.with_element(at, [&reserved, &owner](VigilSlot& slot) noexcept {
                return slot.publish(std::move(reserved).expect("the test above found the reservation"),
                                    std::move(owner));
            });
        }
        const ::fixy::Box<Vigil> refused = VigilSlot::claim(std::move(owner));
        return ::fixy::err(HandleRefusal::full);
    }

    // The mint of the proof that the calling thread holds the Vigil of the
    // handle: the claim door of its slot (VigilSlot::try_hold).  stale for a
    // handle that names no slot of the table.
    [[nodiscard]] ::fixy::Result<HeldVigil, HandleRefusal> mint_held_vigil(VigilHandle handle) noexcept {
        const std::uint64_t position = handle.raw() & kSlotIndexMask;
        if (position >= SlotCount) return ::fixy::err(HandleRefusal::stale);
        return slots_.with_element(slot_index_(position),
                                   [handle](VigilSlot& slot) noexcept { return slot.try_hold(handle); });
    }

    // The Vigil of the proof.
    [[nodiscard]] ::fixy::Ref<Vigil> vigil_of(HeldVigil const& held) const noexcept {
        return slots_.at(slot_index_(held.handle().raw() & kSlotIndexMask)).vigil_of(held);
    }

    // Ends the hold of the calling thread on the Vigil of the proof, and gives
    // the Box of the Vigil, whose end ends the Vigil.  busy while an op of the
    // Vigil runs on this thread, and the slot then stays as it was.
    [[nodiscard]] ::fixy::Result<::fixy::Box<Vigil>, HandleRefusal> withdraw(HeldVigil&& held) noexcept {
        Vigil& vigil = *vigil_of(held);
        if (is_busy(vigil)) return ::fixy::err(HandleRefusal::busy);
        vigil.release_producer_claim();
        VigilSlot::Owner owner =
            slots_.with_element(slot_index_(held.handle().raw() & kSlotIndexMask),
                                [&held](VigilSlot& slot) noexcept { return slot.withdraw(std::move(held)); });
        return VigilSlot::claim(std::move(owner));
    }

    // Frees the slot of the proof, and gives up the producer role of its
    // Vigil, so the next call on any thread claims the two.  busy while an op
    // of the Vigil runs on this thread, and the slot then stays as it was.
    [[nodiscard]] ::fixy::Result<::fixy::Unit, HandleRefusal> release(HeldVigil&& held) noexcept {
        Vigil& vigil = *vigil_of(held);
        if (is_busy(vigil)) return ::fixy::err(HandleRefusal::busy);
        vigil.release_producer_claim();
        slots_.with_element(slot_index_(held.handle().raw() & kSlotIndexMask),
                            [&held](VigilSlot& slot) noexcept { slot.release(std::move(held)); });
        return ::fixy::Unit{};
    }

    // The Vigil of the handle for the record of one op, or no value: the
    // handle names no live Vigil, or another thread holds it.  The fast path
    // reads the slot of the handle, and the first op on a free slot claims
    // it on the cold path.  No value never ends the process, so the op then
    // runs eagerly and the Vigil records nothing.
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::Option<::fixy::Ref<Vigil>> find_recording_vigil(VigilHandle handle) noexcept {
        const std::uint64_t position = handle.raw() & kSlotIndexMask;
        if (position >= SlotCount) return ::fixy::none;
        ::fixy::Option<::fixy::Ref<Vigil>> held = slots_.at(slot_index_(position)).held_vigil(handle);
        if (held.is_some()) [[likely]]
            return held;
        return claim_recording_vigil_(handle);
    }

private:
    // The index of a position below SlotCount.  Each caller tests the
    // position, or masks it, so the optimizer removes the check.
    [[nodiscard]] static constexpr SlotIndex slot_index_(std::uint64_t position) noexcept {
        return SlotIndex::check(position).expect("the caller keeps the position below the count of the slots");
    }

    // The cold path of find_recording_vigil: the claim of a free slot.
    [[gnu::cold, gnu::noinline]] ::fixy::Option<::fixy::Ref<Vigil>>
    claim_recording_vigil_(VigilHandle handle) noexcept {
        const ::fixy::Result<HeldVigil, HandleRefusal> held = mint_held_vigil(handle);
        return held.match(
            [this](HeldVigil const& proof) noexcept {
                return ::fixy::Option<::fixy::Ref<Vigil>>::some(vigil_of(proof));
            },
            [](HandleRefusal) noexcept { return ::fixy::Option<::fixy::Ref<Vigil>>{::fixy::none}; });
    }

    ::fixy::FixedArray<VigilSlot, SlotCount> slots_{};
};

// The table of the process (the table of the process above).
CRUCIBLE_PROCESS_WIDE inline constinit VigilHandleTable<kVigilSlots> vigil_handles{};

}  // namespace crucible::vessel
