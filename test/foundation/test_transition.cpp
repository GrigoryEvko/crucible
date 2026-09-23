// The transition algebra of foundation/algebra/Transition.h, over
// stand-in registries.  Each check names the rule it holds.  A registry
// in its own namespace is one experiment: the coherent registry is the
// baseline, and each incoherent registry breaks exactly one coherence
// rule.

#include <foundation/algebra/Transition.h>

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <meta>
#include <string_view>
#include <type_traits>

namespace tr = ::foundation::algebra::transition;

namespace {

// ── A coherent registry ───────────────────────────────────────────────

template <class T, class K>
struct Put {};
template <class T, class K>
struct Take {};
template <class... Bs>
struct Pick {};
template <class... Bs>
struct Wait {};
template <class B>
struct Again {};
struct Back {};
struct Done {};
struct Halt {};
template <int V, class P>
struct Pin {};
template <int V, class P>
struct Raise {};
template <int V, class P>
struct Lower {};
template <class R>
struct From {};
template <class R>
struct Fault {};
struct Unknown {};

template <int A, int B>
inline constexpr bool at_most_v = A <= B;

template <int V>
inline constexpr bool is_positive_v = V > 0;

namespace good {
inline constexpr tr::combinator put{.shape = ^^Put,
                                    .kind = tr::shape_kind::step,
                                    .direction = tr::polarity::output,
                                    .dual = ^^Take,
                                    .payload_variance = tr::variance::covariant};
inline constexpr tr::combinator take{.shape = ^^Take,
                                     .kind = tr::shape_kind::step,
                                     .direction = tr::polarity::input,
                                     .dual = ^^Put,
                                     .payload_variance = tr::variance::contravariant};
inline constexpr tr::combinator pick{
    .shape = ^^Pick, .kind = tr::shape_kind::choice, .direction = tr::polarity::output, .dual = ^^Wait};
inline constexpr tr::combinator wait{.shape = ^^Wait,
                                     .kind = tr::shape_kind::choice,
                                     .direction = tr::polarity::input,
                                     .dual = ^^Pick,
                                     .annotation = ^^From};
inline constexpr tr::combinator again{.shape = ^^Again, .kind = tr::shape_kind::binder, .dual = ^^Again};
inline constexpr tr::combinator back{.shape = ^^Back, .kind = tr::shape_kind::back, .dual = ^^Back};
inline constexpr tr::combinator done{.shape = ^^Done, .kind = tr::shape_kind::terminal, .dual = ^^Done};
inline constexpr tr::combinator halt{
    .shape = ^^Halt, .kind = tr::shape_kind::terminal, .dual = ^^Halt, .absorbs_suffix = true};
inline constexpr tr::combinator pin{
    .shape = ^^Pin, .kind = tr::shape_kind::wrapper, .dual = ^^Pin, .value_admits = ^^is_positive_v};
// A wrapper pair whose value order is covariant on one side and
// contravariant on the other, so refinement stays closed under duality.
inline constexpr tr::combinator raise{.shape = ^^Raise,
                                      .kind = tr::shape_kind::wrapper,
                                      .dual = ^^Lower,
                                      .value_variance = tr::variance::covariant,
                                      .value_order = ^^at_most_v};
inline constexpr tr::combinator lower{.shape = ^^Lower,
                                      .kind = tr::shape_kind::wrapper,
                                      .dual = ^^Raise,
                                      .value_variance = tr::variance::contravariant,
                                      .value_order = ^^at_most_v};
inline constexpr tr::payload_rule fault{.shape = ^^Fault, .is_sendable = false, .is_label = false};
}  // namespace good

constexpr std::meta::info reg = ^^good;

namespace no_axioms {}

consteval bool well_formed(std::meta::info type) { return tr::fold(reg, type, tr::well_formed_algebra{reg, {}}, {}); }
consteval std::meta::info dual(std::meta::info type) { return tr::fold(reg, type, tr::dual_algebra{reg}, 0); }
consteval std::meta::info compose(std::meta::info type, std::meta::info suffix) {
    return tr::fold(reg, type, tr::compose_algebra{reg, suffix}, 0);
}
consteval bool empty_choice(std::meta::info type) { return tr::fold(reg, type, tr::empty_choice_algebra{reg}, 0); }
consteval bool terminal(std::meta::info type) { return tr::fold(reg, type, tr::terminal_algebra{}, 0); }
consteval tr::verdict refine(std::meta::info sub, std::meta::info super) {
    return tr::refines(reg, ^^no_axioms, sub, super);
}
consteval bool refines(std::meta::info sub, std::meta::info super) { return refine(sub, super).holds; }
consteval std::meta::info plain(std::meta::info type) { return std::meta::dealias(type); }

// ── Coherence ────────────────────────────────────────────────────────

static_assert(tr::check_registry(reg).reason == tr::incoherence::none);

namespace not_involutive {
template <class T, class K>
struct A {};
template <class T, class K>
struct B {};
template <class T, class K>
struct C {};
inline constexpr tr::combinator a{.shape = ^^A,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^B,
                                  .payload_variance = tr::variance::covariant};
inline constexpr tr::combinator b{.shape = ^^B,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::input,
                                  .dual = ^^C,
                                  .payload_variance = tr::variance::contravariant};
inline constexpr tr::combinator c{.shape = ^^C,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^B,
                                  .payload_variance = tr::variance::covariant};
}  // namespace not_involutive
static_assert(tr::check_combinator(^^not_involutive, ^^not_involutive::A).reason
              == tr::incoherence::dual_not_involutive);

namespace same_variance {
template <class T, class K>
struct A {};
template <class T, class K>
struct B {};
inline constexpr tr::combinator a{.shape = ^^A,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^B,
                                  .payload_variance = tr::variance::covariant};
inline constexpr tr::combinator b{.shape = ^^B,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::input,
                                  .dual = ^^A,
                                  .payload_variance = tr::variance::covariant};
}  // namespace same_variance
static_assert(tr::check_registry(^^same_variance).reason == tr::incoherence::payload_variance_not_flipped,
              "a send and a receive that are both covariant break closure under duality");

namespace same_direction {
template <class T, class K>
struct A {};
template <class T, class K>
struct B {};
inline constexpr tr::combinator a{.shape = ^^A,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^B,
                                  .payload_variance = tr::variance::covariant};
inline constexpr tr::combinator b{.shape = ^^B,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^A,
                                  .payload_variance = tr::variance::contravariant};
}  // namespace same_direction
static_assert(tr::check_registry(^^same_direction).reason == tr::incoherence::direction_not_flipped);

namespace missing_dual {
template <class T, class K>
struct A {};
template <class T, class K>
struct Nowhere {};
inline constexpr tr::combinator a{.shape = ^^A,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^Nowhere,
                                  .payload_variance = tr::variance::covariant};
}  // namespace missing_dual
static_assert(tr::check_registry(^^missing_dual).reason == tr::incoherence::dual_unregistered);

namespace twice {
struct Stop {};
inline constexpr tr::combinator one{.shape = ^^Stop, .kind = tr::shape_kind::terminal, .dual = ^^Stop};
inline constexpr tr::combinator two{.shape = ^^Stop, .kind = tr::shape_kind::terminal, .dual = ^^Stop};
}  // namespace twice
static_assert(tr::check_registry(^^twice).reason == tr::incoherence::registered_twice);

namespace ordered_self_dual {
template <int V, class P>
struct W {};
inline constexpr tr::combinator w{.shape = ^^W,
                                  .kind = tr::shape_kind::wrapper,
                                  .dual = ^^W,
                                  .value_variance = tr::variance::covariant,
                                  .value_order = ^^at_most_v};
}  // namespace ordered_self_dual
static_assert(tr::check_registry(^^ordered_self_dual).reason == tr::incoherence::value_variance_not_flipped,
              "a self-dual wrapper with an ordered value is not closed under duality");

namespace order_on_step {
template <class T, class K>
struct A {};
inline constexpr tr::combinator a{.shape = ^^A,
                                  .kind = tr::shape_kind::step,
                                  .direction = tr::polarity::output,
                                  .dual = ^^A,
                                  .value_order = ^^at_most_v};
}  // namespace order_on_step
static_assert(tr::check_registry(^^order_on_step).reason == tr::incoherence::order_on_non_wrapper);

namespace neutral_step {
template <class T, class K>
struct A {};
inline constexpr tr::combinator a{.shape = ^^A, .kind = tr::shape_kind::step, .dual = ^^A};
}  // namespace neutral_step
static_assert(tr::check_registry(^^neutral_step).reason == tr::incoherence::direction_on_neutral_kind);

namespace mixed_absorption {
struct S {};
struct T {};
inline constexpr tr::combinator s{.shape = ^^S, .kind = tr::shape_kind::terminal, .dual = ^^T};
inline constexpr tr::combinator t{.shape = ^^T, .kind = tr::shape_kind::terminal, .dual = ^^S, .absorbs_suffix = true};
}  // namespace mixed_absorption
static_assert(tr::check_registry(^^mixed_absorption).reason == tr::incoherence::absorption_differs);

namespace mixed_kind {
struct S {};
struct T {};
inline constexpr tr::combinator s{.shape = ^^S, .kind = tr::shape_kind::terminal, .dual = ^^T};
inline constexpr tr::combinator t{.shape = ^^T, .kind = tr::shape_kind::back, .dual = ^^S};
}  // namespace mixed_kind
static_assert(tr::check_registry(^^mixed_kind).reason == tr::incoherence::dual_kind_differs);

// ── Decomposition and the refusal of an unknown node ──────────────────

static_assert(tr::is_registered(reg, ^^Put<int, Done>));
static_assert(!tr::is_registered(reg, ^^Unknown));
static_assert(!tr::is_registered(reg, ^^int));
static_assert(tr::first_unregistered(reg, ^^Put<int, Pick<Done, Wait<Unknown>>>) == ^^Unknown);
static_assert(tr::first_unregistered(reg, ^^Put<Unknown, Done>) == std::meta::info{},
              "a payload is not a node, so an unknown payload is not refused");
static_assert(tr::head_shape(reg, ^^Pin<3, Put<int, Done>>) == ^^Put, "a recognizer looks under wrappers");
static_assert(tr::head_shape(reg, ^^int) == ^^int);

// A registered shape with the wrong layout is not registered.
template <class T>
struct Single {};
namespace wrong_layout {
inline constexpr tr::combinator single{
    .shape = ^^Single, .kind = tr::shape_kind::step, .direction = tr::polarity::output, .dual = ^^Single};
}  // namespace wrong_layout
static_assert(!tr::is_registered(^^wrong_layout, ^^Single<int>));

// ── Duality ──────────────────────────────────────────────────────────

using Ping = Put<int, Take<char, Done>>;
using Pong = Take<int, Put<char, Done>>;
static_assert(dual(^^Ping) == plain(^^Pong));
static_assert(dual(dual(^^Ping)) == plain(^^Ping), "duality is an involution without notes");
static_assert(dual(^^Wait<From<int>, Done>) == ^^Pick<Done>, "the dual has no note to keep");
static_assert(dual(^^Pick<Done>) == ^^Wait<Done>);
static_assert(dual(^^Raise<3, Done>) == ^^Lower<3, Done>);
static_assert(dual(^^Again<Put<int, Back>>) == ^^Again<Take<int, Back>>);

// ── Composition ──────────────────────────────────────────────────────

static_assert(compose(^^Put<int, Done>, ^^Ping) == ^^Put<int, Ping>);
static_assert(compose(^^Pick<Done, Halt>, ^^Ping) == ^^Pick<Ping, Halt>, "Halt absorbs the suffix");
static_assert(compose(^^Again<Pick<Put<int, Back>, Done>>, ^^Ping) == ^^Again<Pick<Put<int, Back>, Ping>>);
static_assert(compose(^^Wait<From<int>, Done>, ^^Ping) == ^^Wait<From<int>, Ping>, "composition keeps the note");
static_assert(tr::fold(reg, ^^Pin<2, Put<int, Pick<Done, Done>>>, tr::compose_at_choice_algebra{reg, 0, ^^Ping, {}}, 0)
              == ^^Pin<2, Put<int, Pick<Ping, Done>>>);
static_assert(tr::fold(reg, ^^Pick<Done>, tr::compose_at_choice_algebra{reg, 1, ^^Ping, {}}, 0) == std::meta::info{},
              "an index past the last branch has no answer");
static_assert(tr::first_stop_of_spine(reg, ^^Put<int, Again<Take<int, Back>>>).entry.kind == tr::shape_kind::back);

// ── Well-formedness ──────────────────────────────────────────────────

static_assert(well_formed(^^Ping) && well_formed(^^Pin<1, Ping>));
static_assert(well_formed(^^Again<Put<int, Back>>));
static_assert(well_formed(^^Again<Pick<Put<int, Back>, Done>>));
static_assert(well_formed(^^Again<Put<int, Again<Take<int, Back>>>>));
static_assert(!well_formed(^^Back), "a back node needs a binder");
static_assert(!well_formed(^^Put<int, Back>));
static_assert(!well_formed(^^Again<Back>), "a back node needs a guard");
static_assert(!well_formed(^^Again<Pin<1, Back>>), "a wrapper is not a guard");
static_assert(well_formed(^^Again<Again<Put<int, Back>>>),
              "the inner back node binds the inner binder, which is guarded");
static_assert(!well_formed(^^Again<Put<int, Again<Back>>>), "the inner binder is not guarded");
static_assert(!well_formed(^^Again<Done>) && !well_formed(^^Again<Halt>), "a binder body is not a terminal");
static_assert(!well_formed(^^Pin<0, Done>) && !well_formed(^^Pin<-4, Done>), "the value filter refuses");
static_assert(!well_formed(^^Put<Fault<int>, Done>), "a payload that is not sendable");
static_assert(well_formed(^^Take<Fault<int>, Done>));
static_assert(!well_formed(^^Put<int, Unknown>), "an unknown node is refused");

// ── Terminality and empty choices ────────────────────────────────────

static_assert(terminal(^^Done) && terminal(^^Halt) && terminal(^^Pin<1, Done>));
static_assert(!terminal(^^Back) && !terminal(^^Ping) && !terminal(^^Pick<>));
static_assert(empty_choice(^^Pick<>) && empty_choice(^^Wait<From<int>>));
static_assert(empty_choice(^^Put<int, Pick<Done, Wait<>>>), "the walk covers the whole spine");
static_assert(empty_choice(^^Wait<Take<Fault<int>, Done>, Pin<1, Take<Fault<char>, Done>>>),
              "no branch is a label, under wrappers too");
static_assert(!empty_choice(^^Wait<Take<int, Done>, Take<Fault<int>, Done>>));

// ── The hook: a template of the layer answers for each child ──────────

template <class P, class Scope>
struct wf_entry;
template <class P, class Scope>
inline constexpr bool wf_entry_v = wf_entry<P, Scope>::value;
template <class P, class Scope>
struct wf_entry
    : std::bool_constant<tr::fold(reg, ^^P, tr::well_formed_algebra{reg, {}}, {Scope::depth, Scope::guarded},
                                  ^^wf_entry_v)> {};
// One node answers for itself at every depth.
template <class Scope>
struct wf_entry<Halt, Scope> : std::false_type {};

static_assert(wf_entry_v<Ping, tr::scope<0, true>>);
static_assert(!wf_entry_v<Put<int, Pick<Done, Halt>>, tr::scope<0, true>>,
              "the specialization for Halt answers below the head");
static_assert(wf_entry_v<Again<Put<int, Back>>, tr::scope<0, true>>);
static_assert(!wf_entry_v<Again<Back>, tr::scope<0, true>>, "the hook carries the scope");

// ── The payload preorder ─────────────────────────────────────────────

template <class T>
struct Boxed {};
template <class T>
struct Shed {
    using type = T;
};
template <class T>
struct Shed<Boxed<T>> {
    using type = T;
};
template <class T>
inline constexpr bool is_boxed_v = false;
template <class T>
inline constexpr bool is_boxed_v<Boxed<T>> = true;
template <class T>
using shed_t = typename Shed<T>::type;
template <class T, class U>
inline constexpr bool small_to_wide_v = std::is_same_v<T, short> && std::is_same_v<U, long>;

namespace axioms {
inline constexpr tr::subsort_axiom boxed{.drops = ^^is_boxed_v, .inner = ^^shed_t};
inline constexpr tr::subsort_axiom widen{.weakens = ^^small_to_wide_v};
}  // namespace axioms

static_assert(tr::subsorts(^^axioms, ^^int, ^^int), "the order is reflexive");
static_assert(tr::subsorts(^^axioms, ^^Boxed<Boxed<int>>, ^^int), "drops chain");
static_assert(tr::subsorts(^^axioms, ^^Boxed<short>, ^^long), "a drop, then a weakening");
static_assert(!tr::subsorts(^^axioms, ^^int, ^^Boxed<int>), "nothing rises");
static_assert(!tr::subsorts(^^axioms, ^^long, ^^short));

// An axiom that breaks the contract and sheds to the same type stops at
// the depth bound instead of a recursion without end.
template <class T>
inline constexpr bool always_v = true;
template <class T>
using same_t = T;
namespace looping_axioms {
inline constexpr tr::subsort_axiom loops{.drops = ^^always_v, .inner = ^^same_t};
}  // namespace looping_axioms
static_assert(!tr::subsorts(^^looping_axioms, ^^int, ^^long));

// ── The type graph ───────────────────────────────────────────────────

consteval bool graph_links_back_to_binder() {
    const tr::graph_view graph = tr::graph_of(reg, ^^Again<Put<int, Back>>);
    return graph.nodes.size() == 3 && graph.nodes[0].next == 1 && graph.nodes[1].next == 2 && graph.nodes[2].next == 0
           && tr::settle(graph, 2) == 1;
}
static_assert(graph_links_back_to_binder());

consteval bool graph_records_the_unknown() {
    const tr::graph_view graph = tr::graph_of(reg, ^^Pick<Done, Unknown>);
    return graph.unregistered == ^^Unknown;
}
static_assert(graph_records_the_unknown());

consteval bool graph_marks_labels_and_payloads() {
    const tr::graph_view graph = tr::graph_of(reg, ^^Wait<Take<Fault<int>, Done>, Put<Fault<int>, Done>>);
    return graph.nodes[1].is_label == false && graph.nodes[1].has_restricted_payload
           && graph.nodes[3].has_restricted_payload && graph.nodes[3].is_label;
}
static_assert(graph_marks_labels_and_payloads());

// ── Refinement ───────────────────────────────────────────────────────

// Reflexive on every combinator, the note and the wrappers included.
static_assert(refines(^^Done, ^^Done) && refines(^^Halt, ^^Halt));
static_assert(refines(^^Ping, ^^Ping) && refines(^^Pong, ^^Pong));
static_assert(refines(^^Wait<From<int>, Done, Put<int, Done>>, ^^Wait<From<int>, Done, Put<int, Done>>));
static_assert(refines(^^Again<Put<int, Back>>, ^^Again<Put<int, Back>>));
static_assert(refines(^^Pin<3, Ping>, ^^Pin<3, Ping>) && refines(^^Raise<2, Ping>, ^^Raise<2, Ping>));

// Width: an output choice narrows, an input choice widens.
static_assert(refines(^^Pick<>, ^^Pick<Done>) && refines(^^Pick<Done>, ^^Pick<Done, Ping>));
static_assert(!refines(^^Pick<Done, Ping>, ^^Pick<Done>));
static_assert(refines(^^Wait<Done, Ping>, ^^Wait<Done>) && !refines(^^Wait<Done>, ^^Wait<Done, Ping>));
static_assert(refine(^^Pick<Done, Done>, ^^Pick<Done>).reason == tr::mismatch::branch_count);

// Variance: a covariant value and a contravariant value.
static_assert(refines(^^Raise<1, Done>, ^^Raise<2, Done>) && !refines(^^Raise<2, Done>, ^^Raise<1, Done>));
static_assert(refines(^^Lower<2, Done>, ^^Lower<1, Done>) && !refines(^^Lower<1, Done>, ^^Lower<2, Done>));
static_assert(refines(dual(^^Raise<2, Done>), dual(^^Raise<1, Done>)), "closed under duality");
static_assert(refine(^^Pin<1, Done>, ^^Pin<2, Done>).reason == tr::mismatch::value);

// Shape, note and payload mismatches name the pair.
static_assert(refine(^^Put<int, Done>, ^^Take<int, Done>).reason == tr::mismatch::shape);
static_assert(refine(^^Wait<From<int>, Done>, ^^Wait<From<char>, Done>).reason == tr::mismatch::annotation);
static_assert(refine(^^Wait<From<int>, Done>, ^^Wait<Done>).reason == tr::mismatch::annotation);
static_assert(refine(^^Put<int, Done>, ^^Put<long, Done>).sub == ^^int);
static_assert(refine(^^Take<int, Done>, ^^Take<long, Done>).sub == ^^long,
              "for an input the pair reads supplied against expected");

// The leftmost failure is the one reported.
static_assert(refine(^^Pick<Put<int, Done>, Put<char, Done>>, ^^Pick<Put<long, Done>, Put<short, Done>>).sub == ^^int);

// Unfolds: a loop and the loop unfolded once refine each other.
static_assert(refines(^^Again<Put<int, Back>>, ^^Put<int, Again<Put<int, Back>>>));
static_assert(refines(^^Put<int, Again<Put<int, Back>>>, ^^Again<Put<int, Back>>));
static_assert(refines(^^Again<Put<int, Put<int, Back>>>, ^^Again<Put<int, Back>>),
              "a loop over two sends is an unfolding of a loop over one");
static_assert(!refines(^^Again<Put<int, Back>>, ^^Put<int, Done>));

// The payload rules of rule Sub-&.
static_assert(refine(^^Wait<Done, Take<Fault<int>, Done>>, ^^Wait<Done>).reason == tr::mismatch::non_label_branch);
static_assert(refines(^^Wait<Take<Fault<int>, Done>, Done>, ^^Wait<Take<Fault<int>, Done>, Done>));
static_assert(refine(^^Wait<Take<Fault<int>, Done>, Done>, ^^Wait<Take<Fault<int>, Done>>).reason
              == tr::mismatch::pure_non_label_choice);

// An unknown node is named.
static_assert(refine(^^Pick<Done, Unknown>, ^^Pick<Done>).reason == tr::mismatch::unregistered);

// Transitivity on a chain of width steps.
static_assert(refines(^^Pick<Done>, ^^Pick<Done, Done>) && refines(^^Pick<Done, Done>, ^^Pick<Done, Done, Done>)
              && refines(^^Pick<Done>, ^^Pick<Done, Done, Done>));

// ── Values that reach the program ────────────────────────────────────

constexpr bool runtime_facts[] = {
    tr::check_registry(reg).reason == tr::incoherence::none,
    refines(^^Pick<Done>, ^^Pick<Done, Ping>),
    !refines(^^Pick<Done, Ping>, ^^Pick<Done>),
    well_formed(^^Again<Put<int, Back>>),
    !well_formed(^^Again<Back>),
    tr::subsorts(^^axioms, ^^Boxed<short>, ^^long),
};

constexpr std::string_view names[] = {
    "coherent registry", "narrow output choice", "wide output choice refused", "guarded loop",
    "unguarded loop refused", "drop then weaken",
};

}  // namespace

int main() {
    int failures = 0;
    for (std::size_t index = 0; index < std::size(runtime_facts); ++index) {
        if (!runtime_facts[index]) {
            std::fprintf(stderr, "test_transition: %.*s does not hold\n", static_cast<int>(names[index].size()),
                         names[index].data());
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
