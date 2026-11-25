// Negative-compile test.  This file must fail to compile.
//
// A chunk source holds the one set of the commit words of its window.  A second
// source over the same window would start with no chunk claimed, so it would
// give a chunk that the first source gave, and two heaps would write one chunk.
// A source takes a window only by move, so no two sources take one window.
//
// Expected diagnostic: the deleted constructor that takes a borrowed window.

#include <foundation/alloc/Window.h>
#include <foundation/core/Ref.h>
#include <foundation/os/PageSource.h>

#include <cstddef>

namespace alloc = ::foundation::alloc;
namespace core = ::foundation::core;
namespace os = ::foundation::os;

int main() {
    alloc::WindowDiagnostic diagnostic{};
    alloc::Window window =
        alloc::reserve_window(os::ReserveCap{std::size_t{1} << 30}, core::to(diagnostic)).expect("a window of 1 GiB");
    alloc::ChunkSource source{window};
    return source.claim_chunk().is_some() ? 1 : 0;
}
