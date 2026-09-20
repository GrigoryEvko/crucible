// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A stage opted in to inline dispatch whose handles declare no per-call
// working set.  The opt-in claims the working set fits a private cache,
// and a handle that states no working set cannot support that claim, so
// the pipeline refuses to form.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <optional>
#include <type_traits>

namespace {

template <typename T>
struct ConsumerWithoutWs {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct ProducerWithoutWs {
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

inline void body(ConsumerWithoutWs<int>&&, ProducerWithoutWs<int>&&) noexcept {}

using StageT = fixy::concurrent::Stage<&body, fixy::HotFgCtx>;

}  // namespace

namespace fixy::concurrent {

template <>
struct stage_inline_safe<StageT> : std::true_type {};

}  // namespace fixy::concurrent

int main() {
    using BadPipeline = fixy::concurrent::Pipeline<StageT>;
    return static_cast<int>(sizeof(BadPipeline));
}
