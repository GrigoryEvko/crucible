// The fuel of the bounded asynchronous search: a pair whose search,
// without fuel, exceeds the constexpr operation limit of the build.

#include "session_subtype_attack.h"

namespace test_session_subtype_attack_types {

// ── Fuel ─────────────────────────────────────────────────────────────
//
// A pair from the differential corpus whose bounded search, without
// fuel, exceeds the constexpr operation limit of the build.  With fuel
// the check answers "not proven" well inside the limit.  An answer that
// fuel cuts short is a refusal, never an acceptance, because each rule of
// the search is a conjunction or a disjunction of its sub-results, and
// an exhausted sub-search is false.

struct Nat {};
struct Bool {};
using HardSub = Loop<Offer<Select<Offer<Loop<Recv<Nat, End>>, Recv<Bool, Continue>>, Send<Nat, Send<Bool, Continue>>>,
                           Select<Offer<End, Continue, Send<Bool, Continue>>, Continue, Send<Bool, Continue>>,
                           Send<Bool, Recv<Bool, Recv<Nat, Continue>>>>>;
using HardSuper =
    Loop<Offer<Select<Offer<Loop<Recv<Bool, End>>, Recv<Bool, Continue>>, Send<Nat, Send<Bool, Continue>>>,
               Select<Offer<End, Continue, Send<Bool, Continue>>, Continue, Send<Bool, Continue>>,
               Send<Bool, Recv<Bool, Recv<Nat, Continue>>>>>;
static_assert(!s::is_subtype_async_v<HardSub, HardSuper, ring<3>>
              && !s::is_subtype_async_v<HardSuper, HardSub, ring<3>>);

}  // namespace test_session_subtype_attack_types
