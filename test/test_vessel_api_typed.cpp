#include "../vessel/torch/vessel_api_typed.h"

#include <crucible/Vigil.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace {

using crucible::vessel::TypedHandle;

static_assert(std::is_same_v<
    TypedHandle,
    crucible::safety::Tagged<
        crucible::Vigil*,
        crucible::safety::source::ABIBoundary>>);
static_assert(sizeof(TypedHandle) == sizeof(CrucibleHandle));
static_assert(alignof(TypedHandle) == alignof(CrucibleHandle));
static_assert(std::is_trivially_copy_constructible_v<TypedHandle>);

int g_failures = 0;

#define EXPECT(cond, msg) do {                                                \
    if (!(cond)) {                                                             \
        std::fprintf(stderr, "FAIL: %s -- %s (%s:%d)\n",                      \
                     #cond, (msg), __FILE__, __LINE__);                        \
        ++g_failures;                                                          \
    }                                                                          \
} while (0)

void test_roundtrip() {
    alignas(crucible::Vigil) std::array<std::byte, sizeof(crucible::Vigil)>
        storage{};
    auto* vigil_ptr = reinterpret_cast<crucible::Vigil*>(storage.data());
    CrucibleHandle raw = static_cast<CrucibleHandle>(vigil_ptr);

    auto typed = crucible::vessel::as_vigil_typed(raw);
    EXPECT(typed.value() == vigil_ptr,
           "typed handle must preserve the Vigil pointer value");

    CrucibleHandle roundtrip = crucible::vessel::from_typed(typed);
    EXPECT(roundtrip == raw, "typed handle must roundtrip to CrucibleHandle");
}

} // namespace

int main() {
    test_roundtrip();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_vessel_api_typed: FAIL (%d)\n", g_failures);
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_vessel_api_typed: PASS\n");
    return EXIT_SUCCESS;
}
