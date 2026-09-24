// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A reflection as a template argument prints the entity it names.  A
// function with internal linkage prints the same name in every
// translation unit, so the walk reads the reflection to its entity and
// refuses the internal linkage.
//
// Expected diagnostic: the refusal text of the stable id, which names
// the internal linkage.

#include <foundation/reflect/Hash.h>

#include <meta>

namespace stable_id_reflection_fixture {

static int helper(int value) { return value; }

template <std::meta::info Named>
struct Holds {};

}  // namespace stable_id_reflection_fixture

int main() {
    using namespace stable_id_reflection_fixture;
    return static_cast<int>(::foundation::reflect::stable_type_id<Holds<^^helper>> & 1U) + helper(0);
}
