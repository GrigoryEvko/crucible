#pragma once

// The proof that the calling thread holds the GIL of the Python interpreter,
// and the read of the dispatch library that needs the proof.
//
// A C entry that reads a PyTorch object by its address reads the object only
// while the calling thread holds the GIL.  Then no other Python thread runs,
// so no Python thread can drop the last reference of the object, or give the
// tensor a new storage, during the read.  ctypes keeps the GIL for an entry
// that a PyDLL object binds (GIL_ENTRIES of crucible_native.py).  But each
// ctypes caller can reach the same entry through a CDLL object, which releases
// the GIL.  So the entry asks the interpreter one time at its boundary, and
// GilHeld carries the answer in its type.
//
// The door
//   mint_gil_held is the only door to GilHeld.  It calls PyGILState_Check of
//   the interpreter.  The library declares that function as a weak C symbol,
//   and it does not link libpython.  In the Python process the symbol
//   resolves to the interpreter.  In a process with no interpreter, the
//   address of the symbol is null.  A null symbol and a thread that does not
//   hold the GIL each give none, so a failed lookup refuses each read and
//   reads no memory.
//
// The interpreter
//   The proof is sound for an interpreter with a GIL.  A free-threaded
//   interpreter runs Python threads at the same time, so the Python side
//   calls no GIL entry in it (is_gil_enabled of crucible_native.py).
//
// The proof
//   GilHeld holds nothing.  It does not copy, and the attribute thread_bound
//   keeps it on the thread that made it.  The proof lives in the frame of the
//   entry that made it, and the GIL stays held while that frame runs, because
//   no code of the entry releases the GIL.

#include <crucible/Types.h>
#include <fixy/Core.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/Transfer.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the interpreter of Python gives the check of its GIL as a weak C symbol")
extern "C" {
[[gnu::weak]] int PyGILState_Check();
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace crucible::vessel {

// An Option of GilHeld needs the full class, so the friend declaration of the
// mint deduces its return type, and the definition below gives it.
class[[= ::foundation::transfer::thread_bound{}]] GilHeld {
public:
    GilHeld(GilHeld&&) noexcept = default;
    GilHeld(const GilHeld&) = delete("a proof of the GIL names one call of one thread");
    GilHeld& operator=(const GilHeld&) = delete("a proof of the GIL names one call of one thread");
    GilHeld& operator=(GilHeld&&) = delete("a proof of the GIL names one call of one thread");
    ~GilHeld() = default;

private:
    constexpr GilHeld() noexcept = default;
    friend auto mint_gil_held() noexcept;
};

// Gives the proof when the calling thread holds the GIL of the interpreter,
// and none when the process has no interpreter or the thread does not hold
// the GIL.  One load of the symbol, and one call of the interpreter.
[[nodiscard]] inline auto mint_gil_held() noexcept {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the interpreter of Python gives the check of its GIL as a weak C symbol")
    const bool is_held = PyGILState_Check != nullptr && PyGILState_Check() != 0;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    if (!is_held) return ::fixy::Option<GilHeld>{::fixy::none};
    return ::fixy::Option<GilHeld>::some(GilHeld{});
}

// The generation word of the c10::StorageImpl at the address storage, which
// a C caller gives (UntypedStorage._cdata of Python), as fill_meta gives it to
// the storage at this time.  The proof keeps each Python thread out of the
// read, so the storage stays alive and keeps its data during the read.  The
// address names a live StorageImpl and is not null.  crucible_fallback.cpp
// holds the body, beside the C entry.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the read takes the address of a StorageImpl that the C caller gives")
[[nodiscard]] crucible::StorageGeneration storage_generation_at(const GilHeld& gil, void* storage) noexcept;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace crucible::vessel
