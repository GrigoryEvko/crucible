// A consumer that requires an NV value is not served by an AMD value.
// The named backends are incomparable, so satisfies_v, which reads
// leq(required, provided), is false both ways between them.  A gate that
// admitted the pair would hand a kernel built for one GPU to the other.
//
// This is the case a chain cannot produce.  On a chain a refused provider
// always sits below the requirement.  Here the provider sits neither
// above nor below it.

#include <fixy/Bands.h>

template <typename Provider>
concept covers_nv_requirement = fixy::satisfies_v<Provider, fixy::VendorBackend_v::NV>;

[[nodiscard]] int consume(covers_nv_requirement auto band) {
    return band.peek();
}

int main() {
    fixy::vendor::Amd<int> amd{7, {}};
    return consume(amd);
}
