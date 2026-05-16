// ════════════════════════════════════════════════════════════════════
// example_fixy_cipher_writer — FIXY-E worked example 6/N
//
// THE PATTERN: A CIPHER COLD-TIER APPEND-ONLY WRITER, via fixy::fn
//
// Sibling of the other 5 fixy examples (kernel, optimizer, CNTP
// frame, Forge phase, Mimic emit).  Demonstrates the Cipher
// persistence layer's binding shape — the COLD tier writes an
// append-only event-sourced record to durable storage (NVMe / S3).
//
// THE LOAD-BEARING CONTRAST from example_fixy_forge_phase and
// example_fixy_mimic_backend_hook:
//   - Mutation:   APPEND.  Cipher cold tier never rewrites prior
//                 records; every event is a tail append.  This is
//                 the canonical Mutation=Append binding in Crucible.
//   - Effect:     Row<Bg, IO, Block>.  Cold-tier I/O is blocking;
//                 the writer runs on the bg thread.
//   - Trust:      STRICT (Verified).  Cipher records are Crucible-
//                 internal — only Cipher writes them, only Cipher
//                 reads them.
//   - Source:     STRICT (FromInternal).  Records originate from
//                 within the runtime.
//   - Lifetime:   In<CipherColdTierTag>.  The writer's outputs are
//                 owned by the Cipher tier; freeing the tier
//                 invalidates handles.
//   - Staleness:  STRICT (Fresh).  Each append carries a fresh
//                 epoch; stale appends are rejected at the tier-
//                 promotion boundary.
//
// Reading side-by-side with examples/fn/* peers: Cipher does NOT
// have a substrate-direct example in examples/fn/ today; this fixy
// example is the canonical reference.  When Cipher's runtime entry
// points migrate to fn<> wrapping, the substrate-direct contrast
// fills in.
//
// See CRUCIBLE.md §14 (Cipher 3-tier persistence), MIMIC.md §41
// (federation cache key on cold-tier records).
// ════════════════════════════════════════════════════════════════════

#include <crucible/fixy/Fixy.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace cf  = crucible::fixy;
namespace cd  = crucible::fixy::dim;
namespace cg  = crucible::fixy::grant;
namespace fn  = crucible::safety::fn;
namespace fx  = crucible::effects;

namespace {

// ── Stand-in cold-tier record ──────────────────────────────────────
//
// A single append carries an opaque payload + epoch + content hash.
// Real Cipher records are richer (TraceGraph delta, weight snapshot,
// KernelCache slot, MAP-Elites archive update); the binding shape
// is identical across record kinds.

struct CipherColdRecord {
    std::uint64_t epoch          = 0;
    std::uint64_t content_hash   = 0;
    std::uint32_t payload_bytes  = 0;
    std::uint32_t kind_tag       = 0;  // event kind discriminator
};

// ── Region tag for the Lifetime axis ──────────────────────────────
//
// `CipherColdTierTag` is the region; records live inside it.  Freeing
// the tier (Cipher::cold::reset()) invalidates every outstanding
// record handle.  Permission<CipherColdTierTag> is the substrate's
// CSL gate; the fixy binding inherits that discipline via the
// grant::lifetime_region tag below.

struct CipherColdTierTag {};

// ── Stand-in writer signature ──────────────────────────────────────
//
// Real Cipher's append entry point is on the Cipher class; this
// stand-in is a function pointer so the example stays standalone.

using CipherAppendPtr = bool(*)(const CipherColdRecord& rec) noexcept;

bool append_cold_ref(const CipherColdRecord& rec) noexcept {
    // No I/O in the example — production code calls into Cipher's
    // tier write path (fsync after every batch, content-addressed
    // chunking, lifecycle policy on S3).
    (void)rec;
    return true;
}

// ── fixy::fn binding — per-dim engagement choices ──────────────────
//
// 7 relaxations + 13 strict acknowledgements = 20 dims engaged.  The
// strict-acks are LOAD-BEARING: they communicate Cipher's persistence
// invariants — internal-only, verified, fresh-on-append, bit-exact.

using BoundCipherWriter = cf::fn<CipherAppendPtr,
    // 1. Type — function pointer carrier.
    cf::accept_default_strict_for<cd::Type>,

    // 2. Refinement — pred::True default.  A future ValidColdRecord
    //    predicate could gate on epoch monotonicity at the type level.
    cf::accept_default_strict_for<cd::Refinement>,

    // 3. Usage = Copy — function pointer is freely copyable.
    cg::copy,

    // 4. Effect = Row<Bg, IO, Block> — bg-thread I/O with blocking
    //    fsync.  This declares the writer's full effect surface;
    //    composing into a hot-path TU rejects (effect row exceeds
    //    hot-path's empty row).
    cg::with<fx::Effect::Bg, fx::Effect::IO, fx::Effect::Block>,

    // 5. Security — Cipher records are Crucible-internal; Classified
    //    strict default is correct (no wire visibility).
    cf::accept_default_strict_for<cd::Security>,

    // 6. Protocol — no session-typed handshake; Cipher batches via
    //    its own internal queue.
    cf::accept_default_strict_for<cd::Protocol>,

    // 7. Lifetime = In<CipherColdTierTag> — records are owned by the
    //    Cipher tier; freeing the tier invalidates handles.
    cg::lifetime_region<CipherColdTierTag{}>,

    // 8. Provenance — STRICT (FromInternal).  Records originate from
    //    within the runtime; never from user code.
    cf::accept_default_strict_for<cd::Provenance>,

    // 9. Trust — STRICT (Verified).  Cipher is CI-validated under
    //    bit_exact_recovery_invariant; relaxing to Unverified would
    //    lie about the persistence contract.
    cf::accept_default_strict_for<cd::Trust>,

    // 10. Representation — Opaque strict default; cold-tier records
    //     are serialized at the storage boundary, not at the in-RAM
    //     binding boundary.
    cf::accept_default_strict_for<cd::Representation>,

    // 11. Observability — derived from Effect row.
    cf::accept_default_strict_for<cd::Observability>,

    // 12. Complexity = Linear<1> — O(1·N) in payload byte count.
    cg::complexity_linear<1>,

    // 13. Precision — STRICT (Exact).  Persistence is bit-exact;
    //     a record written at org A and replayed at org B produces
    //     identical post-replay state (DetSafe axiom).
    cf::accept_default_strict_for<cd::Precision>,

    // 14. Space = Bounded<sizeof(CipherColdRecord)> — one record's
    //     worth of working memory per append; storage cost is
    //     amortized into the cold-tier's lifecycle policy.
    cg::space_bounded<sizeof(CipherColdRecord)>,

    // 15. Overflow — Trap strict default; record fields are
    //     integers with no wrap semantics.
    cf::accept_default_strict_for<cd::Overflow>,

    // 16. Mutation = Append — the canonical Cipher cold-tier
    //     semantics.  Records are NEVER rewritten; every write is
    //     a tail append.  Relaxing to Mutable here would break the
    //     event-sourcing contract.
    cg::append_only,

    // 17. Reentrancy — STRICT (NonReentrant).  Cipher serializes
    //     appends through its own bg-thread queue; concurrent
    //     appenders would race on the tail offset.
    cf::accept_default_strict_for<cd::Reentrancy>,

    // 18. Size — Unstated strict default; record kinds vary in
    //     payload size.
    cf::accept_default_strict_for<cd::Size>,

    // 19. Version — RELAXED to 1 (cold-tier wire format v1).
    //     Downstream readers that pin Version=0 reject this binding,
    //     forcing a deliberate format bump.
    cg::version<1>,

    // 20. Staleness — STRICT (Fresh).  Each append carries a fresh
    //     epoch; stale appends are rejected at the tier-promotion
    //     boundary.  Relaxing to Stale<N> would silently admit
    //     out-of-order writes.
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Compile-time invariants ────────────────────────────────────────

static_assert(sizeof(BoundCipherWriter) == sizeof(CipherAppendPtr),
    "EBO collapse failed for Cipher cold-tier writer binding.");

// Discriminating axes — a downstream consumer that demands
// {Mutation=Append, Trust=Verified, Source=FromInternal} accepts ONLY
// Cipher-internal append paths, never user-supplied callables.
static_assert(BoundCipherWriter::mutation_v == fn::MutationMode::Append,
    "Cipher cold-tier MUST be append-only — the event-sourcing "
    "contract depends on it.");

static_assert(std::is_same_v<BoundCipherWriter::trust_t, fn::trust::Verified>,
    "Cipher persistence is CI-validated under "
    "bit_exact_recovery_invariant — Trust=Verified is load-bearing.");

static_assert(std::is_same_v<BoundCipherWriter::source_t,
                             ::crucible::safety::source::FromInternal>,
    "Cipher records originate from within the runtime; FromInternal "
    "is structural.");

static_assert(std::is_same_v<BoundCipherWriter::effect_row_t,
                             fx::Row<fx::Effect::Bg, fx::Effect::IO,
                                     fx::Effect::Block>>,
    "Cipher cold-tier declares Bg+IO+Block; row union with hot-path "
    "TUs must reject.");

static_assert(BoundCipherWriter::version_v == 1,
    "Cipher cold-tier wire format pinned at v1.");

// Theory-corpus interaction — the matcher classifies this binding
// against §30.14.  The Append-only / Bg / Verified / Fresh shape
// does NOT match any unsoundness pattern (the corpus's known shapes
// are M012 / N002 / S010 / S011 / I002 — none of which involve
// Append-only Mutation).
namespace ct = crucible::fixy::theory;
static_assert(ct::which_pattern_matches<
    typename BoundCipherWriter::underlying_fn_t>().empty(),
    "Cipher writer binding must not match any §30.14 corpus entry — "
    "a positive match here would indicate the binding has drifted "
    "into a known-unsoundness neighborhood.");

}  // namespace

int main() {
    BoundCipherWriter writer{append_cold_ref};

    CipherColdRecord record{
        .epoch         = 42,
        .content_hash  = 0xC1FE'B007'C01D'7E37ULL,
        .payload_bytes = 128,
        .kind_tag      = 0x4B45'524E  // 'KERN'
    };

    const bool ok = writer.value()(record);

    std::printf("fixy cipher_writer: append epoch=%lu hash=0x%016lx "
                "kind=0x%08x payload=%u → %s\n",
                static_cast<unsigned long>(record.epoch),
                static_cast<unsigned long>(record.content_hash),
                record.kind_tag, record.payload_bytes,
                ok ? "committed" : "REJECTED");

    std::printf("BoundCipherWriter sizeof = %zu "
                "(== sizeof(CipherAppendPtr) %zu) "
                "[20-dim grade vector, zero runtime cost]\n",
                sizeof(BoundCipherWriter), sizeof(CipherAppendPtr));
    return 0;
}
