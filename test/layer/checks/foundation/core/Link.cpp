// The compile-time checks of foundation/core/Link.h.

#include <foundation/core/Link.h>

#include <foundation/Transfer.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::link_checks {

struct Node {
    int value = 0;
    Link<Node> next{};
};

struct Cell {
    Atomic<int> count{0};
};

// A Link takes one pointer, as an Option of a Ref does.
static_assert(sizeof(Link<Node>) == sizeof(void*));

// A Link copies only from a Link that is not const, and so does a class that
// holds one.
static_assert(std::is_constructible_v<Link<Node>, Link<Node>&> && !std::is_copy_constructible_v<Link<Node>>);
static_assert(std::is_assignable_v<Link<Node>&, Link<Node>&> && !std::is_copy_assignable_v<Link<Node>>);
static_assert(std::is_constructible_v<Node, Node&> && !std::is_copy_constructible_v<Node>);
static_assert(std::is_nothrow_move_constructible_v<Link<Node>> && std::is_trivially_destructible_v<Link<Node>>);

// A Link that is not const gives a Ref<T>, and a const Link gives only a
// Ref<T const>, by each door.
static_assert(std::is_same_v<decltype(std::declval<Link<Node>&>().expect("")), Ref<Node>>);
static_assert(std::is_same_v<decltype(std::declval<Link<Node>&&>().expect("")), Ref<Node>>);
static_assert(std::is_same_v<decltype(std::declval<Link<Node> const&>().expect("")), Ref<Node const>>);
static_assert(std::is_same_v<decltype(std::declval<Link<Node>&>().option()), Option<Ref<Node>>>);
static_assert(std::is_same_v<decltype(std::declval<Link<Node> const&>().option()), Option<Ref<Node const>>>);
static_assert(std::is_same_v<decltype(*std::declval<Link<Node>&>().begin()), Ref<Node>>);
static_assert(std::is_same_v<decltype(*std::declval<Link<Node> const&>().begin()), Ref<Node const>>);

// The transfer facts of refers_deep{}: a const Link is Shareable when its
// object is, and a Link is Sendable only when two threads can write its
// object.
static_assert(transfer::Shareable<Node> && transfer::Shareable<Link<Node> const>);
static_assert(!transfer::Sendable<Link<Node>> && !transfer::SendableBeside<Link<Node>>);
static_assert(transfer::Sendable<Link<Cell>> && transfer::SendableBeside<Link<Cell>>);

// A walk of a chain of two nodes, which writes through a Link that is not
// const and reads through a const Link.
consteval int sum_of_a_chain() {
    Node last{.value = 2};
    Node first{.value = 1, .next = Ref<Node>{to(last)}};
    Link<Node> walk = Ref<Node>{to(first)};
    int sum = 0;
    while (walk.is_some()) {
        const Ref<Node> node = Link<Node>{walk}.expect("the condition of the loop tests the link");
        node->value += 1;
        sum += node->value;
        walk = node->next;
    }
    Node const& read = first;
    for (Ref<Node const> const& object : read.next)
        sum += 10 * object->value;
    const bool is_last = read.next.is_some_and([](Ref<Node const> const& object) { return object->next.is_none(); });
    const int matched = Link<Node>{}.match([](Ref<Node>) { return 1; }, [] { return 0; });
    return sum + (is_last ? 100 : 0) + matched;
}
static_assert(sum_of_a_chain() == 2 + 3 + 30 + 100);

}  // namespace detail::link_checks

}  // namespace foundation::core
