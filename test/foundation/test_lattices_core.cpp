// Sentinel TU for the core-wrapper lattices and the saturation helpers.
// Each header carries its own static_asserts and an inline
// runtime_smoke_test; this file includes each and calls each once.
//
// It also pins the storage regime each lattice selects when it grades a
// value, because that is the property a wrapper author relies on and
// the one a lattice edit can silently change:
//
//   empty grade (EBO)         Bool, Trust, Conf::At, Qtt::At  sizeof(T)
//   grade is the value        Monotone                        sizeof(T)
//   grade derived from value  SeqPrefix with grade_of         sizeof(T)
//   two fields                Staleness, Fractional           more

#include <foundation/Saturate.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/BoolLattice.h>
#include <foundation/algebra/lattices/ConfLattice.h>
#include <foundation/algebra/lattices/FractionalLattice.h>
#include <foundation/algebra/lattices/MonotoneLattice.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/algebra/lattices/SeqPrefixLattice.h>
#include <foundation/algebra/lattices/StalenessSemiring.h>
#include <foundation/algebra/lattices/TrustLattice.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace {

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;

struct pred_positive {
    template <typename T>
    [[nodiscard]] static constexpr bool check(T const& v) noexcept {
        return v > T{0};
    }
};
struct source_internal {};

struct Payload {
    std::uint64_t a{0};
    std::uint64_t b{0};
};

// Regime 1: the grade is empty and collapses.
using RefinedInt = fa::Graded<fa::ModalityKind::Absolute, fl::BoolLattice<pred_positive>, int>;
using TaggedInt = fa::Graded<fa::ModalityKind::RelativeMonad, fl::TrustLattice<source_internal>, int>;
using SecretInt = fa::Graded<fa::ModalityKind::Comonad, fl::conf::SecretTier, int>;
using LinearInt = fa::Graded<fa::ModalityKind::Absolute, fl::qtt::LinearGrade, int>;
static_assert(sizeof(RefinedInt) == sizeof(int));
static_assert(sizeof(TaggedInt) == sizeof(int));
static_assert(sizeof(SecretInt) == sizeof(int));
static_assert(sizeof(LinearInt) == sizeof(int));
static_assert(sizeof(fa::Graded<fa::ModalityKind::Absolute, fl::qtt::LinearGrade, Payload>) == sizeof(Payload));

// Regime 2: the grade is the value.
using MonotonicU64 =
    fa::Graded<fa::ModalityKind::Absolute, fl::MonotoneLattice<std::uint64_t, std::less<std::uint64_t>>, std::uint64_t>;
static_assert(sizeof(MonotonicU64) == sizeof(std::uint64_t));

// Regime 3: the grade is derived from the value through grade_of.
struct Log {
    std::size_t count{0};
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count; }
};
struct Event {};
using AppendOnlyLog = fa::Graded<fa::ModalityKind::Absolute, fl::SeqPrefixLattice<Event>, Log>;
static_assert(fa::LatticeDerivesGrade<fl::SeqPrefixLattice<Event>, Log>);
static_assert(sizeof(AppendOnlyLog) == sizeof(Log));

// Regime 4: value and grade both stored.
using StaleU64 = fa::Graded<fa::ModalityKind::Absolute, fl::StalenessSemiring, std::uint64_t>;
using SharedU64 = fa::Graded<fa::ModalityKind::Absolute, fl::FractionalLattice, std::uint64_t>;
static_assert(sizeof(StaleU64) == sizeof(std::uint64_t) + sizeof(fl::StalenessSemiring::element_type));
static_assert(sizeof(SharedU64) == sizeof(std::uint64_t) + sizeof(fl::Rational));

// The two semirings are also lattices, and the two orders are distinct
// structures on one carrier.
static_assert(fa::Semiring<fl::QttSemiring> && fa::BoundedLattice<fl::QttSemiring>);
static_assert(fa::Semiring<fl::StalenessSemiring> && fa::BoundedLattice<fl::StalenessSemiring>);
static_assert(fa::Semiring<fl::FractionalLattice> && fa::BoundedLattice<fl::FractionalLattice>);

}  // namespace

int main() {
    ::foundation::sat::detail::saturate_self_test::runtime_smoke_test();
    fl::detail::bool_lattice_self_test::runtime_smoke_test();
    fl::detail::trust_lattice_self_test::runtime_smoke_test();
    fl::detail::conf_lattice_self_test::runtime_smoke_test();
    fl::detail::qtt_self_test::runtime_smoke_test();
    fl::detail::monotone_lattice_self_test::runtime_smoke_test();
    fl::detail::seq_prefix_lattice_self_test::runtime_smoke_test();
    fl::detail::staleness_semiring_self_test::runtime_smoke_test();
    fl::detail::fractional_lattice_self_test::runtime_smoke_test();

    // Each regime built at runtime with non-constant operands.
    int value = 7;  // deliberately not constexpr
    RefinedInt refined{value, fl::BoolLattice<pred_positive>::bottom()};
    MonotonicU64 monotonic{static_cast<std::uint64_t>(value)};
    AppendOnlyLog log = AppendOnlyLog::at_bottom();
    StaleU64 stale{static_cast<std::uint64_t>(value), fl::staleness::at(static_cast<std::uint64_t>(value))};
    SharedU64 shared{static_cast<std::uint64_t>(value), fl::Rational{1, 2}};
    if (refined.peek() != value) return 1;
    if (monotonic.grade() != static_cast<std::uint64_t>(value)) return 2;
    if (log.grade().length != 0) return 3;
    if (stale.grade().value != static_cast<std::uint64_t>(value)) return 4;
    if (!(shared.grade() == fl::Rational{2, 4})) return 5;
    return 0;
}
