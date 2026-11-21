#pragma once

// The observation sink: a store of counts of the counter of the core.  A body
// writes it with no context, and only a context whose row holds Nondet reads
// it.
//
// A body times itself with mark and span.  mark stores the counter of the
// core in a slot.  span gives a guard, and the end of the guard adds the
// count of its life to a slot.  The two take no context and give back no
// value of the counter.  So no count reaches a result of the body, and a body
// whose row holds no Nondet, such as a kernel, can time itself.  read gives
// the value of a slot, and it takes the bare tag of Nondet.  Thus the only
// route of a count out of the sink is a read, and only code whose row states
// Nondet makes one.
//
// One thread owns each sink: the thread of its mint.  Only that thread writes
// the slots.  Each mark, each span and the end of each span check the thread
// in each build.  A slot is alone on its cache line.  A write is a release
// store, and a read on each thread is an acquire load.
//
// The counter is the counter of the core of foundation/os/SpinHint.h: RDTSC on
// x86_64 and CNTVCT_EL0 on aarch64.  A count is a count of ticks of that
// counter, not of nanoseconds.  The cost of a mark is one read of the counter,
// one read of the thread pointer, one compare and one store.

#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Index.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/SpinHint.h>

#include <cstddef>
#include <cstdint>

namespace fixy::observe {

// The count of the slots of a sink: at least one, and at most 1024, so a
// sink takes at most 64 KB.
template <std::size_t Slots>
concept IsTimingSlotCount = Slots > 0 && Slots <= 1024;

template <std::size_t Slots>
    requires IsTimingSlotCount<Slots>
class TimingSink;

template <std::size_t Slots>
    requires IsTimingSlotCount<Slots>
[[nodiscard]] TimingSink<Slots> mint_timing_sink() noexcept;

// Only the owner thread writes the sink, so a sink takes no write of another
// thread (thread_bound of foundation/Transfer.h).  A read through a reference
// to const is safe on each thread.
template <std::size_t Slots>
    requires IsTimingSlotCount<Slots>
class[[= ::foundation::transfer::thread_bound{}]] TimingSink {
public:
    using Slot = ::foundation::core::Index<Slots>;

    // The guard of one span of a slot.  Its end adds the count of its life to
    // the slot.  It keeps the count of its start in a private member and has
    // no accessor, so it gives back no value of the counter.  It has no copy
    // and no move, so it ends on the thread and in the scope of its start.
    class [[nodiscard]] Span {
    public:
        Span(const Span&) = delete("a span ends one time, in the scope of its start");
        Span(Span&&) = delete("a span ends one time, in the scope of its start");
        Span& operator=(const Span&) = delete("a span ends one time, in the scope of its start");
        Span& operator=(Span&&) = delete("a span ends one time, in the scope of its start");

        ~Span() {
            std::uint64_t const end = ::foundation::os::read_spin_counter();
            sink_->add_(slot_, ::foundation::core::saturating_sub(end, start_));
        }

    private:
        Span(TimingSink& sink, Slot slot) noexcept
            : sink_{&sink}, slot_{slot}, start_{::foundation::os::read_spin_counter()} {}

        friend class TimingSink;

        TimingSink* sink_ = nullptr;
        Slot slot_{::foundation::core::index<0>()};
        std::uint64_t start_ = 0;
    };

    TimingSink(const TimingSink&) = delete("a sink has one owner thread, and a reader keeps its address");
    TimingSink(TimingSink&&) = delete("a sink has one owner thread, and a reader keeps its address");
    TimingSink& operator=(const TimingSink&) = delete("a sink has one owner thread");
    TimingSink& operator=(TimingSink&&) = delete("a sink has one owner thread");
    ~TimingSink() = default;

    // Stores the counter of the core in the slot.  Only the owner thread
    // marks.
    void mark(Slot slot) & noexcept {
        require_owner_();
        // The count of a cell is an Atomic, and a reader on another thread
        // loads it at the same time.
        slots_.with_element(
            slot, [](Cell& cell) noexcept { cell.count.store_release(::foundation::os::read_spin_counter()); });
    }

    void mark(Slot) && = delete("a temporary sink ends at the end of the full expression, and nobody reads it");

    // The guard of a span of the slot.  Only the owner thread starts a span.
    [[nodiscard]] Span span(Slot slot) & noexcept {
        require_owner_();
        return Span{*this, slot};
    }

    Span span(Slot) && = delete("a temporary sink ends before the span, and the end of the span writes it");

    // The value of the slot.  The read takes the bare tag of Nondet, because
    // a count changes from run to run.
    [[nodiscard]] std::uint64_t read(::foundation::effects::cap::Nondet, Slot slot) const noexcept {
        return slots_.at(slot).count.load_acquire();
    }

private:
    TimingSink() noexcept : owner_{::foundation::effects::detail::producer_claim::calling_thread_identity()} {}

    template <std::size_t FriendSlots>
        requires IsTimingSlotCount<FriendSlots>
    friend TimingSink<FriendSlots> mint_timing_sink() noexcept;

    void require_owner_() const noexcept {
        CRUCIBLE_FATAL_INVARIANT(owner_ == ::foundation::effects::detail::producer_claim::calling_thread_identity());
    }

    // The owner is the one writer of each slot, so no other write comes
    // between its read and its store.
    void add_(Slot slot, std::uint64_t ticks) noexcept {
        require_owner_();
        slots_.with_element(slot, [ticks](Cell& cell) noexcept {
            cell.count.store_release(::foundation::core::saturating_add(cell.count.load_acquire(), ticks));
        });
    }

    // One slot alone on its cache line.  The owner writes the count, and a
    // reader on another thread reads it.  The padding fills the line.
    struct alignas(64) Cell {
        ::foundation::core::Atomic<std::uint64_t> count{};
        ::foundation::core::FixedArray<std::uint8_t, 64 - sizeof(::foundation::core::Atomic<std::uint64_t>)> pad{};
    };

    ::foundation::core::FixedArray<Cell, Slots> slots_{};
    std::uintptr_t owner_ = 0;
    // No byte route builds a sink, so each sink has the owner of its mint.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// The one door of a sink.  The thread of the call is the owner of the sink.
template <std::size_t Slots>
    requires IsTimingSlotCount<Slots>
[[nodiscard]] TimingSink<Slots> mint_timing_sink() noexcept {
    return TimingSink<Slots>{};
}

}  // namespace fixy::observe
