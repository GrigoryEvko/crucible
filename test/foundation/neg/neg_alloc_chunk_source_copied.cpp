// Negative-compile test.  This file must fail to compile.
//
// A copy of a chunk source would be a second set of the commit words of one
// window, and the claims of the two would give one chunk two owners.
//
// Expected diagnostic: the deleted copy constructor of the source.

#include <foundation/alloc/Window.h>
#include <foundation/core/Ref.h>
#include <foundation/os/PageSource.h>

#include <cstddef>

namespace alloc = ::foundation::alloc;
namespace core = ::foundation::core;
namespace os = ::foundation::os;

int main() {
    alloc::WindowDiagnostic diagnostic{};
    alloc::ChunkSource source{
        alloc::reserve_window(os::ReserveCap{std::size_t{1} << 30}, core::to(diagnostic)).expect("a window of 1 GiB")};
    alloc::ChunkSource second{source};
    return source.claim_chunk().is_some() && second.claim_chunk().is_some() ? 1 : 0;
}
