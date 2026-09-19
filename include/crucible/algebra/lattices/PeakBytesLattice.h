#pragma once

// Bounded chain over the peak byte count resident while a value is
// produced.
//
// The order runs by consumption, so the larger count is the higher
// element.  That is the opposite of the tier chains, where the strongest
// claim sits at the top, and it is what makes a gate reading "held at
// most M bytes" admit exactly the values below its own grade.
//
// The join is a maximum, not a sum.  Two peaks combine to the larger one
// only because the productions do not overlap in time.  Adding the peaks
// of concurrent producers is a different calculation and belongs to
// whatever plans the memory, not to this order.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>

#include <compare>
#include <cstdint>
#include <cstdlib>  // std::abort in the runtime smoke test
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// A distinct type, not a bare integer.  The sibling budget axis is also
// a 64-bit unsigned count, so without separate types a call site that
// swapped the two axes would still compile and every downstream
// comparison would silently read the wrong counter.
struct PeakBytes {
    std::uint64_t value{0};

    [[nodiscard]] constexpr bool operator==(PeakBytes const&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(PeakBytes const&) const noexcept = default;

    [[nodiscard]] constexpr operator std::uint64_t() const noexcept { return value; }
};

struct PeakBytesLattice {
    using element_type = PeakBytes;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return element_type{0}; }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return element_type{std::numeric_limits<std::uint64_t>::max()};
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a.value <= b.value; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return element_type{a.value >= b.value ? a.value : b.value};
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return element_type{a.value <= b.value ? a.value : b.value};
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "PeakBytesLattice"; }
};

namespace detail::peak_bytes_lattice_self_test {

static_assert(Lattice<PeakBytesLattice>);
static_assert(BoundedLattice<PeakBytesLattice>);
static_assert(!UnboundedLattice<PeakBytesLattice>);
static_assert(!Semiring<PeakBytesLattice>);

static_assert(sizeof(PeakBytes) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<PeakBytes>);
static_assert(std::is_standard_layout_v<PeakBytes>);

static_assert(!std::is_same_v<PeakBytes, std::uint64_t>);

static_assert(PeakBytesLattice::leq(PeakBytes{0}, PeakBytes{1024}));
static_assert(PeakBytesLattice::leq(PeakBytes{42}, PeakBytes{42}));
static_assert(!PeakBytesLattice::leq(PeakBytes{2048}, PeakBytes{1024}));

static_assert(PeakBytesLattice::bottom().value == 0);
static_assert(PeakBytesLattice::top().value == std::numeric_limits<std::uint64_t>::max());

static_assert(PeakBytesLattice::join(PeakBytes{1024}, PeakBytes{2048}).value == 2048);
static_assert(PeakBytesLattice::join(PeakBytes{2048}, PeakBytes{1024}).value == 2048);
static_assert(PeakBytesLattice::meet(PeakBytes{1024}, PeakBytes{2048}).value == 1024);
static_assert(PeakBytesLattice::meet(PeakBytes{2048}, PeakBytes{1024}).value == 1024);

static_assert(PeakBytesLattice::join(PeakBytes{1024}, PeakBytesLattice::bottom()) == PeakBytes{1024});
static_assert(PeakBytesLattice::meet(PeakBytes{1024}, PeakBytesLattice::top()) == PeakBytes{1024});

static_assert(PeakBytesLattice::join(PeakBytes{99}, PeakBytes{99}).value == 99);
static_assert(PeakBytesLattice::meet(PeakBytes{99}, PeakBytes{99}).value == 99);

// The interior witnesses matter: bottom and top satisfy distributivity
// for reasons that have nothing to do with the order between them.
[[nodiscard]] consteval bool distributive_witness() noexcept {
    PeakBytes a{16};
    PeakBytes b{64};
    PeakBytes c{256};
    auto lhs = PeakBytesLattice::meet(a, PeakBytesLattice::join(b, c));
    auto rhs = PeakBytesLattice::join(PeakBytesLattice::meet(a, b), PeakBytesLattice::meet(a, c));
    return lhs == rhs;
}
static_assert(distributive_witness());

static_assert(verify_bounded_lattice_axioms_at<PeakBytesLattice>(PeakBytesLattice::bottom(), PeakBytes{4096},
                                                                 PeakBytesLattice::top()));
static_assert(verify_bounded_lattice_axioms_at<PeakBytesLattice>(PeakBytes{0}, PeakBytes{1024}, PeakBytes{2048}));
static_assert(verify_bounded_lattice_axioms_at<PeakBytesLattice>(PeakBytes{16}, PeakBytes{64}, PeakBytes{256}));
static_assert(verify_distributive_lattice<PeakBytesLattice>(PeakBytesLattice::bottom(), PeakBytes{4096},
                                                            PeakBytesLattice::top()));
static_assert(verify_distributive_lattice<PeakBytesLattice>(PeakBytes{16}, PeakBytes{64}, PeakBytes{256}));
static_assert(verify_distributive_lattice<PeakBytesLattice>(PeakBytes{1024}, PeakBytes{1024}, PeakBytes{2048}));

static_assert([] consteval {
    PeakBytes b{1024};
    std::uint64_t n = b;
    return n == 1024;
}());

inline void runtime_smoke_test() {
    PeakBytes bot = PeakBytesLattice::bottom();
    PeakBytes topv = PeakBytesLattice::top();
    PeakBytes mid{4096};
    [[maybe_unused]] bool l = PeakBytesLattice::leq(bot, topv);
    [[maybe_unused]] PeakBytes j = PeakBytesLattice::join(mid, topv);
    [[maybe_unused]] PeakBytes m = PeakBytesLattice::meet(mid, bot);

    PeakBytes stage1{2048};
    PeakBytes stage2{4096};
    PeakBytes composed = PeakBytesLattice::join(stage1, stage2);
    if (composed.value != 4096u) std::abort();

    std::uint64_t total = mid;
    if (total != 4096u) std::abort();

    using PeakBytesGraded = Graded<ModalityKind::Absolute, PeakBytesLattice, double>;
    PeakBytesGraded v{3.14, PeakBytes{8192}};
    [[maybe_unused]] auto g = v.grade();
    [[maybe_unused]] auto vp = v.peek();
}

}  // namespace detail::peak_bytes_lattice_self_test

}  // namespace crucible::algebra::lattices
