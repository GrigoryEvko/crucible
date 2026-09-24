// Attacks on the stable identity of a type.
//
// A stable id hashes the printed name of a type, so the name must be a
// function of the type alone: two types must never print one name, and
// one type must print one name in every translation unit.  Each attack
// below is legal C++ that tries to break one of the two.  An attack that
// the walk refuses is pinned by a static assertion here.  An attack that
// still succeeds is an entry of the ledger at the foot of this file, and
// the ledger only shrinks.

#include <foundation/reflect/Hash.h>

#include <cstdio>
#include <cstdlib>
#include <meta>
#include <string_view>

namespace identity_attacks {

using ::foundation::reflect::HasStableIdentity;
using ::foundation::reflect::stable_name_of;
using ::foundation::reflect::stable_type_id;

template <auto Value>
struct Holds {};
template <int& Reference>
struct HoldsReference {};
template <std::meta::info Named>
struct HoldsReflection {};
struct Address {
    int const* pointer;
};
struct Named {};

// A variable and a function with internal linkage.  Every translation
// unit has its own, and each prints the same name.
static int internal_counter = 0;
static int internal_helper(int value) { return value + internal_counter; }
int const internal_constant = 7;
inline int external_counter = 0;
inline int external_helper(int value) { return value; }

// ── Attacks that the walk refuses ────────────────────────────────────

// An object named by its address, by a reference, or inside a class
// value.  No query reads the object back to its variable, so the walk
// refuses the address whatever the linkage of the variable.
static_assert(!HasStableIdentity<Holds<&internal_counter>>);
static_assert(!HasStableIdentity<Holds<&internal_constant>>);
static_assert(!HasStableIdentity<Holds<&external_counter>>);
static_assert(!HasStableIdentity<HoldsReference<internal_counter>>);
static_assert(!HasStableIdentity<Holds<Address{&internal_constant}>>);

// A reflection names an entity, and the walk reads that entity.
static_assert(!HasStableIdentity<HoldsReflection<^^internal_counter>>);
static_assert(!HasStableIdentity<HoldsReflection<^^internal_helper>>);
static_assert(HasStableIdentity<HoldsReflection<^^external_counter>>);
static_assert(HasStableIdentity<HoldsReflection<^^external_helper>>);
static_assert(HasStableIdentity<HoldsReflection<^^Named>>);

// A function named by a pointer is read to the function.
static_assert(!HasStableIdentity<Holds<&internal_helper>>);
static_assert(HasStableIdentity<Holds<&external_helper>>);

// A class value arrives as a template parameter object, which prints the
// value, so the walk reads it as a value.  A reference to a constexpr
// variable of the same type prints the variable, and the walk refuses it.
struct Bound {
    int limit;
};
inline constexpr Bound named_bound{4};
template <Bound const& Reference>
struct HoldsBoundReference {};
static_assert(HasStableIdentity<Holds<Bound{3}>>);
static_assert(HasStableIdentity<Holds<named_bound>>);
static_assert(!HasStableIdentity<HoldsBoundReference<named_bound>>);

// Two classes of one name in two blocks of one function.  The walk
// appends the line and column of a class that a function body declares.
struct LocalIds {
    std::uint64_t inner = 0;
    std::uint64_t outer = 0;
};

inline LocalIds two_classes_one_name() {
    LocalIds ids{};
    {
        struct Shadowed {
            int field;
        };
        ids.inner = stable_type_id<Shadowed>;
    }
    struct Shadowed {
        double field;
    };
    ids.outer = stable_type_id<Shadowed>;
    return ids;
}

// ── The ledger ───────────────────────────────────────────────────────
//
// Each entry names an attack that still succeeds and the reason that no
// type or guard can refuse it.

struct KnownLimit {
    std::string_view attack;
    std::string_view reason;
};

inline constexpr KnownLimit kLedger[] = {
    {"two classes of one name that one macro expansion declares in two blocks of one function share an id",
     "reflection reports the expansion point as the position of both classes and gives no other discriminator, "
     "so only a refusal of every local class could refuse this pair"},
};
static_assert(std::size(kLedger) <= 1, "the ledger only shrinks");

#define IDENTITY_ATTACK_TWO_LOCALS(first, second) \
    {                                             \
        struct Twin {                             \
            int field;                            \
        };                                        \
        first = stable_type_id<Twin>;             \
    }                                             \
    {                                             \
        struct Twin {                             \
            double field;                         \
        };                                        \
        second = stable_type_id<Twin>;            \
    }

inline LocalIds two_classes_one_expansion() {
    LocalIds ids{};
    IDENTITY_ATTACK_TWO_LOCALS(ids.inner, ids.outer)
    return ids;
}

}  // namespace identity_attacks

int main() {
    using namespace identity_attacks;
    int failures = 0;
    const auto expect = [&failures](bool condition, char const* what) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    };

    const LocalIds shadowed = two_classes_one_name();
    expect(shadowed.inner != shadowed.outer, "two local classes of one name share a stable id");
    expect(two_classes_one_name().inner == shadowed.inner, "one local class gives two ids");
    expect(stable_name_of<Named>.find(" @") == std::string_view::npos, "a namespace class carries a position");
    expect(internal_helper(1) == 1, "the internal helper is used");
    expect(stable_type_id<Holds<Bound{3}>> != stable_type_id<Holds<Bound{4}>>, "two class values share an id");
    expect(stable_type_id<Holds<named_bound>> == stable_type_id<Holds<Bound{4}>>,
           "one class value gives two ids when a variable spells it");

    // The ledger entry reproduces: when it stops reproducing, delete it.
    const LocalIds twins = two_classes_one_expansion();
    expect(twins.inner == twins.outer,
           "stale ledger entry: two local classes of one macro expansion no longer share an id, delete it");

    if (failures != 0) return EXIT_FAILURE;
    std::printf("test_identity_attacks: every refused attack refused, %zu ledger entry reproduces\n",
                std::size(kLedger));
    return EXIT_SUCCESS;
}
