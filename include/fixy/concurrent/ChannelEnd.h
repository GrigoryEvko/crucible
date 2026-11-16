#pragma once

// The end of the stream of a channel: why a receive gives no item, and the
// count of the live producer handles that tells the end.
//
// THE END OF THE STREAM
//     A channel is closed when its last live producer handle ended with its
//     token and no producer handle started after that end.  A fresh channel
//     is open, because no producer ended yet.  A handle that starts opens the
//     channel again: the open of an SPSC producer, a lend of an MPSC
//     producer.  A handle that gives its token back through a release door
//     does not close the channel, because the token can open a handle again.
//     A moved-from handle holds no role, and its end changes nothing.
//
//     try_receive of a consumer handle gives the oldest item, NoItem::Empty
//     when the ring is empty and a producer can still push, or
//     NoItem::Closed when the stream ended.  It reads the closed state only
//     after a pop that found the ring empty, with acquire order.  Each push
//     of a producer comes before the end of its handle, so a pop after that
//     read finds each item that the stream still holds.  try_receive gives
//     Closed only when that pop finds the ring empty.  A consumer that saw
//     Closed has thus seen every item of that stream.  A producer that
//     starts after the end starts a new stream on the same ring.
//
//     A drain loop stops on Closed, with no sentinel in the stream.  So a
//     consumer whose producers end with no last message still ends, and the
//     join of its thread ends.
//
// THE WORD OF THE ROSTER
//     ProducerRoster holds one word: the count of the live producer handles
//     in the low 63 bits, and the closed bit at the top.  The closed bit is
//     set only with a count of zero.  Each change of the word is one
//     acquire-release compare-and-swap.  The end of the last handle sets the
//     closed bit in the same step that takes the count to zero.  The start of
//     a handle takes the count up and clears the bit in one step.  So no
//     interleaving of an end and a start loses either one.  Each write of the
//     word is a read-modify-write, so an acquire read of the closed bit
//     synchronizes with the end of each producer handle of the stream.  The
//     word has a cache line of its own, because the producers write it and
//     the consumer reads it.

#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>

#include <cstdint>

namespace fixy::concurrent {

// Why a receive gives no item (THE END OF THE STREAM).
enum class NoItem : std::uint8_t {
    // The ring is empty, and a producer can still push.
    Empty = 0,
    // The last producer handle ended, and the ring is empty.
    Closed = 1,
};

// The live producer handles of one channel, and the end of its stream (THE
// WORD OF THE ROSTER).  Each producer handle of the channel writes it, and
// the consumer reads it.
class ProducerRoster : ::foundation::Pinned<ProducerRoster> {
public:
    constexpr ProducerRoster() noexcept = default;

    // A producer handle starts.  The channel is open again.
    void add_producer() noexcept {
        update_([](std::uint64_t observed) noexcept {
            std::uint64_t const count = observed & count_mask;
            CRUCIBLE_FATAL_INVARIANT(count < count_mask);
            return count + 1;
        });
    }

    // A producer handle ends with its token.  The last one closes the
    // channel.
    void end_producer() noexcept {
        update_([](std::uint64_t observed) noexcept {
            std::uint64_t const count = observed & count_mask;
            CRUCIBLE_FATAL_INVARIANT(count > 0);
            return count == 1 ? closed_bit : count - 1;
        });
    }

    // A producer handle gives its token back through a release door.  The
    // channel stays open, because the token can open a handle again.
    void release_producer() noexcept {
        update_([](std::uint64_t observed) noexcept {
            std::uint64_t const count = observed & count_mask;
            CRUCIBLE_FATAL_INVARIANT(count > 0);
            return count - 1;
        });
    }

    // True when the last producer handle ended and no handle started after
    // it.  An acquire read, so a pop after a true answer sees each push of
    // the stream.
    [[nodiscard]] bool is_closed() const noexcept { return state_->load_acquire() == closed_bit; }

    // The count of the live producer handles.  A snapshot for a test.
    [[nodiscard]] std::uint64_t live_producers() const noexcept { return state_->load_acquire() & count_mask; }

private:
    static constexpr std::uint64_t closed_bit = std::uint64_t{1} << 63;
    static constexpr std::uint64_t count_mask = closed_bit - 1;

    // Stores next(observed) over the word that it read, with one
    // compare-and-swap.  A refused swap reads the word again.
    template <class Next>
    void update_(Next next) noexcept {
        std::uint64_t observed = state_->load_acquire();
        for (;;) {
            bool is_stored = true;
            for (::foundation::core::CasRefusal<std::uint64_t> const& refusal :
                 state_->cas_acq_rel(observed, next(observed)).err()) {
                observed = refusal.observed;
                is_stored = false;
            }
            if (is_stored) return;
        }
    }

    ::foundation::core::CacheLine<::foundation::core::Atomic<std::uint64_t>> state_{};
};

}  // namespace fixy::concurrent
