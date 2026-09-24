// A consumer that budgets for an L1 hit is not served by a Cold value.
// satisfies_v reads leq(required, provided), and Hot sits above Cold on
// the chain, so the gate refuses the pair.  A gate that admitted it would
// let the consumer pay a cache miss that it did not budget for.

#include <fixy/Bands.h>

template <typename Provider>
concept covers_hot_requirement = fixy::satisfies_v<Provider, fixy::ResidencyHeatTag_v::Hot>;

[[nodiscard]] int consume(covers_hot_requirement auto band) {
    return band.peek();
}

int main() {
    fixy::residency_heat::Cold<int> cold{7, {}};
    return consume(cold);
}
