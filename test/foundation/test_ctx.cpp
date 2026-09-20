// Sentinel TU for the execution context.  Ctx.h carries its own
// static_asserts and an inline smoke test; this file includes it, calls
// the smoke test, and keeps the runtime cells of the old
// test_effects.cpp that concern row membership through a context.

#include <foundation/effects/Ctx.h>

#include <type_traits>

namespace {

namespace fe = ::foundation::effects;
using fe::Effect;
using fe::detail::exec_ctx_self_test::BgWitness;
using fe::detail::exec_ctx_self_test::FgWitness;
using fe::detail::exec_ctx_self_test::InitWitness;

// The returned values carry no meaning.  What the calls prove is that
// the overloads resolve at all.
template <class Ctx>
    requires fe::CtxOwnsAnyOf<Ctx, Effect::Init, Effect::Bg>
[[nodiscard]] int needs_init_or_bg(Ctx const&) noexcept {
    return 42;
}

template <class Ctx>
    requires fe::CtxOwnsAllOf<Ctx, Effect::Bg, Effect::Alloc>
[[nodiscard]] int needs_bg_and_alloc(Ctx const&) noexcept {
    return 99;
}

// The rejection side asserts on the concept rather than on a call.  It
// is the same predicate the requires-clause consults.
static_assert(fe::CtxOwnsAnyOf<BgWitness, Effect::Init, Effect::Bg>);
static_assert(fe::CtxOwnsAnyOf<InitWitness, Effect::Init, Effect::Bg>);
static_assert(!fe::CtxOwnsAnyOf<FgWitness, Effect::Init, Effect::Bg>);
static_assert(fe::CtxOwnsAllOf<BgWitness, Effect::Bg, Effect::Alloc>);
static_assert(!fe::CtxOwnsAllOf<InitWitness, Effect::Bg, Effect::Alloc>);
static_assert(!fe::CtxOwnsAllOf<FgWitness, Effect::Bg>);

// A context is two empty axes and one byte.
static_assert(sizeof(fe::ExecCtx<fe::Bg, fe::Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>) == 1);

}  // namespace

int main() {
    fe::detail::exec_ctx_self_test::runtime_smoke_test();

    // Each context is handed the capability it claims (#172).
    BgWitness bg{fe::testing::bg()};
    InitWitness init{fe::testing::init()};
    if (needs_init_or_bg(bg) != 42) return 1;
    if (needs_init_or_bg(init) != 42) return 2;
    if (needs_bg_and_alloc(bg) != 99) return 3;

    // The borrowed capability is the source's own member, not a copy.
    fe::Bg const& held = bg.cap();
    [[maybe_unused]] fe::cap::Block block = held.block;
    return 0;
}
