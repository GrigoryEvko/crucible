// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The compile-time inline claim over a pipeline whose stages never opted
// in to inline dispatch.  The working set is tiny, so the size cannot be
// what refuses; the claim answers false on the missing opt-in alone,
// which is the axis the sibling fixture holds fixed.  As there, the
// claim is the constraint of a function template, so the compiler spells
// the pipeline it refused.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <cstddef>
#include <optional>

namespace {

namespace cc = fixy::concurrent;

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;

template <std::size_t Ws>
struct TinyConsumer {
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 1; }
};

template <std::size_t Ws>
struct TinyProducer {
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

inline void tiny(TinyConsumer<128>&&, TinyProducer<128>&&) noexcept {}

using TinyStage = cc::Stage<&tiny, fixy::HotFgCtx>;

// Deliberately no stage_inline_safe specialisation for TinyStage.

using TinyPipeline = cc::Pipeline<TinyStage, TinyStage, TinyStage>;

static_assert(!TinyPipeline::inline_safe);
static_assert(TinyPipeline::aggregate_working_set_known);

template <class P>
    requires(P::template will_run_inline_v<32 * KiB, 1 * MiB>())
void claim_inline() noexcept {}

}  // namespace

int main() {
    claim_inline<TinyPipeline>();
    return 0;
}
