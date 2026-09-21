#pragma once

// The five named execution contexts of the runtime, as production
// types.  Each is one capability source and one effect row, the two
// axes foundation/effects/Ctx.h kept of the eight the old context
// carried.  The rows are the old tree's, copied from
// include/crucible/effects/_ExecCtx.h:509-531, and each is pinned
// below against the witness foundation recorded for the same shape,
// so the production name and the recorded row cannot drift apart.
//
// A context describes the surrounding scope, not a value.  Where the
// old aliases also said where memory lands, how hot the path is and
// what it promises about termination, those claims ride on the value
// through the band wrappers now, and a channel or a fork that wants a
// budget takes it as its own parameter.
//
// Only the foreground context builds from nothing.  Each of the others
// is handed the capability it claims, because a context is not
// evidence of a capability: it carries one, and the one it carries came
// from mint_bg_context, mint_init_context or mint_test_context.
//
// Old spelling: include/crucible/effects/_ExecCtx.h (HotFgCtx,
// BgDrainCtx, BgCompileCtx, ColdInitCtx, TestRunnerCtx).

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <type_traits>

namespace fixy {

// The context of the foreground thread that runs dispatch.
using HotFgCtx = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::Fg, ::foundation::effects::Row<>>;

using BgDrainCtx =
    ::foundation::effects::ExecCtx<::foundation::effects::Bg, ::foundation::effects::Row<::foundation::effects::Effect::Bg,
                                                                                         ::foundation::effects::Effect::Alloc>>;

// The compile context claims IO on top of the drain row, because
// compiling writes kernel artifacts.
using BgCompileCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Bg, ::foundation::effects::Row<::foundation::effects::Effect::Bg,
                                                          ::foundation::effects::Effect::Alloc,
                                                          ::foundation::effects::Effect::IO>>;

// The load context claims Block on top of the compile row.  Work that
// enters the kernel and waits there, such as a BPF program load waiting
// on the verifier, needs the atom the other two background rows omit.
using BgLoadCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Bg,
    ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Alloc,
                               ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>>;

// The context of process startup, before the threads are pinned.
using ColdInitCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Init, ::foundation::effects::Row<::foundation::effects::Effect::Init,
                                                            ::foundation::effects::Effect::Alloc,
                                                            ::foundation::effects::Effect::IO>>;

// A fixture may claim any effect this row names, and no others.  In
// particular it cannot claim the background or initialization effects,
// so it cannot stand in for either of those contexts.
using TestRunnerCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Test,
    ::foundation::effects::Row<::foundation::effects::Effect::Test, ::foundation::effects::Effect::Alloc,
                               ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>>;

// ---------------------------------------------------------------------
// The header proves its own claims here.

namespace detail::ctx_self_test {

namespace fe = ::foundation::effects;

// Each production context is the shape foundation recorded for it, so
// a rewrite of either side reddens here.
static_assert(std::is_same_v<HotFgCtx, fe::detail::ctx_witnesses::HotFgCtx>);
static_assert(std::is_same_v<BgDrainCtx, fe::detail::ctx_witnesses::BgDrainCtx>);
static_assert(std::is_same_v<BgCompileCtx, fe::detail::ctx_witnesses::BgCompileCtx>);
static_assert(std::is_same_v<BgLoadCtx, fe::detail::ctx_witnesses::BgLoadCtx>);
static_assert(std::is_same_v<ColdInitCtx, fe::detail::ctx_witnesses::ColdInitCtx>);
static_assert(std::is_same_v<TestRunnerCtx, fe::detail::ctx_witnesses::TestRunnerCtx>);

// Both axes of a context are empty types, so each is one byte.
static_assert(sizeof(HotFgCtx) == 1, "The foreground context must be 1 byte");
static_assert(sizeof(BgDrainCtx) == 1, "The background drain context must be 1 byte");
static_assert(sizeof(BgCompileCtx) == 1, "The background compile context must be 1 byte");
static_assert(sizeof(BgLoadCtx) == 1, "The background load context must be 1 byte");
static_assert(sizeof(ColdInitCtx) == 1, "The initialization context must be 1 byte");
static_assert(sizeof(TestRunnerCtx) == 1, "The test runner context must be 1 byte");

// The rows and the sources, restated as the old self-test stated them.
static_assert(std::is_same_v<typename HotFgCtx::cap_type, fe::ctx_cap::Fg>);
static_assert(std::is_same_v<typename HotFgCtx::row_type, fe::Row<>>);
static_assert(std::is_same_v<typename BgDrainCtx::cap_type, fe::Bg>);
static_assert(std::is_same_v<typename BgDrainCtx::row_type, fe::Row<fe::Effect::Bg, fe::Effect::Alloc>>);
static_assert(std::is_same_v<typename BgCompileCtx::row_type, fe::Row<fe::Effect::Bg, fe::Effect::Alloc, fe::Effect::IO>>);
static_assert(std::is_same_v<typename ColdInitCtx::cap_type, fe::Init>);
static_assert(std::is_same_v<typename TestRunnerCtx::cap_type, fe::Test>);

// The aliases already satisfy this, or their own declarations would
// have failed.  Restating it here catches a later rewrite of an alias.
static_assert(fe::Subrow<typename HotFgCtx::row_type, fe::cap_permitted_row_t<typename HotFgCtx::cap_type>>);
static_assert(fe::Subrow<typename BgDrainCtx::row_type, fe::cap_permitted_row_t<typename BgDrainCtx::cap_type>>);
static_assert(fe::Subrow<typename BgCompileCtx::row_type, fe::cap_permitted_row_t<typename BgCompileCtx::cap_type>>);
static_assert(fe::Subrow<typename ColdInitCtx::row_type, fe::cap_permitted_row_t<typename ColdInitCtx::cap_type>>);
static_assert(fe::Subrow<typename TestRunnerCtx::row_type, fe::cap_permitted_row_t<typename TestRunnerCtx::cap_type>>);

// What each context admits.
static_assert(fe::CtxAdmits<HotFgCtx, fe::Row<>>);
static_assert(!fe::CtxAdmits<HotFgCtx, fe::Row<fe::Effect::Bg>>);
static_assert(fe::CtxAdmits<BgDrainCtx, fe::Row<fe::Effect::Bg>>);
static_assert(fe::CtxAdmits<BgDrainCtx, fe::Row<fe::Effect::Bg, fe::Effect::Alloc>>);
static_assert(!fe::CtxAdmits<BgDrainCtx, fe::Row<fe::Effect::IO>>);
static_assert(fe::CtxAdmits<BgCompileCtx, fe::Row<fe::Effect::IO>>);
static_assert(fe::CtxAdmits<TestRunnerCtx, fe::Row<fe::Effect::Block>>);

// What each context owns, and what its source could still authorize.
// The drain context claims two effects but its source permits four,
// so the two groups differ.
static_assert(fe::CtxOwnsCapability<BgDrainCtx, fe::Effect::Bg>);
static_assert(fe::CtxOwnsCapability<BgDrainCtx, fe::Effect::Alloc>);
static_assert(!fe::CtxOwnsCapability<BgDrainCtx, fe::Effect::IO>);
static_assert(fe::CtxOwnsCapability<BgCompileCtx, fe::Effect::IO>);
static_assert(!fe::CtxOwnsCapability<HotFgCtx, fe::Effect::Bg>);

static_assert(fe::CtxCanMint<BgDrainCtx, fe::Effect::Alloc>);
static_assert(fe::CtxCanMint<BgDrainCtx, fe::Effect::IO>);
static_assert(fe::CtxCanMint<BgDrainCtx, fe::Effect::Block>);
static_assert(fe::CtxCanMint<BgDrainCtx, fe::Effect::Bg>);
static_assert(!fe::CtxCanMint<BgDrainCtx, fe::Effect::Init>);
static_assert(fe::CtxCanMint<BgCompileCtx, fe::Effect::Block>);
static_assert(fe::CtxCanMint<ColdInitCtx, fe::Effect::Alloc>);
static_assert(fe::CtxCanMint<ColdInitCtx, fe::Effect::IO>);
static_assert(!fe::CtxCanMint<ColdInitCtx, fe::Effect::Block>);
static_assert(!fe::CtxCanMint<HotFgCtx, fe::Effect::Alloc>);
static_assert(!fe::CtxCanMint<HotFgCtx, fe::Effect::Bg>);
static_assert(fe::CtxCanMint<TestRunnerCtx, fe::Effect::Block>);

// The foreground context builds from nothing; every other one is
// handed its capability.
static_assert(std::is_default_constructible_v<HotFgCtx>);
static_assert(!std::is_default_constructible_v<BgDrainCtx>);
static_assert(!std::is_default_constructible_v<BgCompileCtx>);
static_assert(!std::is_default_constructible_v<ColdInitCtx>);
static_assert(!std::is_default_constructible_v<TestRunnerCtx>);

}  // namespace detail::ctx_self_test

}  // namespace fixy
