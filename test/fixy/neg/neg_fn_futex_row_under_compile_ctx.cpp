// A context without Block refuses a binding that states a futex call.
//
// The binding states no Effect atom, so its Effect grade is the strict
// pole, the empty row.  The futex atom lifts Row<Block>, because the call
// can park the caller.  The row the binding requires joins the two, and
// the compile context holds Bg, Alloc and IO but not Block.  So the gate
// refuses the call at the call site, for the one effect the context
// lacks.
//
// test/fixy/test_fn.cpp holds the positive control: the load context,
// which holds Block, admits the same binding.

#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/atoms/Syscall.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <type_traits>

namespace {

namespace fe = ::foundation::effects;

using FutexBinding = ::fixy::fn<int, ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::futex>>;

// The row is not written here.  It is read off the binding.
template <class Ctx, class Bound>
    requires ::fixy::CtxAdmitsBinding<Ctx, Bound>
[[nodiscard]] int wait_on(Ctx const&, Bound const& bound) noexcept {
    return bound.value();
}

static_assert(std::is_same_v<::fixy::binding_row_t<FutexBinding>, fe::Row<fe::Effect::Block>>);

}  // namespace

int main() {
    ::fixy::BgCompileCtx const compile{fe::testing::bg()};
    const auto bound = ::fixy::mint_fn<int, ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::futex>>(3);
    return wait_on(compile, bound);
}
