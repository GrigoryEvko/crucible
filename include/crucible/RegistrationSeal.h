#pragma once

// The phase of a table that is filled, then sealed, then read with no lock.
//
// A registration writes the table.  After the seal, readers on other threads
// read the table with a plain load.  A registration mints its view first and
// writes later, and a seal on a different thread can land between the two.
// The view cannot see that seal.  A write that the phase did not admit again
// at the write itself would race every lock-free reader.
//
// One atomic word holds the phase, and each write runs inside a section that
// the phase admits:
//
//   Open    --writer enters (CAS, acquire)-->   Writing
//   Writing --writer leaves (store, release)--> Open
//   Open    --seal (CAS, acq_rel)-->            Sealed
//
// The ordering argument, with acquire and release only:
//
//   1. Every transition is a compare-exchange or a store on the one word, so
//      all transitions have one modification order.
//   2. A writer changes the table only between its acquire CAS that leaves
//      Open and its release store that returns to Open.
//   3. The seal CAS reads Open.  That value is the initial value or the
//      release store of the last writer, so every finished write happens
//      before the seal.
//   4. A reader that loads Sealed with acquire synchronizes with the release
//      half of the seal CAS, so it sees every write that the table got.
//   5. Sealed never returns to Open (a test context can reopen a table, see
//      reopen), so no CAS from Open succeeds after the seal and no write
//      starts.  A write that races the seal either finishes before the seal
//      or finds Sealed and writes nothing.
//
// The seal waits while a writer holds the section, and a writer waits while
// another writer holds it.  One registration is a bounded, short piece of
// work, so both waits spin with the pause instruction.  After each failed
// compare-exchange, a wait spins a number of pauses that doubles up to a
// limit.  A compare-exchange that fails still takes the cache line, so
// waiters that retry at once delay the release store of the writer in the
// section.  ThreadSanitizer also takes a read lock for each acquire
// compare-exchange and a write lock for the release store, and its lock
// admits readers first.  When each waiter has a CPU and retries at once,
// the writer in the section never gets the write lock, and the section
// never closes.

#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace crucible {

class RegistrationSeal {
public:
    RegistrationSeal() = default;

    RegistrationSeal(const RegistrationSeal&) = delete("the phase word is the identity of one table");
    RegistrationSeal& operator=(const RegistrationSeal&) = delete("the phase word is the identity of one table");
    RegistrationSeal(RegistrationSeal&&) = delete("the phase word is the identity of one table");
    RegistrationSeal& operator=(RegistrationSeal&&) = delete("the phase word is the identity of one table");

    // Runs `write` while the table is open, with no other writer and no seal
    // in between.  Returns false, and does not run `write`, when the table
    // is sealed.  The section closes even if `write` exits early.
    template <typename Write>
        requires std::is_invocable_r_v<void, Write&>
    [[nodiscard]] bool with_write_section(Write&& write) noexcept(std::is_nothrow_invocable_v<Write&>) {
        if (!enter_writer_()) return false;
        struct Leave {
            RegistrationSeal& seal;
            ~Leave() { seal.phase_.store(Phase::Open, std::memory_order_release); }
        } const leave{*this};
        write();
        return true;
    }

    // Seals the table.  Waits for a writer that holds the section, and seals
    // one time: a second call finds Sealed and returns.
    void seal() noexcept {
        Phase expected = Phase::Open;
        std::uint32_t pause_count = 1;
        while (!phase_.compare_exchange_weak(expected, Phase::Sealed, std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
            if (expected == Phase::Sealed) return;
            expected = Phase::Open;
            back_off_(pause_count);
        }
    }

    // Not constexpr: std::atomic::load is not constexpr, so no constant
    // evaluation can read the phase.
    [[nodiscard]] bool is_sealed() const noexcept { return phase_.load(std::memory_order_acquire) == Phase::Sealed; }

    // Returns a sealed table to the open phase.  Only a test reuses one
    // table across cases, so this takes the test context, which code that
    // ships cannot mint.  The caller makes sure that no writer and no reader
    // uses the table at the same time.
    void reopen(::foundation::effects::Test const&) noexcept { phase_.store(Phase::Open, std::memory_order_release); }

private:
    enum class Phase : std::uint8_t {
        Open,
        Writing,
        Sealed
    };

    // The largest number of pauses that one wait spins between two
    // compare-exchanges.
    static constexpr std::uint32_t max_backoff_pauses_ = 1024;

    // Spins `pause_count` pauses, then doubles the count up to the limit.
    static void back_off_(std::uint32_t& pause_count) noexcept {
        for (std::uint32_t pause = 0; pause < pause_count; ++pause)
            CRUCIBLE_SPIN_PAUSE;
        if (pause_count < max_backoff_pauses_) pause_count *= 2;
    }

    // Takes the section.  Returns false when the table is sealed.
    [[nodiscard]] bool enter_writer_() noexcept {
        Phase expected = Phase::Open;
        std::uint32_t pause_count = 1;
        while (!phase_.compare_exchange_weak(expected, Phase::Writing, std::memory_order_acquire,
                                             std::memory_order_acquire)) {
            if (expected == Phase::Sealed) return false;
            expected = Phase::Open;
            back_off_(pause_count);
        }
        return true;
    }

    std::atomic<Phase> phase_{Phase::Open};
    static_assert(std::atomic<Phase>::is_always_lock_free);
};

}  // namespace crucible
