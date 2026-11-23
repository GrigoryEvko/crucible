// Negative-compile test. This file must fail to compile.
//
// A const Link gives its object only as a Ref of const
// (foundation/core/Link.h).  So a reader of a const node writes nothing
// through the Link of the node.

#include <foundation/core/Link.h>

#include <foundation/core/Ref.h>

namespace {

struct Chain final {};

struct Node {
    using brand_type = Chain;
    int value = 0;
    ::foundation::core::Link<Node, Chain> next{};
};

}  // namespace

int main() {
    Node last{};
    Node head{.value = 1, .next = ::foundation::core::to(last)};
    Node const& read = head;
    const ::foundation::core::Ref<Node, Chain> writer = read.next.expect("the head links the last node");
    writer->value = 2;
    return last.value;
}
