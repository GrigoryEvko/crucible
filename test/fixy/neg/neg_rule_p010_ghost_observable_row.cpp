// P010: ghost x Row<Alloc|IO|Block>.  A ghost binding is erased at
// codegen and emits no instructions, but each of those three effects
// requires emitted code, so the two claims contradict each other.
//
// The corpus entry ghost_runtime_observable reads the same predicate
// from the literature side (Filliatre-Gondelman-Paskevich 2014, Leino
// 2010), so this pack trips both and fn's tier-5 message carries both.
// That is the honest picture: a rule and a corpus entry can cover one
// contradiction, and neither is redundant — the rule is the mechanical
// axis pair, the entry carries the citation.  This fixture stands on
// P010; neg_corpus_ghost_runtime_observable.cpp stands on the entry.
//
// as_public is named so the refusal is not instead about a classified
// value reaching an allocator.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::ghost, ::fixy::atom::as_public, ::fixy::atom::with_alloc> refused{};
    return 0;
}
