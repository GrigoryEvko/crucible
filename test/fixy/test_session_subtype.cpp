// Session subtyping: the synchronous relation, its reason, the derived
// relations, the payload order and the bounded asynchronous relation of
// fixy/session/Subtype.h.  The last part generates protocols from a
// fixed seed and checks the laws on each: reflexivity, transitivity,
// closure under duality, the involution of duality, and that the
// asynchronous relation holds each synchronous pair.
//
// The test is several source files of one executable, so that no
// translation unit holds every check:
//
//   session_subtype.h             the shared part and the generator
//   this file                     the laws of each generated chain and
//                                 main
//   ..._closure_<k>.cpp           part k of the generated pairs of the
//                                 closure under duality
//   ..._relation.cpp              reflexivity, the shapes that do not
//                                 relate, width, position, recursion and
//                                 the labels
//   ..._reason.cpp                the reason and the derived relations
//   ..._payload.cpp               the payload order and the messages of
//                                 a projection
//   ..._async.cpp                 the asynchronous relation

#include "session_subtype.h"

#include <cstdio>
#include <initializer_list>

namespace test_session_subtype_types {

inline constexpr law_counts generated = check_chain_laws();

static_assert(generated.chains == generated_chains);
static_assert(generated.reflexive == generated_chains, "the relation is reflexive on each generated protocol");
static_assert(generated.widened == generated_chains, "each widening rule is admitted by the relation");
static_assert(generated.transitive == generated_chains, "the relation is transitive on each generated chain");
static_assert(generated.dual_closed == generated_chains,
              "the relation up to exits is closed under duality on each chain");
static_assert(generated.involutive == generated_chains, "duality is an involution");
static_assert(generated.dual_well_formed == generated_chains, "the dual of a well-formed protocol is well-formed");
static_assert(generated.async_contains_sync == generated_chains,
              "the asynchronous relation holds each synchronous pair");

}  // namespace test_session_subtype_types

using namespace test_session_subtype_types;

int main() {
    // The generated counts reach the program, so a law that no longer
    // holds shows here as well as in the build.  The closure parts give
    // their pairs, and together they must cover each pair.
    law_counts closure{};
    for (const law_counts& part : {closure_part_0(), closure_part_1(), closure_part_2()}) {
        closure.pairs += part.pairs;
        closure.pair_closure += part.pair_closure;
    }
    const bool holds = generated.reflexive == generated.chains && generated.transitive == generated.chains
                    && generated.dual_closed == generated.chains && closure.pair_closure == closure.pairs
                    && closure.pairs == generated_protocols * generated_protocols;
    if (!holds) {
        std::fprintf(stderr, "test_session_subtype: a generated law does not hold\n");
        return 1;
    }
    return 0;
}
