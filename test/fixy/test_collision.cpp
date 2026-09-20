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
              && live_rules<>::G002_ok && live_rules<>::D002_ok && live_rules<>::P002_ok);

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

// P002 ghost x an emitting surface the effect row does not name.  The
// pair P010 admits is the pair P002 refuses, which is why both exist.
static_assert(!live_rules<at::ghost, at::stdio::write<at::stdio::streams::Stdout>>::P002_ok);
static_assert(!live_rules<at::ghost, at::stdio::write<at::stdio::streams::Stderr>>::P002_ok);
static_assert(live_rules<at::ghost, at::stdio::write<at::stdio::streams::Stdout>>::P010_ok,
              "a stdio write is not an effect-row effect, so P010 cannot see it");
static_assert(live_rules<at::ghost>::P002_ok);
static_assert(live_rules<at::stdio::write<at::stdio::streams::Stdout>>::P002_ok);
static_assert(live_rules<at::copy, at::stdio::write<at::stdio::streams::Stdout>>::P002_ok,
              "only the ghost grade makes an emitted write a contradiction");

// ---------------------------------------------------------------------
// The pending roster.

static_assert(col::pending_rule_count == 22);
static_assert(col::every_pending_axis_is_still_empty());

// pending_axes is a hand-written list, so the pin on its length compares
// it against the atom catalog rather than against its own size.  Counting
// the atomless axes independently also catches a duplicate entry, which a
// length of 8 would otherwise hide.
[[nodiscard]] consteval std::size_t axes_without_an_atom() noexcept {
    std::size_t found = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:member:];
        if constexpr (axis != Axis::Type) {
            if constexpr (!col::axis_has_an_atom<axis>) ++found;
        }
    }
#pragma GCC diagnostic pop
    return found;
}
static_assert(col::pending_axis_count == axes_without_an_atom(),
              "pending_axes must list exactly the axes with no atom, once each");

// ---------------------------------------------------------------------
// The corpus accounts for all 54 codes.
//
// The count below is the one number worth stating here, and it is stated
// against the external catalog rather than against the roster: the first
// shape of this file pinned live == roster - pending, which holds for any
// roster and held while 22 rules were missing.  The three dispositions
// are counted separately and must sum to the catalog's 54.

static_assert(col::rule_corpus_size == 54);
static_assert(col::live_rule_count == 11);

[[nodiscard]] consteval std::size_t corpus_entries_with(col::Disposition wanted) noexcept {
    std::size_t found = 0;
    for (const col::corpus_entry& entry : col::rule_corpus) {
        if (entry.disposition == wanted) ++found;
    }
    return found;
}
static_assert(corpus_entries_with(col::Disposition::Live) == 11);
static_assert(corpus_entries_with(col::Disposition::Pending) == 22);
static_assert(corpus_entries_with(col::Disposition::Absent) == 21);
static_assert(corpus_entries_with(col::Disposition::Live) + corpus_entries_with(col::Disposition::Pending)
                  + corpus_entries_with(col::Disposition::Absent)
              == col::rule_corpus_size);

// No code appears twice, so the 54 are 54 distinct codes rather than a
// list that happens to be 54 long.
[[nodiscard]] consteval bool every_corpus_code_is_unique() noexcept {
    for (std::size_t i = 0; i < col::rule_corpus_size; ++i) {
        for (std::size_t j = i + 1; j < col::rule_corpus_size; ++j) {
            if (col::rule_corpus[i].code == col::rule_corpus[j].code) return false;
        }
    }
    return true;
}
static_assert(every_corpus_code_is_unique());

// Every absent entry says what is missing, so an absence is a claim a
// reader can check rather than a silence.
[[nodiscard]] consteval bool every_absent_entry_gives_a_reason() noexcept {
    for (const col::corpus_entry& entry : col::rule_corpus) {
        if (entry.disposition == col::Disposition::Absent && entry.note.empty()) return false;
    }
    return true;
}
static_assert(every_absent_entry_gives_a_reason());

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
    if (col::pending_axis_count != axes_without_an_atom()) return 1;
    if (col::pending_rule_count != 22) return 2;
    if (col::live_rule_count != 11) return 3;

    std::size_t seen = 0;
    for (const col::pending_rule& rule : col::pending_rules) {
        if (rule.theorem.empty()) return 4;
        ++seen;
    }
    if (seen != col::pending_rule_count) return 5;

    // The corpus accounts for every code, and says something about each.
    std::size_t live = 0;
    std::size_t pending = 0;
    std::size_t absent = 0;
    for (const col::corpus_entry& entry : col::rule_corpus) {
        if (entry.code.empty() || entry.note.empty()) return 7;
        switch (entry.disposition) {
            case col::Disposition::Live: ++live; break;
            case col::Disposition::Pending: ++pending; break;
            case col::Disposition::Absent: ++absent; break;
            default: return 8;
        }
    }
    if (live != 11 || pending != 22 || absent != 21) return 9;
    if (live + pending + absent != col::rule_corpus_size) return 10;
    if (col::rule_corpus_size != 54) return 11;

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
