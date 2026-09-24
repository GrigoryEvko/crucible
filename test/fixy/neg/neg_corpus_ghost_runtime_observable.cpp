// Corpus entry ghost_runtime_observable.
// Filliatre-Gondelman-Paskevich 2014 'The Spirit of Ghost Code' and
// Leino 2010 'Dafny': a binding that engages Usage=Ghost and any
// runtime-observable effect is contradictory, because ghost values are
// erased at compile time and cannot drive runtime presence.  A
// declassification does not apply — this is a ghost-versus-runtime
// category error, not an information-flow channel.
//
// The pack is the one neg_rule_p010_ghost_observable_row.cpp uses.  The
// rule and the entry read the same predicate from two directions, so
// both refuse this binding and fn's tier-5 message carries both: the
// rule as its code, the entry with its citation.  This fixture stands on
// the entry name; its sibling stands on P010.
//
// as_public is named so the refusal is not instead about a classified
// value reaching an allocator.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::ghost, ::fixy::atom::as_public, ::fixy::atom::with_alloc> refused{};
    return 0;
}
