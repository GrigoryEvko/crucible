#pragma once

// The named execution contexts of the runtime, as production types.
// Each is one capability source and one effect row, the two axes of
// the context in foundation/effects/Ctx.h.
//
// foundation/effects/Ctx.h states each row one time, in its record of
// the named contexts (namespace detail::ctx_witnesses), with the reason
// for each row.  Each name here is that record, so the production name
// and the recorded row are one declaration and cannot drift apart.
// The two load contexts each claim Block on top of the row of another
// context.
//
// A context describes the surrounding scope, not a value.  A claim
// about where memory lands or how hot the path is rides on the value,
// through the band wrappers.  A channel or a fork that wants a budget
// takes it as its own parameter.
//
// No context builds from nothing.  Each is handed the capability it
// claims, because a context is not evidence of a capability: it carries
// one, and the one it carries came from mint_context or, for the
// foreground, from the key that the producer claim holds.  The check
// file of foundation/effects/Ctx.h proves the properties of each row of
// its record, and test/fixy/test_contexts.cpp proves them again through
// these names.

#include <foundation/effects/Ctx.h>

namespace fixy {

// The context of the foreground thread that runs dispatch.
using HotFgCtx = ::foundation::effects::detail::ctx_witnesses::HotFgCtx;

// The background drain context: Bg and Alloc.
using BgDrainCtx = ::foundation::effects::detail::ctx_witnesses::BgDrainCtx;

// The background compile context: the drain row and IO.
using BgCompileCtx = ::foundation::effects::detail::ctx_witnesses::BgCompileCtx;

// The background load context: the compile row and Block.
using BgLoadCtx = ::foundation::effects::detail::ctx_witnesses::BgLoadCtx;

// The context of process startup, before the threads are pinned.
using ColdInitCtx = ::foundation::effects::detail::ctx_witnesses::ColdInitCtx;

// The startup load context: the cold init row and Block.
using InitLoadCtx = ::foundation::effects::detail::ctx_witnesses::InitLoadCtx;

// The context of a test fixture: Test, Alloc, IO and Block.
using TestRunnerCtx = ::foundation::effects::detail::ctx_witnesses::TestRunnerCtx;

}  // namespace fixy
