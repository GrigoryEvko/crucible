// safety/OwnedFile.h — RAII wrapper for std::FILE* stdio handles.
// FIXY-V-032 substrate (also covers backlog FIXY-V-230 / Agent 9 §4.2).
//
// ── Why ────────────────────────────────────────────────────────────
//
// Crucible's L0 axioms (MemSafe, LeakSafe) demand RAII closure for
// every owned resource.  `std::FILE*` from `std::fopen()` is a raw C
// stdio handle that LEAKS unless `std::fclose()` is called.  The
// pattern in TraceLoader.h (9 fclose sites, each on a different early-
// return error path) is the canonical RAII anti-pattern — any new
// early-return added without an accompanying fclose silently leaks.
//
// `handles/FileHandle.h` solves the SAME problem for POSIX file
// descriptors (`int fd`).  Stdio's `std::FILE*` is a parallel tier
// with its own buffering semantics; OwnedFile is the lockstep RAII
// wrapper.
//
// ── Discipline ─────────────────────────────────────────────────────
//
//   * `OwnedFile`  — owns the FILE* exclusively, std::fclose() on dtor.
//   * Copy ctor/op — DELETED with reason string; copying a FILE*
//                    would double-close.
//   * Move ctor/op — exchange-into nullptr; moved-from is a no-op dtor.
//   * `get()`      — borrow the raw FILE* for stdio API calls without
//                    transferring ownership.
//   * `release()`  — yield ownership (returns FILE*, leaves *this in
//                    no-op-dtor state); for handing the handle to a C
//                    API that will close it later.
//   * `close_explicit()` — explicit close returning errno on failure;
//                    the dtor ignores fclose errors so callers who
//                    care about flush failure (e.g. fdatasync barrier
//                    for durability) call this and inspect the code.
//
// `sizeof(OwnedFile) == sizeof(FILE*)` under -O3: zero runtime cost.
//
// ── Axioms ─────────────────────────────────────────────────────────
//
//   InitSafe   — default ctor sentinel-init's pointer to nullptr.
//   TypeSafe   — distinct from FILE* (must call .get() to hand off).
//   NullSafe   — is_open() / explicit operator bool() at the boundary.
//   MemSafe    — RAII closes on every exit path; copy = delete.
//   BorrowSafe — moved-from is nullptr; get() is borrow-only.
//   ThreadSafe — pointer ops are not atomic; the handle is owned by
//                whatever thread last moved-into it (linear discipline).
//   LeakSafe   — dtor always closes if non-null; moved-from is null.
//   DetSafe    — fclose is byte-deterministic on the buffer state.

#pragma once

#include <crucible/Platform.h>
#include <crucible/safety/Pre.h>

#include <cerrno>
#include <cstdio>
#include <utility>

namespace crucible::safety {

class [[nodiscard]] OwnedFile {
    std::FILE* fp_ = nullptr;

public:
    OwnedFile() noexcept = default;

    // Take ownership of an already-fopen'd FILE*.  Nullptr is the
    // sentinel; the ctor accepts it (callers that fopen() and ignore
    // failure pass the resulting nullptr in and let is_open() report).
    explicit OwnedFile(std::FILE* fp) noexcept : fp_{fp} {}

    ~OwnedFile() noexcept {
        // Dtor swallows fclose errors — see close_explicit() for the
        // diagnostic variant.  Standard practice: errors during dtor
        // are unactionable (already on the cleanup path).
        if (fp_ != nullptr) { (void)std::fclose(fp_); }
    }

    OwnedFile(const OwnedFile&)            = delete("FILE* is unique; copy would double-close on destruction");
    OwnedFile& operator=(const OwnedFile&) = delete("FILE* is unique; copy would double-close on destruction");

    OwnedFile(OwnedFile&& other) noexcept
        : fp_{std::exchange(other.fp_, nullptr)} {}

    OwnedFile& operator=(OwnedFile&& other) noexcept {
        if (this != &other) {
            if (fp_ != nullptr) (void)std::fclose(fp_);
            fp_ = std::exchange(other.fp_, nullptr);
        }
        return *this;
    }

    // ── Observation surface ────────────────────────────────────────
    [[nodiscard]] bool       is_open() const noexcept { return fp_ != nullptr; }
    explicit operator        bool()    const noexcept { return fp_ != nullptr; }

    // Borrow the raw FILE* — for handing into stdio API calls
    // (fread / fwrite / fseek / ftell / feof / ferror).  Caller must
    // not invoke fclose() on the returned pointer; ownership stays
    // here.  Returns nullptr if the OwnedFile is closed/empty.
    [[nodiscard]] std::FILE* get() const noexcept { return fp_; }

    // Yield ownership — caller becomes responsible for fclose().
    // Returns the raw FILE* and leaves *this in the no-op-dtor state.
    [[nodiscard]] std::FILE* release() noexcept {
        return std::exchange(fp_, nullptr);
    }

    // Explicit close returning errno on failure.  The dtor swallows
    // errors; callers who need to know whether the buffer flushed
    // (e.g. before fsyncing the underlying fd for durability) call
    // this and inspect the return code.  Returns 0 on success or if
    // already closed; non-zero errno on fclose failure.
    [[nodiscard]] int close_explicit() noexcept {
        std::FILE* fp = std::exchange(fp_, nullptr);
        if (fp == nullptr) return 0;
        return std::fclose(fp) == 0 ? 0 : errno;
    }
};

static_assert(sizeof(OwnedFile) == sizeof(std::FILE*),
              "OwnedFile must be a zero-cost FILE* wrapper");

}  // namespace crucible::safety
