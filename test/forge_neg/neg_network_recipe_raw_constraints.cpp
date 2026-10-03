// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/forge/recipes/Network.h>

namespace net = crucible::forge::recipes;

namespace {

void consume(net::DeclaredNetworkRecipeConstraints) {}

}  // namespace

int main() {
    net::NetworkRecipeConstraints raw{};
    consume(raw);
}
