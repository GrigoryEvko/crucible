#pragma once

// The Vigil runtime's mode, and the cell that one thread writes.
//
// The runtime hub reads and publishes the mode through the cell, and it
// names no session over the cell.  So the cell has a header of its own,
// and the hub does not include the session machinery.  The legal
// transitions as a relation, the protocol that an observer drives and
// the factory of its handle are in fixy/session/VigilMode.h, which
// includes this header.
//
// ── One writer ──────────────────────────────────────────────────────
//
// The cell has one writer at each time.  Each write takes the proof of that
// writer, a ModeWriter.  The owner of the cell, the foreground thread of
// the Vigil, holds the writer and publishes each mode that the replay
// context takes.  While a session over the cell is open, the owner lends
// the writer to the session bridge in a ModeLease of
// fixy/session/VigilMode.h, and the close of the session gives the lease
// back with the writer in it.  A moved-from writer authorizes no write, so
// the owner cannot write while the writer is lent.
//
// The cell keeps the run-time identity of the writer that built it, so the
// writer of one cell cannot write a different cell.

#include <fixy/session/NetworkModel.h>

#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Identity.h>

#include <cstdint>

namespace fixy::session::vigil_mode {

enum class Mode : std::uint8_t {
    RECORDING,
    COMPILED,
    DIVERGED,
};

// A transition as a value the session sends.  Each admitted edge has one
// plain class, so the payload walk of fixy/concurrent/PayloadRow.h reads
// it as a class with no member, and its effect row is empty.
struct ModeRecordingToCompiled {
    static constexpr Mode from = Mode::RECORDING;
    static constexpr Mode to = Mode::COMPILED;
};

struct ModeCompiledToRecording {
    static constexpr Mode from = Mode::COMPILED;
    static constexpr Mode to = Mode::RECORDING;
};

class ModeWriter;
class ModeCell;

[[nodiscard]] inline ModeWriter mint_mode_writer() noexcept;

// The proof of the one writer of a mode cell.  It holds the run-time
// identity of its mint, and the cell keeps the identity of the writer that
// built it.  It is linear: a move gives the identity to the target and
// leaves the source with no right to write.  It is not the SoleWriter of
// foundation/core/Atomic.h, because the brand of a SoleWriter is a closure
// type with internal linkage, and no class of a header can hold one.
//
// The move constructor is user-provided, so the class is not trivially
// copyable, and std::bit_cast builds no writer.  No constructor is trivial,
// so no lifetime start over bytes makes one.
class [[nodiscard]] ModeWriter {
    ::foundation::core::Identity identity_;
    bool is_held_ = true;

    explicit ModeWriter(::foundation::core::Identity identity) noexcept : identity_{identity} {}

    friend ModeWriter mint_mode_writer() noexcept;
    friend class ModeCell;

public:
    ModeWriter(ModeWriter&& other) noexcept : identity_{other.identity_}, is_held_{other.is_held_} {
        other.is_held_ = false;
    }
    ModeWriter(const ModeWriter&) = delete("a copy gives the mode cell a second writer");
    ModeWriter& operator=(const ModeWriter&) = delete("a copy gives the mode cell a second writer");
    ModeWriter& operator=(ModeWriter&&) = delete("a writer names one cell for its whole life");
    ~ModeWriter() = default;
};

// Mints the writer of a new mode cell.  The cell takes the writer at its
// construction and keeps its identity, so the writer writes that cell and
// no other.  The cost is one atomic add.
[[nodiscard]] inline ModeWriter mint_mode_writer() noexcept { return ModeWriter{::foundation::core::mint_identity()}; }

namespace detail {

// A write of the cell showed a writer that the cell does not take: a
// moved-from writer, or the writer of a different cell.
[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_foreign_mode_writer() noexcept {
    ::foundation::core::fatal(
        "fixy::session::diagnostic [VigilMode_Writer_Not_Held]: a write of the mode cell showed a writer that the "
        "cell does not take: a writer that a move emptied, such as the writer of an owner that lent it to a "
        "session, or the writer of a different cell.  The cell has one writer at each time.  Write with the writer "
        "that built the cell, and take it back from the lease before the owner writes.");
}

// A session asked for an edge whose source the cell does not hold.  The
// session holds the one writer, so no other thread moved the cell, and a
// later try of the same edge cannot succeed.  A false answer would make the
// handle try the edge again with no end.
[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_edge_not_at_source() noexcept {
    ::foundation::core::fatal(
        "fixy::session::diagnostic [VigilModeBridge_Edge_Not_At_Source]: the mode session sent an edge, and the "
        "cell does not stand at the source of the edge.  The session holds the one writer of the cell, so no other "
        "thread moved the cell, and the session reports an edge that it cannot take.  Send the edge whose source is "
        "the mode that the cell holds.");
}

}  // namespace detail

// Two threads can write a cell at the same time: each write of the mode is
// atomic, and the identity of the writer is const (foundation/Transfer.h).
class ModeCell : public ::foundation::Pinned<ModeCell> {
    ::foundation::core::Atomic<Mode> value_{Mode::RECORDING};
    // The identity of the writer that built the cell.
    ::foundation::core::Identity const writer_;

    // Each write shows the writer that built the cell, held, and not a
    // writer that a move emptied.
    void expect_writer_(const ModeWriter& writer) const noexcept {
        if (!writer.is_held_ || !(writer.identity_ == writer_)) [[unlikely]]
            detail::abort_on_foreign_mode_writer();
    }

    [[nodiscard]] static ::foundation::core::Identity identity_of_(const ModeWriter& writer) noexcept {
        if (!writer.is_held_) [[unlikely]]
            detail::abort_on_foreign_mode_writer();
        return writer.identity_;
    }

public:
    using state_type = Mode;

    // A session over the cell moves the cell and no peer, so each choice
    // puts no label (Local choices in fixy/session/Handle.h).
    static constexpr Network session_network = Network::Local;

    // The cell starts in RECORDING, and keeps the identity of `writer`.
    explicit ModeCell(const ModeWriter& writer) noexcept : writer_{identity_of_(writer)} {}

    ModeCell(const ModeCell&) = delete("Vigil mode cell is process-local state");
    ModeCell& operator=(const ModeCell&) = delete("Vigil mode cell is process-local state");
    ModeCell(ModeCell&&) = delete("atomic mode cell is the channel identity");
    ModeCell& operator=(ModeCell&&) = delete("atomic mode cell is the channel identity");

    // Each load is an acquire, so an observer that reads a mode also sees
    // what the writer wrote before it published that mode.
    [[nodiscard]] Mode load() const noexcept { return value_.load_acquire(); }

    // The owner publishes each mode with a release store.
    void publish_compiled(const ModeWriter& writer) noexcept {
        expect_writer_(writer);
        value_.store_release(Mode::COMPILED);
    }

    void publish_recording_after_divergence(const ModeWriter& writer) noexcept {
        expect_writer_(writer);
        value_.store_release(Mode::RECORDING);
    }

    // Moves the cell along one edge of the mode session, for the ModeLease
    // of fixy/session/VigilMode.h that holds the writer.  Each value of the
    // cell is a value that the holder of the writer stored.  A cell that is
    // not at the source of the edge ends the process, so an answer is always
    // true.
    [[nodiscard]] bool take_edge(const ModeWriter& writer, Mode from, Mode to) noexcept {
        expect_writer_(writer);
        if (value_.load_acquire() != from) [[unlikely]]
            detail::abort_on_edge_not_at_source();
        value_.store_release(to);
        return true;
    }
};

}  // namespace fixy::session::vigil_mode
