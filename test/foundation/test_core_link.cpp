// Tests of foundation/core/Link.h: a walk of a chain, the write through a
// Link that is not const, the read through a const Link, and the end of the
// process at the expect of an empty Link.

#include <foundation/core/Link.h>

#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

namespace {

using ::foundation::core::Link;
using ::foundation::core::Ref;

struct Node {
    int value = 0;
    Link<Node> next{};
};

// A walk of three nodes writes each node through a Link that is not const.
void test_walk_writes_through_a_link() {
    Node third{.value = 3};
    Node second{.value = 2, .next = ::foundation::core::erase_brand(::foundation::core::to(third))};
    Node first{.value = 1, .next = ::foundation::core::erase_brand(::foundation::core::to(second))};
    Link<Node> walk = ::foundation::core::erase_brand(::foundation::core::to(first));
    int count = 0;
    while (walk.is_some()) {
        const Ref<Node> node = Link<Node>{walk}.expect("the condition of the loop tests the link");
        node->value *= 10;
        ++count;
        walk = node->next;
    }
    CRUCIBLE_FATAL_INVARIANT(count == 3);
    CRUCIBLE_FATAL_INVARIANT(first.value == 10 && second.value == 20 && third.value == 30);
}

// A const node gives its Link only as a Ref of const, and the pass over an
// empty Link makes no pass.
void test_const_link_reads() {
    Node last{.value = 7};
    Node head{.value = 1, .next = ::foundation::core::erase_brand(::foundation::core::to(last))};
    Node const& read = head;
    int seen = 0;
    for (Ref<Node const> const& object : read.next)
        seen += object->value;
    for (Ref<Node const> const& object : last.next)
        seen += 100 * object->value;
    CRUCIBLE_FATAL_INVARIANT(seen == 7);
    CRUCIBLE_FATAL_INVARIANT(read.next.is_some_and([](Ref<Node const> const& object) { return object->value == 7; }));
    const int matched = read.next.match([](Ref<Node const> object) { return object->value; }, [] { return -1; });
    CRUCIBLE_FATAL_INVARIANT(matched == 7);
    CRUCIBLE_FATAL_INVARIANT(Link<Node>{::foundation::core::none}.is_none());
}

void expect_of_an_empty_link() {
    Link<Node> const empty{};
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
