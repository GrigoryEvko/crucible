#pragma once

// The block of SIGPIPE in one thread, for a write that must not end the
// process by SIGPIPE.
//
// Pipe signal
//   A write to a pipe or a socket whose reader has gone raises SIGPIPE in the
//   thread of the write, and the default action ends the process.  A send
//   takes MSG_NOSIGNAL, but write(2) has no such flag.  So a write that must
//   not end the process holds a PipeSignalBlock for the write.  The block
//   adds SIGPIPE to the mask of the calling thread.  A write that then gives
//   epipe leaves the SIGPIPE of the write pending, and the writer marks the
//   block (mark_broken_pipe).  The destructor takes that pending signal with
//   a wait of zero, and then it removes SIGPIPE from the mask.  The action of
//   SIGPIPE in the process does not change.
//
//   A thread that blocked SIGPIPE before the block keeps its mask, and the
//   signal of the write stays pending for it.  A SIGPIPE that a different
//   process sends during the write merges with the signal of the write, so
//   the destructor takes it too.
//
// Raw calls
//   The block issues rt_sigprocmask and rt_sigtimedwait raw, in
//   src/foundation/os/Syscall.cpp, with a set that holds only SIGPIPE.  The
//   enumeration Syscall names neither call, so no door can issue them with a
//   different set, and the deny row of the signal calls in
//   utils/rule-table.txt refuses each other raw signal call (the owner
//   decision of 2026-11-14).  The set holds neither of the two signals that
//   the C library keeps for its threads.  A raw call takes no lock, allocates
//   nothing and does not change errno, so the report sink can hold a block in
//   a signal handler.  The wait has a time of zero, so it does not block, and
//   ThreadSanitizer keeps no state of a mask, so the two calls need no route
//   (the sanitizers of foundation/os/Syscall.h).
//
// The block lives on the stack of its thread: it does not copy or move, and
// new refuses it.  This header is in the floor layer F0_os of the rule
// table, so the start-up check of the kernel floor, the report sink and the
// file door can use it.

#include <cstddef>

namespace foundation::os {

namespace detail {

// Adds SIGPIPE to the mask of the calling thread.  It gives true when the
// mask held SIGPIPE before the call.
[[nodiscard]] bool block_pipe_signal_() noexcept;

// Ends a block that block_pipe_signal_ began, which gave WAS_BLOCKED.  When
// the mask did not hold SIGPIPE before the block, the call takes a pending
// SIGPIPE after a write that gave epipe (HAS_BROKEN_PIPE), and then it
// removes SIGPIPE from the mask.  Otherwise it changes nothing.
void release_pipe_signal_(bool was_blocked, bool has_broken_pipe) noexcept;

}  // namespace detail

// The block of SIGPIPE in the calling thread, for the life of the object
// (the pipe signal).
class PipeSignalBlock final {
public:
    [[nodiscard]] static PipeSignalBlock block_calling_thread() noexcept { return PipeSignalBlock{}; }

    PipeSignalBlock(PipeSignalBlock const&) = delete("the block belongs to one thread, and one object ends it");
    PipeSignalBlock(PipeSignalBlock&&) = delete("the block belongs to one thread, and one object ends it");
    PipeSignalBlock& operator=(PipeSignalBlock const&) = delete("the block belongs to one thread");
    PipeSignalBlock& operator=(PipeSignalBlock&&) = delete("the block belongs to one thread");
    static void* operator new(std::size_t) = delete("a PipeSignalBlock lives on the stack of its thread");
    static void* operator new[](std::size_t) = delete("a PipeSignalBlock lives on the stack of its thread");

    ~PipeSignalBlock() { detail::release_pipe_signal_(was_blocked_, has_broken_pipe_); }

    // Marks that a write under the block gave epipe, so the destructor takes
    // the SIGPIPE of that write.
    void mark_broken_pipe() noexcept { has_broken_pipe_ = true; }

private:
    PipeSignalBlock() noexcept : was_blocked_{detail::block_pipe_signal_()} {}

    bool was_blocked_ = false;
    bool has_broken_pipe_ = false;
};

}  // namespace foundation::os
