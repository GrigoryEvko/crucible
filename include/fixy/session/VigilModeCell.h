#pragma once

// The Vigil runtime's mode, and the cell that one thread owns.
//
// The runtime hub reads and publishes the mode through the cell, and it
// names no session over the cell.  So the cell has a header of its own,
// and the hub does not include the session machinery.  The legal
// transitions as a relation, the protocol that an observer drives and
// the factory of its handle are in fixy/session/VigilMode.h, which
// includes this header.

#include <fixy/session/NetworkModel.h>

#include <foundation/Pinned.h>
#include <foundation/Platform.h>

#include <atomic>
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

class ModeCell : public ::foundation::Pinned<ModeCell> {
    std::atomic<Mode> value_{Mode::RECORDING};

public:
    using state_type = Mode;

    // A session over the cell moves the cell and no peer, so each choice
    // puts no label (Local choices in fixy/session/Handle.h).
    static constexpr Network session_network = Network::Local;

    constexpr ModeCell() noexcept = default;

    ModeCell(const ModeCell&) = delete("Vigil mode cell is process-local state");
    ModeCell& operator=(const ModeCell&) = delete("Vigil mode cell is process-local state");
    ModeCell(ModeCell&&) = delete("atomic mode cell is the channel identity");
    ModeCell& operator=(ModeCell&&) = delete("atomic mode cell is the channel identity");

    // The owner of the cell publishes with release stores, and each load
    // is an acquire, so an observer that reads a mode also sees what the
    // owner wrote before it published that mode.  A session moves the
    // cell along one admitted edge with a compare-and-swap from the source
    // of the edge.  The cell holds one of two modes, so a compare that
    // fails finds the target.  A cell in a third mode stops the process at
    // the compare, and no session writes over it.

    [[nodiscard]] Mode load() const noexcept { return value_.load(std::memory_order_acquire); }

    void publish_compiled() noexcept { value_.store(Mode::COMPILED, std::memory_order_release); }

    void publish_recording_after_divergence() noexcept { value_.store(Mode::RECORDING, std::memory_order_release); }

    // Each returns true: the cell then stands at the target of the edge.
    bool publish_from_session(ModeRecordingToCompiled) noexcept { return take_edge_(Mode::RECORDING, Mode::COMPILED); }

    bool publish_from_session(ModeCompiledToRecording) noexcept { return take_edge_(Mode::COMPILED, Mode::RECORDING); }

private:
    bool take_edge_(Mode from, Mode to) noexcept {
        Mode seen = from;
        if (!value_.compare_exchange_strong(seen, to, std::memory_order_acq_rel, std::memory_order_acquire)) {
            CRUCIBLE_FATAL_INVARIANT(seen == to);
        }
        return true;
    }
};

}  // namespace fixy::session::vigil_mode
