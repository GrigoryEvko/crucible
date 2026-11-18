// A spin that ends at a budget of turns reports the give-up to its caller,
// and the outcome depends on the timing of another thread.  So the atom
// bounded_spin lifts to Row<Nondet>, and a context with the empty row, the
// row of the foreground, does not admit the binding.

#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/atoms/Sync.h>
#include <foundation/effects/Ctx.h>

namespace {

namespace fe = ::foundation::effects;

template <class Ctx, class Bound>
    requires ::fixy::CtxAdmitsBinding<Ctx, Bound>
[[nodiscard]] int spin_on(Ctx const&, Bound const& bound) noexcept {
    return bound.value();
}

}  // namespace

int main() {
    const auto bound = ::fixy::mint_fn<int, ::fixy::atom::sync::bounded_spin>(3);
    return spin_on(fe::testing::foreground(), bound);
}
