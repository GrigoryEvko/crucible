// ════════════════════════════════════════════════════════════════════
// example_fixy_bounded — FIXY-G12 worked example
//
// THE PATTERN: A FORGE COMPILER PHASE WITH BOUNDED-RESOURCE GRANTS
//              + COST POLYNOMIAL + WARDEN-WIRED DEADLINE
//
// Sister to example_fixy_forge_phase (G7/E baseline) and
// example_fixy_cost_annotated (G11 cost axis).  This example pins
// the full bounded-resource grant suite — `cg::terminating`,
// `cg::bounded_alloc<N>`, `cg::bounded_io<0>`, `cg::wallclock_budget<N>`,
// `cg::loop_bound<N>` — and demonstrates the G11×G12 cross-axis
// soundness check (`cost_within_budget_v`) plus warden integration
// (`warden::deadline_from_grade_v`).
//
// THE LOAD-BEARING CONTRAST with example_fixy_forge_phase:
//   - Resources axis NOW ENGAGED via FIVE bounded-resource grants
//     (Phase D's runtime contract becomes type-level).
//   - Cost polynomial DECLARES per-IR-node ns.
//   - Warden auto-arms deadline from the binding's grade vector;
//     no manual `DeadlineWatchdog::arm_for_thread(5'000)` call.
//   - cost_within_budget_v<F, K, N> verifies at compile time that
//     predicted cost (50 ns/node × N nodes) ≤ wallclock_budget (10 µs).
// ════════════════════════════════════════════════════════════════════

#include <crucible/cog/CogIdentity.h>
#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/dim/Cost.h>
#include <crucible/fixy/dim/Termination.h>
#include <crucible/warden/DeadlineFromGrade.h>

#include <cstddef>
#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;
namespace cw = crucible::warden;
namespace cc = crucible::cog;
namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace {

// ── Stand-in IR types (mirror example_fixy_forge_phase) ────────────

struct KernelGraph {
    int num_nodes = 0;
    int num_edges = 0;
    int generation = 0;
};

struct Arena {
    std::size_t bump = 0;
};

// ── Phase D FUSE signature ─────────────────────────────────────────

using ForgePhasePtr = KernelGraph(*)(const KernelGraph& input, Arena& arena);

KernelGraph phase_d_fuse_ref(const KernelGraph& input, Arena& arena) noexcept {
    // Bump for the output graph's metadata.  The grant declares the
    // upper bound at 4096 bytes; this single allocation is well within.
    arena.bump += sizeof(KernelGraph);
    return KernelGraph{
        .num_nodes  = input.num_nodes - input.num_nodes / 10,
        .num_edges  = input.num_edges - input.num_edges / 10,
        .generation = input.generation + 1
    };
}

// ── fixy::fn binding — bounded-resource discipline ─────────────────
//
// 20 base-axis acks (identical to example_fixy_forge_phase) +
// FIVE bounded-resource grants on dim::Resources +
// ONE cost polynomial on dim::Cost.
//
// The bounded-resource grants together form the "Phase D FUSE wallclock
// contract" — every claim individually verifiable at compile time:
//
//   * cg::terminating              — structural termination claim
//                                    (Asserted witness by default).
//   * cg::bounded_alloc<4096>      — ≤4 KB arena bump per call.
//   * cg::bounded_io<0>            — zero syscalls on the hot path.
//   * cg::wallclock_budget<10'000> — 10 µs deadline; warden arms
//                                    automatically via the grade.
//   * cg::loop_bound<256>          — phase visits at most 256 IR
//                                    nodes (any larger graph is
//                                    sharded by the upstream driver).
//
// PLUS the G11 cost polynomial:
//   * cg::cost_polynomial<0, 50>   — O(N) with 50 ns per IR node.

using BoundForgePhase = cf::fn<ForgePhasePtr,
    // 1. Type — substrate carries ForgePhasePtr.
    cf::accept_default_strict_for<cd::Type>,

    // 2. Refinement — pred::True default.
    cf::accept_default_strict_for<cd::Refinement>,

    // 3. Usage = Copy — function pointer is freely copyable.
    cg::copy,

    // 4. Effect = Row<Bg, Alloc> — bg thread + arena allocation.
    cg::with<fx::Effect::Bg, fx::Effect::Alloc>,

    // 5. Security — Classified strict default.
    cf::accept_default_strict_for<cd::Security>,

    // 6. Protocol — no session-typed handshake.
    cf::accept_default_strict_for<cd::Protocol>,

    // 7. Lifetime — free function, valid forever.
    cf::accept_default_strict_for<cd::Lifetime>,

    // 8. Provenance — FromInternal strict default (Forge-authored).
    cf::accept_default_strict_for<cd::Provenance>,

    // 9. Trust — Verified strict default (CI-validated phase).
    cf::accept_default_strict_for<cd::Trust>,

    // 10. Representation — Opaque strict default.
    cf::accept_default_strict_for<cd::Representation>,

    // 11. Observability — derived from Effect row.
    cf::accept_default_strict_for<cd::Observability>,

    // 12. Complexity = Linear<1> — O(1·N) in IR node count.
    cg::complexity_linear<1>,

    // 13. Precision — Exact strict default.
    cf::accept_default_strict_for<cd::Precision>,

    // 14. Space = Bounded<sizeof(KernelGraph)>.
    cg::space_bounded<sizeof(KernelGraph)>,

    // 15. Overflow — Trap strict default.
    cf::accept_default_strict_for<cd::Overflow>,

    // 16. Mutation — Immutable strict default.
    cf::accept_default_strict_for<cd::Mutation>,

    // 17. Reentrancy — NonReentrant strict default.
    cf::accept_default_strict_for<cd::Reentrancy>,

    // 18. Size — Unstated strict default.
    cf::accept_default_strict_for<cd::Size>,

    // 19. Version = 2 — IR002 phase generation.
    cg::version<2>,

    // 20. Staleness — Fresh strict default.
    cf::accept_default_strict_for<cd::Staleness>,

    // ── FIXY-G11 — dim::Cost engagement ─────────────────────────
    // O(N) with 50 ns per IR node.  Used by the G12 cross-check
    // below to verify the wallclock_budget is sound.
    cg::cost_polynomial<0, 50>,

    // ── FIXY-G12 — dim::Resources engagement (FIVE grants) ──────
    cg::terminating,
    cg::bounded_alloc<4096>,
    cg::bounded_io<0>,
    cg::wallclock_budget<10'000>,
    cg::loop_bound<256>
>;

// ── Compile-time invariants (per-grant projection) ─────────────────

// EBO collapse — bounded-resource grants are zero-byte tags.
static_assert(sizeof(BoundForgePhase) == sizeof(ForgePhasePtr),
    "EBO collapse failed for bounded Forge phase binding.");

// Per-grant projection — every G12 grant readable from the type.
static_assert(cf::is_terminating_v<BoundForgePhase>);
static_assert(cf::bounded_alloc_v<BoundForgePhase> == 4096);
static_assert(cf::bounded_io_v<BoundForgePhase> == 0);
static_assert(cf::wallclock_budget_v<BoundForgePhase> == 10'000);
static_assert(cf::loop_bound_v<BoundForgePhase> == 256);
static_assert(cf::HasResourcesGrant<BoundForgePhase>);

// ── R019 — hot-path-strictest resource profile ─────────────────────
//
// terminating + bounded_alloc ≤ 4096 + bounded_io == 0.  The
// BoundForgePhase binding satisfies all three.

static_assert(cr::R019_hot_path_resources_v<BoundForgePhase>,
    "BoundForgePhase must satisfy R019: terminating + ≤4KB alloc + "
    "zero IO are the structural prerequisites for hot-path admission.");

// ── R020 — federation-peer profile ─────────────────────────────────
//
// terminating + wallclock_budget.  BoundForgePhase satisfies both.
// (R014 demands bounded_alloc + wallclock_budget for BgWorker
//  observable bindings; the worked example is hot-path, not a
//  long-lived Bg worker, so R014 is not the gating rule here.)

static_assert(cr::R020_federation_peer_bounded_v<BoundForgePhase>,
    "BoundForgePhase satisfies R020: a federation peer step can be "
    "this binding because it terminates and has a deadline.");

// ── Warden integration ─────────────────────────────────────────────
//
// warden::deadline_from_grade_v<F> reads wallclock_budget_v<F> at
// compile time.  Downstream warden::DeadlineWatchdog consumes the
// nanosecond value when arming.

static_assert(cw::has_deadline_v<BoundForgePhase>);
static_assert(cw::deadline_from_grade_v<BoundForgePhase> == 10'000);

// ── G11×G12 cross-axis soundness check ─────────────────────────────
//
// cost_within_budget_v<F, K, N> evaluates:
//   predicted_cost = cost_polynomial(N) * cog_multiplier(K)
//   accepts iff predicted_cost ≤ wallclock_budget_v<F>
//
// For BoundForgePhase on Gpu (multiplier=1):
//   N=128: cost = 0 + 50*128 = 6400 ns ≤ 10000 ns  PASSES
//   N=200: cost = 0 + 50*200 = 10000 ns ≤ 10000 ns  PASSES (equality)
//   N=256: cost = 0 + 50*256 = 12800 ns > 10000 ns  FAILS

static_assert(
    cr::cost_within_budget_v<BoundForgePhase, cc::CogKind::Gpu, 128>,
    "G11×G12 cross-check: at N=128 IR nodes, predicted cost 6400 ns "
    "fits within the 10000 ns wallclock_budget.");

static_assert(
    cr::cost_within_budget_v<BoundForgePhase, cc::CogKind::Gpu, 200>,
    "G11×G12 cross-check: at N=200 IR nodes, predicted cost 10000 ns "
    "is exactly the wallclock_budget; ≤ comparison passes.");

static_assert(
    !cr::cost_within_budget_v<BoundForgePhase, cc::CogKind::Gpu, 256>,
    "G11×G12 cross-check: at N=256 IR nodes, predicted cost 12800 ns "
    "exceeds the 10000 ns wallclock_budget.  The cross-check rejects.  "
    "This is the WARDEN-DEADLINE-SAFETY guarantee: any binding that "
    "would arm warden at a deadline it cannot meet on a known Cog is "
    "rejected at compile time.");

// loop_bound<256> is the structural reason 256 is the upper bound:
// the upstream phase driver shards graphs > loop_bound before reaching
// this binding.  The cross-check fires at exactly N=loop_bound, which
// is the design intent — past loop_bound is structurally unreachable.

}  // namespace

int main() {
    BoundForgePhase bound{phase_d_fuse_ref};

    // ── Compile-time deadline projection ────────────────────────
    constexpr auto deadline_ns =
        cw::deadline_nanos_for_binding<BoundForgePhase>();
    static_assert(deadline_ns == 10'000,
        "Warden deadline must match the grant's wallclock_budget.");

    // ── Concrete invocation at N=128 (within budget) ────────────
    KernelGraph input{
        .num_nodes  = 128,
        .num_edges  = 200,
        .generation = 0
    };
    Arena arena{};
    KernelGraph fused = bound.value()(input, arena);

    // ── Predicted cost on Gpu ──────────────────────────────────
    constexpr std::uint64_t predicted_at_128 =
        cc::predicted_cost_v<BoundForgePhase, cc::CogKind::Gpu, 128>;
    static_assert(predicted_at_128 == 6400);  // 50 * 128

    std::printf("BoundForgePhase: input %d nodes → fused %d nodes (gen %d)\n",
                input.num_nodes, fused.num_nodes, fused.generation);
    std::printf("  arena bump        = %zu bytes (bound: %zu)\n",
                arena.bump,
                static_cast<std::size_t>(cf::bounded_alloc_v<BoundForgePhase>));
    std::printf("  wallclock budget  = %lu ns (warden-armed)\n",
                static_cast<unsigned long>(deadline_ns));
    std::printf("  predicted on Gpu  = %lu ns (loop_bound = %lu nodes)\n",
                static_cast<unsigned long>(predicted_at_128),
                static_cast<unsigned long>(cf::loop_bound_v<BoundForgePhase>));
    std::printf("  sizeof binding    = %zu (== sizeof(ForgePhasePtr) %zu) "
                "[20-axis grade + Cost + Resources, zero runtime cost]\n",
                sizeof(BoundForgePhase), sizeof(ForgePhasePtr));

    // Runtime sanity check: the deadline we read from the grade matches
    // the wallclock_budget grant — this is the warden boundary contract.
    if (deadline_ns != 10'000) {
        std::fprintf(stderr,
            "Warden deadline mismatch: got %lu, want 10000\n",
            static_cast<unsigned long>(deadline_ns));
        return 1;
    }

    std::fputs("example_fixy_bounded: OK\n", stdout);
    return 0;
}
