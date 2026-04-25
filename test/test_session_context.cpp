// Runtime harness for L2 typing context Γ (task #343, SEPLOG-I1).
// Almost all coverage is in-header static_asserts; this file adds a
// lightweight runtime exercise so ctest has an executable to run,
// and demonstrates wiring Γ against the L1 combinators from Session.h
// for a realistic two-session scenario.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionContext.h>

#include <cstdio>
#include <type_traits>

namespace {

using namespace crucible::safety::proto;

// ── Two fixture sessions: TraceRing and KernelCache ────────────────

struct TraceRingSession     {};
struct KernelCacheSession   {};

struct Producer  {};
struct Consumer  {};
struct Writer    {};
struct Reader    {};

struct TraceEntry   {};
struct KernelEntry  {};

// L1 combinator-built local types
using ProducerT = Loop<Send<TraceEntry, Continue>>;
using ConsumerT = Loop<Recv<TraceEntry, Continue>>;
using WriterT   = Loop<Send<KernelEntry, Continue>>;
using ReaderT   = Loop<Recv<KernelEntry, Continue>>;

// Each session's Γ
using TraceRingGamma = Context<
    Entry<TraceRingSession, Producer, ProducerT>,
    Entry<TraceRingSession, Consumer, ConsumerT>>;

using KernelGamma = Context<
    Entry<KernelCacheSession, Writer, WriterT>,
    Entry<KernelCacheSession, Reader, ReaderT>>;

// Composed Γ covers both sessions — disjoint tags, no overlap.
using CombinedGamma = compose_context_t<TraceRingGamma, KernelGamma>;

// ── Integration with Session.h: lookup returns a usable session type ──

static_assert(
    std::is_same_v<
        lookup_context_t<CombinedGamma, TraceRingSession, Producer>,
        ProducerT>);
static_assert(
    std::is_same_v<
        dual_of_t<lookup_context_t<CombinedGamma, TraceRingSession, Producer>>,
        ConsumerT>);  // producer's dual IS consumer's local type

// The dual of the producer's LOCAL type equals the consumer's LOCAL
// type — a basic sanity check that Γ + L1 combinators line up.
static_assert(
    std::is_same_v<
        dual_of_t<lookup_context_t<CombinedGamma, TraceRingSession, Producer>>,
        lookup_context_t<CombinedGamma, TraceRingSession, Consumer>>);

// ── Simulating one step of reduction via update_entry_t ───────────

// The producer's initial position is Loop<Send<TraceEntry, Continue>>.
// After one Send in Session.h the handle's protocol advances to
// Continue → resolved via LoopCtx → back to Send<TraceEntry, Continue>
// (same loop body).  At the Γ level, after the producer completes one
// loop iteration the entry's local_type is unchanged — a fixed-point
// property of Loop<...Continue> protocols.
using AfterLoopIter = update_entry_t<
    CombinedGamma, TraceRingSession, Producer, ProducerT>;
static_assert(std::is_same_v<AfterLoopIter, CombinedGamma>);

// A genuine state change: upgrade the producer's local type to a
// refined version (e.g., post-subtype check).  The update preserves
// other entries while swapping the producer's type.
using RefinedProducerT = Loop<Send<TraceEntry, End>>;  // single-iteration variant
using RefinedGamma = update_entry_t<
    CombinedGamma, TraceRingSession, Producer, RefinedProducerT>;
static_assert(
    std::is_same_v<
        lookup_context_t<RefinedGamma, TraceRingSession, Producer>,
        RefinedProducerT>);
static_assert(
    std::is_same_v<
        lookup_context_t<RefinedGamma, TraceRingSession, Consumer>,
        ConsumerT>);  // consumer unchanged

// ── Simulating crash: remove_entry_t removes a role from Γ ─────────

// KernelCache's Writer crashes; the Reader survives.  Remove Writer
// from Γ; Reader remains.
using AfterWriterCrash =
    remove_entry_t<CombinedGamma, KernelCacheSession, Writer>;
static_assert(context_size_v<AfterWriterCrash> == 3);
static_assert( contains_key_v<AfterWriterCrash, TraceRingSession,   Producer>);
static_assert( contains_key_v<AfterWriterCrash, TraceRingSession,   Consumer>);
static_assert(!contains_key_v<AfterWriterCrash, KernelCacheSession, Writer>);
static_assert( contains_key_v<AfterWriterCrash, KernelCacheSession, Reader>);

// ── Domain iteration: every key in the domain is contained in Γ ───

// This is a pure type-level invariant: ∀ Key<S, R> ∈ domain_of_t<Γ>,
// contains_key_v<Γ, S, R>.  We verify for the four known keys:
static_assert(contains_key_v<CombinedGamma, TraceRingSession,   Producer>);
static_assert(contains_key_v<CombinedGamma, TraceRingSession,   Consumer>);
static_assert(contains_key_v<CombinedGamma, KernelCacheSession, Writer>);
static_assert(contains_key_v<CombinedGamma, KernelCacheSession, Reader>);

// ── Runtime smoke ──────────────────────────────────────────────────

int run_context_size_check() {
    // Trivial runtime exercise to give ctest an executable to run.
    // The real coverage is the static_asserts above.
    if (context_size_v<CombinedGamma> != 4) return 1;
    if (context_size_v<EmptyContext>  != 0) return 1;
    if (!is_empty_context_v<EmptyContext>)  return 1;
    if ( is_empty_context_v<CombinedGamma>) return 1;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_context_size_check(); rc != 0) return rc;
    std::puts("session_context: Γ structure + compose + lookup + update + remove OK");
    return 0;
}
