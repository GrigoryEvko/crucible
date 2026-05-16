// ════════════════════════════════════════════════════════════════════
// example_custom_kernel — FIXY-B5 / Phase B first worked example
//
// THE PATTERN: A USER-SUPPLIED KERNEL FUNCTION, via fixy::fn
//
// Same kernel-binding pattern as examples/fn/example_custom_kernel.cpp
// (the substrate-direct version using safety::fn::Fn<...>), but
// composed against the reject-by-default surface in fixy/Fn.h.
//
// The contrast is deliberate.  Read the two side-by-side:
//
//   examples/fn/example_custom_kernel.cpp  — 19 POSITIONAL axes.
//       The author specifies every per-axis grade in order.  Brittle
//       to dim-reordering, but compact when every axis is non-default.
//
//   examples/fixy/example_custom_kernel.cpp (this file)
//       — N RELAXATION grants + 20−N strict-default acknowledgements.
//       The author specifies ONLY what they relax; every other axis
//       must be explicitly acknowledged as strict-default via
//       `accept_default_strict_for<dim::X>`.  Verbose, but
//       reject-by-default: a reviewer can grep `grant::` to see the
//       full relaxation surface, and silently-defaulted assumptions
//       are impossible (an unread axis fails the IsAccepted gate).
//
// THIS FILE: a GEMM kernel binding for the bg thread, showing every
// dim's engagement choice with one-line rationale.  The resulting
// `fixy::fn<KernelPtr, Grants...>` is byte-equivalent to the raw
// function pointer (`sizeof == sizeof(void*)`) — the 20 type-level
// axes impose ZERO runtime overhead.
//
// WHAT TO READ NEXT (Phase C): stance-bound minting via
// `cs::mint_fn_for<cs::BgWorker>(KernelPtr)` collapses the 20-tag
// pack into one canonical pre-baked stance — fewer characters per
// call site, same compile-time discipline.
// ════════════════════════════════════════════════════════════════════

#include <crucible/fixy/Fixy.h>

#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace {

// ── Trust-rationale tag ─────────────────────────────────────────────
//
// `grant::trust_assumed_for<TaintClass>` takes a typename parameter as
// the audit-trail rationale.  An empty tag struct works — the tag
// itself names the unverified-trust class for review/grep.  (Aside:
// `grant::trust_assumed<auto Rationale>` exists for embedded-literal
// rationales, but C++ structural-NTTP rules prohibit raw string
// literals, so the tag-class form is the production shape.)
struct UserSuppliedKernel_NoFormalProof {};

// ── Stand-in kernel signature ───────────────────────────────────────
//
// Same scalar GEMM as examples/fn/example_custom_kernel.cpp; the
// binding shape is what matters, not the implementation quality.

using GemmFp32Ptr = void(*)(const float* a,    // M × K, row-major
                            const float* b,    // K × N, row-major
                            float*       c,    // M × N, row-major (output)
                            int          m,
                            int          n,
                            int          k);

void scalar_gemm_ref(const float* a, const float* b, float* c,
                     int m, int n, int k) noexcept {
    for (int row = 0; row < m; ++row) {
        for (int col = 0; col < n; ++col) {
            float acc = 0.f;
            for (int p = 0; p < k; ++p) {
                acc += a[row * k + p] * b[p * n + col];
            }
            c[row * n + col] = acc;
        }
    }
}

// ── The fixy::fn binding — per-dim engagement choices ──────────────
//
// Every dim appears EXACTLY once.  Each line is either a `grant::*`
// (relaxation, the axis is moved off the strict default) or an
// `accept_default_strict_for<dim::X>` (acknowledgement, the axis is
// kept at strict).  IsAccepted ensures full coverage; a missing dim
// is a compile error naming the missing axis.

using BoundGemm = cf::fn<GemmFp32Ptr,
    // 1. Type — substrate carries GemmFp32Ptr; no relaxation needed.
    cf::accept_default_strict_for<cd::Type>,

    // 2. Refinement — pred::True is correct; no additional gate.
    cf::accept_default_strict_for<cd::Refinement>,

    // 3. Usage = Copy — function pointer is freely copyable (vs the
    //    Linear strict default that would be wrong for a fnptr).
    cg::copy,

    // 4. Effect = Row<Bg> — kernel runs on the bg thread only.
    //    Strict default Row<> would silently allow the binding to be
    //    invoked from any context.
    cg::with<fx::Effect::Bg>,

    // 5. Security — kernel sees user model weights; Classified
    //    (strict default) is correct.  No declassification.
    cf::accept_default_strict_for<cd::Security>,

    // 6. Protocol — no session-typed handshake; proto::None default
    //    is correct.
    cf::accept_default_strict_for<cd::Protocol>,

    // 7. Lifetime — free function, valid forever; lifetime::Static
    //    default is correct.
    cf::accept_default_strict_for<cd::Lifetime>,

    // 8. Provenance = from_source<FromUser> — user-supplied kernel
    //    (vs the FromInternal strict default that would lie about
    //    where the pointer came from).
    cg::from_source<::crucible::safety::source::FromUser>,

    // 9. Trust = trust_assumed<rationale> — user-supplied code with
    //    no formal proof.  Strict default Verified would assert a
    //    safety claim the substrate cannot prove.
    cg::trust_assumed_for<UserSuppliedKernel_NoFormalProof>,

    // 10. Representation — function pointer is opaque addr;
    //     ReprKind::Opaque default is correct.
    cf::accept_default_strict_for<cd::Representation>,

    // 11. Observability — derived from Effect row; no engagement
    //     needed beyond strict-acknowledgement.
    cf::accept_default_strict_for<cd::Observability>,

    // 12. Complexity — GEMM is O(MNK) cubic.  Phase B vocab only
    //     covers Constant / Linear<N> / Quadratic<N> / Unstated.
    //     Cubic doesn't fit, so cost::Unstated (strict default) is
    //     the honest choice.
    cf::accept_default_strict_for<cd::Complexity>,

    // 13. Precision = F32 — GEMM accumulates in FP32 (vs the Exact
    //     strict default that would falsely promise bit-equality
    //     across reduction orderings).
    cg::precision_f32,

    // 14. Space — output written in-place; space::Zero default is
    //     correct (kernel allocates no aux memory).
    cf::accept_default_strict_for<cd::Space>,

    // 15. Overflow — IEEE 754 FP overflow → infinity; OverflowMode::
    //     Trap default is correct.
    cf::accept_default_strict_for<cd::Overflow>,

    // 16. Mutation = Mutable — kernel writes the C output buffer.
    //     Strict default Immutable would lie about the write
    //     surface.
    cg::mutable_in_place,

    // 17. Reentrancy = Reentrant — multiple GEMM kernels can run
    //     concurrently on disjoint buffers.  Strict default
    //     NonReentrant would forbid parallel kernel launches.
    cg::reentrant,

    // 18. Size — observation-depth N/A for a kernel pointer;
    //     size_pol::Unstated default is correct.
    cf::accept_default_strict_for<cd::Size>,

    // 19. Version — first revision of this binding; strict default
    //     1 is correct.
    cf::accept_default_strict_for<cd::Version>,

    // 20. Staleness — no time-based decay on a function pointer;
    //     stale::Fresh default is correct.
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Compile-time invariants — every axis routed correctly ──────────
//
// EBO collapse: the 20-axis grade vector adds zero bytes to the
// function pointer.  If this fires, a fixy-side resolver leaked a
// runtime member.
static_assert(sizeof(BoundGemm) == sizeof(GemmFp32Ptr),
    "EBO collapse failed — fixy::fn must remain byte-equivalent to its Type "
    "regardless of how many dim grants are engaged.");

// Per-axis introspection pin — every relaxation actually flowed
// through the resolver to the substrate's safety::fn::Fn<...>.
static_assert(BoundGemm::usage_v       == fn::UsageMode::Copy);
static_assert(BoundGemm::security_v    == fn::SecLevel::Classified);
static_assert(BoundGemm::overflow_v    == fn::OverflowMode::Trap);
static_assert(BoundGemm::mutation_v    == fn::MutationMode::Mutable);
static_assert(BoundGemm::reentrancy_v  == fn::ReentrancyMode::Reentrant);
static_assert(BoundGemm::version_v     == 1);
static_assert(std::is_same_v<typename BoundGemm::source_t,
                             ::crucible::safety::source::FromUser>);
static_assert(std::is_same_v<typename BoundGemm::trust_t,
                             ::crucible::safety::trust::Unverified>);
static_assert(std::is_same_v<typename BoundGemm::precision_t,
                             fn::precision::F32>);
static_assert(std::is_same_v<typename BoundGemm::effect_row_t,
                             fx::Row<fx::Effect::Bg>>);

// IsAccepted itself — pin that the engagement gate fires on the
// pack (and would reject if any dim were dropped).  This is the
// load-bearing reject-by-default property.
static_assert(cf::IsAccepted<
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<fx::Effect::Bg>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cg::from_source<::crucible::safety::source::FromUser>,
    cg::trust_assumed_for<UserSuppliedKernel_NoFormalProof>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cg::precision_f32,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cg::mutable_in_place,
    cg::reentrant,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>);

}  // namespace

int main() {
    // Materialize the binding from the function pointer via the
    // explicit constructor (per-axis customization beyond stance
    // defaults).  For the 8-canonical-stance shape, prefer
    // `cs::mint_fn_for<cs::BgWorker>(scalar_gemm_ref)` — see
    // examples/fn/example_custom_kernel.cpp's mint_fn analogue and
    // fixy/Stance.h.
    BoundGemm bound{scalar_gemm_ref};

    constexpr int M = 2, N = 2, K = 2;
    const float a[M * K] = { 1.f, 2.f,
                             3.f, 4.f };
    const float b[K * N] = { 5.f, 6.f,
                             7.f, 8.f };
    float c[M * N] = {};

    bound.value()(a, b, c, M, N, K);

    // C = [[1·5+2·7, 1·6+2·8], [3·5+4·7, 3·6+4·8]]
    //   = [[19, 22], [43, 50]]
    std::printf("fixy custom_kernel result: [[%g, %g], [%g, %g]] "
                "(expected [[19, 22], [43, 50]])\n",
                static_cast<double>(c[0]),
                static_cast<double>(c[1]),
                static_cast<double>(c[2]),
                static_cast<double>(c[3]));

    std::printf("BoundGemm sizeof = %zu (== sizeof(GemmFp32Ptr) %zu) "
                "[20-dim grade vector, zero runtime cost]\n",
                sizeof(BoundGemm), sizeof(GemmFp32Ptr));
    return 0;
}
