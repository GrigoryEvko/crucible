// Negative compile test.  This file must fail to compile.
//
// A graph of one stage.  Its input is an input end of the graph, and the
// handle of that end states no capacity.  The gate of the graph holds, but
// the marked system cannot give the channel of the end a capacity, so
// marked_system_of_stage_graph refuses the graph with a text that names the
// port.

#include <fixy/Ctx.h>
#include <fixy/concurrent/StageNet.h>

#include <cstddef>

namespace {

namespace cc = fixy::concurrent;

template <typename T>
struct FakeChannel {};

// The handle of the input end states no channel_capacity.
template <typename T>
struct UnstatedConsumer {
    using channel_type = FakeChannel<T>;
    [[nodiscard]] ::foundation::core::Option<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct SizedProducer {
    using channel_type = FakeChannel<T>;
    static constexpr std::size_t channel_capacity = 1;
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

inline void body(UnstatedConsumer<int>&&, SizedProducer<int>&&) noexcept {}

using Stage = cc::Stage<&body, fixy::HotFgCtx>;
using UnstatedInputGraph = cc::StageGraph<cc::StagePack<Stage>, cc::EdgePack<>>;

[[maybe_unused]] constexpr auto bad_system = cc::marked_system_of_stage_graph<UnstatedInputGraph>();

}  // namespace

int main() { return 0; }
