// D001: an indirect call whose family names no signature.
//
// The family is an opaque tag class.  It names no function type, so no
// one can read whether the callee throws.  A function declared with no
// noexcept-specifier is potentially-throwing ([except.spec]), and the
// rule reads an unknown callee the same way.  The remedy is to name the
// signature: a noexcept function pointer type, or a member type
// `signature` of the tag class.  The pack trips D001 alone.

#include <fixy/Fn.h>

namespace {
struct region_ready_family final {};
}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::dispatch::indirect_call<region_ready_family>> refused{};
    return 0;
}
