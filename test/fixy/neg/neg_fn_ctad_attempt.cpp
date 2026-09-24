// Tier 0: class template argument deduction is not a door.
//
// Every value-carrying fn is born through a mint factory, so that one
// grep for "mint_" finds every authorization event in the tree.  With no
// deduction guide at all, `fixy::fn{42}` would stop at "no viable
// deduction guide", which names no remedy; fn ships a guide that routes
// every deduction to a blocked sentinel type instead, so the attempt
// reaches the tier-0 assertion and that assertion names the factories.
//
// This is the one tier whose fixture cannot also be a payload or a pack
// case: the sentinel is an ordinary empty class, so every later tier
// would pass for it.  Tier 0 is asked first for exactly that reason.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] auto deduced = ::fixy::fn{42};
    return 0;
}
