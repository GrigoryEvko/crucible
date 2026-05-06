#include <crucible/concurrent/Pipeline.h>
#include <crucible/effects/ExecCtx.h>

#include <optional>
#include <type_traits>

namespace neg_pipeline_inline_opt_in_missing_ws {

template <typename T>
struct ConsumerWithoutWs {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct ProducerWithoutWs {
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

static void body(ConsumerWithoutWs<int>&&, ProducerWithoutWs<int>&&) noexcept {}

using StageT = ::crucible::concurrent::Stage<
    &body,
    ::crucible::effects::HotFgCtx>;

}  // namespace neg_pipeline_inline_opt_in_missing_ws

namespace crucible::concurrent {

template <>
struct stage_inline_safe<
    ::neg_pipeline_inline_opt_in_missing_ws::StageT> : std::true_type {};

}  // namespace crucible::concurrent

int main() {
    using BadPipeline = ::crucible::concurrent::Pipeline<
        ::neg_pipeline_inline_opt_in_missing_ws::StageT>;
    return static_cast<int>(sizeof(BadPipeline));
}
