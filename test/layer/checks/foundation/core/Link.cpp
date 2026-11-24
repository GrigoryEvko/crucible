// The compile-time checks of foundation/core/Link.h.

#include <foundation/core/Link.h>

#include <foundation/Brand.h>
#include <foundation/Transfer.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Text.h>
#include <foundation/core/Verbs.h>

#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::link_checks {

// The brand of the store of the nodes of the checks.  A node states it, so
// to(node) gives a Ref of the brand, as a store whose objects carry its brand
// gives.
struct Chain final {};

// A second store.
struct Other final {};

struct Node {
    using brand_type = Chain;
    int value = 0;
    Link<Node, Chain> next{};
};

struct Cell {
    Atomic<int> count{0};
};

using NodeLink = Link<Node, Chain>;

// A Link takes one pointer, as an Option of a Ref does.
static_assert(sizeof(NodeLink) == sizeof(void*));

// A Link takes a fresh brand: the erased brand gives no Link.
template <class Brand>
concept LinkBrand = requires { typename Link<Node, Brand>; };
static_assert(LinkBrand<Chain> && !LinkBrand<::foundation::brand::DefaultBrand>);

// A Link takes a Ref of its own brand, and no Ref of a different brand.
static_assert(std::is_constructible_v<NodeLink, Ref<Node, Chain>>);
static_assert(!std::is_constructible_v<NodeLink, Ref<Node, Other>>);
static_assert(!std::is_constructible_v<NodeLink, Option<Ref<Node, Other>>>);

// A Link copies only from a Link that is not const, and so does a class that
// holds one.
static_assert(std::is_constructible_v<NodeLink, NodeLink&> && !std::is_copy_constructible_v<NodeLink>);
static_assert(std::is_assignable_v<NodeLink&, NodeLink&> && !std::is_copy_assignable_v<NodeLink>);
static_assert(std::is_constructible_v<Node, Node&> && !std::is_copy_constructible_v<Node>);
static_assert(std::is_nothrow_move_constructible_v<NodeLink> && std::is_trivially_destructible_v<NodeLink>);

// A Link that is not const gives a Ref<T, Brand>, and a const Link gives only
// a Ref<T const, Brand>, by each door.
static_assert(std::is_same_v<decltype(std::declval<NodeLink&>().expect("")), Ref<Node, Chain>>);
static_assert(std::is_same_v<decltype(std::declval<NodeLink&&>().expect("")), Ref<Node, Chain>>);
static_assert(std::is_same_v<decltype(std::declval<NodeLink const&>().expect("")), Ref<Node const, Chain>>);
static_assert(std::is_same_v<decltype(std::declval<NodeLink&>().option()), Option<Ref<Node, Chain>>>);
static_assert(std::is_same_v<decltype(std::declval<NodeLink const&>().option()), Option<Ref<Node const, Chain>>>);
static_assert(std::is_same_v<decltype(*std::declval<NodeLink&>().begin()), Ref<Node, Chain>>);
static_assert(std::is_same_v<decltype(*std::declval<NodeLink const&>().begin()), Ref<Node const, Chain>>);

// The match calls one arm, and a call is no consume, so an arm that holds a
// linear value by value binds to the deleted twin of each match.
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] LinearEnd {
    int handle = 0;
    constexpr explicit LinearEnd(int value) noexcept : handle{value} {}
    constexpr LinearEnd(LinearEnd&&) noexcept = default;
    constexpr LinearEnd& operator=(LinearEnd&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    LinearEnd(LinearEnd const&) = delete("a linear end of the check does not copy");
    LinearEnd& operator=(LinearEnd const&) = delete("a linear end of the check does not copy");
};
using HoldsEnd = decltype([held = LinearEnd{1}](auto) noexcept { return held.handle; });
using HoldsNothing = decltype([](auto) noexcept { return 0; });
using NoneArm = decltype([]() noexcept { return 0; });
template <class Target, class Arm>
concept MatchesLink =
    requires(Target link, Arm arm) { static_cast<Target>(link).match(static_cast<Arm&&>(arm), NoneArm{}); };
static_assert(MatchesLink<NodeLink&, HoldsNothing> && MatchesLink<NodeLink&&, HoldsNothing>
              && MatchesLink<NodeLink const&, HoldsNothing>);
static_assert(!MatchesLink<NodeLink&, HoldsEnd> && !MatchesLink<NodeLink&&, HoldsEnd>
              && !MatchesLink<NodeLink const&, HoldsEnd>);

// The store check of foundation/core/CaptureStore.h reads the target of a
// const Link as const (gives_const_access_), because each door of a const
// Link gives only a Ref of a const target: option, expect, the pass above,
// match and is_some_and.  An arm of the match of a const Link takes no Ref of
// a writable target, and the closed surface below makes a new door of Link
// come with its own pin here.
using TakesWritable = decltype([](Ref<Node, Chain> const&) noexcept { return 0; });
using TakesConst = decltype([](Ref<Node const, Chain> const&) noexcept { return 0; });
static_assert(MatchesLink<NodeLink const&, TakesConst> && !MatchesLink<NodeLink const&, TakesWritable>);
inline constexpr TextView link_names{"Link is_some is_none option expect match is_some_and begin end"};
CRUCIBLE_CLOSED_SURFACE(link_names, NodeLink);

// The transfer facts of refers_deep{}: a const Link is Shareable when its
// object is, and a Link is Sendable only when two threads can write its
// object.
static_assert(transfer::Shareable<Node> && transfer::Shareable<NodeLink const>);
static_assert(!transfer::Sendable<NodeLink> && !transfer::SendableBeside<NodeLink>);
static_assert(transfer::Sendable<Link<Cell, Chain>> && transfer::SendableBeside<Link<Cell, Chain>>);

// A walk of a chain of two nodes, which writes through a Link that is not
// const and reads through a const Link.
consteval int sum_of_a_chain() {
    Node last{.value = 2};
    Node first{.value = 1, .next = to(last)};
    NodeLink walk = to(first);
    int sum = 0;
    while (walk.is_some()) {
        const Ref<Node, Chain> node = NodeLink{walk}.expect("the condition of the loop tests the link");
        node->value += 1;
        sum += node->value;
        walk = node->next;
    }
    Node const& read = first;
    for (Ref<Node const, Chain> const& object : read.next)
        sum += 10 * object->value;
    const bool is_last =
        read.next.is_some_and([](Ref<Node const, Chain> const& object) { return object->next.is_none(); });
    const int matched = NodeLink{}.match([](Ref<Node, Chain>) { return 1; }, [] { return 0; });
    return sum + (is_last ? 100 : 0) + matched;
}
static_assert(sum_of_a_chain() == 2 + 3 + 30 + 100);

}  // namespace detail::link_checks

}  // namespace foundation::core
