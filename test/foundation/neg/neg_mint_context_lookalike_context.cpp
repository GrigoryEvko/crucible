// Deriving from the family base is not membership.  A class that
// derives from ContextBase, names a real key as its key_type and keeps
// a public default constructor is still not a context, because the
// factory's constraint reads the roster in Effect.h and the roster is
// the three classes that header lists.
//
// The key is never built here: forge::rvalue yields a reference to one
// without a constructor call, so the refusal is the constraint's and
// not the passkey's.
//
// VIOLATION: a TU mints a lookalike context with a real key.
//
// Expected diagnostic: no matching call to mint_context, with the
// IsContext atom of CanMintContext unsatisfied.

#include <foundation/effects/Effect.h>

#include <cstdlib>

namespace forge {
template <class T>
[[gnu::noinline]] T&& rvalue() noexcept {
    std::abort();
}
}  // namespace forge

namespace {
namespace fe = ::foundation::effects;

struct Lookalike final : fe::detail::ContextBase<Lookalike, fe::detail::ctx_mint::bg_key, fe::Effect::Bg, fe::Effect::Alloc> {
    constexpr Lookalike() noexcept = default;
};
}  // namespace

int main() {
    [[maybe_unused]] auto forged = fe::mint_context<Lookalike>(forge::rvalue<fe::detail::ctx_mint::bg_key>());
    return 0;
}
