// Written by `loop derive witness-fixtures --write` from the witness row of
// fixy::concurrent::PipelineDag<fixy::concurrent::StageGraph<fixy::concurrent::StagePack<fixy::concurrent::Stage<&fixy::concurrent::detail::stage_witness::stage_pass_through, fixy::HotFgCtx>, fixy::concurrent::Stage<&fixy::concurrent::detail::stage_witness::stage_pass_through, fixy::HotFgCtx>>, fixy::concurrent::EdgePack<fixy::concurrent::StageEdge<0, 1>>>> in utils/rule-table.txt.  Edit the row, not this file.
// clang-format off: the writer owns this layout, and --check compares it byte for byte.
//
// fixy::concurrent::PipelineDag<fixy::concurrent::StageGraph<fixy::concurrent::StagePack<fixy::concurrent::Stage<&fixy::concurrent::detail::stage_witness::stage_pass_through, fixy::HotFgCtx>, fixy::concurrent::Stage<&fixy::concurrent::detail::stage_witness::stage_pass_through, fixy::HotFgCtx>>, fixy::concurrent::EdgePack<fixy::concurrent::StageEdge<0, 1>>>> attests to a fact that it cannot see.  The expression below is
// the raw data that a forger gives its constructor, and the fixture keeps the door
// shut: the compiler must refuse the construction with "is private within this context".
//
// The second diagnostic is the constructor as the compiler writes it, with
// parentheses.  The source spells it with braces, so the text of the fixture
// cannot match it.

#include <fixy/concurrent/Pipeline.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace forge {
// Stand-ins for the values that a forger must give.  Neither returns: the
// fixture compiles, and it never runs.
template <class T> [[gnu::noinline]] T& lvalue() noexcept { std::abort(); }
template <class T> [[gnu::noinline]] T&& rvalue() noexcept { std::abort(); }
}  // namespace forge

int main() {
    [[maybe_unused]] auto forged = fixy::concurrent::PipelineDag<fixy::concurrent::StageGraph<fixy::concurrent::StagePack<fixy::concurrent::Stage<&fixy::concurrent::detail::stage_witness::stage_pass_through, fixy::HotFgCtx>, fixy::concurrent::Stage<&fixy::concurrent::detail::stage_witness::stage_pass_through, fixy::HotFgCtx>>, fixy::concurrent::EdgePack<fixy::concurrent::StageEdge<0, 1>>>>{forge::rvalue<fixy::concurrent::Stage<&fixy::concurrent::detail::stage_witness::stage_pass_through, fixy::HotFgCtx>>(), forge::rvalue<fixy::concurrent::Stage<&fixy::concurrent::detail::stage_witness::stage_pass_through, fixy::HotFgCtx>>()};
    return 0;
}
