// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The compile-time inline claim, asked of a stated cache size, over a
// pipeline whose working set exceeds it.  Five stages of ten mebibytes
// per handle sum to one hundred mebibytes, above both the 32 KiB L1d and
// the 1 MiB L2 the claim states.  The stages ARE opted in to inline
// dispatch, so the refusal is the size and not the missing opt-in, which
// is the sibling fixture's axis.  The claim is the constraint of a
// function template, so the compiler spells the pipeline and the two
// sizes it was asked about in its satisfaction note.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <cstddef>
#include <optional>
#include <type_traits>

namespace {

namespace cc = fixy::concurrent;

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;

template <std::size_t Ws>
struct HeavyConsumer {
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 1; }
};

template <std::size_t Ws>
struct HeavyProducer {
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

inline void heavy(HeavyConsumer<10 * MiB>&&, HeavyProducer<10 * MiB>&&) noexcept {}

using HeavyStage = cc::Stage<&heavy, fixy::HotFgCtx>;

}  // namespace

namespace fixy::concurrent {

template <>
struct stage_inline_safe<HeavyStage> : std::true_type {};

}  // namespace fixy::concurrent

namespace {

using HugePipeline = cc::Pipeline<HeavyStage, HeavyStage, HeavyStage, HeavyStage, HeavyStage>;

static_assert(HugePipeline::aggregate_per_call_working_set == 100 * MiB);
static_assert(HugePipeline::inline_safe);
static_assert(HugePipeline::aggregate_working_set_known);

// A site that runs a pipeline on the hot path and states the cache it
// was built for.
template <class P>
    requires(P::template will_run_inline_v<32 * KiB, 1 * MiB>())
void claim_inline() noexcept {}

}  // namespace

int main() {
    claim_inline<HugePipeline>();
    return 0;
}
