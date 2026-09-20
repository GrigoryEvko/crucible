// Sentinel TU for fixy/Collision.h: every live rule admits the empty
// pack, refuses its own pair, and admits each half of that pair; the
// pending roster is exactly the axes no atom reaches; and the rules
// reach fn's gate.
//
// The colliding packs are exercised through collision::live_rules and
// collision::grades rather than through fn.  Naming fn with a colliding
// pack is a build failure by design, so a rule that could only be
// observed that way could not be tested at all — only the whole TU
// failing would show it, which says nothing about which rule fired.

#include <fixy/Collision.h>
#include <fixy/Fn.h>

#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

namespace {

namespace col = ::fixy::collision;
namespace at = ::fixy::atom;
using ::fixy::Axis;
using ::fixy::axis_traits;
using col::grades;
using col::live_rules;
using Eff = ::foundation::effects::Effect;

// The test brings its own tag: the sample tag the family roster uses
// lives in a detail namespace and is not the test's to name.
struct tls_tag final {};

// ---------------------------------------------------------------------
// grades answers without fn.

static_assert(std::is_same_v<grades<>::on<Axis::Usage>, typename axis_traits<Axis::Usage>::strict>);
static_assert(std::is_same_v<grades<at::copy>::on<Axis::Usage>, at::copy>);
static_assert(std::is_same_v<grades<at::copy>::on<Axis::Effect>, typename axis_traits<Axis::Effect>::strict>);
static_assert(grades<at::copy>::mentions<Axis::Usage>);
static_assert(!grades<at::copy>::mentions<Axis::Effect>);

// The same answer fn gives, which is the point of keeping them separate
// rather than the point of keeping them different.
static_assert(std::is_same_v<grades<at::copy>::on<Axis::Usage>, ::fixy::fn<int, at::copy>::grade_on<Axis::Usage>>);
static_assert(std::is_same_v<grades<>::on<Axis::Mutation>, ::fixy::fn<int>::grade_on<Axis::Mutation>>);

// ---------------------------------------------------------------------
// Every live rule admits the strictest binding.

static_assert(live_rules<>::valid);
static_assert(live_rules<>::validate());
static_assert(live_rules<>::L002_ok && live_rules<>::M012_ok && live_rules<>::P010_ok && live_rules<>::L007_ok
              && live_rules<>::T001_ok && live_rules<>::R002_ok && live_rules<>::R003_ok && live_rules<>::L006_ok
              && live_rules<>::G002_ok && live_rules<>::D002_ok);

// ---------------------------------------------------------------------
// Each rule refuses its pair and admits each half.  Both directions per
// rule, because a rule that only ever says yes proves nothing.

// L002 borrow x async
static_assert(!live_rules<at::borrow, at::coroutine>::L002_ok);
static_assert(!live_rules<at::borrow, at::with<Eff::Bg>>::L002_ok);
static_assert(live_rules<at::borrow>::L002_ok);
static_assert(live_rules<at::coroutine>::L002_ok);

// M012 monotonic x concurrent without an atomic representation
static_assert(!live_rules<at::mut_monotonic, at::coroutine>::M012_ok);
static_assert(live_rules<at::mut_monotonic>::M012_ok);
static_assert(live_rules<at::mut_monotonic, at::coroutine, at::repr<::fixy::pole::ReprKind::Atomic>>::M012_ok);
static_assert(!live_rules<at::mut_monotonic, at::coroutine, at::repr<::fixy::pole::ReprKind::C>>::M012_ok);

// P010 ghost x an effect that must emit code
static_assert(!live_rules<at::ghost, at::with<Eff::Alloc>>::P010_ok);
static_assert(!live_rules<at::ghost, at::with<Eff::IO>>::P010_ok);
static_assert(!live_rules<at::ghost, at::with<Eff::Block>>::P010_ok);
static_assert(live_rules<at::ghost>::P010_ok);
static_assert(live_rules<at::ghost, at::with<Eff::Test>>::P010_ok, "Test is not an emitted-code effect");

// L007 borrow x Bg row
static_assert(!live_rules<at::borrow, at::with<Eff::Bg>>::L007_ok);
static_assert(live_rules<at::borrow>::L007_ok);
static_assert(live_rules<at::with<Eff::Bg>>::L007_ok);

// T001 capability x unverified provenance
static_assert(!live_rules<at::capability_usage, at::trust_unverified>::T001_ok);
static_assert(live_rules<at::capability_usage>::T001_ok);
static_assert(live_rules<at::trust_unverified>::T001_ok);
static_assert(live_rules<at::capability_usage, at::trust_verified>::T001_ok);

// R002 / R003 coroutine x borrow, coroutine x Bg row
static_assert(!live_rules<at::coroutine, at::borrow>::R002_ok);
static_assert(live_rules<at::coroutine>::R002_ok);
static_assert(!live_rules<at::coroutine, at::with<Eff::Bg>>::R003_ok);
static_assert(live_rules<at::coroutine>::R003_ok);

// G002 thread-local x atomic representation
static_assert(!live_rules<at::global::thread_local_<tls_tag>, at::repr<::fixy::pole::ReprKind::Atomic>>::G002_ok);
static_assert(live_rules<at::global::thread_local_<tls_tag>>::G002_ok);
static_assert(live_rules<at::repr<::fixy::pole::ReprKind::Atomic>>::G002_ok);

// D002 unbounded recursion x unbounded cost
static_assert(!live_rules<at::dispatch::recurses<0>, at::cost_unbounded>::D002_ok);
static_assert(live_rules<at::dispatch::recurses<0>>::D002_ok);
static_assert(live_rules<at::cost_unbounded>::D002_ok);
static_assert(live_rules<at::dispatch::recurses<0>, at::cost_linear<4>>::D002_ok);

// ---------------------------------------------------------------------
// The pending roster.

static_assert(col::pending_axis_count == 8);
static_assert(col::pending_rule_count == 22);
static_assert(col::live_rule_count == 10);
static_assert(col::every_pending_axis_is_still_empty());

// Each pending axis really has no atom, and each axis carrying a live
// rule really has one.  Both halves, so the roster is not merely
// self-consistent.
static_assert(!col::axis_has_an_atom<Axis::Regime>);
static_assert(!col::axis_has_an_atom<Axis::FpMode>);
static_assert(!col::axis_has_an_atom<Axis::MemoryScope>);
static_assert(col::axis_has_an_atom<Axis::Usage>);
static_assert(col::axis_has_an_atom<Axis::Effect>);
static_assert(col::axis_has_an_atom<Axis::ControlFlow>);
static_assert(col::axis_has_an_atom<Axis::GlobalState>);

// Every pending rule names an axis that is on the pending roster.  A
// rule filed against a live axis would be a rule someone forgot to
// write.
[[nodiscard]] consteval bool every_pending_rule_waits_on_a_pending_axis() noexcept {
    for (const col::pending_rule& rule : col::pending_rules) {
        if (!col::axis_is_pending(rule.waits_on)) return false;
        if (rule.theorem.empty()) return false;
    }
    return true;
}
static_assert(every_pending_rule_waits_on_a_pending_axis());

// ---------------------------------------------------------------------
// The rules reach fn's gate.

static_assert(::fixy::IsAccepted<int, at::borrow>);
static_assert(::fixy::IsAccepted<int, at::coroutine>);
static_assert(!::fixy::IsAccepted<int, at::borrow, at::coroutine>);
static_assert(!::fixy::IsAccepted<int, at::capability_usage, at::trust_unverified>);
static_assert(col::CollisionRules<::fixy::fn<int, at::copy>>::valid);
static_assert(!col::CollisionRules<::fixy::fn<int, at::borrow, at::coroutine>>::valid);

// A static_assert proves the constant-evaluated path only.
[[nodiscard]] int check_runtime_paths() {
    if (col::pending_axis_count != 8) return 1;
    if (col::pending_rule_count != 22) return 2;
    if (col::live_rule_count != 10) return 3;

    std::size_t seen = 0;
    for (const col::pending_rule& rule : col::pending_rules) {
        if (rule.theorem.empty()) return 4;
        ++seen;
    }
    if (seen != col::pending_rule_count) return 5;

    // The binding the rules admit still carries its value.
    const auto bound = ::fixy::mint_fn<int, at::borrow>(11);
    if (bound.value() != 11) return 6;
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_runtime_paths(); rc != 0) return rc;
    return 0;
}
