#pragma once

// A cache key pairs a content hash with the row hash computed here. Two
// computations whose content hashes match but whose rows differ are the
// same work under different effect regimes, and they must land in
// different slots. A pure-row kernel is safe to share between
// installations. An IO-row kernel is not. One slot for both silently
// breaks that.
//
// Four properties hold, and consumers depend on all four.
//
// A row is a set of effect atoms, so the hash is invariant under
// permutation of the pack and under repeated atoms. Sorting and dedup
// before the fold buy that. Without them two spellings of one row would
// address two slots and fragment the cache.
//
// Cardinality participates, because a longer row is a strictly stronger
// capability claim than its prefix.
//
// A bare type contributes zero. That value means "no row" and matches
// the default a cache key is born with.
//
// The empty row is not zero. It is a real row that happens to carry no
// effects, so it is seeded from the hash offset basis instead. Were it
// zero, a computation carrying an empty row would alias its own bare
// payload, and those are different things.
//
// Old spelling: include/crucible/safety/diag/RowHashFold.h.
//
// What changed, and it is the point of the port. The old header carried
// one partial specialization per wrapper, fifty-two of them, each
// pairing a hand-allocated salt byte with the wrapper's own template
// parameters. Every new wrapper took a new salt, a new specialization
// and a new chance to reuse a byte. In this tree a wrapper is a spelling
// over Graded, so one specialization over Graded covers every one of
// them: the axis is the lattice type, the tier is the lattice's pinned
// grade, and the payload is the fold's own recursion. Adding a wrapper
// now adds nothing here.
//
// Two salts survive, because the two types they cover are not Graded:
// the effect row itself and the computation carrier.
//
// The old header had a sibling, RowHashGrade.h, answering the other
// question: which instance is this, rather than which type. That surface
// is not ported here and its old header is not marked, so a reader
// looking for row_hash_with_grade finds it where it has always been.
//
// Portability bound, and the fold widens it. These hashes agree only
// within one compiler, standard library and ABI. The arithmetic, the
// effect enum values and the salts are all portable, but a lattice's
// identity is derived from a reflected name, and that name is
// implementation-specific. Under the old per-wrapper salts only a
// handful of wrapper kinds folded such a name; under one fold every
// graded wrapper does. Two peers on different toolchains can therefore
// compute different hashes for one type, or collide two types onto one
// slot if their names happen to hash alike. The bare hash is a safe join
// key only among peers on one toolchain. Peers that are not must key
// through federation_key_with_toolchain, which makes the two toolchains
// disjoint by construction. The toolchain is deliberately kept out of
// row_hash_contribution itself, because folding it in would move every
// hash already published.

#include <foundation/algebra/Modality.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// The carrier is forward-declared rather than included. A partial
// specialization only has to deduce its template parameters, and pulling
// the definition in would drag the graded substrate, the whole lattice
// family and the reflection paths into every consumer of this header. A
// translation unit that names the carrier as an argument includes its
// own header for it.
//
// Graded needs no declaration at all, because the fold matches on the
// shape a wrapper publishes rather than on the template itself.
namespace foundation::effects {
template <typename R, typename T>
class Computation;
}  // namespace foundation::effects

namespace foundation::diag {

namespace detail {

using ::foundation::reflect::combine_ids;
using ::foundation::reflect::fmix64;
using ::foundation::reflect::detail::FNV1A_OFFSET_BASIS;

// Two salts, because two types in this scheme are not graded wrappers.
// The row is the grade itself rather than a carrier of one, and the
// computation carrier pairs a row with a payload without pinning a
// lattice. Everything else reaches the fold below.
//
// A salt owns a distinct high byte and nothing ors into the low bytes of
// either, because neither type has a tier to encode. Changing a salt
// already in use moves every hash published under it.
inline constexpr std::uint64_t WRAPPER_GRADED_TAG = 0x0100000000000000ULL;

// The sort is quadratic. The array holds one entry per effect atom and
// the universe is capped well below the point where that matters, which
// is cheaper than depending on the algorithm header for a bounded
// compile-time problem.
template <std::size_t N>
[[nodiscard]] consteval std::array<std::uint64_t, N> sorted_uints(std::array<std::uint64_t, N> xs) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (xs[j] < xs[i]) {
                std::uint64_t const tmp = xs[i];
                xs[i] = xs[j];
                xs[j] = tmp;
            }
        }
    }
    return xs;
}

// The array must already be sorted, and the caller must have encoded
// cardinality into the seed. An effect whose underlying value is zero
// would otherwise collide with the empty row, because mixing a seed with
// zero returns the mix of the seed alone.
template <std::size_t N>
[[nodiscard]] consteval std::uint64_t fmix64_fold(std::array<std::uint64_t, N> const& xs, std::uint64_t seed) noexcept {
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < N; ++i) {
        h = fmix64(h ^ xs[i]);
    }
    return h;
}

// The seed takes this count, not the pack size. A row written with a
// repeated atom must seed the same as the row written once, or the two
// diverge in the seed even though the dedup fold makes the rest of the
// digest identical.
template <std::size_t N>
[[nodiscard]] consteval std::size_t unique_count_sorted(std::array<std::uint64_t, N> const& xs) noexcept {
    if constexpr (N == 0) {
        return 0;
    } else {
        std::size_t count = 1;
        for (std::size_t i = 1; i < N; ++i) {
            if (xs[i] != xs[i - 1]) ++count;
        }
        return count;
    }
}

// Skipping adjacent repeats is what makes the row hash a function of the
// effect set. Two rows over the same atoms agree whatever their
// declaration order and whatever their multiplicity. The seed must carry
// the unique count for the same reason.
template <std::size_t N>
[[nodiscard]] consteval std::uint64_t fmix64_fold_unique_sorted(std::array<std::uint64_t, N> const& xs,
                                                                std::uint64_t seed) noexcept {
    if constexpr (N == 0) {
        return seed;
    } else {
        std::uint64_t h = fmix64(seed ^ xs[0]);
        for (std::size_t i = 1; i < N; ++i) {
            if (xs[i] != xs[i - 1]) {
                h = fmix64(h ^ xs[i]);
            }
        }
        return h;
    }
}

// Every row hash starts here. Mixing the cardinality into the seed keeps
// rows of different length apart whatever coincidences the fold over
// their bodies produces.
[[nodiscard]] consteval std::uint64_t cardinality_seed(std::uint64_t cardinality) noexcept {
    return fmix64(FNV1A_OFFSET_BASIS ^ cardinality);
}

inline constexpr std::uint64_t EMPTY_ROW_HASH = cardinality_seed(0);

// This names the compiler and standard library through predefined
// macros alone. It touches no reflected name, so it is identical on any
// machine running one toolchain and differs across toolchains by
// construction. That is exactly the discriminator a cross-toolchain
// federation key needs, and it stays out of the row hash itself.
[[nodiscard]] consteval std::uint64_t federation_toolchain_id() noexcept {
    std::uint64_t h = FNV1A_OFFSET_BASIS;
    h = combine_ids(h, static_cast<std::uint64_t>(__GNUC__));
    h = combine_ids(h, static_cast<std::uint64_t>(__GNUC_MINOR__));
    h = combine_ids(h, static_cast<std::uint64_t>(__GNUC_PATCHLEVEL__));
    h = combine_ids(h, static_cast<std::uint64_t>(__GLIBCXX__));
    return h;
}

inline constexpr std::uint64_t FEDERATION_TOOLCHAIN_TAG = federation_toolchain_id();

}  // namespace detail

// A bare type carries no row. Every wrapper that carries one is a
// spelling over Graded and reaches the fold below, so this primary is
// the answer for payload types only, and the answer is "nothing".
template <typename T>
struct row_hash_contribution {
    static constexpr std::uint64_t value = 0;
};

template <typename T>
inline constexpr std::uint64_t row_hash_contribution_v = row_hash_contribution<T>::value;

// The identity of one axis at one tier. A lattice type is the axis and
// the pinned grade together: DetSafeLattice::At<Pure> and
// DetSafeLattice::At<Impure> are different types, so they take different
// slots without either one naming a salt.
//
// Specializing this maps a second lattice type onto an existing
// identity. That is what a rename or an alias needs in order to keep
// reaching entries already cached, and what peers need when they spell
// one axis differently. It is also where a predicate rename lands, since
// a refinement's predicate is a template argument of its lattice.
// Aliasing a lattice without specializing here fragments the cache
// silently.
//
// The default is a reflected name, which is the toolchain bound the
// header comment sets out. Nothing here falls back to a shared constant
// when a lattice declines to name itself: two unnamed lattices must not
// share a slot, and a type always has a name even when it does not
// publish one.
template <typename L>
struct lattice_canonical_id {
    static constexpr std::uint64_t value = ::foundation::reflect::stable_type_id<L>;
};

template <typename L>
inline constexpr std::uint64_t lattice_canonical_id_v = lattice_canonical_id<L>::value;

// A row denotes a set of effect atoms, and union, intersection and
// difference over rows are all set-shaped, so hash equality has to match
// set equality. Composing two policies that each declare one effect can
// hand this a pack with a repeat, and without the dedup that pack would
// take a slot of its own for a row the type system already treats as
// equal.

template <::foundation::effects::Effect... Es>
struct row_hash_contribution<::foundation::effects::Row<Es...>> {
    static constexpr std::uint64_t value = []() consteval -> std::uint64_t {
        constexpr std::size_t N = sizeof...(Es);
        if constexpr (N == 0) {
            return detail::cardinality_seed(0);
        } else {
            std::array<std::uint64_t, N> const raw_vals{static_cast<std::uint64_t>(
                static_cast<std::underlying_type_t<::foundation::effects::Effect>>(Es))...};
            auto const sorted = detail::sorted_uints(raw_vals);
            // The seed takes the number of distinct atoms, never the
            // pack size, and it is mixed before any atom. That also
            // keeps the atom whose underlying value is zero from folding
            // into the seed unchanged.
            std::size_t const unique_n = detail::unique_count_sorted(sorted);
            std::uint64_t const seed = detail::cardinality_seed(unique_n);
            return detail::fmix64_fold_unique_sorted(sorted, seed);
        }
    }();
};

// Combining the row with the payload contribution satisfies three
// properties this carrier owes the cache, and the self-tests pin all
// three.
//
// A carrier is distinct from its own bare row. A row is metadata and a
// carrier is a value, and the combiner returns something other than X
// when folding X with a zero payload contribution.
//
// A carrier is blind to a bare payload. Payload identity belongs to the
// content hash, the other half of the key, so a kernel returning one
// scalar type shares a row signature with a kernel returning another.
//
// A carrier does not collapse over a payload that carries a row of its
// own. A carrier nested inside a carrier keeps the inner row in the
// outer hash, so nesting cannot alias the flattened form.
//
// The row folds first and the payload second. That order is fixed, not
// incidental, because the combiner is order-sensitive.

template <typename R, typename T>
struct row_hash_contribution<::foundation::effects::Computation<R, T>> {
    static constexpr std::uint64_t value =
        detail::combine_ids(row_hash_contribution_v<R>, row_hash_contribution_v<T>);
};

// The shape the fold reads. A wrapper reaches the fold by publishing a
// modality, a lattice and a payload type, and those three names are the
// substrate's own contract: Graded declares them, and so does every
// wrapper that inherits the facade over Graded rather than aliasing it.
//
// Matching on the shape rather than on Graded itself is what makes the
// fold total. A wrapper written as an alias and a wrapper written as a
// class holding a Graded member are the same claim about a value, and a
// fold that saw only the alias would hand every class-shaped wrapper the
// primary template's zero. That is the fail-open the old header warned
// about, and the reason it needed a specialisation per wrapper.
template <typename W>
concept GradedShaped = requires {
    typename W::lattice_type;
    typename W::value_type;
    { W::modality } -> std::convertible_to<::foundation::algebra::ModalityKind>;
};

// The one fold. Every graded wrapper in the tree reaches this, and
// nothing else is needed for any of them.
//
// Three things separate one carrier from another, and all three fold in.
// The modality says how the grade relates to the value, so a comonadic
// carrier and an absolute one over the same lattice are different
// claims. The lattice says which axis at which tier. The payload
// recurses, which is what makes a stack of wrappers a fold rather than a
// table: the inner contribution reaches the outer hash.
//
// The combiner is order-sensitive, so a wrapper's position in the stack
// changes the hash. Two stacks that nest the same wrappers in opposite
// order are different keys, which is the intent: nesting order carries
// meaning, and there is one canonical order for callers to build in.
//
// What does not fold in is the runtime grade a relative or stepping
// carrier holds per instance. This key identifies a type, not an
// instance. A consumer that needs to tell two instances apart folds the
// grade in on top of this value rather than in place of it.
//
// The computation carrier above is graded-backed and so answers this
// concept too. Its own specialisation is the more specialised of the
// two, so it wins the partial order and keeps the row-first fold that
// its published hashes were built from.
template <GradedShaped W>
struct row_hash_contribution<W> {
    static constexpr std::uint64_t value =
        detail::combine_ids(detail::combine_ids(detail::WRAPPER_GRADED_TAG
                                                    | static_cast<std::uint64_t>(W::modality),
                                                lattice_canonical_id_v<typename W::lattice_type>),
                            row_hash_contribution_v<typename W::value_type>);
};

template <typename T>
[[nodiscard]] consteval std::uint64_t row_hash_of() noexcept {
    return row_hash_contribution_v<T>;
}

template <typename T>
inline constexpr std::uint64_t row_hash_of_v = row_hash_of<T>();

// Peers on one toolchain join on the bare row hash and need nothing
// here. Peers that may run different compilers or different major
// versions of one compiler key through the function below, which folds
// the toolchain in and maps the same computation to disjoint slots on
// each side. That prevents both the silent collision and the false
// sharing of a slot whose reflected-name bits only happened to match.
[[nodiscard]] consteval std::uint64_t federation_toolchain_tag() noexcept { return detail::FEDERATION_TOOLCHAIN_TAG; }

template <typename T>
[[nodiscard]] consteval std::uint64_t federation_key_with_toolchain() noexcept {
    return detail::combine_ids(detail::FEDERATION_TOOLCHAIN_TAG, row_hash_contribution_v<T>);
}

template <typename T>
inline constexpr std::uint64_t federation_key_with_toolchain_v = federation_key_with_toolchain<T>();

}  // namespace foundation::diag
