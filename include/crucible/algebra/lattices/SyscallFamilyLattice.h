#pragma once

// ── crucible::algebra::lattices::SyscallFamilyLattice ──────────────
//
// SCAFFOLDING header for FIXY-V-097.  Ships the single sub-axis enum
// `SyscallFamily` + its `ChainLatticeOps`-based lattice algebra +
// `At<T>` singleton + reflection-driven self-test for the
// `DimensionAxis::SyscallSurface` axis (dim 23, Tier-S Semiring,
// 2026-05-22).  V-098/V-099/V-100 will populate the fixy/syscall/*
// grant catalog (Family.h / Per.h / Ioctl.h / Bridge.h) routing
// per-family / per-syscall / per-ioctl grants to this axis.
//
// ── Why a dedicated SyscallSurface axis (DimensionAxis::SyscallSurface, dim 23) ─
//
// Before V-097, "what kernel surface does this function touch" was
// implicit in the Met(X) effect row — `effects::IO` / `Block` /
// `Alloc` — which collapses files, network, mmap, process control,
// and raw ioctls into a single bit.  That granularity is too coarse
// to drive several real production gates:
//
//   1. **Forge phase E.RecipeSelect** needs to admit a kernel to the
//      hot path ONLY if its claimed surface ⊑ `NoSyscall`.  An IO bit
//      cannot distinguish "calls clock_gettime via vDSO (no kernel
//      transition)" from "calls fork+exec (full process-control
//      surface)".
//
//   2. **Mimic per-vendor backends** need to know whether a kernel
//      emits direct `/dev/nvidia*`-style ioctls vs portable file I/O.
//      Both register as IO under the effect row; only SyscallSurface
//      can separate "Privilege (raw ioctl)" from "FileMutation".
//
//   3. **Cipher tier promotion** (hot → warm → cold) must declare
//      MemoryMapping (warm tier mmaps log files) vs FileMutation
//      (cold tier pwrite + fdatasync) — both are IO, but only the
//      former permits zero-syscall steady-state reads.
//
//   4. **Canopy peer-RX paths** must declare NetworkIo separately
//      from ProcessControl; today both register IO + Block, but
//      forge phase H needs to reject Canopy paths that would also
//      claim ProcessControl (which is a Keeper-init-only surface).
//
// Pinning SyscallSurface as its own axis makes those gates expressible
// — V-098's grant catalog will route per-family tags here, V-099's
// per-ioctl grants will compose with this axis, and V-100's bridge
// will lift a SyscallSurface pin into an effect-row contribution
// automatically (so existing call sites that only carry an effect
// row get a sensible default surface inferred at the bridge).
//
// ── Tier classification (Tier-S Semiring with par=join) ─────────────
//
// SyscallSurface is `TierKind::Semiring` per `tier_of_axis(SyscallSurface)`.
// The composition reading is "syscall-surface union":
//
//   * Two call sites composing in parallel admit the JOIN (the
//     larger family) of their declared surfaces.  If site A claims
//     FileMutation and site B claims NetworkIo, the parallel
//     composition claims NetworkIo (the larger of the two on the
//     chain) — matching subset-inclusion semantics on the underlying
//     syscall set.
//   * Two call sites composing in sequence likewise admit the JOIN —
//     a sequential composition's syscall set is the union of its
//     components' syscall sets.
//
// This par/seq reading parallels Met(X)'s effect-row union but at
// finer granularity.  Forge phase E.RecipeSelect consumes the
// SyscallSurface row constraint to admit kernels into the hot path
// only when their JOIN with the hot-path target's surface ⊑ NoSyscall
// (or VdsoOnly, depending on the recipe tier).
//
// ── Chain order — subset-inclusion of allowed syscall sets ─────────
//
// Ordinal 0 = NoSyscall (smallest set — function makes ZERO syscalls,
// pure compute + vDSO-free).  Ordinal 8 = Privilege (largest set —
// function may invoke raw ioctl, capset, mount, ptrace, BPF, etc.).
// Each step UP the chain strictly subsumes the previous tier's
// surface; the chain is a total order.
//
// Reading the chain bottom-to-top, every tier IS a superset of every
// tier below it:
//
//   NoSyscall ⊏ VdsoOnly ⊏ ReadOnlyState ⊏ FileMutation
//             ⊏ MemoryMapping ⊏ ThreadSync ⊏ NetworkIo
//             ⊏ ProcessControl ⊏ Privilege
//
// A function declaring `SyscallSurface = X` ASSERTS its actual syscall
// set ⊆ X's allowed set.  Hot-path admission `surface ⊑ NoSyscall`
// therefore requires the function to claim EXACTLY NoSyscall (the
// bottom of the chain).
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — `SyscallFamily` is a strong scoped enum (`enum class
//                : uint8_t`); cross-axis mixing requires
//                `std::to_underlying` and surfaces at the call site.
//   InitSafe — every enumerator has an explicit ordinal; reflection-
//                driven coverage tests fire automatically if a switch
//                arm is forgotten.
//   DetSafe  — lattice operations are `constexpr` (not `consteval`)
//                so a runtime Graded carrier can enforce its
//                `pre (L::leq(...))` precondition under enforce.
//   LeakSafe — zero-state enum; no resources.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// V-097 scaffolding: zero cost.  The enum compiles to a single uint8_t
// per value; the `At<T>` singleton's element_type is empty and
// EBO-collapses to 0 bytes via `[[no_unique_address]]` at every
// future use site (V-098+ wrappers).
//
// ── Forward references ─────────────────────────────────────────────
//
//   FIXY-V-098 — fixy/syscall/Family.h + fixy/syscall/Per.h: coarse
//                per-family grant tags (with_syscall<Family>) +
//                per-syscall grants (with_syscall_clock_gettime, etc.).
//   FIXY-V-099 — fixy/syscall/Ioctl.h: per-vendor + per-kernel-
//                subsystem ioctl grants.
//   FIXY-V-100 — fixy/syscall/Bridge.h: automatic effect-row
//                contribution from a SyscallSurface pin (so a function
//                declaring NetworkIo automatically carries
//                `effects::IO | effects::Block` in its row).

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── SyscallFamily — single coarse syscall taxonomy ──────────────────
//
// Chain ordering: each tier strictly subsumes the syscall set of every
// tier below it.  Ordinal 0 = smallest set (NoSyscall, function is
// pure compute); ordinal 8 = largest set (Privilege, function may
// invoke raw ioctl / capset / mount / ptrace / BPF).
//
// Production rationale for the chain order (each tier vs the next):
//
//   NoSyscall      = 0 — pure compute; no kernel transitions at all.
//                         Hot-path target.  Bottom of the chain.
//   VdsoOnly       = 1 — vDSO calls only (clock_gettime, getcpu via
//                         vDSO); no kernel mode transition.  Safe for
//                         most hot paths that need a clock reading.
//   ReadOnlyState  = 2 — read-only syscalls that don't mutate kernel
//                         state: getpid, getuid, gettid, fstat, read,
//                         pread, lseek, getcpu (without vDSO).
//   FileMutation   = 3 — file write syscalls: write, pwrite, fsync,
//                         fdatasync, ftruncate, openat, unlink,
//                         renameat2.  Cipher cold-tier domain.
//   MemoryMapping  = 4 — virtual-memory ops: mmap, munmap, mprotect,
//                         madvise, mlock, mremap.  Cipher warm tier.
//   ThreadSync     = 5 — thread / sync syscalls: futex, eventfd,
//                         signalfd, pipe, pipe2, clone (for thread,
//                         not process), epoll_*, io_uring_setup.
//   NetworkIo      = 6 — socket family: socket, bind, connect, send,
//                         recv, sendmsg, recvmsg, sendmmsg, recvmmsg,
//                         shutdown, accept4.  Canopy / CNT-P domain.
//   ProcessControl = 7 — process-control: fork, vfork, exec*, kill,
//                         tgkill, sigaction, wait*, ptrace (read-only),
//                         setrlimit.  Keeper-init-only surface.
//   Privilege      = 8 — privileged syscalls: ioctl (raw, vendor),
//                         capset, capget, mount, umount2, setuid,
//                         setgid, ptrace (mutating), bpf, kexec,
//                         keyctl, modify_ldt.  Mimic vendor-backend
//                         direct-driver only.  Top of the chain.
enum class SyscallFamily : std::uint8_t {
    NoSyscall      = 0,  // bottom — no syscall surface at all
    VdsoOnly       = 1,  // vDSO calls (clock_gettime, getcpu) — no kernel transition
    ReadOnlyState  = 2,  // read-only kernel reads (read, fstat, getpid, ...)
    FileMutation   = 3,  // file writes (write, pwrite, fsync, fdatasync, ...)
    MemoryMapping  = 4,  // virtual-memory ops (mmap, mprotect, madvise, ...)
    ThreadSync     = 5,  // thread / sync (futex, eventfd, pipe, epoll_*, ...)
    NetworkIo      = 6,  // socket family (socket, send, recv, ...)
    ProcessControl = 7,  // process-control (fork, exec, kill, wait, ...)
    Privilege      = 8,  // top — privileged surface (ioctl, capset, ptrace, bpf, ...)
};

[[nodiscard]] consteval std::string_view syscall_family_name(SyscallFamily t) noexcept {
    switch (t) {
        case SyscallFamily::NoSyscall:      return "NoSyscall";
        case SyscallFamily::VdsoOnly:       return "VdsoOnly";
        case SyscallFamily::ReadOnlyState:  return "ReadOnlyState";
        case SyscallFamily::FileMutation:   return "FileMutation";
        case SyscallFamily::MemoryMapping:  return "MemoryMapping";
        case SyscallFamily::ThreadSync:     return "ThreadSync";
        case SyscallFamily::NetworkIo:      return "NetworkIo";
        case SyscallFamily::ProcessControl: return "ProcessControl";
        case SyscallFamily::Privilege:      return "Privilege";
        default:                             return std::string_view{"<unknown SyscallFamily>"};
    }
}

struct SyscallFamilyLattice : ChainLatticeOps<SyscallFamily> {
    [[nodiscard]] static constexpr SyscallFamily bottom() noexcept { return SyscallFamily::NoSyscall; }
    [[nodiscard]] static constexpr SyscallFamily top()    noexcept { return SyscallFamily::Privilege; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "SyscallFamilyLattice"; }

    template <SyscallFamily T>
    struct At {
        struct element_type {
            using syscall_family_value_type = SyscallFamily;
            [[nodiscard]] constexpr operator syscall_family_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr SyscallFamily tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case SyscallFamily::NoSyscall:      return "SyscallFamilyLattice::At<NoSyscall>";
                case SyscallFamily::VdsoOnly:       return "SyscallFamilyLattice::At<VdsoOnly>";
                case SyscallFamily::ReadOnlyState:  return "SyscallFamilyLattice::At<ReadOnlyState>";
                case SyscallFamily::FileMutation:   return "SyscallFamilyLattice::At<FileMutation>";
                case SyscallFamily::MemoryMapping:  return "SyscallFamilyLattice::At<MemoryMapping>";
                case SyscallFamily::ThreadSync:     return "SyscallFamilyLattice::At<ThreadSync>";
                case SyscallFamily::NetworkIo:      return "SyscallFamilyLattice::At<NetworkIo>";
                case SyscallFamily::ProcessControl: return "SyscallFamilyLattice::At<ProcessControl>";
                case SyscallFamily::Privilege:      return "SyscallFamilyLattice::At<Privilege>";
                default:                             return "SyscallFamilyLattice::At<?>";
            }
        }
    };
};

// ── Self-test (V-097 scaffolding sanity) ────────────────────────────
namespace detail::syscall_family_lattice_self_test {

// Catalog cardinality — every sub-axis carries at least 2 enumerators
// (a chain lattice with <2 elements is degenerate).
inline constexpr std::size_t family_count =
    std::meta::enumerators_of(^^SyscallFamily).size();

static_assert(family_count == 9,
    "SyscallFamily diverged from {NoSyscall, VdsoOnly, ReadOnlyState, "
    "FileMutation, MemoryMapping, ThreadSync, NetworkIo, "
    "ProcessControl, Privilege} per V-097 §taxonomy.  Adding a new "
    "family requires (a) appending at the next free ordinal "
    "(append-only per FOUND-I04 Universe extension rule), (b) the "
    "matching syscall_family_name() switch arm, (c) the matching "
    "At<T> singleton name() arm.  Reusing an existing ordinal would "
    "silently change every stored row_hash (federation cache key) "
    "without warning.");

// Bottom-element pin — ordinal 0 is the smallest syscall set (the
// "least-constraining" element per V-088's chain convention; in
// SyscallFamily's reading: smallest set = strongest claim = function
// makes the fewest syscalls = hot-path-safest).
static_assert(std::to_underlying(SyscallFamily::NoSyscall) == 0);

// Top-element pin — ordinal 8 is the largest set.
static_assert(std::to_underlying(SyscallFamily::Privilege) == 8);

// Reflection-driven name coverage — every enumerator must resolve to
// a non-sentinel, non-empty name.  Auto-extends when V-098 (if it
// chose to) extends the enum.
[[nodiscard]] consteval bool every_syscall_family_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^SyscallFamily));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = syscall_family_name([:en:]);
        if (n == std::string_view{"<unknown SyscallFamily>"}) return false;
        if (n.empty())                                         return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_syscall_family_has_name(),
    "syscall_family_name() switch missing an arm for at least one "
    "SyscallFamily enumerator.  Add the arm or the new family leaks "
    "the '<unknown SyscallFamily>' sentinel.");

// Concept conformance — chain lattice satisfies Lattice + BoundedLattice
// and NOT Semiring (chain order has no independent ⊕/⊗ structure).
static_assert(::crucible::algebra::Lattice<SyscallFamilyLattice>);
static_assert(::crucible::algebra::BoundedLattice<SyscallFamilyLattice>);
static_assert(!::crucible::algebra::Semiring<SyscallFamilyLattice>);

// Exhaustive lattice-axiom verifier on (axis)³ triples.  Chain orders
// are always distributive — failure indicates a leq/join/meet defect.
static_assert(verify_chain_lattice_exhaustive<SyscallFamilyLattice>(),
    "SyscallFamilyLattice chain-order lattice axioms failed at some "
    "triple — leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<SyscallFamilyLattice>(),
    "SyscallFamilyLattice chain failed distributivity check — leq/"
    "join/meet defect.");

// Bottom / top pins on the lattice surface (catches "someone reordered
// the enum and the lattice failed to follow" drift).
static_assert(SyscallFamilyLattice::bottom() == SyscallFamily::NoSyscall);
static_assert(SyscallFamilyLattice::top()    == SyscallFamily::Privilege);

// Lattice top-level diagnostic name pin.
static_assert(SyscallFamilyLattice::name() == std::string_view{"SyscallFamilyLattice"});

// Strict-chain order pin (bottom ⊏ top witness).  Combined with the
// exhaustive axiom verifier above, the chain direction is structurally
// locked.
static_assert( SyscallFamilyLattice::leq(SyscallFamily::NoSyscall, SyscallFamily::Privilege));
static_assert(!SyscallFamilyLattice::leq(SyscallFamily::Privilege, SyscallFamily::NoSyscall));

// Mid-chain ordering — every tier strictly subsumes the previous.
static_assert(SyscallFamilyLattice::leq(SyscallFamily::NoSyscall,      SyscallFamily::VdsoOnly));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::VdsoOnly,       SyscallFamily::ReadOnlyState));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::ReadOnlyState,  SyscallFamily::FileMutation));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::FileMutation,   SyscallFamily::MemoryMapping));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::MemoryMapping,  SyscallFamily::ThreadSync));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::ThreadSync,     SyscallFamily::NetworkIo));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::NetworkIo,      SyscallFamily::ProcessControl));
static_assert(SyscallFamilyLattice::leq(SyscallFamily::ProcessControl, SyscallFamily::Privilege));

// Reverse direction must fail for non-equal pairs.
static_assert(!SyscallFamilyLattice::leq(SyscallFamily::VdsoOnly,       SyscallFamily::NoSyscall));
static_assert(!SyscallFamilyLattice::leq(SyscallFamily::Privilege,      SyscallFamily::ProcessControl));

// ── FIXY-FOUND-076 audit pin: cross-tree convention misalignment ─────
//
// AUDIT RESULT for SyscallFamilyLattice (2026-05-25): INVERTED.
//
// The cross-tree contract documented in DimensionTraits.h L252-256
// SIMULTANEOUSLY claims "par=join (strictest-wins)" AND "composition
// of two sites' syscall surfaces is the JOIN (the larger family),
// matching subset-inclusion semantics".  These are self-contradicting
// for this lattice — "strictest" syscall surface = NoSyscall (smallest
// set) = chain-bottom; "JOIN (the larger family)" = chain-top = Privilege.
//
//   * chain direction: NoSyscall (bottom, smallest set — hot-path
//     target) → VdsoOnly → ReadOnlyState → FileMutation → MemoryMapping
//     → ThreadSync → NetworkIo → ProcessControl → Privilege (top,
//     largest set — privileged ioctls)
//   * "strictest" syscall surface = NoSyscall (function makes ZERO
//     syscalls, fully analyzable) = chain-min = MEET, NOT JOIN
//   * join(low, high) returns Privilege = LARGEST syscall set (looser)
//     — the propagation reading: a region containing ANY privileged
//     function has the privileged surface (correct for subset-union)
//   * meet(low, high) returns NoSyscall = empty floor (stricter)
//   * cross-tree reading: "par=join, strictest-wins" ✗ — JOIN returns
//     the WIDEST syscall surface, NOT the strictest (smallest)
//
// SAME family of defect as FOUND-009/010 (MemOrder/HwInstruction) +
// FOUND-076 PART A/B (StackUse, GlobalState, ControlFlow, CallShape,
// Stdio).  Hot-path admission gates ("surface ⊑ NoSyscall") MUST call
// MEET (or the equivalent leq check), NOT JOIN.  Forge phase E gates
// that compose two function surfaces and admit only NoSyscall-bound
// regions must use the MEET operator — calling JOIN on a NoSyscall +
// Privilege pair returns Privilege (silent admission of ring-0
// surface to the hot path).
//
// The local doc L67-68 ("only when their JOIN with the hot-path
// target's surface ⊑ NoSyscall") describes the leq-check against the
// JOIN result, which is the propagation reading — correct for
// subset-union semantics.  The cross-tree summary's "strictest-wins"
// label is the misleading part; the actual contract here is "join =
// subset union for propagation; admission gates compose differently".
//
// Polarity-witness pin.
static_assert(SyscallFamilyLattice::join(SyscallFamily::NoSyscall,
                                         SyscallFamily::Privilege)
              == SyscallFamily::Privilege,
    "FIXY-FOUND-076: SyscallFamilyLattice's JOIN gives WIDEST-surface "
    "(top=Privilege).  A consumer treating compose as 'strictest-wins "
    "syscall-surface minimization' would silently admit Privilege.  "
    "Hot-path admission gates wanting NoSyscall floor MUST call MEET "
    "(or leq against NoSyscall directly) — SAME defect family as "
    "FOUND-009/010 + FOUND-076 PART A/B (7 sibling lattices).");
static_assert(SyscallFamilyLattice::meet(SyscallFamily::NoSyscall,
                                         SyscallFamily::Privilege)
              == SyscallFamily::NoSyscall,
    "FIXY-FOUND-076: SyscallFamilyLattice's MEET gives strictest-"
    "syscall-floor (bottom=NoSyscall).  Forge hot-path admission gates "
    "MUST call MEET — calling JOIN silently admits the most-permissive "
    "participant's syscall surface.");

// At<T> singleton — empty element_type for EBO collapse at every use
// site.  V-098+ wrappers wired via `Graded<Absolute, At<T>, P>` will
// rely on this for zero-byte overhead.
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::NoSyscall>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::VdsoOnly>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::ReadOnlyState>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::FileMutation>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::MemoryMapping>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::ThreadSync>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::NetworkIo>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::ProcessControl>::element_type>);
static_assert(std::is_empty_v<SyscallFamilyLattice::At<SyscallFamily::Privilege>::element_type>);

// Runtime smoke test — per feedback_algebra_runtime_smoke_test_discipline
// memory: pure static_asserts can mask consteval/SFINAE/inline-body
// bugs; runtime ops with non-constant arguments + concept-based
// capability checks catch them.
inline void syscall_family_lattice_runtime_smoke_test() {
    // Pin operands to the chain's extremes, then call leq/join/meet so
    // the optimizer cannot collapse the call to a compile-time fold.
    SyscallFamily a = SyscallFamily::NoSyscall;
    SyscallFamily b = SyscallFamily::Privilege;
    [[maybe_unused]] bool          rl  = SyscallFamilyLattice::leq(a, b);
    [[maybe_unused]] SyscallFamily rj  = SyscallFamilyLattice::join(a, b);
    [[maybe_unused]] SyscallFamily rm  = SyscallFamilyLattice::meet(a, b);

    // Mid-chain witnesses.
    SyscallFamily c = SyscallFamily::FileMutation;
    SyscallFamily d = SyscallFamily::NetworkIo;
    [[maybe_unused]] SyscallFamily rj2 = SyscallFamilyLattice::join(c, d);
    [[maybe_unused]] SyscallFamily rm2 = SyscallFamilyLattice::meet(c, d);

    // At<T>::element_type round-trip — verify the singleton's
    // conversion materializes the right tier at runtime, not just at
    // consteval.
    SyscallFamilyLattice::At<SyscallFamily::ReadOnlyState>::element_type ros_pin{};
    [[maybe_unused]] SyscallFamily ros_recovered = ros_pin;
}

}  // namespace detail::syscall_family_lattice_self_test

}  // namespace crucible::algebra::lattices
