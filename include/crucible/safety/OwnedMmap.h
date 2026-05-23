// safety/OwnedMmap.h — RAII wrapper for mmap'd regions.
// FIXY-V-231 substrate (also covers backlog Agent 9 §4.3 + #138 Owned*).
//
// ── Why ────────────────────────────────────────────────────────────
//
// Crucible's L0 axioms (MemSafe, LeakSafe) demand RAII closure for
// every owned resource.  A raw `(void* addr, size_t len)` pair from
// `::mmap()` LEAKS unless `::munmap(addr, len)` is called — and the
// pattern in `src/perf/{SenseHub,PmuSample,SchedSwitch,SchedTpBtf,
// SyscallLatency,SyscallTpBtf,LockContention}.cpp` (7 hubs × paired
// mmap/munmap calls) is the canonical RAII anti-pattern: any new
// early-return added without an accompanying munmap silently leaks
// virtual address space + page-cache backing.
//
// `safety/OwnedFile.h` solves the SAME problem for `std::FILE*`;
// `safety/FileHandle.h` for POSIX `int fd`.  mmap regions are a
// parallel tier with their own kernel-state semantics (private vs
// shared, file-backed vs anonymous, hugepage vs base page).  This
// header is the lockstep RAII wrapper, paralleling the FileHandle
// pattern.
//
// V-225's `fixy/Mmap.h` shipped an inline copy of this type — the
// substrate alias at `crucible::fixy::mmap::OwnedMmap<...>` now
// re-exports the safety-tree definition so consumers OUTSIDE the
// fixy syscall surface (e.g. V-236's perf-hub refactor) can reach
// the RAII discipline without pulling the fixy/Mmap.h tree.
//
// ── Phantom parameters ─────────────────────────────────────────────
//
// `OwnedMmap<Tag, Prot, Share>` carries three phantom-typed template
// parameters.  The safety-tree wrapper itself NEVER interprets Prot
// or Share — they are pure type-level metadata for downstream
// consumers to read:
//
//   * Tag    — per-call-site identity; forces distinct mmap regions
//              to live in distinct types so a `TraceRing` mapping
//              cannot accidentally substitute for a `MetaLog`
//              mapping at any function-call boundary.  Recommended
//              shape: empty `struct TagName {};` per region.
//   * Prot   — protection-bit tier (`prot::ReadOnly` / `WriteCopy` /
//              `ReadWrite` / `Exec`).  Read by fixy::mmap's W^X gate
//              when verifying `prot::Exec` requires `trusted_jit`.
//   * Share  — primary mode (`share::Private` / `Shared` / `Anonymous`)
//              and/or additive flag.  Read by fixy::mmap's anon-mint
//              gate and by FOUND-G47 residency-tier consumers.
//
// Per FOUND-G47 (ResidencyHeat lattice augmented with mmap-specific
// tiers: anonymous / file-backed / hugepage), the (Prot, Share)
// pair encodes WHERE the region lives — consumers can statically
// check residency match without parsing strings.
//
// ── Discipline ─────────────────────────────────────────────────────
//
//   * `OwnedMmap` — owns the (addr, len) pair exclusively; the dtor
//                   calls `::munmap(addr_, len_)` iff `is_mapped()`.
//   * Copy ctor/op — DELETED with reason string; copying would alias
//                    the underlying VA region and trigger double-
//                    munmap on destruction (the inverse of double-
//                    fclose / double-free).
//   * Move ctor/op — exchange-into `MAP_FAILED` sentinel; moved-from
//                    is a no-op dtor.
//   * `data()`     — borrow the raw `void*` for kernel API hand-off
//                    or pointer-arithmetic on the region.  Caller
//                    must not invoke `::munmap` on the returned
//                    pointer; ownership stays here.
//   * `size()`     — region length in bytes (the value passed to
//                    `::mmap()` at construction; munmap uses the
//                    same value).
//   * `is_mapped()`— gate on every `data()` / `size()` consumer;
//                    distinguishes a live region from MAP_FAILED.
//   * `release()`  — yield ownership; returns the `(void*, size_t)`
//                    pair as `std::pair` and leaves *this in the
//                    no-op-dtor state.  For handing the region to a
//                    kernel-owned subsystem (e.g. perf_event_open
//                    ringbuf transferred to the kernel) that will
//                    close it on its own schedule.  Rare; almost
//                    every owner-transfer uses move-construction.
//
// `sizeof(OwnedMmap<...>) == sizeof(void*) + sizeof(size_t)` under
// -O3: zero runtime cost beyond the raw {addr, len} payload.
//
// ── Axioms ─────────────────────────────────────────────────────────
//
//   InitSafe   — default ctor sentinel-init's addr_ to MAP_FAILED and
//                len_ to 0; both fields have NSDMI.
//   TypeSafe   — distinct from raw void* (must call .data()); per-Tag
//                identity means two distinct OwnedMmap<TagA, ...> and
//                OwnedMmap<TagB, ...> are unrelated types even with
//                identical (Prot, Share).
//   NullSafe   — is_mapped() gate before every dereference; explicit
//                MAP_FAILED sentinel; .data() returns MAP_FAILED on
//                moved-from instances (caller responsibility to
//                check is_mapped() first).
//   MemSafe    — RAII unmaps on every exit path; copy = delete with
//                reason; move clears the source so dtor is a no-op.
//   BorrowSafe — moved-from is sentinel; .data() is borrow-only; no
//                shared ownership of the underlying VA region.
//   ThreadSafe — pointer ops are not atomic; the region is owned by
//                whatever thread last moved-into it (linear
//                discipline).  munmap itself is thread-safe per
//                POSIX.
//   LeakSafe   — dtor always unmaps if is_mapped(); moved-from is
//                sentinel and skips the syscall.
//   DetSafe    — munmap is byte-deterministic on the VA range; the
//                mmap address itself is kernel-ASLR-dependent
//                (intentionally non-deterministic for security —
//                no replay path observes the address bits).

#pragma once

#include <crucible/Platform.h>

#include <sys/mman.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── OwnedMmap<Tag, Prot, Share> — Linear RAII region ─────────────────
//
// Move-only RAII over an mmap'd region.  Destructor calls
// `::munmap(addr_, len_)` iff `is_mapped()` (the addr_ != MAP_FAILED
// && addr_ != nullptr predicate).  Move semantics swap the carrier to
// `MAP_FAILED` so the moved-from instance no longer claims the region.
//
// Tag, Prot, Share are phantom-typed template parameters that the
// safety wrapper itself never interprets — downstream consumers
// (fixy::mmap concept gates, FOUND-G47 residency consumers) read
// them for type-level discrimination.

template <typename Tag, typename Prot, typename Share>
class [[nodiscard]] OwnedMmap {
    void*       addr_ = MAP_FAILED;
    std::size_t len_  = 0;

public:
    using tag_type   = Tag;
    using prot_type  = Prot;
    using share_type = Share;

    OwnedMmap() noexcept = default;

    // Take ownership of an already-mmap'd (addr, length) pair.
    // MAP_FAILED is the sentinel; ctor accepts it (callers that
    // ::mmap() and ignore failure pass the result in and let
    // is_mapped() report).  Length must be the SAME length passed to
    // ::mmap(); munmap uses it verbatim.
    explicit OwnedMmap(void* address, std::size_t length) noexcept
        : addr_{address}, len_{length} {}

    OwnedMmap(const OwnedMmap&)            = delete("mmap region is unique; copy would double-unmap on destruction");
    OwnedMmap& operator=(const OwnedMmap&) = delete("mmap region is unique; copy would double-unmap on destruction");

    OwnedMmap(OwnedMmap&& other) noexcept
        : addr_{std::exchange(other.addr_, MAP_FAILED)},
          len_ {std::exchange(other.len_, 0)} {}

    OwnedMmap& operator=(OwnedMmap&& other) noexcept {
        if (this != &other) {
            release_();
            addr_ = std::exchange(other.addr_, MAP_FAILED);
            len_  = std::exchange(other.len_, 0);
        }
        return *this;
    }

    ~OwnedMmap() noexcept { release_(); }

    // ── Observation surface ────────────────────────────────────────
    [[nodiscard]] void*       data()      const noexcept { return addr_; }
    [[nodiscard]] std::size_t size()      const noexcept { return len_; }
    [[nodiscard]] bool        is_mapped() const noexcept {
        return addr_ != MAP_FAILED && addr_ != nullptr;
    }

    // Yield ownership — caller becomes responsible for ::munmap.
    // Returns the {addr, length} pair and leaves *this in the
    // no-op-dtor state.  For handing the region to a kernel-owned
    // subsystem (e.g. perf_event_open ringbuf transferred to the
    // kernel) that will close it on its own schedule.  Rare; almost
    // every owner-transfer uses move-construction instead.
    [[nodiscard]] std::pair<void*, std::size_t> release() noexcept {
        return {std::exchange(addr_, MAP_FAILED),
                std::exchange(len_,  0)};
    }

private:
    void release_() noexcept {
        if (is_mapped()) {
            ::munmap(addr_, len_);
            addr_ = MAP_FAILED;
            len_  = 0;
        }
    }
};

// ── Layout discipline ────────────────────────────────────────────────
//
// `sizeof(OwnedMmap<...>) == sizeof(void*) + sizeof(size_t)` — the
// wrapper carries exactly the {addr, len} pair, no hidden bookkeeping.
// Anchored at one instantiation; the alignment of the pair on Linux
// x86_64 / aarch64 is 8, so no implicit padding.

namespace self_test {
struct DummyTag   {};
struct DummyProt  {};
struct DummyShare {};
using SmokeOwnedMmap = OwnedMmap<DummyTag, DummyProt, DummyShare>;

static_assert(!std::is_copy_constructible_v<SmokeOwnedMmap>,
              "OwnedMmap must be move-only — copy would double-unmap");
static_assert(!std::is_copy_assignable_v<SmokeOwnedMmap>);
static_assert(std::is_nothrow_move_constructible_v<SmokeOwnedMmap>);
static_assert(std::is_nothrow_move_assignable_v<SmokeOwnedMmap>);
static_assert(std::is_nothrow_default_constructible_v<SmokeOwnedMmap>);
static_assert(std::is_nothrow_destructible_v<SmokeOwnedMmap>);
static_assert(sizeof(SmokeOwnedMmap) == sizeof(void*) + sizeof(std::size_t),
              "OwnedMmap is exactly {addr, len} — no hidden padding");
}  // namespace self_test

}  // namespace crucible::safety
