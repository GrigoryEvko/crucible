// Negative compile test. This file must fail to compile.

#include <crucible/forge/recipes/Network.h>

namespace net = crucible::forge::recipes;

namespace {

void consume(net::DeclaredNetworkRecipeConstraints) {}

}  // namespace

int main() {
    net::NetworkRecipeConstraints raw{};
    consume(raw);
}
