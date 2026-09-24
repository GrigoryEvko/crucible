// A context without IO refuses a binding that states an IO system call.
//
// getpid reads the state of the process, and fixy/atoms/Syscall.h lifts
// that family to Row<IO>.  The binding states no Effect atom, so the row
// it requires is the lift alone.  The drain context holds Bg and Alloc
// but not IO, so the gate refuses the call at the call site, for the one
// effect the context lacks.  The binding names as_public, so the corpus
// has no classified channel to refuse and the gate is the only refusal.
//
// test/fixy/test_fn.cpp holds the positive control: the compile context,
// which holds IO, admits the same binding.

#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/atoms/Syscall.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <type_traits>

namespace {

namespace fe = ::foundation::effects;

using ProcessStateRead = ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::getpid>;
using ProcessStateBinding = ::fixy::fn<int, ProcessStateRead, ::fixy::atom::as_public>;

// The row is not written here.  It is read off the binding.
template <class Ctx, class Bound>
    requires ::fixy::CtxAdmitsBinding<Ctx, Bound>
[[nodiscard]] int read_process_state(Ctx const&, Bound const& bound) noexcept {
    return bound.value();
}

static_assert(std::is_same_v<::fixy::binding_row_t<ProcessStateBinding>, fe::Row<fe::Effect::IO>>);

}  // namespace

int main() {
    ::fixy::BgDrainCtx const drain{fe::testing::bg()};
    const auto bound = ::fixy::mint_fn<int, ProcessStateRead, ::fixy::atom::as_public>(5);
    return read_process_state(drain, bound);
}
