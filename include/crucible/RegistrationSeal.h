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
//   Open    --writer enters (CAS, acq_rel)-->   Writing
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
// A writer holds the section as a WriteSection of its table.  The claim door
// try_enter_write_section makes the acquire CAS, and the end of the section
// makes the release store.  Each writer door of a table takes the section, so
// no write runs outside the section.  The section names its table type, so a
// section of one table type reaches no writer door of another type.  A
// section moves, and a move leaves the source with no binding.  The guard
// rule use-after-move refuses a use of a moved local section in the source,
// before any run, and the precondition of each writer door refuses a
// moved-from section that reaches the door by another route.
//
// The seal waits while a writer holds the section, and a writer waits while
// another writer holds it.  One registration is a bounded, short piece of
// work, so both waits spin with the pause instruction.  Each wait has no
// exit while the holder of the section does not leave it, and a spin with
// no exit carries Div (foundation/effects/Effect.h).  So the seal takes a
// context that owns Div, and a writer takes one of two contexts:
//
//   - a context that owns Div, which spins while another writer holds the
//     section
//   - the context of a Vigil's producer claim (VigilFgCtx), which owns no
//     atom.  Each production registration comes from that thread, and the
//     claim lets one thread hold it at a time, so that writer meets no other
//     writer.  It takes the section with one compare-exchange and no spin.
//     A second writer in the section ends the process.
//
// After each failed
// compare-exchange, a wait spins a number of pauses that doubles up to a
// limit.  A compare-exchange that fails still takes the cache line, so
// waiters that retry at once delay the release store of the writer in the
// section.  ThreadSanitizer also takes a read lock for each acquire
// compare-exchange and a write lock for the release store, and its lock
// admits readers first.  When each waiter has a CPU and retries at once,
// the writer in the section never gets the write lock, and the section
// never closes.

#include <crucible/ForegroundCtx.h>
#include <fixy/Core.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible {

// A context that may wait with no exit: it owns Div.
template <class Ctx>
concept CtxMaySpinForSeal = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Div>;

// A context that may enter the write section of a table: the context of a
// Vigil's producer claim, the one writer of the process, or a context that
// owns Div, which spins while another writer holds the section.
template <class Ctx>
concept CtxMayEnterWriteSection = std::same_as<std::remove_cvref_t<Ctx>, VigilFgCtx> || CtxMaySpinForSeal<Ctx>;

class RegistrationSeal;

// The write section of one table of the type Table, which one writer holds.
// The seal of the table makes a section only when its compare-exchange takes
// the phase from Open to Writing, so the writes through the section happen
// only on the thread that holds it, until the section ends.  The end of the
// section makes the release store that gives the phase back to Open.  The
// section holds the binding of its seal, so a writer door of the table
// checks that the section names its own seal: two tables of one type have
// two seals.  A move gives the binding to the new section and leaves the
// source with no binding.  The end of a section with no binding stores
// nothing, and a writer door refuses it.  Only RegistrationSeal makes a
// section, so the class is closed, and the section is the proof of a writer
// door of a synchronized table (foundation/Transfer.h).
template <class Table>
class[[= ::foundation::linear::authority{}]] WriteSection {
public:
    WriteSection(WriteSection const&) = delete("a section is the one write section of its table");
    WriteSection& operator=(WriteSection const&) = delete("a section is the one write section of its table");
    WriteSection(WriteSection&&) noexcept = default;
    WriteSection& operator=(WriteSection&&) = delete("a section holds the write section of one seal for its life");

    ~WriteSection();

    // True when the section is the write section of seal.
    [[nodiscard]] bool names(RegistrationSeal const& seal) const noexcept;

private:
    explicit WriteSection(::foundation::ChannelBinding<RegistrationSeal> seal) noexcept : seal_{std::move(seal)} {}

    friend class RegistrationSeal;

    ::foundation::ChannelBinding<RegistrationSeal> seal_;
};

// The proof of a test that empties one table of the type Table and opens it
// again (the clear door of a table).  A sealed table gives no write section,
// so the clear takes this proof in place of a section.  The constructor takes
// the test context, which code that ships cannot mint.  A Test has no public
// constructor other than its copy and its move, so only a holder of the test
// context builds the proof, and the proof is a proof of the clear door of a
// synchronized table (foundation/Transfer.h).
template <class Table>
class[[= ::foundation::linear::authority{}]] TableReopen final {
public:
    explicit TableReopen(::foundation::effects::Test const& /*test*/) noexcept {}

    TableReopen(TableReopen const&) = delete("a proof of a reopen opens one table one time");
    TableReopen& operator=(TableReopen const&) = delete("a proof of a reopen opens one table one time");
    TableReopen(TableReopen&&) noexcept = default;
    TableReopen& operator=(TableReopen&&) = delete("a proof of a reopen opens one table one time");
    ~TableReopen() = default;
};

class RegistrationSeal {
public:
    RegistrationSeal() = default;

    RegistrationSeal(const RegistrationSeal&) = delete("the phase word is the identity of one table");
    RegistrationSeal& operator=(const RegistrationSeal&) = delete("the phase word is the identity of one table");
    RegistrationSeal(RegistrationSeal&&) = delete("the phase word is the identity of one table");
    RegistrationSeal& operator=(RegistrationSeal&&) = delete("the phase word is the identity of one table");

    // Takes the write section of the table of the type Table that holds this
    // seal, or gives no value when the table is sealed.  The context of the
    // writer picks the entry: one compare-exchange for the writer of a
    // Vigil's producer claim, and a spin for a context that owns Div.
    template <class Table, typename Ctx>
        requires CtxMayEnterWriteSection<Ctx>
    [[nodiscard]] ::fixy::Option<WriteSection<Table>> try_enter_write_section(Ctx const& /*ctx*/) noexcept {
        bool is_open = false;
        if constexpr (std::same_as<Ctx, VigilFgCtx>) {
            is_open = enter_sole_writer_();
        } else {
            is_open = enter_writer_();
        }
        if (!is_open) return ::fixy::none;
        return ::fixy::Option<WriteSection<Table>>::some(WriteSection<Table>{
            ::foundation::ChannelBinding<RegistrationSeal>{*this, ::foundation::BindingKey<RegistrationSeal>{}}});
    }

    // Seals the table.  Waits for a writer that holds the section, and seals
    // one time: a second call finds Sealed and returns.  The wait has no
    // exit while the writer stays in the section, so the context owns Div.
    template <typename Ctx>
        requires CtxMaySpinForSeal<Ctx>
    void seal(Ctx const& /*ctx*/) noexcept {
        for (std::uint32_t pause_count = 1;; back_off_(pause_count)) {
            Phase const observed = swap_from_open_(Phase::Sealed);
            if (observed == Phase::Open || observed == Phase::Sealed) return;
        }
    }

    // Not constexpr: Atomic::load_acquire is not constexpr, so no constant
    // evaluation can read the phase.
    [[nodiscard]] bool is_sealed() const noexcept { return phase_.load_acquire() == Phase::Sealed; }

    // Returns a sealed table to the open phase.  Only a test reuses one
    // table across cases, so this takes the proof of a reopen, which only
    // the test context builds.  The caller makes sure that no writer and no
    // reader uses the table at the same time.
    template <class Table>
    void reopen(TableReopen<Table> const& /*reopen*/) noexcept {
        phase_.store_release(Phase::Open);
    }

private:
    enum class Phase : std::uint8_t {
        Open,
        Writing,
        Sealed
    };

    template <class Table>
    friend class WriteSection;

    // The largest number of pauses that one wait spins between two
    // compare-exchanges.
    static constexpr std::uint32_t max_backoff_pauses_ = 1024;

    // Spins `pause_count` pauses, then doubles the count up to the limit.
    static void back_off_(std::uint32_t& pause_count) noexcept {
        for (std::uint32_t pause = 0; pause < pause_count; ++pause)
            CRUCIBLE_SPIN_PAUSE;
        if (pause_count < max_backoff_pauses_) pause_count *= 2;
    }

    // One compare-exchange from Open to next.  Gives the phase that the word
    // held: Open when the exchange took place, and each other phase when it
    // did not.
    [[nodiscard]] Phase swap_from_open_(Phase next) noexcept {
        return phase_.cas_acq_rel(Phase::Open, next)
            .match([](::fixy::Unit) noexcept { return Phase::Open; },
                   [](::fixy::CasRefusal<Phase> refusal) noexcept { return refusal.observed; });
    }

    // Takes the section for the one writer of the process, with one
    // compare-exchange.  Returns false when the table is sealed.  A seal can
    // land between the mint of the view and this call, and the exchange then
    // reads Sealed.  Another writer in the section breaks the claim of one
    // writer, so it ends the process.
    [[nodiscard]] bool enter_sole_writer_() noexcept {
        Phase const observed = swap_from_open_(Phase::Writing);
        if (observed == Phase::Writing) [[unlikely]] {
            ::fixy::fatal("two writers entered the write section of a table, but the writer of a Vigil's producer "
                          "claim is the one writer of the process.  A writer on another thread takes a context that "
                          "owns Div.");
        }
        return observed == Phase::Open;
    }

    // Takes the section, and spins while another writer holds it.  Returns
    // false when the table is sealed.
    [[nodiscard]] bool enter_writer_() noexcept {
        for (std::uint32_t pause_count = 1;; back_off_(pause_count)) {
            Phase const observed = swap_from_open_(Phase::Writing);
            if (observed == Phase::Open) return true;
            if (observed == Phase::Sealed) return false;
        }
    }

    // Gives the section back: the release store of the last write.
    void leave_section_() noexcept { phase_.store_release(Phase::Open); }

    // The identity of this seal, which a section compares with the identity
    // of its binding.
    [[nodiscard]] ::foundation::ChannelIdentity<RegistrationSeal> identity_() const noexcept {
        return ::foundation::ChannelIdentity<RegistrationSeal>{this};
    }

    ::fixy::Atomic<Phase> phase_{Phase::Open};
};

template <class Table>
WriteSection<Table>::~WriteSection() {
    if (seal_.is_bound()) seal_->leave_section_();
}

template <class Table>
bool WriteSection<Table>::names(RegistrationSeal const& seal) const noexcept {
    return seal_.identity() == seal.identity_();
}

}  // namespace crucible
