// MUST fail to compile: Linear outside HotPath inverts the canonical
// nesting order.  HotPath holds position 0 and Linear holds position 12,
// so a Linear that wraps a HotPath runs the positions downward.  Such a
// stack addresses a different federation cache slot than the canonical
// one, and a site that requires CanonicallyOrdered must refuse it.

#include <fixy/CanonicalOrder.h>

namespace {

namespace co = ::fixy::canonical_order;

template <co::CanonicallyOrdered Stack>
constexpr int accept_canonical_stack() {
    return 0;
}

using InvertedStack = ::fixy::Linear<::fixy::HotPath<::fixy::HotPathTier_v::Hot, int>>;

}  // namespace

int main() { return accept_canonical_stack<InvertedStack>(); }
