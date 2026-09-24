// A consumer that requires a block-scope publication is not served by an
// inner-shareable one.  satisfies_v reads the outer lattice's own leq in
// the admission direction, leq(required, provided), and that is false
// both ways across the two trunks, so the gate refuses an ARM-trunk
// provider at an accelerator-trunk requirement.
//
// The refusal is what keeps the partial order honest.  On a chain a
// provider either covers a requirement or sits below it; here it can do
// neither, and a gate that fell back to "close enough" would offer the
// value to observers the fence never reached.

#include <fixy/Bands.h>

template <typename Provider>
concept covers_cta_requirement = fixy::satisfies_v<Provider, fixy::MemoryScope_v::Cta>;

[[nodiscard]] int consume(covers_cta_requirement auto band) {
    return band.peek();
}

int main() {
    fixy::scoped_fence::Inner<int> inner{7, {}};
    return consume(inner);
}
