// A body that emits through an I/O channel runs only where the
// surrounding context admits that effect.  The foreground context claims
// the empty row — its capability source permits nothing else — so a
// binding that declared IO cannot be called with it, and the refusal is
// at the call site rather than inside the body.
//
// The gate is lifted from the binding's own Effect grade.  It used to be
// hand-spelled Row<IO>, because atom::with<Es...> carried no `lifts_to`
// and nothing mapped the Effect axis's grade to a row for context
// admission: the row nobody computed was the empty row, the empty row is
// a Subrow of every context's, and a caller who wanted the gate wrote
// the row a second time.  Writing it twice is what made it a gap rather
// than a rule — the second spelling can disagree with the first, and
// nothing compares them.
//
// Now fixy/Atom.h's closed relation answers for both shapes the grade
// takes, the stated with<Es...> and the bare Row the strict pole leaves
// behind, and fixy/Fn.h's CtxAdmitsBinding reads it.  The role names IO
// once, in the binding.  The eight neg_os_*_ctx_lacks_* fixtures cover
// the same lift on the syscall atoms, and
// neg_os_fs_mint_file_with_atom_widens_required_row covers the direction
// this change points the other way.

#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/Role.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace {

namespace fe = ::foundation::effects;

// The row is not written here.  It is read off the binding.
template <class Ctx, class Bound>
    requires ::fixy::CtxAdmitsBinding<Ctx, Bound>
[[nodiscard]] int emit(Ctx const&, Bound const& bound) noexcept {
    return bound.value();
}

// The claim the gate rests on, stated where a reader of the refusal can
// check it: the role declares IO, and the foreground context admits
// nothing.
static_assert(std::is_same_v<::fixy::binding_row_t<::fixy::role::IoFunction<int>>, fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<fe::row_type_of_t<::fixy::HotFgCtx>, fe::Row<>>);

}  // namespace

int main() {
    const auto emitted = ::fixy::mint_fn_for<::fixy::role::IoFunction>(7);
    return emit(::fixy::HotFgCtx{}, emitted);
}
