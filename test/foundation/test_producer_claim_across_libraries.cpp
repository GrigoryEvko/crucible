// A producer claim that one shared object wins and a second shared object
// destroys.
//
// The project compiles with -fvisibility=hidden, so each shared object that
// instantiates ProducerClaim<Brand> holds its own record of the brand.  The
// PyTorch vessel has this shape: the recording kernels in
// libcrucible_dispatch.so win the claim of a Vigil, and crucible_destroy in
// libcrucible_vessel.so destroys it.  When the destructor leaves the record of
// the shared object that it runs in, it finds no entry there and ends the
// process.  The destructor must leave the record that the win entered.
//
// The first cycle wins on the main thread, and the second cycle wins on a
// second thread.  If the first destructor left the entry in the record of the
// winner, the second win finds a live claim of another thread and ends the
// process.

#include "producer_claim_libraries.h"

#include <cstdio>
#include <thread>

namespace {

using producer_claim_libraries::SharedBrand;

void cycle_on_this_thread() {
    SharedBrand* brand = producer_claim_libraries::make_brand();
    producer_claim_libraries::win_claim(*brand);
    producer_claim_libraries::destroy_brand(brand);
}

}  // namespace

int main() {
    cycle_on_this_thread();
    { std::jthread second_thread{cycle_on_this_thread}; }
    std::puts("test_producer_claim_across_libraries: a claim won in one shared object and destroyed in another "
              "leaves the record it entered");
    return 0;
}
