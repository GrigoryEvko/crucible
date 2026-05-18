// ── test_fixy_umbrella — sentinel TU for crucible/Fixy.h ───────────
//
// Pulls the entire fixy:: surface via the umbrella header into one
// TU compiled under project warning flags.  This proves:
//
//   1. Every fixy header compiles cleanly together (no ODR clash,
//      no using-declaration ambiguity, no header-order dependency).
//   2. CRUCIBLE_FIXY=1 is defined after the include.
//   3. The umbrella exposes every minter family — Cap, Perm, Sess,
//      Pipe, Bridge, Substr (per-substrate variants), Mach, Safety,
//      plus Fn — through one well-known include path.
//
// Failure mode: if a future fixy/*.h adds a `using` that collides
// with another fixy/*.h, this TU surfaces the collision under
// -Werror.  Any future addition to fixy/ MUST keep this TU green.

#include <crucible/Fixy.h>

#include <type_traits>

#ifndef CRUCIBLE_FIXY
#  error "crucible/Fixy.h umbrella did not define CRUCIBLE_FIXY"
#endif

static_assert(CRUCIBLE_FIXY == 1,
    "crucible/Fixy.h umbrella must set CRUCIBLE_FIXY=1.");

// Minter-family reachability via the umbrella.  Each line below
// asserts that the named function template is reachable through
// `crucible::fixy::<sub>::*` after a single `#include <crucible/Fixy.h>`.

namespace fixy = crucible::fixy;

// Cap
static_assert(std::is_same_v<
    decltype(&fixy::cap::mint_cap<
                 ::crucible::effects::Effect::Alloc,
                 ::crucible::effects::ctx_cap::Bg>),
    decltype(&::crucible::effects::mint_cap<
                 ::crucible::effects::Effect::Alloc,
                 ::crucible::effects::ctx_cap::Bg>)>,
    "fixy::cap::mint_cap must be reachable via the umbrella.");

// ── fixy-A4-011 / fixy-M-28 dual-path drift detector ──────────────
//
// Linear / Secret / SharedPermission (and their mint factories) are
// re-exported on TWO namespace paths intentionally — by-feature
// carve-outs (fixy::safety::, fixy::perm::) AND the one-stop
// value-wrapping directory (fixy::wrap::).  Today both paths name
// the SAME substrate symbol via using-declarations, so the dual
// re-export is structurally redundant rather than ambiguous.  These
// static_asserts pin that fact at compile time: if a future patch
// changes one path's `using` to point at a divergent symbol, the
// build breaks here rather than silently giving callers two
// different "Linear<int>" types.  See Safety.h header docs +
// Wrap.h "Dual-export discipline" block for the policy.

static_assert(std::is_same_v<
    fixy::safety::Linear<int>,
    fixy::wrap::Linear<int>>,
    "fixy-A4-011: fixy::safety::Linear and fixy::wrap::Linear must "
    "resolve to the same substrate symbol.");

static_assert(std::is_same_v<
    fixy::safety::Secret<int>,
    fixy::wrap::Secret<int>>,
    "fixy-A4-011: fixy::safety::Secret and fixy::wrap::Secret must "
    "resolve to the same substrate symbol.");

static_assert(std::is_same_v<
    decltype(&fixy::safety::mint_linear<int, int>),
    decltype(&fixy::wrap::mint_linear<int, int>)>,
    "fixy-A4-011: fixy::safety::mint_linear and fixy::wrap::mint_linear "
    "must resolve to the same substrate symbol.");

static_assert(std::is_same_v<
    decltype(&fixy::safety::mint_secret<int, int>),
    decltype(&fixy::wrap::mint_secret<int, int>)>,
    "fixy-A4-011: fixy::safety::mint_secret and fixy::wrap::mint_secret "
    "must resolve to the same substrate symbol.");

// Tag for the SharedPermission identity assertion below.  Lives in
// an anonymous namespace so it can't collide with any other TU's
// tag tree; the per-tag root mint is gated by SharedPermissionPool
// at runtime so the test compiles without instantiating either.
namespace {
struct A4_011_TestTag {};
}  // namespace

static_assert(std::is_same_v<
    fixy::perm::SharedPermission<A4_011_TestTag>,
    fixy::wrap::SharedPermission<A4_011_TestTag>>,
    "fixy-A4-011: fixy::perm::SharedPermission and fixy::wrap::SharedPermission "
    "must resolve to the same substrate symbol.");

// Per-namespace reachability checks (compile-only, no instantiation).
// Each `using namespace` block names a fixy:: sub-namespace.  If the
// umbrella failed to pull a header, the name would not exist and
// these blocks would emit "no namespace named" under -Werror.

namespace {

void reach_sub_namespaces() {
    using namespace fixy::cap;
    using namespace fixy::perm;
    using namespace fixy::sess;
    using namespace fixy::pipe;
    using namespace fixy::bridge;
    using namespace fixy::substr::spsc;
    using namespace fixy::substr::swmr;
    using namespace fixy::substr::chaselev;
    using namespace fixy::substr::metalog;
    using namespace fixy::substr::chainedge;
    using namespace fixy::substr::mpmc;
    using namespace fixy::substr::calendar_grid;
    using namespace fixy::substr::sharded_calendar_grid;
    using namespace fixy::substr::sharded_grid;
    using namespace fixy::mach;
    using namespace fixy::safety;
    using namespace fixy::wrap;    // fixy-A4-011 / fixy-M-27: wrap re-exports
    using namespace fixy::stance;
    using namespace fixy::grant;
    using namespace fixy::dim;
    (void)0;
}

}  // namespace

int main() {
    reach_sub_namespaces();
    return 0;
}
