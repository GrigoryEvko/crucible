// Sentinel TU for fixy/Aliases.h: the F* row names order as a chain of
// budgets, the Is* concepts read that order, and the value forms Pure
// and Tot are the DetSafe band over a Computation, for a copyable and a
// move-only payload alike.

#include <fixy/Aliases.h>

#include <fixy/Bands.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstdio>
#include <memory>
#include <type_traits>
#include <utility>

namespace {

using ::fixy::Computation;
using ::fixy::Effect;
using ::fixy::Row;

struct MoveOnlyValue {
    int v{0};
    constexpr MoveOnlyValue() = default;
    constexpr explicit MoveOnlyValue(int x) noexcept : v{x} {}
    MoveOnlyValue(MoveOnlyValue const&) = delete;
    MoveOnlyValue& operator=(MoveOnlyValue const&) = delete;
    constexpr MoveOnlyValue(MoveOnlyValue&&) noexcept = default;
    constexpr MoveOnlyValue& operator=(MoveOnlyValue&&) noexcept = default;
};

// The rows are the budgets a caller row is checked against.
static_assert(fixy::IsPure<Row<>>);
static_assert(!fixy::IsPure<Row<Effect::Block>>);
static_assert(fixy::IsDiv<Row<Effect::Block>>);
static_assert(fixy::IsST<Row<Effect::Alloc, Effect::IO>>);
static_assert(!fixy::IsST<Row<Effect::Bg>>);
static_assert(fixy::IsAll<Row<Effect::Bg, Effect::Init, Effect::Test>>);

// The value forms compile for int and for a move-only payload, and each
// is the Pure-tier DetSafe band over the Computation, so it costs
// sizeof(T) and answers the band queries.
using PureInt = fixy::Pure<int>;
using PureMove = fixy::Pure<MoveOnlyValue>;
using TotIoInt = fixy::Tot<Row<Effect::IO>, int>;
using TotIoMove = fixy::Tot<Row<Effect::IO>, MoveOnlyValue>;

static_assert(sizeof(PureInt) == sizeof(int));
static_assert(sizeof(PureMove) == sizeof(MoveOnlyValue));
static_assert(sizeof(TotIoInt) == sizeof(int));
static_assert(sizeof(TotIoMove) == sizeof(MoveOnlyValue));

static_assert(fixy::IsBandOf<fixy::DetSafeLattice, PureInt>);
static_assert(fixy::IsBandOf<fixy::DetSafeLattice, TotIoMove>);
static_assert(fixy::band_tier_v<PureInt> == fixy::DetSafeTier_v::Pure);
static_assert(fixy::band_tier_v<TotIoMove> == fixy::DetSafeTier_v::Pure);
static_assert(std::is_same_v<fixy::band_value_t<PureMove>, Computation<Row<>, MoveOnlyValue>>);
static_assert(std::is_same_v<fixy::band_value_t<TotIoInt>::row_type, Row<Effect::IO>>);

static_assert(std::is_copy_constructible_v<PureInt>);
static_assert(!std::is_copy_constructible_v<PureMove>);
static_assert(std::is_move_constructible_v<PureMove>);
static_assert(std::is_move_constructible_v<TotIoMove>);

// A Pure value is admissible wherever PhiloxRng is required.
static_assert(fixy::satisfies_v<PureInt, fixy::DetSafeTier_v::PhiloxRng>);
static_assert(fixy::satisfies_v<TotIoInt, fixy::DetSafeTier_v::Pure>);

// The empty-row Computation opens with extract; a Tot over a non-empty
// row does not.
template <typename C>
concept can_extract = requires(C c) { std::move(c).extract(); };
static_assert(can_extract<fixy::band_value_t<PureInt>>);
static_assert(!can_extract<fixy::band_value_t<TotIoInt>>);

constexpr PureInt pinned{Computation<Row<>, int>{42}, {}};
static_assert(pinned.peek().extract() == 42);
static_assert(fixy::tier_of(pinned) == fixy::DetSafeTier_v::Pure);

// The alias types and the concept names must also compile outside a
// static_assert operand, where consteval-versus-constexpr accessor
// regressions and inline-body faults surface.  Nothing here can fail at
// run time; the value is that the bodies are instantiated at all.
void instantiate_every_alias_outside_an_assert() noexcept {
    [[maybe_unused]] constexpr bool pure_is_pure = fixy::IsPure<fixy::PureRow>;
    [[maybe_unused]] constexpr bool div_is_div = fixy::IsDiv<fixy::DivRow>;
    [[maybe_unused]] constexpr bool st_is_st = fixy::IsST<fixy::STRow>;
    [[maybe_unused]] constexpr bool all_is_all = fixy::IsAll<fixy::AllRow>;
    [[maybe_unused]] constexpr auto pure_size = ::foundation::effects::row_size_v<fixy::PureRow>;
    [[maybe_unused]] constexpr auto all_size = ::foundation::effects::row_size_v<fixy::AllRow>;

    static_assert(fixy::IsPure<fixy::PureRow> && fixy::IsAll<fixy::AllRow>);

    fixy::Pure<int> pure_value{};
    fixy::Tot<Row<Effect::IO>, int> tot_value{};

    [[maybe_unused]] auto pure_tier = fixy::tier_of(pure_value);
    [[maybe_unused]] auto tot_tier = fixy::tier_of(tot_value);

    static_assert(std::is_same_v<decltype(pure_value), fixy::Pure<int>>);
    static_assert(std::is_same_v<decltype(tot_value), fixy::Tot<Row<Effect::IO>, int>>);
}

}  // namespace

int main() {
    instantiate_every_alias_outside_an_assert();

    volatile int raw = 5;
    const int seed = raw;

    PureInt pure{Computation<Row<>, int>{seed}, {}};
    if (pure.peek().extract() != seed) {
        std::fprintf(stderr, "test_aliases: Pure<int> lost its value\n");
        return 1;
    }

    PureMove pure_move{Computation<Row<>, MoveOnlyValue>{MoveOnlyValue{seed}}, {}};
    MoveOnlyValue out = std::move(pure_move).consume().extract();
    if (out.v != seed) {
        std::fprintf(stderr, "test_aliases: Pure<MoveOnlyValue> lost its value\n");
        return 1;
    }

    TotIoMove tot_move{Computation<Row<Effect::IO>, MoveOnlyValue>{MoveOnlyValue{seed}}, {}};
    auto relaxed = fixy::relax<fixy::DetSafeTier_v::PhiloxRng>(std::move(tot_move));
    if (fixy::tier_of(relaxed) != fixy::DetSafeTier_v::PhiloxRng) {
        std::fprintf(stderr, "test_aliases: relax of a Tot value did not move the tier\n");
        return 1;
    }
    if (std::move(relaxed).consume().graded().consume().v != seed) {
        std::fprintf(stderr, "test_aliases: Tot<IO, MoveOnlyValue> lost its value\n");
        return 1;
    }

    fixy::Tot<fixy::AllRow, std::unique_ptr<int>> tot_all{
        Computation<fixy::AllRow, std::unique_ptr<int>>{std::make_unique<int>(seed)}, {}};
    if (*tot_all.peek().graded().peek() != seed) {
        std::fprintf(stderr, "test_aliases: Tot<AllRow, unique_ptr<int>> lost its value\n");
        return 1;
    }
    return 0;
}
