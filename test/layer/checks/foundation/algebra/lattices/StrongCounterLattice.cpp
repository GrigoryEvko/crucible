// The compile-time checks of foundation/algebra/lattices/StrongCounterLattice.h.

#include <foundation/algebra/lattices/StrongCounterLattice.h>

namespace foundation::algebra::lattices {

namespace detail::strong_counter_lattice_self_test {

// A count reached from genesis by `steps` successor steps: the only way
// a constant expression reaches an interior count.  Linear in steps.
template <typename L>
[[nodiscard]] consteval typename L::element_type after_steps(std::uint64_t steps) noexcept {
    typename L::element_type count = L::bottom();
    for (std::uint64_t i = 0; i < steps; ++i)
        count = L::successor(count);
    return count;
}

template <typename L>
[[nodiscard]] consteval bool laws_hold_for() noexcept {
    // The interior witnesses matter: bottom and top satisfy the
    // distributive law for reasons that have nothing to do with the
    // order between them.
    auto const c2 = after_steps<L>(2);
    auto const c5 = after_steps<L>(5);
    auto const c8 = after_steps<L>(8);
    auto const c42 = after_steps<L>(42);
    return verify_bounded_lattice_axioms_at<L>(L::bottom(), after_steps<L>(1024), L::top())
        && verify_bounded_lattice_axioms_at<L>(L::bottom(), c42, after_steps<L>(99))
        && verify_bounded_lattice_axioms_at<L>(after_steps<L>(1), c2, after_steps<L>(3))
        && verify_bounded_lattice_axioms_at<L>(c42, L::top(), L::bottom())
        && verify_distributive_lattice<L>(L::bottom(), after_steps<L>(1024), L::top())
        && verify_distributive_lattice<L>(c2, c5, c8) && verify_distributive_lattice<L>(c42, c42, c8)
        && verify_distributive_lattice<L>(c8, c2, c5);
}

template <typename L>
[[nodiscard]] consteval bool pins_hold_for() noexcept {
    constexpr std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
    auto const c3 = after_steps<L>(3);
    auto const c7 = after_steps<L>(7);
    auto const c41 = after_steps<L>(41);
    return L::bottom().raw() == 0 && L::top().raw() == max && L::leq(L::bottom(), c3) && L::leq(c7, c7)
        && !L::leq(c7, c3) && L::join(c3, c7).raw() == 7 && L::join(c7, c3).raw() == 7 && L::meet(c3, c7).raw() == 3
        && L::meet(c7, c3).raw() == 3 && L::join(c41, L::bottom()) == c41 && L::meet(c41, L::top()) == c41
        && L::join(L::top(), c41) == L::top() && L::meet(L::bottom(), c41) == L::bottom() &&
           typename L::element_type{} == L::bottom()
        // The successor is one step up and strictly above its input.
        && L::successor(L::bottom()).raw() == 1 && L::leq(c41, L::successor(c41))
        && !(L::successor(c41) == c41)
        // A bound is compared, never joined: at least and at most meet at
        // the count itself.
        && L::is_at_least(c7, typename L::bound_type{7}) && !L::is_at_least(c7, typename L::bound_type{8})
        && L::is_at_most(c7, typename L::bound_type{7}) && !L::is_at_most(c7, typename L::bound_type{6})
        && L::is_at_least(c7, typename L::bound_type{c7}) && L::is_at_most(L::top(), typename L::bound_type{max});
}

// The image carries the axis and the count, and names the axis first.
template <typename L>
[[nodiscard]] consteval bool image_pins_hold_for() noexcept {
    auto const image = L::image_of(after_steps<L>(258));
    return detail::count_image::read_word(image, 0) == L::image_axis()
        && detail::count_image::read_word(image, 8) == 258 && image[8] == std::byte{2} && image[9] == std::byte{1};
}

// The count cannot be written through an element, and no integer builds
// one.
template <typename E>
concept count_is_writable = requires(E e) { e.raw() = 0; };

template <typename L>
[[nodiscard]] consteval bool shape_holds_for() noexcept {
    using E = typename L::element_type;
    using B = typename L::bound_type;
    return Lattice<L> && BoundedLattice<L> && !UnboundedLattice<L> && !Semiring<L> && sizeof(E) == sizeof(std::uint64_t)
        && std::is_standard_layout_v<E> && !std::is_same_v<E, std::uint64_t> && !std::is_convertible_v<E, std::uint64_t>
        && !std::is_convertible_v<std::uint64_t, E> && !std::is_constructible_v<E, std::uint64_t>
        && !std::is_constructible_v<E, int> && !std::is_constructible_v<E, B>
        && !std::is_convertible_v<B, E>
        // Not buildable from bytes, and still passed in a register.
        && !std::is_trivially_copyable_v<E> && std::is_trivially_copy_constructible_v<E>
        && std::is_trivially_move_constructible_v<E> && std::is_trivially_destructible_v<E>
        && !::foundation::lifetime::ImplicitLifetimeThroughout<E> && !count_is_writable<E>;
}

static_assert(shape_holds_for<EpochLattice>());
static_assert(shape_holds_for<GenerationLattice>());
static_assert(shape_holds_for<PeakBytesLattice>());
static_assert(shape_holds_for<BitsBudgetLattice>());

static_assert(pins_hold_for<EpochLattice>());
static_assert(pins_hold_for<GenerationLattice>());
static_assert(pins_hold_for<PeakBytesLattice>());
static_assert(pins_hold_for<BitsBudgetLattice>());

static_assert(laws_hold_for<EpochLattice>());
static_assert(laws_hold_for<GenerationLattice>());
static_assert(laws_hold_for<PeakBytesLattice>());
static_assert(laws_hold_for<BitsBudgetLattice>());

static_assert(image_pins_hold_for<EpochLattice>() && image_pins_hold_for<BitsBudgetLattice>());
static_assert(EpochLattice::image_axis() != GenerationLattice::image_axis()
                  && PeakBytesLattice::image_axis() != BitsBudgetLattice::image_axis()
                  && EpochLattice::image_axis() != PeakBytesLattice::image_axis(),
              "two axes share one image identity, so an image of one reads back as the other");

// The axis words are a wire format.  They depend on declared identifiers
// only, and these values hold on each toolchain.  A renamed tag or a
// changed fold fails here, before the read of a stored record fails.
static_assert(detail::count_image::source_path(^^counter_tags::epoch)
              == "foundation::algebra::lattices::counter_tags::epoch");
static_assert(EpochLattice::image_axis() == 0x2c4bdfd19c9f0e35ULL);
static_assert(GenerationLattice::image_axis() == 0xf680919b94cde330ULL);
static_assert(PeakBytesLattice::image_axis() == 0x21904f464c944554ULL);
static_assert(BitsBudgetLattice::image_axis() == 0xcc6bdfaca385eca3ULL);

// A template specialization has no source path, because the path cannot
// hold its arguments.  A tag of that shape has no image door.  The test
// of a tag in an unnamed namespace is in a source file, because a header
// holds no unnamed namespace.
template <int Width>
struct templated_axis {
    static constexpr std::string_view lattice_name = "TemplatedAxis";
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::weaker_is_higher;
};
template <typename L>
concept has_image_door = requires(typename L::element_type count) { L::image_of(count); };
static_assert(has_image_door<EpochLattice>);
static_assert(!has_image_door<StrongCounterLattice<templated_axis<1>>>);

// The sum exists on a use axis and not on a version axis.
template <typename L>
concept has_sum = requires(typename L::element_type a) { L::saturating_sum(a, a); };
static_assert(has_sum<PeakBytesLattice> && has_sum<BitsBudgetLattice>);
static_assert(!has_sum<EpochLattice> && !has_sum<GenerationLattice>);
static_assert(PeakBytesLattice::saturating_sum(after_steps<PeakBytesLattice>(3), after_steps<PeakBytesLattice>(4)).raw()
              == 7);
static_assert(PeakBytesLattice::saturating_sum(PeakBytesLattice::top(), after_steps<PeakBytesLattice>(4))
                  == PeakBytesLattice::top(),
              "the sum clamps at the top");

// The four axes are four types, and no two of them mix.  The pairs
// below cover each unordered pair once.
template <typename A, typename B>
concept mixes = std::is_convertible_v<A, B> || std::is_convertible_v<B, A> || requires(A a, B b) { a == b; };

static_assert(!std::is_same_v<Epoch, Generation>);
static_assert(!std::is_same_v<PeakBytes, BitsBudget>);
static_assert(!mixes<Epoch, Generation>);
static_assert(!mixes<Epoch, PeakBytes>);
static_assert(!mixes<Epoch, BitsBudget>);
static_assert(!mixes<Generation, PeakBytes>);
static_assert(!mixes<Generation, BitsBudget>);
static_assert(!mixes<PeakBytes, BitsBudget>);
static_assert(!mixes<EpochBound, GenerationBound>);

// The detector answers yes for a pair that does mix, so the assertions
// above cannot pass vacuously.
static_assert(mixes<Epoch, Epoch>);

static_assert(EpochLattice::name() == "EpochLattice");
static_assert(GenerationLattice::name() == "GenerationLattice");
static_assert(PeakBytesLattice::name() == "PeakBytesLattice");
static_assert(BitsBudgetLattice::name() == "BitsBudgetLattice");

// The grade is carried per instance, so a carrier over one axis pays
// the eight bytes of the counter.
struct EightByteValue {
    unsigned long long v{0};
};
static_assert(sizeof(Graded<ModalityKind::Absolute, PeakBytesLattice, EightByteValue>) == 16);
static_assert(sizeof(Graded<ModalityKind::Absolute, BitsBudgetLattice, EightByteValue>) == 16);

// A use counter grades the Graded way.  A version counter in its numeric
// order does not, and Graded refuses it.
static_assert(GradableLattice<PeakBytesLattice> && GradableLattice<BitsBudgetLattice>);
static_assert(!GradableLattice<EpochLattice> && !GradableLattice<GenerationLattice>);

// A tag that states no orientation is not a counter tag.
struct silent_tag {
    static constexpr std::string_view lattice_name = "Silent";
};
struct unstated_tag {
    static constexpr std::string_view lattice_name = "Unstated";
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::unstated;
};
static_assert(!CounterTag<silent_tag> && !CounterTag<unstated_tag>);
static_assert(CounterTag<counter_tags::epoch> && CounterTag<counter_tags::bits_budget>);

}  // namespace detail::strong_counter_lattice_self_test

}  // namespace foundation::algebra::lattices
