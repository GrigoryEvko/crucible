// ── test_fixy_call_with_caps — FIXY-G3 positive test ──────────────────
//
// Pin call_with_caps + caps_cover_v + CtxCallable for:
//   * Binding declaring Effect=Row<IO>; caller minting CapsPack<cap::IO>
//     covers; call succeeds.
//   * Binding declaring Effect=Row<Alloc, IO>; caller passing
//     CapsPack<cap::Alloc, cap::IO> covers; succeeds.
//   * BgCtx-shaped stub with a .caps() accessor satisfies CtxCallable.

#include <crucible/fixy/Fixy.h>

#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;

namespace {

using LoggerPtr = void(*)(int);

void logger_impl(int code) noexcept {
    (void)code;
}

// Binding 1: Effect=Row<IO> only.
using IoLoggerFn = cf::fn<LoggerPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<fx::Effect::IO>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// Binding 2: Effect=Row<Alloc, IO> — more caps required.
using AllocIoFn = cf::fn<LoggerPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<fx::Effect::Alloc, fx::Effect::IO>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// Bg-shaped context stub carrying CapsPack<cap::Alloc, cap::IO, cap::Block>.
struct StubBgCtx {
    [[nodiscard]] constexpr auto caps() const noexcept {
        return cf::CapsPack<fx::cap::Alloc, fx::cap::IO, fx::cap::Block>{};
    }
};

// Compile-time invariants.
using IoOnlyCaps    = cf::CapsPack<fx::cap::IO>;
using AllocIoCaps   = cf::CapsPack<fx::cap::Alloc, fx::cap::IO>;
using FullBgCaps    = cf::CapsPack<fx::cap::Alloc, fx::cap::IO, fx::cap::Block>;

static_assert(cf::caps_cover_v<IoOnlyCaps,  typename IoLoggerFn::effect_row_t>);
static_assert(cf::caps_cover_v<AllocIoCaps, typename AllocIoFn::effect_row_t>);
static_assert(cf::caps_cover_v<FullBgCaps,  typename IoLoggerFn::effect_row_t>);
static_assert(cf::caps_cover_v<FullBgCaps,  typename AllocIoFn::effect_row_t>);
static_assert(!cf::caps_cover_v<IoOnlyCaps, typename AllocIoFn::effect_row_t>,
    "IO-only caps cannot cover {Alloc, IO} row.");

static_assert(cf::CtxCallable<IoLoggerFn, StubBgCtx>);
static_assert(cf::CtxCallable<AllocIoFn,  StubBgCtx>);

}  // namespace

int main() {
    IoLoggerFn io{&logger_impl};
    AllocIoFn  ai{&logger_impl};

    cf::call_with_caps<IoLoggerFn>(io, IoOnlyCaps{}, 42);
    cf::call_with_caps<AllocIoFn>(ai, AllocIoCaps{}, 7);

    StubBgCtx ctx{};
    cf::call_with_ctx<IoLoggerFn>(io, ctx, 123);
    cf::call_with_ctx<AllocIoFn>(ai, ctx, 99);

    std::fputs("test_fixy_call_with_caps: OK\n", stdout);
    return 0;
}
