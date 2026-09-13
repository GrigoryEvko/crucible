// Almost all coverage is static_asserts inside the headers. The runtime
// body here exists only so the harness has something to execute, and the
// file wires the context against the protocol combinators for a two-session
// scenario.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionContext.h>

#include <cstdio>
#include <type_traits>

namespace {

using namespace crucible::safety::proto;

struct TraceRingSession {};
struct KernelCacheSession {};

struct Producer {};
struct Consumer {};
struct Writer {};
struct Reader {};

struct TraceEntry {};
struct KernelEntry {};

using ProducerT = Loop<Send<TraceEntry, Continue>>;
using ConsumerT = Loop<Recv<TraceEntry, Continue>>;
using WriterT = Loop<Send<KernelEntry, Continue>>;
using ReaderT = Loop<Recv<KernelEntry, Continue>>;

using TraceRingGamma =
    Context<Entry<TraceRingSession, Producer, ProducerT>, Entry<TraceRingSession, Consumer, ConsumerT>>;

using KernelGamma = Context<Entry<KernelCacheSession, Writer, WriterT>, Entry<KernelCacheSession, Reader, ReaderT>>;

// The two contexts carry disjoint session tags, which is what makes the
// composition well defined.
using CombinedGamma = compose_context_t<TraceRingGamma, KernelGamma>;

static_assert(std::is_same_v<lookup_context_t<CombinedGamma, TraceRingSession, Producer>, ProducerT>);
static_assert(std::is_same_v<dual_of_t<lookup_context_t<CombinedGamma, TraceRingSession, Producer>>, ConsumerT>);

static_assert(std::is_same_v<dual_of_t<lookup_context_t<CombinedGamma, TraceRingSession, Producer>>,
                             lookup_context_t<CombinedGamma, TraceRingSession, Consumer>>);

// One send advances the protocol to Continue, which resolves back to the
// same loop body. So a completed iteration leaves the entry's local type
// exactly as it was. A loop that always continues is a fixed point here.
using AfterLoopIter = update_entry_t<CombinedGamma, TraceRingSession, Producer, ProducerT>;
static_assert(std::is_same_v<AfterLoopIter, CombinedGamma>);

// A real state change, by contrast: the producer's local type is swapped
// for a refined one and every other entry survives untouched.
using RefinedProducerT = Loop<Send<TraceEntry, End>>;  // single-iteration variant
using RefinedGamma = update_entry_t<CombinedGamma, TraceRingSession, Producer, RefinedProducerT>;
static_assert(std::is_same_v<lookup_context_t<RefinedGamma, TraceRingSession, Producer>, RefinedProducerT>);
static_assert(std::is_same_v<lookup_context_t<RefinedGamma, TraceRingSession, Consumer>,
                             ConsumerT>);  // consumer unchanged

// The writer's role is removed, as it would be after a crash. The reader
// survives, so three of the four keys remain.
using AfterWriterCrash = remove_entry_t<CombinedGamma, KernelCacheSession, Writer>;
static_assert(context_size_v<AfterWriterCrash> == 3);
static_assert(contains_key_v<AfterWriterCrash, TraceRingSession, Producer>);
static_assert(contains_key_v<AfterWriterCrash, TraceRingSession, Consumer>);
static_assert(!contains_key_v<AfterWriterCrash, KernelCacheSession, Writer>);
static_assert(contains_key_v<AfterWriterCrash, KernelCacheSession, Reader>);

// The invariant is that every key in the domain is contained in the
// context. It is sampled here at the four known keys.
static_assert(contains_key_v<CombinedGamma, TraceRingSession, Producer>);
static_assert(contains_key_v<CombinedGamma, TraceRingSession, Consumer>);
static_assert(contains_key_v<CombinedGamma, KernelCacheSession, Writer>);
static_assert(contains_key_v<CombinedGamma, KernelCacheSession, Reader>);

int run_context_size_check() {
    if (context_size_v<CombinedGamma> != 4) return 1;
    if (context_size_v<EmptyContext> != 0) return 1;
    if (!is_empty_context_v<EmptyContext>) return 1;
    if (is_empty_context_v<CombinedGamma>) return 1;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_context_size_check(); rc != 0) return rc;
    std::puts("session_context: Γ structure + compose + lookup + update + remove OK");
    return 0;
}
