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
// Not every carrier that holds a discipline is a one-axis grade, so two
// more shapes reach a fold of their own. A session handle carries a
// protocol that steps on every operation, and it publishes the Stepping
// contract instead of a lattice. Every other carrier names its claim
// with a discipline identity and names its payload, and one fold reads
// those two. A type with a claim and neither shape still falls to the
// zero below, so test/fixy/test_row_hash_wrappers.cpp reads the carrier
// roster by reflection and fails on any member that folds to zero and
// has no stated reason to.
//
// The old header had a sibling, RowHashGrade.h, answering the other
// question: which instance is this, rather than which type. That surface
// is not ported here and its old header is not marked, so a reader
// looking for row_hash_with_grade finds it where it has always been.
//
// Portability bound, and it is not one bound but three. The arithmetic,
// the effect enum values and the salts are portable. A lattice identity
// comes from a reflected name, and that name is implementation-specific.
// So each specialisation below owns its own answer. A consumer that
// reads one blanket answer guards the wrong half.
//
// The row specialisation is portable. It folds the underlying values of
// an append-only enum through pure arithmetic, and it reaches no
// reflected name at all. A peer on another toolchain computes the same
// value for the same row.
//
// The graded fold is not portable, and the port widened that. Under the
// old per-wrapper salts a handful of wrapper kinds folded a reflected
// name. Under one fold every graded wrapper does, because the lattice
// identity is one of the fold's three inputs. The multi-axis binding in
// fixy/Fn.h folds such a name for every axis but two.
//
// The carrier is portable exactly when both of its halves are.
//
// So two peers on different toolchains can compute different hashes for
// one graded type. Or they can collide two graded types onto one slot,
// when their names happen to hash alike. A miss is safe and a hit is
// not, and nothing here selects between them. Peers that may differ must
// key through federation_key_with_toolchain, which makes the two
// toolchains disjoint by construction.
//
// The toolchain stays out of row_hash_contribution, and the reason is
// the row specialisation rather than any hash already published. The
// live federation key reaches that specialisation alone, because
// crucible/cipher/ComputationCacheFederation.h constrains its row half
// to an effect row. That value is portable today. To fold the toolchain
// in unconditionally would make the one portable half non-portable, in
// exchange for a guarantee the caller already takes per key.
//
// A cross-build witness covers the rest.
// tools/dump_row_hashes_foundation.cpp prints this fold from a separate
// binary and CI diffs the output
// against a committed golden, because a self-test in one translation
// unit cannot see a reflected name move underneath it.

#include <foundation/algebra/Modality.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// The carriers are forward-declared rather than included. A partial
// specialization only has to deduce its template parameters, and pulling
// the definitions in would drag the graded substrate, the whole lattice
// family and the reflection paths into every consumer of this header. A
// translation unit that names a carrier as an argument includes its own
// header for it.
//
// Graded needs no declaration at all, because the fold matches on the
// shape a wrapper publishes rather than on the template itself.
namespace foundation::effects {
template <typename R, typename T>
class Computation;
template <class Cap, class R>
class ExecCtx;
template <Effect Cap, class Source>
class Capability;
}  // namespace foundation::effects

namespace foundation::diag {

namespace detail {

using ::foundation::reflect::combine_ids;
using ::foundation::reflect::fmix64;
using ::foundation::reflect::detail::FNV1A_OFFSET_BASIS;

// Three types in this scheme are not graded wrappers, so three salts sit
// here. The row is the grade itself rather than a carrier of one, and the
// computation carrier pairs a row with a payload without pinning a
// lattice; neither of those needs a constant, because the cardinality
// seed and the row-then-payload order already separate them. Everything
// else reaches the fold below, with the one exception the second constant
// names.
//
// A salt owns a distinct high byte, and only the graded salt ors anything
// into its low bytes, because only a graded wrapper has one tier to
// encode. Changing a salt already in use moves every hash published
// under it, so a new salt takes the next free high byte rather than
// reshaping one that is spoken for.
inline constexpr std::uint64_t WRAPPER_GRADED_TAG = 0x0100000000000000ULL;

// The salt of a multi-axis binding, which is the one carrier in the tree
// that cannot reach the graded fold below.
//
// That fold reads one modality, one lattice and one payload, because
// Graded is a one-axis substrate. A binding over the axis table is the
// resolver across those axes: it publishes a grade per axis and so has no
// singular lattice type and no singular modality to publish. Giving it
// one would mean inventing a product lattice and naming a modality it
// does not have, which relocates the same problem into that lattice's
// canonical id. It carries its own specialisation instead, folding its
// resolved grades in axis order, and this salt is what keeps that fold
// off the zero slot when every axis happens to contribute zero.
inline constexpr std::uint64_t WRAPPER_MULTI_AXIS_BINDING_TAG = 0x0200000000000000ULL;

// The salt of a carrier that names its claim with a discipline identity
// rather than a lattice. It keeps such a carrier off the graded slots
// when a discipline identity and a lattice happen to share a name.
inline constexpr std::uint64_t WRAPPER_DISCIPLINE_TAG = 0x0300000000000000ULL;

// The salt of a stepping carrier. Its low bytes take the modality, as
// the graded salt's do, so a later second stepping kind separates from
// the first without a salt of its own.
inline constexpr std::uint64_t WRAPPER_STEPPING_TAG = 0x0400000000000000ULL;

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
//
// Two ABI macros that the documented tuple names are absent here, and
// measurement settles both.
//
// __GXX_ABI_VERSION moves under -fabi-version while display_string_of
// returns a byte-identical spelling. Measured on this toolchain at 1021,
// 1018 and 1015, against one spelling of std::string and of int. The
// macro governs mangled names, which this fold never reads. To fold it in
// would separate peers that share one reflected-name universe.
//
// _GLIBCXX_USE_CXX11_ABI has one admissible value in any translation unit
// that can compute a hash at all. bits/version.h gates
// __glibcxx_reflection on that macro, so the old ABI declares no
// display_string_of and compiles none of this. The macro does reach a
// type spelling where it is live, because std::string prints under the
// __cxx11 inline namespace. That gate is what closes the hole, not the
// spelling.
//
// __GLIBCXX__ is a release date, so a point release that renames nothing
// still moves this tag. That direction is a miss rather than a collision,
// and the miss is the safe one.
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

// Publishing one graded member is a claim to be graded. A type that
// publishes a lattice or a modality and still reaches the primary below
// has published part of a contract, and it would fold to zero without a
// word. That is the fail-open the port set out to close, so it is a hard
// error here rather than a silent slot.
template <typename T>
concept PublishesGradedMember = requires { typename T::lattice_type; } || requires { T::modality; };

// A bare type carries no row. Every carrier that holds one publishes a
// shape a fold below reads, so this primary is the answer for payload
// types and grade vocabulary only, and the answer is "nothing".
template <typename T>
struct row_hash_contribution {
    static_assert(!PublishesGradedMember<T>,
                  "this type publishes lattice_type or modality but no complete shape a row-hash fold reads, "
                  "so it would take the zero slot that every bare payload shares.  A graded carrier publishes "
                  "lattice_type, value_type and modality together.  A stepping carrier publishes "
                  "protocol_type, resource_type and the Stepping modality.  Any other carrier publishes "
                  "row_discipline and row_payload.");
    static constexpr std::uint64_t value = 0;
};

template <typename T>
inline constexpr std::uint64_t row_hash_contribution_v = row_hash_contribution<T>::value;

// The identity of one axis at one tier. A lattice type is the axis and
// the pinned grade together: DetSafeLattice::At<Pure> and
// DetSafeLattice::At<Impure> are different types, so they take different
// slots without either one naming a salt.
//
// Specializing this maps a second type onto an existing identity.
// That is what a rename or an alias needs in order to keep
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
//
// The name is narrower than the role, and it stays. Not every identity
// folded through this trait belongs to a lattice: the multi-axis binding
// in fixy/Fn.h resolves an axis to an atom type or to a pole type, and it
// folds those here as well. The alternative was a second trait named for
// grades, defaulting the same way. One table is right, and the recursion
// is what decides it rather than taste. A binding's payload axis recurses
// into row_hash_contribution, which reaches the graded fold below, which
// reads this trait. Both folds are therefore already live inside one key.
// Two tables could disagree about one type, and that type would then
// carry two identities within a single key, which is an inconsistency
// rather than the fragmentation one table risks. A rename mapped in one
// table and forgotten in the other is the ordinary way that happens.
//
// So a third kind of grade widens this comment. It does not add a peer
// trait.
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
//
// A graded wrapper can make a claim its lattice does not see. A sealed
// refinement is one: it grades on the same predicate lattice as the open
// one, and the one thing that separates them, that the sealed form
// cannot be mutated in place, reaches neither the lattice nor the
// modality. Such a wrapper also publishes row_discipline, and that
// identity folds in between the lattice and the payload. A wrapper that
// publishes none keeps the hash it had before this step existed.
template <GradedShaped W>
struct row_hash_contribution<W> {
    static constexpr std::uint64_t value = []() consteval -> std::uint64_t {
        std::uint64_t h = detail::combine_ids(detail::WRAPPER_GRADED_TAG | static_cast<std::uint64_t>(W::modality),
                                              lattice_canonical_id_v<typename W::lattice_type>);
        if constexpr (requires { typename W::row_discipline; }) {
            h = detail::combine_ids(h, lattice_canonical_id_v<typename W::row_discipline>);
        }
        return detail::combine_ids(h, row_hash_contribution_v<typename W::value_type>);
    }();
};

// ── Carriers that are not one-axis grades ───────────────────────────
//
// A carrier folds two things: an identity that names its claim at its
// tier, and the payloads the claim is over. The identity goes through
// lattice_canonical_id, so a rename maps onto the old identity the same
// way a lattice rename does. The payloads recurse, which keeps a row
// nested inside a carrier visible in the carrier's hash and keeps a bare
// payload out of it.
//
// An identity names the grade-bearing arguments and leaves the payload
// out. A declared-only struct in a row_discipline namespace beside the
// carrier is the usual spelling, instantiated with whatever arguments
// change the claim. A carrier with no payload at all may name itself.
//
// Brands, owner tags and region tags are identities of instances, not
// claims, and none of them folds in. That matches SharedPermission,
// whose graded fold is blind to its tag.

// The payload list of a carrier over more than one value. The fold reads
// the entries in order, so two carriers that list the same payloads in a
// different order are different keys, as nesting order is for graded
// wrappers.
template <typename... Payloads>
struct row_payloads {};

namespace detail {

template <typename P>
struct payload_fold {
    [[nodiscard]] static consteval std::uint64_t over(std::uint64_t h) noexcept {
        return combine_ids(h, row_hash_contribution_v<P>);
    }
};

template <typename... Ps>
struct payload_fold<row_payloads<Ps...>> {
    [[nodiscard]] static consteval std::uint64_t over(std::uint64_t h) noexcept {
        ((h = combine_ids(h, row_hash_contribution_v<Ps>)), ...);
        return h;
    }
};

}  // namespace detail

// The fold every discipline carrier reaches, spelled once so that a
// carrier folded by a specialization and a carrier folded by its
// published shape agree bit for bit.
template <typename Identity, typename Payload>
inline constexpr std::uint64_t discipline_row_hash_v = detail::payload_fold<Payload>::over(
    detail::combine_ids(detail::WRAPPER_DISCIPLINE_TAG, lattice_canonical_id_v<Identity>));

// The published shape. A carrier declares its own claim in its own body:
//
//   using row_discipline = row_discipline::write_once;
//   using row_payload    = T;
//
// A member alias is instantiated with the class, so a carrier whose
// payload can only be computed lazily, such as a permission whose row is
// looked up and may be undeclared, specializes row_hash_contribution in
// its own header through discipline_row_hash_v instead.
template <typename W>
concept DisciplineShaped = !GradedShaped<W> && requires {
    typename W::row_discipline;
    typename W::row_payload;
};

template <DisciplineShaped W>
struct row_hash_contribution<W> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<typename W::row_discipline, typename W::row_payload>;
};

// A session handle does not instantiate Graded, and
// foundation/algebra/Modality.h says why: every operation advances its
// grade, so a handle produces a handle at the next grade rather than
// one at the same grade over a new value. Its grade is the protocol it
// sits at, and its payload is the resource it steps over. The protocol
// takes the lattice's place in the fold, through the same canonical-id
// hook.
//
// The abandonment policy does not fold in. It decides whether a dropped
// handle aborts, which is a property of the build, and a handle built
// under one policy makes the same claim as a handle built under the
// other.
template <typename W>
concept SteppingShaped = !GradedShaped<W> && requires {
    typename W::protocol_type;
    typename W::resource_type;
    { W::modality } -> std::convertible_to<::foundation::algebra::ModalityKind>;
    requires W::modality == ::foundation::algebra::ModalityKind::Stepping;
};

template <SteppingShaped W>
struct row_hash_contribution<W> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(detail::WRAPPER_STEPPING_TAG | static_cast<std::uint64_t>(W::modality),
                            lattice_canonical_id_v<typename W::protocol_type>),
        row_hash_contribution_v<typename W::resource_type>);
};

// ── The effect layer's own carriers ─────────────────────────────────
//
// These three live here beside the row, for the reason the computation
// carrier does: they are the effect layer's own types, and each one
// carries a row that the fold has to see.

namespace row_discipline {
template <class Cap>
struct exec_ctx;
template <class Source>
struct capability;
}  // namespace row_discipline

// A capability context is its own identity, since it has no payload, and
// its payload is the row it permits. Two contexts that permit one row
// are still two claims, because one of them names the background thread.
template <typename C>
    requires ::foundation::effects::IsContext<C>
struct row_hash_contribution<C> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<C, typename C::template permitted_as<::foundation::effects::Row>>;
};

// An execution context claims a capability source and a row under it.
// The source changes the claim, so it is in the identity, and the row is
// the payload.
template <class Cap, class R>
struct row_hash_contribution<::foundation::effects::ExecCtx<Cap, R>> {
    static constexpr std::uint64_t value = discipline_row_hash_v<row_discipline::exec_ctx<Cap>, R>;
};

// A capability is a linear token for one effect, minted from a source.
// The effect folds as the one-atom row it grants, so a capability and a
// context that permit the same atom agree on that half.
template <::foundation::effects::Effect E, class Source>
struct row_hash_contribution<::foundation::effects::Capability<E, Source>> {
    static constexpr std::uint64_t value =
        discipline_row_hash_v<row_discipline::capability<Source>, ::foundation::effects::Row<E>>;
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
