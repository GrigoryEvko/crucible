// ── test_fixy_handle — sentinel TU for fixy/Handle.h ───────────────
//
// Pulls fixy/Handle.h into a TU compiled under project warning flags
// so the header's 15 dual-export sentinels + cardinality witness
// execute under enforcement.  Witnesses:
//
//   1. Every fixy::handle::X alias resolves to safety::X (15 aliases —
//      Fd, FileHandle, Once, Lazy, SetOnce, OneShotFlag, PublishOnce,
//      PublishSlot, LazyEstablishedChannel, AlignedBuffer (U-016b),
//      PublishCommitCell (U-016b), open_read (U-016c),
//      open_write_truncate (U-016c), HugePageBuffer (V-034),
//      OwnedFile (V-032-audit)).
//   2. Cardinality FLOOR witness (handle_alias_cardinality >= 13)
//      trips if a future contributor REMOVES a handle alias without
//      updating both Handle.h's colocated ceiling AND this floor —
//      per FIXY-U-127 / U-128 floor-vs-ceiling discipline.  Growth
//      past 13 is silent here and auto-tracked by the header's `==`.
//   3. End-to-end RAII round-trip via the fixy:: alias proves no
//      name-shadow drift past the sentinel — exercises FileHandle
//      default-ctor + dtor, OneShotFlag signal/peek, (V-034)
//      HugePageBuffer<int>::allocate + dtor, and (V-032-audit)
//      OwnedFile move semantics + release-into-fclose through the
//      fixy::handle:: path.
//
// FIXY-U-016 (base) + U-016b (AlignedBuffer + PublishCommitCell) +
// U-016c (open_read + open_write_truncate) + V-034 (HugePageBuffer) +
// V-037 (PublishCommit pattern: structural-contract sentinels +
// read-side API round-trip + friend-gated write-side round-trip) +
// V-032-audit (OwnedFile: move-only RAII + close-on-dtor witness).
// Doc-block updated by U-131 to reflect post-U-016b/c expansion.

#include <crucible/fixy/Handle.h>

#include <atomic>       // FIXY-V-037 — std::memory_order_relaxed for cell.load
#include <bit>          // FIXY-V-034 — std::bit_cast for pointer→uintptr_t
#include <cstdint>      // FIXY-V-034 — std::uintptr_t for alignment check
#include <cstdio>       // FIXY-V-032-audit — std::fopen / FILE* / std::tmpfile
#include <type_traits>
#include <utility>

namespace safe  = ::crucible::safety;
namespace fhand = ::crucible::fixy::handle;

// ─── 1. All 9 type carriers alias the substrate ───────────────────

namespace test_fixy_handle {
struct ProbeT {};
struct ProbeProto {};
struct ProbeResource {};
}  // namespace test_fixy_handle

namespace th = test_fixy_handle;

static_assert(std::is_same_v<fhand::Fd, safe::Fd>);
static_assert(std::is_same_v<fhand::FileHandle, safe::FileHandle>);
static_assert(std::is_same_v<fhand::Once, safe::Once>);
static_assert(std::is_same_v<fhand::Lazy<th::ProbeT>, safe::Lazy<th::ProbeT>>);
static_assert(std::is_same_v<fhand::SetOnce<th::ProbeT>, safe::SetOnce<th::ProbeT>>);
static_assert(std::is_same_v<fhand::OneShotFlag, safe::OneShotFlag>);
static_assert(std::is_same_v<fhand::PublishOnce<th::ProbeT>, safe::PublishOnce<th::ProbeT>>);
static_assert(std::is_same_v<fhand::PublishSlot<th::ProbeT>, safe::PublishSlot<th::ProbeT>>);
static_assert(std::is_same_v<
    fhand::LazyEstablishedChannel<th::ProbeProto, th::ProbeResource>,
    safe::LazyEstablishedChannel<th::ProbeProto, th::ProbeResource>>);

// FIXY-U-016b — AlignedBuffer + PublishCommitCell additions.
static_assert(std::is_same_v<fhand::AlignedBuffer<th::ProbeT>,
                             safe::AlignedBuffer<th::ProbeT>>);
static_assert(std::is_same_v<
    fhand::PublishCommitCell<th::ProbeT, th::ProbeProto>,
    safe::PublishCommitCell<th::ProbeT, th::ProbeProto>>);

// FIXY-V-037 — LOAD-BEARING structural contracts mirrored at TU level
// under project warning flags.  These re-prove (cf. Handle.h's
// publish_commit_detail-mirror block) the cache-line isolation,
// Pinned channel identity, and trivial-dtor guarantees that the
// PublishCommit pattern's BorrowSafe / ThreadSafe / DetSafe axiom
// coverage rests on.  If a future contributor weakens any of these
// at the substrate without auditing the fixy:: surface, ONE of
// these sentinels reddens — at the alias path, not just at substrate.
static_assert(
    alignof(fhand::PublishCommitCell<th::ProbeT, th::ProbeProto>) == 64,
    "fixy::handle::PublishCommitCell must be alignas(64) — false sharing "
    "between fg's load_acquire and bg's bump_by release-store would "
    "regress to ~40× MESI ping-pong (CLAUDE.md §IX).");
static_assert(
    !std::is_move_constructible_v<
        fhand::PublishCommitCell<th::ProbeT, th::ProbeProto>>,
    "fixy::handle::PublishCommitCell must not be move-constructible "
    "— channel identity is the cell's atomic storage address.");
static_assert(
    !std::is_copy_constructible_v<
        fhand::PublishCommitCell<th::ProbeT, th::ProbeProto>>,
    "fixy::handle::PublishCommitCell must not be copy-constructible "
    "— two cells claiming one channel identity would break the "
    "single-writer BorrowSafe invariant.");
static_assert(
    std::is_trivially_destructible_v<
        fhand::PublishCommitCell<th::ProbeT, th::ProbeProto>>,
    "fixy::handle::PublishCommitCell must be trivially destructible "
    "— a non-trivial dtor means someone added a managed resource "
    "without updating the Pinned discipline.");

// FIXY-U-016c — open_read + open_write_truncate free-function identity.
static_assert(std::is_same_v<
    decltype(&fhand::open_read),
    decltype(&safe::open_read)>);
static_assert(std::is_same_v<
    decltype(&fhand::open_write_truncate),
    decltype(&safe::open_write_truncate)>);

// FIXY-V-034 — HugePageBuffer<T> identity + madvise-hint guarantee
// re-stated at TU level so it executes under the project warning
// flags (not just inside Handle.h's self_test:: block).
static_assert(std::is_same_v<fhand::HugePageBuffer<th::ProbeT>,
                             safe::HugePageBuffer<th::ProbeT>>);
static_assert(fhand::HugePageBuffer<th::ProbeT>::huge_page_bytes
              == ::crucible::warden::kHugePageBytes,
              "fixy::handle::HugePageBuffer<T>::huge_page_bytes must equal "
              "warden::kHugePageBytes — madvise(MADV_HUGEPAGE) requires "
              "the allocation to be aligned to the kernel huge-page boundary.");

// FIXY-V-037 — Write-side fixture for the PublishCommit pattern's
// friend-gated bump.  Declared at namespace scope so its identity is
// stable as the `WriteAuth` template argument; the cell instantiated
// with this type then sees its `static_publish_one` member as a friend
// and may call the otherwise-private `bump()` from inside.  This
// exercises the load-bearing contract that the friend relationship
// SURVIVES the fixy::handle:: alias path — a hand-rolled refactor that
// accidentally introduced a shim wrapper without `friend WriteAuth;`
// re-emission would red this fixture at link time.
namespace test_fixy_handle {
struct PubCommitWriteAuth {
    // Friend-gated single-bump helper.  Compiles ONLY because
    // PublishCommitCell<PubCommitTag, PubCommitWriteAuth> declares
    // `friend PubCommitWriteAuth;` — i.e., the type-erased re-export
    // path preserves the friend identity.
    template <typename Cell>
    static auto bump_once(Cell& cell) noexcept {
        return cell.bump();
    }
};
struct PubCommitTag {};
}  // namespace test_fixy_handle

// FIXY-V-032-audit — OwnedFile alias identity + LOAD-BEARING structural
// contracts mirrored at TU level under project warning flags.  Closes
// the fixy_clean_headers CI guard regression: TraceLoader.h (on the
// FIXY-U-096z certified registry) was reaching `::crucible::safety::
// OwnedFile` directly because V-032 added the substrate without
// surfacing it via fixy::handle::.
//
// Identity: the alias must resolve to safety::OwnedFile so that
// existing TraceLoader / warden::CpuTopology consumers continue to
// see the same RAII type after the migration.
static_assert(std::is_same_v<fhand::OwnedFile, safe::OwnedFile>);

// Structural contracts (4): copy-rejection (LeakSafe + MemSafe —
// no double-close), nothrow-move (strong-guarantee container
// compatibility — std::expected<OwnedFile, error_code>), nothrow-dtor
// (early-return safety under -fno-exceptions), and zero-overhead
// (sizeof == sizeof(FILE*) — no hidden cost at every TraceLoader
// stack frame).  These re-state the substrate contracts at the alias
// path so a future weakening that escapes the safety/ self-test
// reddens here too.
static_assert(
    !std::is_copy_constructible_v<fhand::OwnedFile>,
    "fixy::handle::OwnedFile must not be copy-constructible — copying "
    "a FILE* would double-close at destruction (LeakSafe + MemSafe).");
static_assert(
    !std::is_copy_assignable_v<fhand::OwnedFile>,
    "fixy::handle::OwnedFile must not be copy-assignable — copy-assign "
    "leaks the LHS's existing FILE* and double-closes the RHS's.");
static_assert(
    std::is_nothrow_move_constructible_v<fhand::OwnedFile>,
    "fixy::handle::OwnedFile must have a nothrow move constructor — "
    "it appears inside std::expected<>-shaped return channels that "
    "demand noexcept move for the strong exception guarantee.");
static_assert(
    std::is_nothrow_move_assignable_v<fhand::OwnedFile>,
    "fixy::handle::OwnedFile must have a nothrow move-assign for "
    "symmetric strong-guarantee handling.");
static_assert(
    std::is_nothrow_destructible_v<fhand::OwnedFile>,
    "fixy::handle::OwnedFile destructor must be noexcept — it runs on "
    "every early-return path and stdio errors must not propagate.");
static_assert(
    sizeof(fhand::OwnedFile) == sizeof(std::FILE*),
    "fixy::handle::OwnedFile must be sizeof(std::FILE*) — any inflation "
    "balloons the cost of every TraceLoader stack frame.");

// ─── 2. Cardinality FLOOR witness mirror ──────────────────────────
//
// Per FIXY-U-127 / U-128 floor-vs-ceiling split: the EXACT ceiling
// pin (`== 13`) lives in fixy/Handle.h colocated with the source-
// of-truth constant; THIS TU only holds the FLOOR pin (`>= 13`)
// which catches the inverse direction — an accidental REMOVAL of a
// fixy::handle:: alias that escaped review.  Growth past 13 is
// silent here and auto-tracked by the header's `==` ceiling.

static_assert(
    ::crucible::fixy::handle::self_test::handle_alias_cardinality >= 13,
    "floor: fixy::handle:: alias cardinality regressed below 13 — "
    "an alias was removed without updating both Handle.h's colocated "
    "ceiling pin AND this floor witness.");

// ─── 3. End-to-end RAII through the fixy:: alias ──────────────────
//
// Exercises FileHandle::default_ctor + dtor via the fixy::handle::
// alias path.  Proves no name-shadow drift — the destructor closes
// the underlying fd via the substrate's RAII discipline.

int main() {
    {
        fhand::FileHandle fh;
        // Default-constructed handle owns no fd; destructor is a no-op
        // but still travels through the substrate vtable / RAII path,
        // proving the alias is honored at construction and destruction.
        (void)fh;
    }
    {
        // OneShotFlag round-trip — signal + peek via the fixy:: alias.
        fhand::OneShotFlag flag;
        flag.signal();
        bool seen = flag.peek();
        (void)seen;
    }
    {
        // FIXY-V-034 — HugePageBuffer<int> allocate + dtor round-trip
        // via the fixy::handle:: alias.  Allocates one int rounded up
        // to a 2-MB-aligned block; aligned_alloc succeeds even when the
        // host has no hugepages configured (it's an alignment request,
        // not a hugepage-backing request — madvise(MADV_HUGEPAGE) is
        // applied later by warden::register_hot_region at the call
        // site, and silently falls back to small pages if unavailable).
        // Exercises substrate `allocate()` factory + RAII dtor's
        // std::free, proving the alias preserves both the alignment
        // contract and the ownership-transfer semantic.
        fhand::HugePageBuffer<int> buf = fhand::HugePageBuffer<int>::allocate(1);
        if (buf.data() == nullptr) return 1;            // allocate failure → fail
        if (buf.size() != 1)       return 2;            // size must match count
        if (buf.bytes() < fhand::HugePageBuffer<int>::huge_page_bytes) return 3;
        // Pointer must be huge-page-aligned — the load-bearing guarantee
        // re-verified at runtime (the static_assert above proved the
        // *constant* matches; this proves the *runtime* allocation
        // honors it).  std::bit_cast (C++26 §III rule — reinterpret_cast
        // is banned) inspects the pointer's bit-pattern as uintptr_t for
        // alignment masking.
        const auto addr = std::bit_cast<std::uintptr_t>(buf.data());
        if ((addr & (fhand::HugePageBuffer<int>::huge_page_bytes - 1)) != 0)
            return 4;                                    // alignment violated
        // Sentinel value fits in signed int (positive 32-bit half); the
        // canonical 0xDEADBEEF would trigger -Werror=sign-conversion on
        // `int buf[]`.  We just need a non-zero write-then-read witness.
        buf[0] = 0x12345678;
        if (buf[0] != 0x12345678) return 5;             // write-then-read sanity
        // dtor on scope exit returns the block via std::free.
    }
    {
        // FIXY-V-037 — PublishCommitCell round-trip via fixy::handle::.
        // Exercises the FULL public read-side API (4 methods) plus the
        // friend-gated write-side via the namespace-scope WriteAuth
        // fixture defined above.  Verifies through the alias path:
        //   (a) default-construct yields counter == 0 (atomic NSDMI)
        //   (b) all 4 read methods agree on the initial value
        //   (c) friend-gated bump returns the previous value (issued-
        //       ticket semantics) and advances the counter exactly once
        //   (d) acquire-load after bump sees the new value (intra-thread,
        //       so this proves the release/acquire happens-before chain
        //       compiles correctly through the alias)
        using Cell = fhand::PublishCommitCell<
            th::PubCommitTag, th::PubCommitWriteAuth>;
        Cell cell;
        // (a) + (b) — read API agreement at initial state.
        if (cell.load_acquire() != 0)                        return 10;
        if (cell.peek_relaxed() != 0)                        return 11;
        if (cell.get() != 0)                                 return 12;
        if (cell.load(std::memory_order_relaxed) != 0)       return 13;
        // (c) — friend-gated bump via WriteAuth.  bump() returns the
        // PREVIOUS counter value (== 0 here), then advances the cell.
        const auto previous = th::PubCommitWriteAuth::bump_once(cell);
        if (previous != 0)                                   return 14;
        // (d) — post-bump read API sees the advanced value.
        if (cell.load_acquire() != 1)                        return 15;
        if (cell.peek_relaxed() != 1)                        return 16;
        // Second bump to prove issued-ticket arithmetic, not boolean
        // flag semantics.  Previous return == 1; counter now == 2.
        if (th::PubCommitWriteAuth::bump_once(cell) != 1)    return 17;
        if (cell.load_acquire() != 2)                        return 18;
        // Cell dtor on scope exit is trivial; no resource to free.
    }
    {
        // FIXY-V-032-audit — OwnedFile RAII round-trip via fixy::handle::.
        // Witnesses the move-only ownership + close-on-dtor discipline
        // that V-032 introduced at the substrate and V-032-audit
        // surfaced at the umbrella.  std::tmpfile() creates a
        // self-deleting temporary FILE* that survives the entire stdio
        // round-trip without touching the real filesystem — perfect
        // for a deterministic test (no path collisions, no leftover
        // artifacts).
        //
        // Sequence:
        //   (a) Construct OwnedFile from std::tmpfile() — must report
        //       is_open / explicit bool == true.
        //   (b) get() returns the raw FILE* without ownership transfer.
        //   (c) Write + fflush + rewind + read round-trip via the
        //       borrowed FILE* to prove the alias preserves stdio
        //       semantics through the type.
        //   (d) Move into a second OwnedFile — source must transition
        //       to !is_open (moved-from is null-sentinel), dest takes
        //       ownership.
        //   (e) Destructor on dest closes the FILE* (verified by
        //       absence of leaks under ASan / LeakSan in the default
        //       preset; no separate runtime assertion possible — the
        //       sanitizers ARE the witness).
        std::FILE* raw = std::tmpfile();
        if (raw == nullptr) {
            // Sandbox may forbid tmpfile (no writable /tmp).  Treat as
            // skip-not-fail: the static_assert sentinels above already
            // prove the alias-path identity; the runtime witness is a
            // belt-and-suspenders RAII exercise.
            return 0;
        }
        fhand::OwnedFile a{raw};
        if (!a.is_open())               return 20;
        if (!a)                         return 21;
        if (a.get() != raw)             return 22;
        // Write-then-read round-trip via the borrowed FILE*.
        constexpr int sentinel = 0x42;
        if (std::fputc(sentinel, a.get()) != sentinel) return 23;
        if (std::fflush(a.get()) != 0)                  return 24;
        std::rewind(a.get());
        if (std::fgetc(a.get()) != sentinel)           return 25;
        // Move-into-new-owner.  Source must surrender ownership.
        fhand::OwnedFile b{std::move(a)};
        if (a.is_open())                return 26;  // moved-from is null
        if (!b.is_open())               return 27;  // dest holds the FILE*
        if (b.get() != raw)             return 28;  // ptr identity preserved
        // Dtor on b at scope exit calls std::fclose(raw); tmpfile is
        // self-deleting so no /tmp residue.  ASan / LeakSan in the
        // default preset will surface any leak.
    }
    return 0;
}
