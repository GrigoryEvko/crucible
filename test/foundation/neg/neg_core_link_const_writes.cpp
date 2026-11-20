// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A const Link gives its object only as a Ref of const
// (foundation/core/Link.h).  So a reader of a const node writes nothing
// through the Link of the node.

#include <foundation/core/Link.h>

#include <foundation/core/Ref.h>

namespace {

struct Node {
    int value = 0;
    ::foundation::core::Link<Node> next{};
};

}  // namespace

int main() {
    Node last{};
    Node head{.value = 1, .next = ::foundation::core::erase_brand(::foundation::core::to(last))};
    Node const& read = head;
    const ::foundation::core::Ref<Node> writer = read.next.expect("the head links the last node");
    writer->value = 2;
    return last.value;
}
