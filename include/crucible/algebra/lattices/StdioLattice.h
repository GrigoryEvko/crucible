#pragma once

// ── crucible::algebra::lattices::StdioLattice ───────────────────────
//
// SCAFFOLDING header for FIXY-V-241 (3/3).  Ships the `Stdio` sub-axis
// enum + its `ChainLatticeOps`-based lattice + `At<T>` singleton +
// reflection-driven self-test for the `DimensionAxis::Stdio` axis (dim
// 28, Tier-S Semiring, 2026-05-23).  V-242 wraps it as a
// `safety/Stdio.h` Graded carrier; V-243 adds the CollisionCatalog
// rules; V-246 (fixy/grant/Stdio.h) routes the stdio grants here.
//
// ── Why a dedicated Stdio axis (DimensionAxis::Stdio, dim 28) ────────
//
// CLAUDE.md §XII bans stdio on the hot path ("No fprintf / std::cout /
// std::printf / std::format on hot path — format parsing ≥100 ns,
// output syscalls flush buffers, noise pollutes measurement").  That
// rule was prose-only; this axis makes it a type-level admission gate.
// Gates it drives:
//
//   1. Forge phase E.RecipeSelect hot-path admission `stdio ⊑ NoStdio`:
//      a kernel that writes to a console stream cannot run on the
//      foreground path.
//   2. The cost gradient (no-cost → format-cost → syscall-cost →
//      unbounded-block) lets Observe attribute per-call latency to the
//      declared stdio tier.
//   3. DetSafe / bench discipline: an `UnbufferedWrite` or
//      `InteractiveRead` on a measured path invalidates timing.
//
// ── Tier classification (Tier-S Semiring with par=join) ─────────────
//
// Stdio is `TierKind::Semiring` per `tier_of_axis(Stdio)`.  The
// composition reading is "stdio-surface union": two sites composing
// admit the JOIN (the more-disruptive stdio surface).
//
// ── Chain order — subset-inclusion of stdio surface / disruptiveness ─
//
//   NoStdio ⊏ BufferedWrite ⊏ UnbufferedWrite ⊏ InteractiveRead
//
// Ordinal 0 = NoStdio (no console I/O); ordinal 3 = InteractiveRead
// (blocks on user input — the most disruptive).  A function declaring
// `Stdio = X` ASSERTS its actual stdio surface ⊆ X's allowed set;
// hot-path admission `stdio ⊑ NoStdio` requires the bottom tier exactly.
// Per-tier rationale (each strictly more disruptive than the one below):
//
//   NoStdio         = 0 — performs no stdio at all.  Hot-path target;
//                          required tier for foreground code.
//   BufferedWrite   = 1 — writes to a BUFFERED stream (stdout to a pipe
//                          / full-buffered sink): format-parse cost
//                          (~100 ns+) but flush is deferred.  Cold /
//                          debug paths only.
//   UnbufferedWrite = 2 — writes to an UNBUFFERED / flushed stream
//                          (stderr, std::endl, explicit fflush): a write
//                          SYSCALL per call.  Above BufferedWrite by the
//                          forced flush transition.
//   InteractiveRead = 3 — reads stdin / blocks on interactive console
//                          input: UNBOUNDED blocking on an external
//                          actor.  Top of the chain; the most disruptive
//                          stdio surface, never permissible on any
//                          bounded-latency path.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — strong scoped enum (`enum class : uint8_t`).
//   InitSafe — explicit ordinals; reflection-driven name coverage.
//   DetSafe  — `constexpr` lattice ops.
//   LeakSafe — zero-state enum.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero.  One uint8_t; empty `At<T>` element_type EBO-collapses.
//
// ── Forward references ─────────────────────────────────────────────
//
//   FIXY-V-242 — safety/Stdio.h: Graded<Absolute, At<T>, P> carrier.
//   FIXY-V-243 — safety/CollisionCatalog.h: HotPath × Stdio rules.
//   FIXY-V-246 — fixy/grant/Stdio.h: buffered_write / unbuffered_write /
//                interactive_read grant tags.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── Stdio — console-I/O surface taxonomy ────────────────────────────
//
// Chain ordering: each tier is a strictly more disruptive stdio surface
// than the one below it.  Ordinal 0 = NoStdio; 3 = InteractiveRead.
enum class Stdio : std::uint8_t {
    NoStdio         = 0,  // bottom — no console I/O
    BufferedWrite   = 1,  // write to a buffered stream (deferred flush; format cost)
    UnbufferedWrite = 2,  // write to an unbuffered / flushed stream (syscall per call)
    InteractiveRead = 3,  // top — reads stdin / blocks on interactive input
};

[[nodiscard]] consteval std::string_view stdio_name(Stdio t) noexcept {
    switch (t) {
        case Stdio::NoStdio:         return "NoStdio";
        case Stdio::BufferedWrite:   return "BufferedWrite";
        case Stdio::UnbufferedWrite: return "UnbufferedWrite";
        case Stdio::InteractiveRead: return "InteractiveRead";
        default:                     return std::string_view{"<unknown Stdio>"};
    }
}

struct StdioLattice : ChainLatticeOps<Stdio> {
    [[nodiscard]] static constexpr Stdio bottom() noexcept { return Stdio::NoStdio; }
    [[nodiscard]] static constexpr Stdio top()    noexcept { return Stdio::InteractiveRead; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "StdioLattice"; }

    template <Stdio T>
    struct At {
        struct element_type {
            using stdio_value_type = Stdio;
            [[nodiscard]] constexpr operator stdio_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr Stdio tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case Stdio::NoStdio:         return "StdioLattice::At<NoStdio>";
                case Stdio::BufferedWrite:   return "StdioLattice::At<BufferedWrite>";
                case Stdio::UnbufferedWrite: return "StdioLattice::At<UnbufferedWrite>";
                case Stdio::InteractiveRead: return "StdioLattice::At<InteractiveRead>";
                default:                     return "StdioLattice::At<?>";
            }
        }
    };
};

// ── Self-test (V-241 scaffolding sanity) ────────────────────────────
namespace detail::stdio_lattice_self_test {

inline constexpr std::size_t stdio_count =
    std::meta::enumerators_of(^^Stdio).size();

static_assert(stdio_count == 4,
    "Stdio diverged from {NoStdio, BufferedWrite, UnbufferedWrite, "
    "InteractiveRead} per V-241 §taxonomy.  Adding a new tier requires "
    "(a) appending at the next free ordinal (append-only per FOUND-I04), "
    "(b) the matching stdio_name() arm, (c) the matching At<T> name() arm.");

static_assert(std::to_underlying(Stdio::NoStdio)         == 0);
static_assert(std::to_underlying(Stdio::InteractiveRead) == 3);
static_assert(std::is_same_v<std::underlying_type_t<Stdio>, std::uint8_t>);

[[nodiscard]] consteval bool every_stdio_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Stdio));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = stdio_name([:en:]);
        if (n == std::string_view{"<unknown Stdio>"}) return false;
        if (n.empty())                                return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_stdio_has_name(),
    "stdio_name() switch missing an arm for at least one Stdio enumerator.");

static_assert(::crucible::algebra::Lattice<StdioLattice>);
static_assert(::crucible::algebra::BoundedLattice<StdioLattice>);
static_assert(!::crucible::algebra::Semiring<StdioLattice>);

static_assert(verify_chain_lattice_exhaustive<StdioLattice>(),
    "StdioLattice chain-order lattice axioms failed — leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<StdioLattice>(),
    "StdioLattice chain failed distributivity — leq/join/meet defect.");

static_assert(StdioLattice::bottom() == Stdio::NoStdio);
static_assert(StdioLattice::top()    == Stdio::InteractiveRead);
static_assert(StdioLattice::name() == std::string_view{"StdioLattice"});

static_assert( StdioLattice::leq(Stdio::NoStdio, Stdio::InteractiveRead));
static_assert(!StdioLattice::leq(Stdio::InteractiveRead, Stdio::NoStdio));

static_assert(StdioLattice::leq(Stdio::NoStdio,         Stdio::BufferedWrite));
static_assert(StdioLattice::leq(Stdio::BufferedWrite,   Stdio::UnbufferedWrite));
static_assert(StdioLattice::leq(Stdio::UnbufferedWrite, Stdio::InteractiveRead));

static_assert(!StdioLattice::leq(Stdio::BufferedWrite,   Stdio::NoStdio));
static_assert(!StdioLattice::leq(Stdio::InteractiveRead, Stdio::UnbufferedWrite));

// par=join (more-disruptive dominates); NoStdio is the join identity.
static_assert(StdioLattice::join(Stdio::BufferedWrite, Stdio::UnbufferedWrite)
              == Stdio::UnbufferedWrite);
static_assert(StdioLattice::join(Stdio::NoStdio, Stdio::BufferedWrite)
              == Stdio::BufferedWrite);
// and=meet (less-disruptive floor).
static_assert(StdioLattice::meet(Stdio::InteractiveRead, Stdio::BufferedWrite)
              == Stdio::BufferedWrite);

static_assert(std::is_empty_v<StdioLattice::At<Stdio::NoStdio>::element_type>);
static_assert(std::is_empty_v<StdioLattice::At<Stdio::BufferedWrite>::element_type>);
static_assert(std::is_empty_v<StdioLattice::At<Stdio::UnbufferedWrite>::element_type>);
static_assert(std::is_empty_v<StdioLattice::At<Stdio::InteractiveRead>::element_type>);
static_assert(StdioLattice::At<Stdio::UnbufferedWrite>::tier == Stdio::UnbufferedWrite);

// Runtime smoke — non-constant operands.
inline void stdio_lattice_runtime_smoke_test() {
    Stdio a = Stdio::NoStdio;
    Stdio b = Stdio::InteractiveRead;
    [[maybe_unused]] bool  rl = StdioLattice::leq(a, b);
    [[maybe_unused]] Stdio rj = StdioLattice::join(a, b);
    [[maybe_unused]] Stdio rm = StdioLattice::meet(a, b);

    Stdio c = Stdio::BufferedWrite;
    Stdio d = Stdio::UnbufferedWrite;
    [[maybe_unused]] Stdio rj2 = StdioLattice::join(c, d);
    [[maybe_unused]] Stdio rm2 = StdioLattice::meet(c, d);

    StdioLattice::At<Stdio::BufferedWrite>::element_type bw_pin{};
    [[maybe_unused]] Stdio bw_recovered = bw_pin;
}

}  // namespace detail::stdio_lattice_self_test

}  // namespace crucible::algebra::lattices
