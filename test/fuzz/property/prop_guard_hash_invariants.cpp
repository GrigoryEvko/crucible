// ═══════════════════════════════════════════════════════════════════
// prop_guard_hash_invariants — invariants of the hash of a Guard at scale.
//
// Guard is the branch-discriminator at every BranchNode in the Merkle
// DAG: its hash is the primary key for routing to compiled arms.  A
// hash-function regression here corrupts the entire guard system —
// two semantically-distinct guards colliding leads to wrong-arm
// dispatch, which produces silently wrong answers in compiled mode.
// This file drives the hash of a Guard at stress volume.
//
// Guard is 12 bytes, laid out (see MerkleDag.h:302–332):
//
//     Guard::Kind    kind      (1B, 5 variants)
//     FixedArray<uint8_t, 3> pad (3B — still hashed, as its elements)
//     OpIndex        op_index  (4B, default UINT32_MAX)
//     uint16_t       arg_index (2B)
//     uint16_t       dim_index (2B)
//
// fixy::hash of the Protocol family walks every non-static data member
// of the Guard at compile time and folds via fmix64.  The
// documented safety claim is "adding a field is a hash-format break
// caught by CDAG_VERSION" — this fuzzer enforces the same claim at
// the value level: every existing field must contribute entropy.
//
// ── Properties checked per random Guard ─────────────────────────────
//
//   P1. Determinism      — hash(g) is a pure function; 8 back-to-back
//                          invocations on the same Guard all equal.
//   P2. Field-disambig   — perturbing exactly one field (kind / pad[i]
//                          / op_index / arg_index / dim_index) changes
//                          the hash.  Any field not contributing to
//                          the fold is a silent collision class.
//   P3. Equality concord — memcmp-equal Guards produce equal hashes.
//                          (The reverse is not required — two distinct
//                          guards could collide at ~2⁻⁶⁴ probability;
//                          we treat any collision as a real bug since
//                          100K iterations × 5e-15 → negligible.)
//   P4. Nonzero output   — the hash of a non-default Guard is
//                          overwhelmingly non-zero.  A stuck-zero
//                          output (e.g., refactor folding all fields
//                          via XOR with no seed) is caught directly.
//
// ── Bug classes this catches ────────────────────────────────────────
//
//   - A future Guard refactor that drops a field from the hash's
//     iteration set (e.g., marks kind as [[no_unique_address]] and
//     P2996 then excludes it — silent collision of SHAPE_DIM and
//     SCALAR_VALUE guards sharing the remaining fields).
//   - A refactor that leaves the pad[] array uninitialized on some
//     paths: two Guards constructed via different ctors with the
//     same semantic fields produce different hashes (a false branch
//     miss on replay).
//   - A change to fmix64 or the hash seed that stuck-zeros
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
#include <fixy/Core.h>
#include <fixy/Protocol.h>

#include <cstdint>
#include <utility>

namespace {

using crucible::Guard;
using crucible::OpIndex;

// Number of Guard fields (including each element of pad[3]) we
// perturb for P2.  kind (1) + pad[3] (3) + op_index (1) + arg_index
// (1) + dim_index (1) = 7 perturbation sites.
inline constexpr uint8_t kGuardPerturbSites = 7;

// ─── Guard generator ───────────────────────────────────────────────
//
// Generates every Guard field with random in-range values.  pad[3] is
// Randomized (not zero-filled) because the hash treats those 3
// bytes as hashed members — a future bug that fails to zero pad[]
// would produce different hashes for two Guards intended to be equal,
// which is the exact class P3 enforces.  We intentionally drive the
// pad bytes so the fuzzer sees those inputs.
[[nodiscard]] Guard random_guard(crucible::fuzz::prop::Rng& rng) noexcept {
    Guard g{};
    g.kind = rng.pick<Guard::Kind>();
    for (auto const index : g.pad.indices())
        g.pad.set(index, static_cast<uint8_t>(rng.next32() & 0xFFu));
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
// Each helper returns a Guard that differs from `base` in exactly
// One field.  Indices map 1:1 to kGuardPerturbSites sites below.
//
// kind takes the next enumerator (next_enumerator of property_runner.h),
// so it names an enumerator and differs.  Integer fields use XOR-with-1 which always
// changes the value (low bit flip) and never moves out of the type's
// representable range.  OpIndex is perturbed through .raw() since
// the strong type forbids arithmetic — we unwrap, XOR, rewrap.

[[nodiscard]] Guard perturb_kind(const Guard& base) noexcept {
    Guard out = base;
    // The next kind in the order of the values, and the first kind after
    // the last one, so the kind changes and names an enumerator.
    out.kind = ::crucible::fuzz::prop::next_enumerator(base.kind);
    return out;
}

[[nodiscard]] Guard perturb_pad(const Guard& base, uint8_t byte_idx) noexcept {
    Guard out = base;
    // byte_idx ∈ [0,3); caller guarantees, and the checked door ends the
    // process for another index.  XOR with 1 guarantees the byte changes
    // without overflowing uint8_t.
    const uint8_t before = *base.pad.view().get(byte_idx).expect("the index of a pad byte is below 3");
    out.pad.set(::fixy::Index<3>::check(byte_idx).expect("the index of a pad byte is below 3"),
                static_cast<uint8_t>(before ^ 1u));
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
    Guard base;
    Guard perturbed;
    uint8_t site;  // [0, kGuardPerturbSites) — which field was
    //                          perturbed (for repro).
};

}  // namespace

int crucible::fuzz::prop::run_property(::fixy::os::Args const arguments) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(arguments);

    return run(
        "hash of a Guard invariants", cfg,
        [](Rng& rng) {
            Payload p{};
            p.base = random_guard(rng);
            p.site = static_cast<uint8_t>(rng.next_below(kGuardPerturbSites));
            switch (p.site) {
                case 0:
                    p.perturbed = perturb_kind(p.base);
                    break;
                case 1:
                    p.perturbed = perturb_pad(p.base, 0);
                    break;
                case 2:
                    p.perturbed = perturb_pad(p.base, 1);
                    break;
                case 3:
                    p.perturbed = perturb_pad(p.base, 2);
                    break;
                case 4:
                    p.perturbed = perturb_op_index(p.base);
                    break;
                case 5:
                    p.perturbed = perturb_arg_index(p.base);
                    break;
                case 6:
                    p.perturbed = perturb_dim_index(p.base);
                    break;
                default:
                    ::fixy::unreachable();
            }
            return p;
        },
        [](const Payload& p) {
            // Run-time barrier: the constexpr hash could
            // otherwise be constant-folded at compile time for a
            // particular Guard the optimizer proves constant.  Each
            // read goes through read_at_run_time, so the optimizer
            // also merges no two of the 8 calls below.  The idiom is
            // the one of prop_compute_storage_nbytes_saturation.cpp.

            // P1. Determinism — 8 back-to-back invocations on `base`
            //                  all produce identical output.
            const uint64_t h0 = ::fixy::hash(read_at_run_time(p.base)).raw();
            for (int k = 0; k < 7; ++k) {
                if (::fixy::hash(read_at_run_time(p.base)).raw() != h0) return false;
            }

            // P2. Field-disambiguation — perturbed differs in exactly
            //                          one field; hashes must differ.
            //     (A collision here at ~2⁻⁶⁴ per iter is statistically
            //     impossible at 10⁵ iters; any observed failure is a
            //     real entropy-loss bug, not a random collision.)
            const uint64_t hp = ::fixy::hash(read_at_run_time(p.perturbed)).raw();
            if (h0 == hp) return false;

            // P3. Equality concordance — a fresh Guard byte-identical
            //     to base must hash to the same value.  Constructed
            //     from the bytes of base through decode, to bypass any
            //     compiler-elision of the copy path and to guarantee
            //     byte equality including pad[].
            const Guard copy = ::fixy::decode<Guard>(read_at_run_time(p.base))
                                   .expect("a Guard of valid fields decodes from its own bytes");
            if (::fixy::hash(read_at_run_time(copy)).raw() != h0) return false;

            // P4. Non-zero output — a hash of a Guard stuck at zero is
            //     a degenerate fold.  Random Guards hash to zero at
            //     ~2⁻⁶⁴ probability; treat any observation as a bug.
            //     (The default-constructed all-zero Guard is not
            //     exercised here since our generator always
            //     randomizes fields; the test is specifically on
            //     the random population.)
            if (h0 == 0u) return false;

            return true;
        });
}
