#include <crucible/effects/OsUniverse.h>

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/EffectRowLattice.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <type_traits>

namespace {

struct TestFailure {};
int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

#define EXPECT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw TestFailure{};                                                                     \
        }                                                                                            \
    } while (0)

namespace fx = ::crucible::effects;
using U = fx::OsUniverse;
using L = fx::EffectRowLattice;
using EL = L::element_type;

// A templated function constrained on the Universe concept fires the
// substitution check at each call site.  That catches a regression the
// header's static_assert wall can miss, such as a refactor dropping
// the `lattice` typedef.

template <fx::Universe Univ>
constexpr bool admits_universe() noexcept {
    return true;
}

// This one additionally proves the consteval forwarding still composes
// after substitution.
template <fx::Universe Univ>
constexpr std::string_view describe_atom(typename Univ::atom_t a) noexcept {
    return Univ::atom_name(a);
}

void test_universe_concept_satisfied() { EXPECT_TRUE(admits_universe<U>()); }

void test_atom_t_binding() { EXPECT_TRUE((std::is_same_v<U::atom_t, fx::Effect>)); }

void test_lattice_binding() { EXPECT_TRUE((std::is_same_v<U::lattice, fx::EffectRowLattice>)); }

void test_cardinality_matches_effect_count() {
    EXPECT_TRUE(U::cardinality == fx::effect_count);
    EXPECT_TRUE(U::cardinality == 6);
}

void test_name_runtime() {
    auto nm = U::name();
    EXPECT_TRUE(nm == "OsUniverse");
}

void test_atom_name_runtime() {
    // The arguments are non-constant so the forwarder is exercised at
    // the boundary rather than answered by constant folding.
    fx::Effect atoms[] = {
        fx::Effect::Alloc, fx::Effect::IO, fx::Effect::Block, fx::Effect::Bg, fx::Effect::Init, fx::Effect::Test,
    };
    const char* expected[] = {
        "Alloc", "IO", "Block", "Bg", "Init", "Test",
    };
    for (std::size_t i = 0; i < 6; ++i) {
        auto nm = U::atom_name(atoms[i]);
        EXPECT_TRUE(nm == std::string_view{expected[i]});
        EXPECT_TRUE(describe_atom<U>(atoms[i]) == std::string_view{expected[i]});
    }
}

void test_bit_position_matches_underlying_value() {
    // The contract is that an atom's bit position equals its
    // enumerator's underlying value.  Extending the universe is
    // append-only, so an existing position never moves.
    EXPECT_TRUE(U::bit_position(fx::Effect::Alloc) == 0);
    EXPECT_TRUE(U::bit_position(fx::Effect::IO) == 1);
    EXPECT_TRUE(U::bit_position(fx::Effect::Block) == 2);
    EXPECT_TRUE(U::bit_position(fx::Effect::Bg) == 3);
    EXPECT_TRUE(U::bit_position(fx::Effect::Init) == 4);
    EXPECT_TRUE(U::bit_position(fx::Effect::Test) == 5);
}

void test_bit_position_bridge_with_row_descriptor() {
    // The contract every row-carrying type rests on is that
    // row_descriptor_v<Row<E>> equals 1 << bit_position(E).
    fx::Effect atoms[] = {
        fx::Effect::Alloc, fx::Effect::IO, fx::Effect::Block, fx::Effect::Bg, fx::Effect::Init, fx::Effect::Test,
    };
    EL singletons[] = {
        fx::row_descriptor_v<fx::Row<fx::Effect::Alloc>>, fx::row_descriptor_v<fx::Row<fx::Effect::IO>>,
        fx::row_descriptor_v<fx::Row<fx::Effect::Block>>, fx::row_descriptor_v<fx::Row<fx::Effect::Bg>>,
        fx::row_descriptor_v<fx::Row<fx::Effect::Init>>,  fx::row_descriptor_v<fx::Row<fx::Effect::Test>>,
    };
    for (std::size_t i = 0; i < 6; ++i) {
        auto bp = U::bit_position(atoms[i]);
        EL mask = EL{1} << bp;
        EXPECT_TRUE(mask == singletons[i]);
    }
}

void test_at_singleton_bridge() {
    // At<Atoms...>::bits() collapses to row_descriptor_v<Row<Atoms...>>.
    // The header's static_asserts cover the compile-time path, so these
    // runtime reads are what prove the accessor agrees.
    EL at_alloc = L::At<fx::Effect::Alloc>::bits();
    EL rd_alloc = fx::row_descriptor_v<fx::Row<fx::Effect::Alloc>>;
    EXPECT_TRUE(at_alloc == rd_alloc);

    EL at_pair = L::At<fx::Effect::Alloc, fx::Effect::IO>::bits();
    EL rd_pair = fx::row_descriptor_v<fx::Row<fx::Effect::Alloc, fx::Effect::IO>>;
    EXPECT_TRUE(at_pair == rd_pair);

    EL at_universe = L::At<fx::Effect::Alloc, fx::Effect::IO, fx::Effect::Block, fx::Effect::Bg, fx::Effect::Init,
                           fx::Effect::Test>::bits();
    EXPECT_TRUE(at_universe == L::top());
}

void test_at_element_type_is_empty() {
    // Emptiness is what lets a [[no_unique_address]] grade slot
    // collapse to zero bytes, which is the whole zero-cost claim of
    // every row-carrying wrapper built on this lattice.
    EXPECT_TRUE((std::is_empty_v<L::At<>::element_type>));
    EXPECT_TRUE((std::is_empty_v<L::At<fx::Effect::Alloc>::element_type>));
    EXPECT_TRUE((std::is_empty_v<L::At<fx::Effect::Alloc, fx::Effect::IO>::element_type>));
}

void test_at_satisfies_lattice_concept() {
    EXPECT_TRUE((::crucible::algebra::Lattice<L::At<>>));
    EXPECT_TRUE((::crucible::algebra::Lattice<L::At<fx::Effect::Alloc>>));
    EXPECT_TRUE((::crucible::algebra::BoundedLattice<L::At<>>));
    EXPECT_TRUE((::crucible::algebra::BoundedLattice<L::At<fx::Effect::Alloc, fx::Effect::IO>>));
}

void test_universe_runtime_smoke() {
    fx::runtime_smoke_test_os_universe();
    // The smoke test is noexcept and returns nothing, so reaching this
    // line is the whole claim.
    EXPECT_TRUE(true);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_os_universe:\n");
    run_test("test_universe_concept_satisfied", test_universe_concept_satisfied);
    run_test("test_atom_t_binding", test_atom_t_binding);
    run_test("test_lattice_binding", test_lattice_binding);
    run_test("test_cardinality_matches_effect_count", test_cardinality_matches_effect_count);
    run_test("test_name_runtime", test_name_runtime);
    run_test("test_atom_name_runtime", test_atom_name_runtime);
    run_test("test_bit_position_matches_underlying_value", test_bit_position_matches_underlying_value);
    run_test("test_bit_position_bridge_with_row_descriptor", test_bit_position_bridge_with_row_descriptor);
    run_test("test_at_singleton_bridge", test_at_singleton_bridge);
    run_test("test_at_element_type_is_empty", test_at_element_type_is_empty);
    run_test("test_at_satisfies_lattice_concept", test_at_satisfies_lattice_concept);
    run_test("test_universe_runtime_smoke", test_universe_runtime_smoke);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
