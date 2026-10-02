// The fuel of the bounded asynchronous search: a pair whose search,
// without fuel, exceeds the constexpr operation limit of the build.

#include "session_subtype_attack.h"

namespace test_session_subtype_attack_types {

// ── Fuel ─────────────────────────────────────────────────────────────
//
// A pair from the differential corpus whose bounded search spends all
// its fuel in each direction, on a channel of 3 messages and on a
// channel of 4 messages.  On the channel of 4 messages, the search
// without fuel exceeds the constexpr operation limit of the build.  With
// fuel, the check answers "not proven" well inside the limit.  An answer
// that the fuel stops is a refusal, never an acceptance, because each
// rule of the search is a conjunction or a disjunction of its
// sub-results, and a sub-search with no fuel left is false.

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
static_assert(!s::is_subtype_async_v<HardSub, HardSuper, ring<4>>
              && !s::is_subtype_async_v<HardSuper, HardSub, ring<4>>);

}  // namespace test_session_subtype_attack_types
