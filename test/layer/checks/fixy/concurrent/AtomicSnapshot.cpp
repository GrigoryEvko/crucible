// The compile-time checks of fixy/concurrent/AtomicSnapshot.h.

#include <fixy/concurrent/AtomicSnapshot.h>

#include <foundation/Transfer.h>

namespace fixy::concurrent {

// Each part of a snapshot is an atomic cell, so the writer and the readers use
// one snapshot at the same time, and an array of snapshots lends one with no
// key.
namespace {
struct SnapshotCheckOwner final {};
struct SnapshotCheckRole final {};
using CheckedSnapshot =
    AtomicSnapshot<std::uint64_t, ::foundation::SoleWriterProof<SnapshotCheckOwner, SnapshotCheckRole>>;
}  // namespace
static_assert(::foundation::transfer::Synchronized<CheckedSnapshot>);

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "AtomicSnapshot's seq counter requires lock-free uint64_t atomic");

}  // namespace fixy::concurrent
