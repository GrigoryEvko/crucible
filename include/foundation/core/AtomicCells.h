#pragma once

// The shared view of a run of atomic cells.
//
//   share_atomic_cells(run)   the View of the run of a ViewMut whose element
//                             is a cell of the base (AtomicCell of
//                             foundation/core/Atomic.h), with the extent of
//                             the ViewMut
//
// A ViewMut is the one writer of its run.  A run of atomic cells can have
// many writers at the same time, as a &[AtomicU64] of Rust can, because each
// write to a cell is one atomic operation.  An Atomic is Pinned and is not
// trivially copyable, so no assignment, no fill and no copy takes it.  The
// ViewMut, its parts and each View of this door then write a cell only
// through its atomic operations, and no two writes race.
//
// A body of a frame gives the View to each shard of a parallel combine, and
// each shard keeps its own copy.  The View borrows the run of the frame, as
// the ViewMut does.  The frame keeps the run alive while the body runs, and
// the result of a frame holds no borrow (FrameResult of
// foundation/core/Choice.h).  So the View does not leave the frame through the
// result.  A thread door moves a View of cells to another thread only when the
// cell is Synchronized (foundation/Transfer.h).
//
// This header stands above Region.h and Atomic.h.  Region.h includes Ref.h,
// and Ref.h includes Atomic.h for the word of a Ref, so Atomic.h cannot name
// a ViewMut.

#include <foundation/Brand.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Region.h>

#include <cstddef>

namespace foundation::core {

// The View of the run of a ViewMut of atomic cells, with the extent of the
// ViewMut.  The door reads the run through a const reference, so the frame
// keeps its ViewMut, and a body can take the View more than one time.  A run
// of an element that is not a cell of the base, such as an int or a class
// that holds an Atomic, has no form.  Complexity: O(1).
template <class T, ::foundation::brand::IsBrand Brand, std::size_t Extent>
    requires AtomicCell<T>
[[nodiscard]] constexpr View<T, Extent> share_atomic_cells(ViewMut<T, Brand, Extent> const& run) noexcept {
    return detail::run_of_(run);
}

}  // namespace foundation::core
