// reflect_hash runtime cost vs hand-rolled FNV over the same fields.
//
// P2996 reflection emits a per-field hash chain at compile time; the
// runtime code is identical in principle to a hand-written fmix64 loop.
// The bench confirms the abstraction has zero steady-state cost — the
// Mann-Whitney A/B at the end should come out "indistinguishable" on a
// clean machine.
//
// Extended (post REFL-1..4) — three new side-by-side comparisons that
// directly answer "did the refactors regress performance?":
//
//   • feedback_signature: new (reflection) vs old (packed bit-shift)
//   • loopterm_hash:      new (reflection) vs old (packed bit-shift)
//   • compute_recipe_hash: actual (packed)  vs hypothetical (reflection)
//                           — proves the refactor avoided a real cost

#include <cstdint>
#include <cstdio>
#include <utility>

#include <crucible/Expr.h>       // detail::fmix64
#include <crucible/MerkleDag.h>  // FeedbackEdge, LoopTermKind, NumericalRecipe
#include <crucible/NumericalRecipe.h>
#include <crucible/Reflect.h>

#include "bench_harness.h"

namespace {

struct Small {
    uint32_t id;
    uint16_t kind;
    uint16_t flags;
    int64_t  payload;
};

struct Wide {
    uint64_t schema;
    uint64_t shape;
    uint64_t scope;
    uint64_t callsite;
    uint32_t num_inputs;
    uint32_t num_outputs;
    uint8_t  op_flags;
    uint8_t  pad[7];
    int64_t  scalars[5];
};

uint64_t manual_hash_small(const Small& s) noexcept {
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    h = h * 0x9E3779B97F4A7C15ULL ^ crucible::detail::fmix64(s.id);
    h = h * 0x9E3779B97F4A7C15ULL ^ crucible::detail::fmix64(s.kind);
    h = h * 0x9E3779B97F4A7C15ULL ^ crucible::detail::fmix64(s.flags);
    h = h * 0x9E3779B97F4A7C15ULL ^ crucible::detail::fmix64(
        static_cast<uint64_t>(s.payload));
    return crucible::detail::fmix64(h);
}

// ── Pre-REFL-2 implementation of feedback_signature ──────────────
//
// Preserved here verbatim so the bench can A/B the manual packed
// pattern against the reflection-driven version that ships in
// MerkleDag.h.  Real callers see only the new version; this baseline
// exists only inside this TU.
uint64_t feedback_signature_packed(
    std::span<const crucible::FeedbackEdge> edges) noexcept
{
    if (edges.empty()) return 0;
    constexpr uint64_t kSeed = 0x6665656462616B73ULL;
    uint64_t h = kSeed;
    for (const auto& e : edges) {
        uint64_t packed = (static_cast<uint64_t>(e.output_idx) << 16) | e.input_idx;
        h = crucible::detail::fmix64(h ^ packed);
    }
    h = crucible::detail::fmix64(h ^ edges.size());
    return h;
}

// ── Pre-REFL-3 implementation of loopterm_hash ───────────────────
//
// Manual packing pattern: term_kind (1 byte) + repeat_count (4 bytes)
// folded into one u64 word, epsilon bit_cast to u32 then xored with
// a separate salt + fmix64.  Two fmix64 calls total.
uint64_t loopterm_hash_packed(
    crucible::LoopTermKind term_kind,
    uint32_t               repeat_count,
    float                  epsilon) noexcept
{
    constexpr uint64_t kTermSalt = 0x7465726D696E6174ULL;
    constexpr uint64_t kEpsSalt  = 0x65707369006C6F6EULL;
    uint64_t packed = static_cast<uint64_t>(std::to_underlying(term_kind)) |
                      (static_cast<uint64_t>(repeat_count) << 8);
    uint64_t h = crucible::detail::fmix64(packed ^ kTermSalt);
    h ^= crucible::detail::fmix64(
        static_cast<uint64_t>(std::bit_cast<uint32_t>(epsilon)) ^ kEpsSalt);
    return h;
}

// ── Hypothetical reflection version of compute_recipe_hash ───────
//
// To answer "what would the perf be if we had refactored
// compute_recipe_hash to use reflection?" — apply reflect_fmix_fold
// to a Spec struct (the loopterm_hash pattern).  Bit pattern would
// shift; goldens would all need updating.  Bench measures whether
// the perf difference is significant enough to justify keeping
// the manual version.
struct RecipeSpec {
    crucible::ScalarType           accum_dtype;
    crucible::ScalarType           out_dtype;
    crucible::ReductionAlgo        reduction_algo;
    crucible::RoundingMode         rounding;
    crucible::ScalePolicy          scale_policy;
    crucible::SoftmaxRecurrence    softmax;
    crucible::ReductionDeterminism determinism;
    uint8_t                        flags;
};

uint64_t compute_recipe_hash_reflected(
    const crucible::NumericalRecipe& r) noexcept
{
    constexpr uint64_t kSeed = 0x9E3779B97F4A7C15ULL;
    return crucible::reflect_fmix_fold<kSeed>(RecipeSpec{
        r.accum_dtype, r.out_dtype, r.reduction_algo, r.rounding,
        r.scale_policy, r.softmax, r.determinism, r.flags
    });
}

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== reflect ===\n\n");

    // All three Runs use volatile-seeded inputs so the compiler can't
    // hoist the hash computation out of the body. The seeds live in the
    // outer scope so the IIFE-lambdas can capture by reference.
    volatile uint32_t vid       = 42;
    volatile uint16_t vkind     = 7;
    volatile uint16_t vflags    = 0x3F;
    volatile int64_t  vpayload  = 0x1234'5678'9ABC'DEF0LL;
    volatile uint64_t vschema   = 0xAABB'CCDD'0000'0000ULL;

    auto make_small = [&]() noexcept {
        Small s{};
        s.id      = vid;
        s.kind    = vkind;
        s.flags   = vflags;
        s.payload = vpayload;
        return s;
    };

    bench::Report reports[] = {
        bench::run("reflect_hash<Small>  (4 fields)", [&]{
            Small s = make_small();
            auto h = crucible::reflect_hash(s);
            bench::do_not_optimize(h);
        }),
        bench::run("manual fmix64 chain  (same 4)", [&]{
            Small s = make_small();
            auto h = manual_hash_small(s);
            bench::do_not_optimize(h);
        }),
        bench::run("reflect_hash<Wide>  (19 fields)", [&]{
            Wide w{};
            w.schema      = vschema;
            w.shape       = vschema ^ 1;
            w.scope       = vschema ^ 2;
            w.callsite    = vschema ^ 3;
            w.num_inputs  = 2;
            w.num_outputs = 1;
            w.op_flags    = 0x1;
            w.scalars[0]  = 1;
            w.scalars[1]  = 2;
            auto h = crucible::reflect_hash(w);
            bench::do_not_optimize(h);
        }),
    };

    bench::emit_reports_text(reports);

    // The core claim: reflect_hash<Small> and manual_hash_small produce
    // statistically indistinguishable timings on a clean machine. If
    // this goes [REGRESS], reflection's emitted code has drifted from
    // the hand-rolled form.
    std::printf("\n=== compare ===\n");
    bench::compare(reports[0], reports[1]).print_text(stdout);

    bench::emit_reports_json(reports, json);

    // ═══════════════════════════════════════════════════════════════
    // Post-REFL refactor comparisons.  Each pair measures the actual
    // perf delta of the reflection refactor against its pre-refactor
    // packed-manual baseline (preserved verbatim above).  These answer
    // the "did we leave perf on the table?" question with concrete
    // numbers.
    // ═══════════════════════════════════════════════════════════════
    std::printf("\n=== reflect refactor comparisons ===\n\n");

    // ── feedback_signature: reflection vs packed (REFL-2) ──────────
    //
    // Tiny FeedbackEdge (2 × u16 = 4 B); span of 8 edges per call.
    // Reflection version walks edges and applies reflect_hash(e) +
    // outer fmix64 fold; packed version applies one shift+OR per edge
    // + outer fmix64 fold.
    crucible::FeedbackEdge edges_buf[8] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 4},
        {4, 5}, {5, 6}, {6, 7}, {7, 0}
    };
    volatile uint16_t bump = 0;  // prevents constant-fold

    bench::Report refactor_reports[] = {
        bench::run("feedback_signature  (reflection, 8 edges)", [&]{
            edges_buf[0].output_idx = bump;  // perturb each iter
            auto h = crucible::feedback_signature(
                std::span<const crucible::FeedbackEdge>{edges_buf, 8});
            bench::do_not_optimize(h);
        }),
        bench::run("feedback_signature  (packed,     8 edges)", [&]{
            edges_buf[0].output_idx = bump;
            auto h = feedback_signature_packed(
                std::span<const crucible::FeedbackEdge>{edges_buf, 8});
            bench::do_not_optimize(h);
        }),

        // ── loopterm_hash: reflection vs packed (REFL-3) ───────────
        //
        // Three fields (LoopTermKind + uint32_t + float).  Reflection
        // does 3 fmix64 calls (one per field via reflect_fmix_fold);
        // packed does 1 fmix64 on combined word + 1 fmix64 on epsilon
        // = 2 fmix64 calls total.  Expected reflection ~1.5× the
        // packed cost.
        bench::run("loopterm_hash       (reflection)", [&]{
            crucible::LoopNode ln{};
            ln.term_kind    = crucible::LoopTermKind::REPEAT;
            ln.repeat_count = 100u + bump;
            ln.epsilon      = 1.0e-6f;
            auto h = crucible::loopterm_hash(ln);
            bench::do_not_optimize(h);
        }),
        bench::run("loopterm_hash       (packed)", [&]{
            auto h = loopterm_hash_packed(
                crucible::LoopTermKind::REPEAT,
                100u + bump,
                1.0e-6f);
            bench::do_not_optimize(h);
        }),

        // ── compute_recipe_hash: packed (actual) vs reflection (hyp.)
        //
        // 8 single-byte semantic fields packing perfectly into one
        // uint64_t — actual implementation does 1 pack + 1 fmix64.
        // Reflection hypothetical does 8 fmix64 calls (~6× slower);
        // bench measures the cost we AVOIDED by keeping it manual.
        bench::run("compute_recipe_hash (packed actual)", [&]{
            crucible::NumericalRecipe r{};
            r.accum_dtype = crucible::ScalarType::Float;
            r.out_dtype   = crucible::ScalarType::Half;
            r.flags       = static_cast<uint8_t>(bump);
            auto h = crucible::compute_recipe_hash(r);
            bench::do_not_optimize(h);
        }),
        bench::run("compute_recipe_hash (reflection hypothetical)", [&]{
            crucible::NumericalRecipe r{};
            r.accum_dtype = crucible::ScalarType::Float;
            r.out_dtype   = crucible::ScalarType::Half;
            r.flags       = static_cast<uint8_t>(bump);
            auto h = compute_recipe_hash_reflected(r);
            bench::do_not_optimize(h);
        }),
    };

    bench::emit_reports_text(refactor_reports);

    std::printf("\n=== refactor compares ===\n");
    std::printf("\nfeedback_signature: reflection vs packed\n");
    bench::compare(refactor_reports[0], refactor_reports[1]).print_text(stdout);
    std::printf("\nloopterm_hash: reflection vs packed\n");
    bench::compare(refactor_reports[2], refactor_reports[3]).print_text(stdout);
    std::printf("\ncompute_recipe_hash: packed-actual vs reflection-hypothetical\n");
    bench::compare(refactor_reports[4], refactor_reports[5]).print_text(stdout);

    bench::emit_reports_json(refactor_reports, json);
    return 0;
}
