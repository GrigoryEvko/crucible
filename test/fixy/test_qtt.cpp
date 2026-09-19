// Sentinel TU for fixy/Qtt.h: the two usage grades collapse to sizeof(T),
// the wrapper is move-only with one door, consume takes an rvalue only,
// and the header's runtime smoke test runs under the test flags.
//
// There is no runtime double-consume check to exercise; the header
// states the decision.

#include <fixy/Qtt.h>

#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/lattices/QttSemiring.h>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;
using ::fixy::Affine;
using ::fixy::Linear;
using QttGrade = fa::lattices::QttGrade;

struct TwoWords {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
};

// Regime 1: the grade is empty and collapses under EBO.
static_assert(sizeof(Linear<int>) == sizeof(int));
static_assert(sizeof(Linear<double>) == sizeof(double));
static_assert(sizeof(Linear<TwoWords>) == sizeof(TwoWords));
static_assert(alignof(Linear<TwoWords>) == alignof(TwoWords));
static_assert(sizeof(Affine<int>) == sizeof(int));
static_assert(sizeof(Affine<double>) == sizeof(double));
static_assert(sizeof(Affine<TwoWords>) == sizeof(TwoWords));
static_assert(alignof(Affine<TwoWords>) == alignof(TwoWords));

// The two aliases are the one template at its two bounded grades.
static_assert(std::is_same_v<Linear<int>, ::fixy::Qtt<QttGrade::One, int>>);
static_assert(std::is_same_v<Affine<int>, ::fixy::Qtt<QttGrade::Zero, int>>);
static_assert(!std::is_same_v<Linear<int>, Affine<int>>);

template <auto Grade>
concept CanNameQtt = requires { typename ::fixy::Qtt<Grade, int>; };

static_assert(CanNameQtt<QttGrade::One>);
static_assert(CanNameQtt<QttGrade::Zero>);
static_assert(!CanNameQtt<QttGrade::Omega>, "an unrestricted value is the bare T");
static_assert(!CanNameQtt<1>, "the grade is a QttGrade, not an integer");

// Move-only.
static_assert(!std::is_copy_constructible_v<Linear<int>>);
static_assert(!std::is_copy_assignable_v<Linear<int>>);
static_assert(std::is_nothrow_move_constructible_v<Linear<int>>);
static_assert(std::is_nothrow_move_assignable_v<Linear<int>>);
static_assert(!std::is_copy_constructible_v<Affine<int>>);
static_assert(!std::is_copy_assignable_v<Affine<int>>);
static_assert(std::is_nothrow_move_constructible_v<Affine<int>>);
static_assert(std::is_nothrow_move_assignable_v<Affine<int>>);

// One door: no public constructor of any shape.
static_assert(!std::is_default_constructible_v<Linear<int>>);
static_assert(!std::is_constructible_v<Linear<int>, int>);
static_assert(!std::is_constructible_v<Linear<int>, std::in_place_t, int>);
static_assert(!std::is_default_constructible_v<Affine<int>>);
static_assert(!std::is_constructible_v<Affine<int>, int>);
static_assert(!std::is_constructible_v<Affine<int>, std::in_place_t, int>);

// consume takes an rvalue and nothing else.
template <typename W>
concept ConsumesLvalue = requires(W& w) { w.consume(); };
template <typename W>
concept ConsumesConstRvalue = requires(W const& w) { std::move(w).consume(); };
template <typename W>
concept ConsumesRvalue = requires(W&& w) { std::move(w).consume(); };

static_assert(!ConsumesLvalue<Linear<int>>);
static_assert(!ConsumesConstRvalue<Linear<int>>);
static_assert(ConsumesRvalue<Linear<int>>);
static_assert(!ConsumesLvalue<Affine<int>>);
static_assert(!ConsumesConstRvalue<Affine<int>>);
static_assert(ConsumesRvalue<Affine<int>>);

// The diagnostic surface agrees with the substrate.
static_assert(fa::GradedWrapper<Linear<int>>);
static_assert(fa::GradedWrapper<Affine<int>>);
static_assert(fa::is_graded_wrapper_v<Linear<int>>);
static_assert(fa::is_graded_wrapper_v<Affine<int>>);
static_assert(Linear<int>::lattice_name() == "QttSemiring::At<1>");
static_assert(Affine<int>::lattice_name() == "QttSemiring::At<0>");
static_assert(Linear<int>::value_type_name().ends_with("int"));
static_assert(Affine<int>::value_type_name().ends_with("int"));
static_assert(std::is_same_v<Linear<int>::lattice_type, fa::lattices::qtt::LinearGrade>);
static_assert(std::is_same_v<Affine<int>::lattice_type, fa::lattices::qtt::Erased>);
static_assert(Linear<int>::modality == fa::ModalityKind::Absolute);
static_assert(Affine<int>::modality == fa::ModalityKind::Absolute);

// The rejection traits are fail-closed until a token specializes them.
static_assert(!::fixy::is_already_linear_v<int>);
static_assert(!::fixy::is_already_linear_v<Linear<int>>);
static_assert(!::fixy::is_already_consume_disciplined_v<int>);
static_assert(!::fixy::is_already_consume_disciplined_v<Affine<int>>);

// The mint is usable in a constant expression.
constexpr int minted_and_consumed = ::fixy::mint_linear<int>(7).peek();
static_assert(minted_and_consumed == 7);
constexpr int affine_minted = ::fixy::mint_affine<int>(9).peek();
static_assert(affine_minted == 9);

// A destructor counter that distinguishes drop's two branches: a Linear
// drop destroys the value inside drop, an Affine drop destroys nothing.
struct Tracked {
    int* destroyed = nullptr;

    explicit Tracked(int* counter) noexcept : destroyed{counter} {}
    Tracked(Tracked&& other) noexcept : destroyed{std::exchange(other.destroyed, nullptr)} {}
    Tracked& operator=(Tracked&& other) noexcept {
        destroyed = std::exchange(other.destroyed, nullptr);
        return *this;
    }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    ~Tracked() {
        if (destroyed != nullptr) ++*destroyed;
    }
};

int check_drop_branches() {
    int linear_destroyed = 0;
    {
        Linear<Tracked> owned = ::fixy::mint_linear<Tracked>(&linear_destroyed);
        drop(std::move(owned));
        if (linear_destroyed != 1) return 10;
    }
    if (linear_destroyed != 1) return 11;

    int affine_destroyed = 0;
    {
        Affine<Tracked> maybe = ::fixy::mint_affine<Tracked>(&affine_destroyed);
        drop(std::move(maybe));
        if (affine_destroyed != 0) return 12;
    }
    if (affine_destroyed != 1) return 13;

    return 0;
}

int check_consume_and_move() {
    Linear<std::unique_ptr<int>> owned = ::fixy::mint_linear<std::unique_ptr<int>>(std::make_unique<int>(5));
    if (*owned.peek() != 5) return 20;

    Linear<std::unique_ptr<int>> moved = std::move(owned);
    if (*moved.peek() != 5) return 21;

    std::unique_ptr<int> out = std::move(moved).consume();
    if (out == nullptr || *out != 5) return 22;

    Affine<std::unique_ptr<int>> speculative = ::fixy::mint_affine<std::unique_ptr<int>>(std::make_unique<int>(6));
    Affine<std::unique_ptr<int>> kept = std::move(speculative);
    if (*kept.peek() != 6) return 23;

    return 0;
}

}  // namespace

int main() {
    ::fixy::detail::qtt_self_test::runtime_smoke_test();

    if (int rc = check_drop_branches(); rc != 0) return rc;
    if (int rc = check_consume_and_move(); rc != 0) return rc;

    return 0;
}
