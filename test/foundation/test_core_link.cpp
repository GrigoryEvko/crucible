// Tests of foundation/core/Link.h: a walk of a chain, the write through a
// Link that is not const, the read through a const Link, and the end of the
// process at the expect of an empty Link.  The nodes live in an arena of a
// family, and each Link takes the brand of the family.

#include <foundation/core/Link.h>

#include <foundation/Platform.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include <cstddef>

namespace {

using ::foundation::core::BumpArena;
using ::foundation::core::Link;
using ::foundation::core::Ref;
using Bytes = ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>;

// The family of the arenas of the nodes of the tests.
struct[[= ::foundation::brand::arena_family_brand{}]] Nodes final {};

struct Node {
    int value = 0;
    Link<Node, Nodes> next{};
};

// A walk of three nodes writes each node through a Link that is not const.
void test_walk_writes_through_a_link() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    BumpArena<Nodes> arena = ::foundation::core::mint_bump_arena<Nodes>(Bytes{4096});
    Ref<Node, Nodes> const third = arena.make<Node>(alloc);
    third->value = 3;
    Ref<Node, Nodes> const second = arena.make<Node>(alloc);
    second->value = 2;
    second->next = third;
    Ref<Node, Nodes> const first = arena.make<Node>(alloc);
    first->value = 1;
    first->next = second;
    Link<Node, Nodes> walk = first;
    int count = 0;
    while (walk.is_some()) {
        const Ref<Node, Nodes> node = Link<Node, Nodes>{walk}.expect("the condition of the loop tests the link");
        node->value *= 10;
        ++count;
        walk = node->next;
    }
    CRUCIBLE_FATAL_INVARIANT(count == 3);
    CRUCIBLE_FATAL_INVARIANT(first->value == 10 && second->value == 20 && third->value == 30);
}

// A const node gives its Link only as a Ref of const, and the pass over an
// empty Link makes no pass.
void test_const_link_reads() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    BumpArena<Nodes> arena = ::foundation::core::mint_bump_arena<Nodes>(Bytes{4096});
    Ref<Node, Nodes> const last = arena.make<Node>(alloc);
    last->value = 7;
    Ref<Node, Nodes> const head = arena.make<Node>(alloc);
    head->value = 1;
    head->next = last;
    Node const& read = *head;
    int seen = 0;
    for (Ref<Node const, Nodes> const& object : read.next)
        seen += object->value;
    for (Ref<Node const, Nodes> const& object : static_cast<Node const&>(*last).next)
        seen += 100 * object->value;
    CRUCIBLE_FATAL_INVARIANT(seen == 7);
    CRUCIBLE_FATAL_INVARIANT(
        read.next.is_some_and([](Ref<Node const, Nodes> const& object) { return object->value == 7; }));
    const int matched = read.next.match([](Ref<Node const, Nodes> object) { return object->value; }, [] { return -1; });
    CRUCIBLE_FATAL_INVARIANT(matched == 7);
    CRUCIBLE_FATAL_INVARIANT((Link<Node, Nodes>{::foundation::core::none}.is_none()));
}

void expect_of_an_empty_link() {
    Link<Node, Nodes> const empty{};
    static_cast<void>(empty.expect("the test reads an empty link"));
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_walk_writes_through_a_link();
    test_const_link_reads();
    ::fixy::check::expect_dies(test, expect_of_an_empty_link, ::fixy::TextView{"the test reads an empty link"});
    ::fixy::check::pass(test, "test_core_link: passed\n");
    return 0;
}
