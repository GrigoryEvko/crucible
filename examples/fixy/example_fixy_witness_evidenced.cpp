// ════════════════════════════════════════════════════════════════════
// example_fixy_witness_evidenced — FIXY-G9 worked example
//
// THE PATTERN: PROGRESSIVE WITNESS UPGRADE OF A Mimic NV EMIT BINDING.
//
// Three versions of the SAME binding shape (Mimic NV SASS emit):
//
//   V1: every grant uses DefaultWitness = Asserted<UnnamedRationale>.
//       This is what production code looks like BEFORE FIXY-G9.
//   V2: Trust + Precision + Recipe axes get CrossValidated witness
//       after cross-vendor numerics CI lands a green run.
//   V3: Provenance axis gets FormallyVerified witness after a
//       small-SMT discharge (placeholder ProofCert).
//
// Downstream consumers (AdaptiveScheduler hot-tier admission, Cipher
// hot-tier promotion, Federation peer) demand different witness
// floors per axis.  V1 fails most floor checks; V2/V3 progressively
// admit through more consumers.
// ════════════════════════════════════════════════════════════════════

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/Witness.h>
#include <crucible/safety/witness/Witness.h>

#include <cstdint>
#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;
namespace sw = crucible::safety::witness;

namespace {

// Phantom proof-cert tag for the FormallyVerified witness in V3.
struct mimic_nv_emit_proof {};

// ── V1: Asserted across the board ────────────────────────────────────
//
// Production code as it looks BEFORE FIXY-G9.  No witness override on
// any axis; every grant inherits DefaultWitness.

using MimicEmitV1 = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,                                              // Usage
    cg::with<crucible::effects::Effect::Bg,
             crucible::effects::Effect::Alloc>,            // Effect
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cg::vendor_nv,                                         // Representation
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cg::precision_f32,                                     // Precision
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cg::mutable_in_place,                                  // Mutation
    cg::reentrant,                                         // Reentrancy
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── V2: CrossValidated on Trust + Precision + Recipe ────────────────
//
// After cross-vendor numerics CI's first green run, the Trust /
// Precision / Recipe axes can be evidence-upgraded to CrossValidated.
// Reentrancy stays Tested<> (test coverage exists).

using MimicEmitV2 = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<crucible::effects::Effect::Bg,
             crucible::effects::Effect::Alloc>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for_e<cd::Trust,
        sw::CrossValidated<101>>,                          // bumped
    cg::vendor_backend_e<cg::VendorBackend::NV,
        sw::CrossValidated<102>>,                          // bumped
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cg::precision_f32_e<sw::CrossValidated<103>>,          // bumped
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cg::mutable_in_place,
    cg::reentrant_e<sw::Tested<201>>,                      // bumped to Tested
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── V3: FormallyVerified on Provenance ──────────────────────────────
//
// After a small-SMT discharge proves the source-tag flow is
// non-leaking, Provenance gets FormallyVerified.

using MimicEmitV3 = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<crucible::effects::Effect::Bg,
             crucible::effects::Effect::Alloc>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for_e<cd::Provenance,
        sw::FormallyVerified<mimic_nv_emit_proof>>,        // proof
    cf::accept_default_strict_for_e<cd::Trust,
        sw::CrossValidated<101>>,
    cg::vendor_backend_e<cg::VendorBackend::NV,
        sw::CrossValidated<102>>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cg::precision_f32_e<sw::CrossValidated<103>>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cg::mutable_in_place,
    cg::reentrant_e<sw::Tested<201>>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Downstream consumer floor demands ────────────────────────────────
//
// AdaptiveScheduler hot-tier: needs at least Tested on Reentrancy.
// Cipher hot-tier promotion: needs at least CrossValidated on Trust.
// Federation peer (cross-org): needs at least CrossValidated on
//                              Provenance.

// V1 fails every demand.
static_assert(!cf::FnWitnessAtLeast<MimicEmitV1, cd::Reentrancy,
                                    sw::Tested<0>>);
static_assert(!cf::FnWitnessAtLeast<MimicEmitV1, cd::Trust,
                                    sw::CrossValidated<0>>);
static_assert(!cf::FnWitnessAtLeast<MimicEmitV1, cd::Provenance,
                                    sw::CrossValidated<0>>);

// V2 passes AdaptiveScheduler + Cipher hot-tier; still fails
// federation peer.
static_assert(cf::FnWitnessAtLeast<MimicEmitV2, cd::Reentrancy,
                                   sw::Tested<0>>);
static_assert(cf::FnWitnessAtLeast<MimicEmitV2, cd::Trust,
                                   sw::CrossValidated<0>>);
static_assert(!cf::FnWitnessAtLeast<MimicEmitV2, cd::Provenance,
                                    sw::CrossValidated<0>>);

// V3 passes every demand.
static_assert(cf::FnWitnessAtLeast<MimicEmitV3, cd::Reentrancy,
                                   sw::Tested<0>>);
static_assert(cf::FnWitnessAtLeast<MimicEmitV3, cd::Trust,
                                   sw::CrossValidated<0>>);
static_assert(cf::FnWitnessAtLeast<MimicEmitV3, cd::Provenance,
                                   sw::CrossValidated<0>>);

// R016 (HotPath::Hot residency) needs Tested floor on Trust +
// Reentrancy.  V2 and V3 satisfy R016; V1 does not.
static_assert(!cr::R016_requires_witness_floor_v<MimicEmitV1>);
static_assert(cr::R016_requires_witness_floor_v<MimicEmitV2>);
static_assert(cr::R016_requires_witness_floor_v<MimicEmitV3>);

}  // namespace

int main() {
    // Runtime smoke — pin the progression at runtime.
    bool v1_admits = cr::R016_requires_witness_floor_v<MimicEmitV1>;
    bool v2_admits = cr::R016_requires_witness_floor_v<MimicEmitV2>;
    bool v3_admits = cr::R016_requires_witness_floor_v<MimicEmitV3>;

    if (v1_admits) {
        std::fprintf(stderr, "V1 should not satisfy R016\n");
        return 1;
    }
    if (!v2_admits) {
        std::fprintf(stderr, "V2 should satisfy R016\n");
        return 2;
    }
    if (!v3_admits) {
        std::fprintf(stderr, "V3 should satisfy R016\n");
        return 3;
    }

    std::fputs("example_fixy_witness_evidenced: OK\n", stdout);
    return 0;
}
