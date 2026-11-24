// The body of update gets a copy that ends with the call, so the body stores
// no borrow of it in an object outside the call.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

namespace {

struct NodeBrand final {};

struct Node {
    using brand_type = NodeBrand;
    int label = 0;
};

}  // namespace

int main() {
    ::foundation::core::FixedArray<Node, 2> nodes{};
    ::foundation::core::Option<::foundation::core::Ref<Node, NodeBrand>> kept{::foundation::core::none};
    nodes.update(::foundation::core::index<0>(),
                 [&kept](Node& first) noexcept { kept = ::foundation::core::some(::foundation::core::to(first)); });
    return kept.is_some() ? 1 : 0;
}
