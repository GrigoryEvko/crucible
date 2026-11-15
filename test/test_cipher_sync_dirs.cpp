// Each change of a directory of the Cipher store reaches storage: the store
// syncs the directory that holds each rename and each new directory.  A
// rename that only a sync of another directory follows, or a new directory
// that no sync of its parent follows, can go away in a power loss.  The store
// then loses an entry that it reported as stored.
//
// The test runs an open, a store and a persist of session events in a child
// process, and the parent traces the system calls of the child.  It records
// each mkdirat, renameat and renameat2 that succeeds, with the directory that
// holds its new entry, and each fsync that succeeds, with the path of its
// descriptor.  The check: each change has a later fsync of the directory
// that holds it.  The store syncs a directory with fsync and a file with
// fdatasync, so the sync of a file is no sync of a directory here.
//
// A host that refuses ptrace gives a skip.

#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <foundation/Quarantine.h>
#include <foundation/os/PathText.h>

#include "scratch_dir.h"

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a traced child shows the directory of each sync of the Cipher store")
#include <fcntl.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// A change of a directory, or a sync of a directory.
enum class EventKind : int {
    change,
    sync
};

// One event of the trace: the directory that holds the new entry of a
// change, or the directory of a sync.
struct TraceEvent {
    EventKind kind = EventKind::change;
    char path[512] = {};
};

// The events of a trace, in the order of the system calls.
struct TraceLog {
    TraceEvent events[512] = {};
    int count = 0;
    bool is_full = false;
};

// The exit code of a child that the host does not let the parent trace.
constexpr int kTraceRefused = 2;

void push_event(TraceLog& log, EventKind kind, const char* path) {
    if (log.count == static_cast<int>(sizeof log.events / sizeof log.events[0])) {
        log.is_full = true;
        return;
    }
    TraceEvent& event = log.events[log.count];
    event.kind = kind;
    std::snprintf(event.path, sizeof event.path, "%s", path);
    ++log.count;
}

// Reads the text at address in the child into out, one word at a time, up
// to its zero.  False when a word does not read.
bool read_child_text(pid_t child, std::uint64_t address, char* out, std::size_t capacity) {
    std::size_t at = 0;
    while (at + sizeof(long) < capacity) {
        errno = 0;
        const long word = ::ptrace(PTRACE_PEEKDATA, child, reinterpret_cast<void*>(address + at), nullptr);
        if (errno != 0) return false;
        std::memcpy(out + at, &word, sizeof word);
        if (std::memchr(&word, 0, sizeof word) != nullptr) return true;
        at += sizeof word;
    }
    out[capacity - 1] = '\0';
    return true;
}

// The path of a descriptor of the child, or of its current directory for
// AT_FDCWD.  False when the link does not read.
bool read_child_descriptor_path(pid_t child, int descriptor, char* out, std::size_t capacity) {
    char link[64] = {};
    if (descriptor == AT_FDCWD) {
        std::snprintf(link, sizeof link, "/proc/%d/cwd", static_cast<int>(child));
    } else {
        std::snprintf(link, sizeof link, "/proc/%d/fd/%d", static_cast<int>(child), descriptor);
    }
    const ssize_t length = ::readlink(link, out, capacity - 1);
    if (length <= 0) return false;
    out[length] = '\0';
    return true;
}

// The directory that holds the entry name under the directory base.  A path
// that does not fit out gives the empty text, which names no directory.
void parent_of_entry(const char* base, const char* name, char* out, std::size_t capacity) {
    const std::size_t base_length = name[0] == '/' ? 0 : std::strlen(base);
    const std::size_t name_length = std::strlen(name);
    if (base_length + 1 + name_length + 1 > capacity) {
        out[0] = '\0';
        return;
    }
    std::memcpy(out, base, base_length);
    std::size_t length = base_length;
    if (base_length != 0) out[length++] = '/';
    std::memcpy(out + length, name, name_length + 1);
    length += name_length;
    while (length > 1 && out[length - 1] == '/')
        out[--length] = '\0';
    char* const slash = std::strrchr(out, '/');
    if (slash == out) {
        out[1] = '\0';
    } else if (slash != nullptr) {
        *slash = '\0';
    }
}

// The descriptor in a word of a system call.  A descriptor is an int, so the
// low 32 bits hold it, also for AT_FDCWD.
int descriptor_of(std::uint64_t word) { return static_cast<int>(static_cast<std::uint32_t>(word)); }

// True when the call makes a new entry in a directory.  dir_word and
// name_word name the descriptor and the path of the new entry.
bool is_change_call(const __ptrace_syscall_info& info, std::uint64_t& dir_word, std::uint64_t& name_word) {
    const std::uint64_t number = info.entry.nr;
    if (number == __NR_mkdirat) {
        dir_word = info.entry.args[0];
        name_word = info.entry.args[1];
        return true;
    }
    bool is_rename = number == __NR_renameat2;
#if defined(__NR_renameat)
    is_rename = is_rename || number == __NR_renameat;
#endif
    if (is_rename) {
        dir_word = info.entry.args[2];
        name_word = info.entry.args[3];
        return true;
    }
    return false;
}

// Runs body in a child that the parent traces, and records its changes and
// syncs in log.  Gives 0 when the child ends with no error, kTraceRefused
// when the host refuses the trace, and 1 for each other failure.
template <class Body>
int trace_child(Body& body, TraceLog& log) {
    const pid_t child = ::fork();
    if (child < 0) return 1;
    if (child == 0) {
        if (::ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) != 0) ::_exit(kTraceRefused);
        ::raise(SIGSTOP);
        const bool is_done = body();
        // _exit skips the leak check at the exit, which would trace the
        // child a second time.
        ::_exit(is_done ? 0 : 1);
    }
    int status = 0;
    if (::waitpid(child, &status, 0) != child) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status) == kTraceRefused ? kTraceRefused : 1;
    if (::ptrace(PTRACE_SETOPTIONS, child, nullptr, PTRACE_O_TRACESYSGOOD | PTRACE_O_EXITKILL) != 0) return 1;
    // The event of the last entry stop, which its exit stop records when the
    // call succeeds.
    bool has_pending = false;
    EventKind pending_kind = EventKind::change;
    char pending[512] = {};
    int signal_to_deliver = 0;
    while (true) {
        if (::ptrace(PTRACE_SYSCALL, child, nullptr, reinterpret_cast<void*>(static_cast<long>(signal_to_deliver)))
            != 0) {
            return 1;
        }
        signal_to_deliver = 0;
        if (::waitpid(child, &status, 0) != child) return 1;
        if (WIFEXITED(status)) return WEXITSTATUS(status) == 0 ? 0 : 1;
        if (WIFSIGNALED(status)) return 1;
        if (!WIFSTOPPED(status)) continue;
        if (WSTOPSIG(status) != (SIGTRAP | 0x80)) {
            signal_to_deliver = WSTOPSIG(status);
            continue;
        }
        __ptrace_syscall_info info{};
        if (::ptrace(PTRACE_GET_SYSCALL_INFO, child, reinterpret_cast<void*>(sizeof info), &info) <= 0) return 1;
        if (info.op == PTRACE_SYSCALL_INFO_ENTRY) {
            has_pending = false;
            std::uint64_t dir_word = 0;
            std::uint64_t name_word = 0;
            if (is_change_call(info, dir_word, name_word)) {
                char base[512] = {};
                char name[512] = {};
                has_pending = read_child_descriptor_path(child, descriptor_of(dir_word), base, sizeof base)
                           && read_child_text(child, name_word, name, sizeof name);
                if (has_pending) parent_of_entry(base, name, pending, sizeof pending);
                pending_kind = EventKind::change;
            } else if (info.entry.nr == __NR_fsync) {
                has_pending =
                    read_child_descriptor_path(child, descriptor_of(info.entry.args[0]), pending, sizeof pending);
                pending_kind = EventKind::sync;
            }
        } else if (info.op == PTRACE_SYSCALL_INFO_EXIT) {
            if (has_pending && info.exit.rval == 0) push_event(log, pending_kind, pending);
            has_pending = false;
        }
    }
}

// The index of the first change that no later sync of its directory
// follows, or -1.  Complexity: quadratic in the count of events.
int first_unsynced_change(const TraceLog& log) {
    for (int change = 0; change < log.count; ++change) {
        if (log.events[change].kind != EventKind::change) continue;
        bool is_synced = false;
        for (int later = change + 1; later < log.count && !is_synced; ++later) {
            is_synced = log.events[later].kind == EventKind::sync
                     && std::strcmp(log.events[later].path, log.events[change].path) == 0;
        }
        if (!is_synced) return change;
    }
    return -1;
}

// The count of the changes of the trace.
int change_count(const TraceLog& log) {
    int count = 0;
    for (int index = 0; index < log.count; ++index) {
        if (log.events[index].kind == EventKind::change) ++count;
    }
    return count;
}

// Prints the trace, for a failure.
void print_trace(const TraceLog& log) {
    for (int index = 0; index < log.count; ++index) {
        std::fprintf(stderr, "  %s %s\n", log.events[index].kind == EventKind::change ? "change" : "sync  ",
                     log.events[index].path);
    }
}

// The characters of the path of the scratch directory.
[[nodiscard]] fixy::TextView text_of(crucible::test::ScratchDir const& scratch) noexcept {
    return fixy::mint_foreign_text(scratch.path().data(), scratch.path().size());
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

using PathBuffer = foundation::os::PathText<fixy::kPathBytes>;

// The log of the trace is large, so it is not on the stack of main.
TraceLog g_trace_log{};

[[nodiscard]] crucible::Cipher open_cipher(fixy::TextView root) {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    return crucible::Cipher::open(ctx, ctx.cap().alloc, fixy::mint_tagged<fixy::tags::source::External>(root));
}

// Opens a new store at root, stores one region, and persists one batch of
// session events.  True when the store gives the two hashes.
[[nodiscard]] bool store_into(PathBuffer const& root) {
    fixy::TestRunnerCtx const ctx{foundation::effects::testing::test()};
    crucible::Cipher cipher = open_cipher(root.c_text().view());
    if (!cipher.is_open()) return false;
    const auto view = cipher.mint_open_view(ctx);
    fixy::BumpArena arena{1 << 16};
    const fixy::View<crucible::TraceEntry> ops =
        arena.array<crucible::TraceEntry>(ctx.cap().alloc, 1, crucible::TraceEntry{});
    ops.get(0).expect("the region holds one op")->schema_hash = crucible::SchemaHash{0x5EC0};
    const crucible::ContentHash stored = cipher.store(
        ctx, view, crucible::Cipher::content_addressed(crucible::make_region(ctx.cap().alloc, arena, ops)));
    const fixy::HeapArray<crucible::Cipher::SessionEvent> events = fixy::mint_heap_array(
        ctx.cap().alloc, 1,
        crucible::Cipher::SessionEvent::cipher_event(fixy::session::SessionOp::StoreCommitted, fixy::session::StepId{1},
                                                     fixy::session::StateHash{0x5E55}, 1));
    const crucible::ContentHash persisted = cipher.persist_session_events(ctx, view, ctx.cap().alloc, events.view());
    return static_cast<bool>(stored) && static_cast<bool>(persisted);
}

}  // namespace

int main() {
    crucible::test::ScratchDir const scratch{"crucible-cipher-sync"};
    if (!scratch.is_ready()) {
        fixy::report(fixy::Sink::Err, "[skipped] cannot make a scratch directory under /tmp\n");
        return 0;
    }
    PathBuffer root{};
    root.append(text_of(scratch)).expect("the root fits its buffer");
    root.append(fixy::TextView{"/store"}).expect("the root fits its buffer");

    auto body = [&root]() { return store_into(root); };
    const int traced = trace_child(body, g_trace_log);
    if (traced == kTraceRefused) {
        fixy::report(fixy::Sink::Err, "[skipped] the host does not let the test trace a child\n");
        return 0;
    }
    if (traced != 0 || g_trace_log.is_full) {
        fixy::report(fixy::Sink::Err, "the traced store failed, or its trace did not fit the log\n");
        return 1;
    }
    // The open makes the root and objects, the store makes the directory of
    // the object and renames the object, and the persist makes two
    // directories and renames the batch.
    if (change_count(g_trace_log) < 7) {
        fixy::report(fixy::Sink::Err, "the trace holds {} changes of a directory, and the store makes at least 7\n",
                     change_count(g_trace_log));
        print_trace(g_trace_log);
        return 1;
    }
    const int unsynced = first_unsynced_change(g_trace_log);
    if (unsynced >= 0) {
        fixy::report(fixy::Sink::Err, "a change of a directory has no later sync of that directory:\n");
        print_trace(g_trace_log);
        return 1;
    }
    return 0;
}
