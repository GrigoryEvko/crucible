// Negative-compile test.  This file must fail to compile.
//
// The words of the commits are in the chunk source of the window, and each
// door that changes one is a member of the source.  So a window alone gives no
// chunk, and no code pairs a window with the words of a second window.
//
// Expected diagnostic: a window has no door that claims a chunk.

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
    return window.claim_chunk().is_some() ? 1 : 0;
}
