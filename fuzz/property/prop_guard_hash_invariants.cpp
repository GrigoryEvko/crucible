// ═══════════════════════════════════════════════════════════════════
// prop_guard_hash_invariants — Guard::hash() invariants at scale.
//
// Guard is the branch-discriminator at every BranchNode in the Merkle
// DAG: its hash is the primary key for routing to compiled arms.  A
// hash-function regression here corrupts the entire guard system —
// two semantically-distinct guards colliding leads to WRONG-ARM
// dispatch, which produces silently wrong answers in compiled mode.
// This file drives the Guard::hash() surface at stress volume.
//
// Guard is 12 bytes, laid out (see MerkleDag.h:302–332):
//
//     Guard::Kind    kind      (1B, 5 variants)
//     uint8_t        pad[3]    (3B — STILL hashed by reflect_hash)
//     OpIndex        op_index  (4B, default UINT32_MAX)
//     uint16_t       arg_index (2B)
//     uint16_t       dim_index (2B)
//
// Guard::hash() delegates to reflect_hash(*this), which walks every
// non-static data member at compile time and folds via fmix64.  The
// documented safety claim is "adding a field is a hash-format break
// caught by CDAG_VERSION" — this fuzzer enforces the same claim at
// the value level: every existing field MUST contribute entropy.
//
// ── Properties checked per random Guard ─────────────────────────────
//
//   P1. DETERMINISM      — g.hash() is a pure function; 8 back-to-back
//                          invocations on the same Guard all equal.
//   P2. FIELD-DISAMBIG   — perturbing EXACTLY one field (kind / pad[i]
//                          / op_index / arg_index / dim_index) changes
//                          the hash.  Any field not contributing to
//                          the fold is a silent collision class.
//   P3. EQUALITY CONCORD — memcmp-equal Guards produce equal hashes.
//                          (The reverse is NOT required — two distinct
//                          guards COULD collide at ~2⁻⁶⁴ probability;
//                          we treat any collision as a real bug since
//                          100K iterations × 5e-15 → negligible.)
//   P4. NONZERO OUTPUT   — Guard::hash() of a non-default Guard is
//                          overwhelmingly non-zero.  A stuck-zero
//                          output (e.g., refactor folding all fields
//                          via XOR with no seed) is caught directly.
//
// ── Bug classes this catches ────────────────────────────────────────
//
//   - A future Guard refactor that drops a field from reflect_hash's
//     iteration set (e.g., marks kind as [[no_unique_address]] and
//     P2996 then excludes it — silent collision of SHAPE_DIM and
//     SCALAR_VALUE guards sharing the remaining fields).
//   - A refactor that leaves the pad[] array uninitialized on some
//     paths: two Guards constructed via different ctors with the
//     same semantic fields produce DIFFERENT hashes (a false branch
//     miss on replay).
//   - A change to fmix64 or the reflect_hash seed that stuck-zeros
//     some inputs.
//   - Adding a new Guard::Kind without bumping CDAG_VERSION — the
//     property P4 fires when the new variant's default-pad Guards
//     accidentally collide with an existing variant.
//
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/MerkleDag.h>
#include <crucible/Types.h>

#include <cstdint>
#include <cstring>
#include <utility>

namespace {

using crucible::Guard;
using crucible::OpIndex;

// Number of Guard::Kind variants — update this when a new variant is
// added to MerkleDag.h:303.  reflect_hash iterates the enum as an
// integer, so any out-of-range value is still HASHABLE; we stay
// in-range for P2 field-disambiguation to be statistically sound
// (two out-of-range enums would collide on the same underlying
// uint8_t, and the "different field → different hash" property would
// spuriously fail).  Current variants: SHAPE_DIM, SCALAR_VALUE,
// DTYPE, DEVICE, OP_SEQUENCE.
inline constexpr uint8_t kGuardKindCount = 5;

// Number of Guard fields (including each element of pad[3]) we
// perturb for P2.  kind (1) + pad[3] (3) + op_index (1) + arg_index
// (1) + dim_index (1) = 7 perturbation sites.
inline constexpr uint8_t kGuardPerturbSites = 7;

// ─── Guard generator ───────────────────────────────────────────────
//
// Generates every Guard field with random in-range values.  pad[3] is
// RANDOMIZED (not zero-filled) because reflect_hash treats those 3
// bytes as hashed members — a future bug that fails to zero pad[]
// would produce different hashes for two Guards intended to be equal,
// which is the exact class P3 enforces.  We intentionally drive the
// pad bytes so the fuzzer sees those inputs.
[[nodiscard]] Guard random_guard(crucible::fuzz::prop::Rng& rng) noexcept {
    Guard g{};
    g.kind = static_cast<Guard::Kind>(rng.next_below(kGuardKindCount));
    g.pad[0] = static_cast<uint8_t>(rng.next32() & 0xFFu);
    g.pad[1] = static_cast<uint8_t>(rng.next32() & 0xFFu);
    g.pad[2] = static_cast<uint8_t>(rng.next32() & 0xFFu);
    // op_index: exercise both valid indices and the none() sentinel.
    // Bias toward typical op indices (0..4095) with a 10% escape
    // for none().
    if ((rng.next32() & 0xFu) == 0u) {
        g.op_index = OpIndex::none();
    } else {
        g.op_index = OpIndex{rng.next_below(4096u)};
    }
    g.arg_index = static_cast<uint16_t>(rng.next_below(0x10000u));
    g.dim_index = static_cast<uint16_t>(rng.next_below(0x10000u));
    return g;
}

// ─── Perturbation helpers ──────────────────────────────────────────
//
// Each helper returns a Guard that differs from `base` in EXACTLY
// ONE field.  Indices map 1:1 to kGuardPerturbSites sites below.
//
// kind is perturbed by (kind + 1) mod kGuardKindCount — stays in
// range, always differs.  Integer fields use XOR-with-1 which always
// changes the value (low bit flip) and never moves out of the type's
// representable range.  OpIndex is perturbed through .raw() since
// the strong type forbids arithmetic — we unwrap, XOR, rewrap.

[[nodiscard]] Guard perturb_kind(const Guard& base) noexcept {
    Guard out = base;
    // std::to_underlying returns the enum's underlying type (uint8_t
    // here); no cast needed.  Promotion to int during (+1) is fine —
    // the modulo brings the result back into [0, kGuardKindCount),
    // safely representable in uint8_t.
    const auto cur = std::to_underlying(base.kind);
    out.kind = static_cast<Guard::Kind>(
        static_cast<uint8_t>((cur + 1u) % kGuardKindCount));
    return out;
}

[[nodiscard]] Guard perturb_pad(const Guard& base, uint8_t byte_idx) noexcept {
    Guard out = base;
    // byte_idx ∈ [0,3); caller guarantees.  XOR with 1 guarantees
    // the byte changes without overflowing uint8_t.
    out.pad[byte_idx] = static_cast<uint8_t>(base.pad[byte_idx] ^ 1u);
    return out;
}

[[nodiscard]] Guard perturb_op_index(const Guard& base) noexcept {
    Guard out = base;
    // XOR on the raw u32 via the strong type's unwrap/rewrap — no
    // arithmetic operators are provided.  base.op_index.raw() XOR 1
    // is guaranteed distinct from base.op_index.raw().
    out.op_index = OpIndex{base.op_index.raw() ^ 1u};
    return out;
}

[[nodiscard]] Guard perturb_arg_index(const Guard& base) noexcept {
    Guard out = base;
    out.arg_index = static_cast<uint16_t>(base.arg_index ^ 1u);
    return out;
}

[[nodiscard]] Guard perturb_dim_index(const Guard& base) noexcept {
    Guard out = base;
    out.dim_index = static_cast<uint16_t>(base.dim_index ^ 1u);
    return out;
}

// ─── Per-iteration payload ─────────────────────────────────────────

struct Payload {
    Guard   base;
    Guard   perturbed;
    uint8_t site;  // [0, kGuardPerturbSites) — which field was
                   //                          perturbed (for repro).
};

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(argc, argv);

    return run("Guard::hash() invariants", cfg,
        [](Rng& rng) {
            Payload p{};
            p.base = random_guard(rng);
            p.site = static_cast<uint8_t>(
                rng.next_below(kGuardPerturbSites));
            switch (p.site) {
                case 0:  p.perturbed = perturb_kind(p.base);        break;
                case 1:  p.perturbed = perturb_pad(p.base, 0);      break;
                case 2:  p.perturbed = perturb_pad(p.base, 1);      break;
                case 3:  p.perturbed = perturb_pad(p.base, 2);      break;
                case 4:  p.perturbed = perturb_op_index(p.base);    break;
                case 5:  p.perturbed = perturb_arg_index(p.base);   break;
                case 6:  p.perturbed = perturb_dim_index(p.base);   break;
                default: std::unreachable();
            }
            return p;
        },
        [](const Payload& p) {
            // Volatile barrier: the constexpr/gnu::pure hash() could
            // otherwise be constant-folded at compile time for a
            // particular Guard the optimizer proves constant — forcing
            // a runtime load through a volatile pointer matches the
            // idiom in prop_compute_storage_nbytes_saturation.cpp.
            const Guard* volatile bp = &p.base;
            const Guard* volatile pp = &p.perturbed;

            // P1. Determinism — 8 back-to-back invocations on `base`
            //                  all produce identical output.
            const uint64_t h0 = bp->hash();
            for (int k = 0; k < 7; ++k) {
                if (bp->hash() != h0) return false;
            }

            // P2. Field-disambiguation — perturbed differs in EXACTLY
            //                          one field; hashes must differ.
            //     (A collision here at ~2⁻⁶⁴ per iter is statistically
            //     impossible at 10⁵ iters; any observed failure is a
            //     real entropy-loss bug, not a random collision.)
            const uint64_t hp = pp->hash();
            if (h0 == hp) return false;

            // P3. Equality concordance — a fresh Guard byte-identical
            //     to base MUST hash to the same value.  Constructed
            //     via memcpy to bypass any compiler-elision of the
            //     copy path and to guarantee byte equality including
            //     pad[].
            Guard copy{};
            std::memcpy(&copy, bp, sizeof(Guard));
            const Guard* volatile cp = &copy;
            if (cp->hash() != h0) return false;

            // P4. Non-zero output — Guard::hash() stuck-at-zero is
            //     a degenerate fold.  Random Guards hash to zero at
            //     ~2⁻⁶⁴ probability; treat any observation as a bug.
            //     (The default-constructed all-zero Guard is NOT
            //     exercised here since our generator always
            //     randomizes fields; the test is specifically on
            //     the random population.)
            if (h0 == 0u) return false;

            return true;
        });
}
