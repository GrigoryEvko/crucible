#pragma once

// A classified value cannot silently duplicate, so copy is deleted and
// a move is the only transfer. Deriving a new value keeps the
// classification, and the single exit is a named declassification
// whose policy tag is the audit trail.

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/_ConfLattice.h>

#include <concepts>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Searching for a declassification by its policy tag only enumerates
// every escape from classification if the policy universe is closed.
// A convention alone would not close it, because an ad-hoc struct
// declared anywhere would serve as a policy and the search would miss
// the site. The marker base below is what makes the set closed, and
// each tag is final so that no subclass can launder the audit trail
// through subtype coercion.
namespace secret_policy {

// Empty, and exists only to be detected. Every policy inherits it.
struct secret_policy_base {};

struct AuditedLogging final : secret_policy_base {};  // log with audit trail
struct WireSerialize final : secret_policy_base {};  // encrypted-channel serialization
struct HashForCompare final : secret_policy_base {};  // release as hash (not the source)
struct LengthOnly final : secret_policy_base {};  // release only size metadata
struct UserDisplay final : secret_policy_base {};  // display in UI (e.g., last-4 of card)

// A declassification policy has to match the axis it discharges. The
// policies above authorize an export channel or a relaxation of
// confidentiality, and neither says anything about how stale a value
// may be. Using one of them to admit a replay window would be an
// over-broad discharge, so the temporal axis gets its own tag.
struct AuthorizedReplay final : secret_policy_base {};  // admits a bounded replay window

// The roster is the ordered, single source of truth for the tag set,
// and downstream consumers alias it rather than restating it. Keeping
// it in this file means a new tag and its roster entry are authored
// together, in one place.
namespace roster {

using All = std::tuple<AuditedLogging, WireSerialize, HashForCompare, LengthOnly, UserDisplay, AuthorizedReplay>;

inline constexpr std::size_t kCount = std::tuple_size_v<All>;

static_assert(kCount == 6, "secret_policy::roster::All grew beyond 6 tags. Adding a tag takes "
                           "three steps in this file: declare the `struct NewTag final : "
                           "secret_policy_base {}` above this roster, append the type to "
                           "roster::All, and raise this expected count. A downstream "
                           "consumer aliases this roster and pins its own cardinality, so "
                           "that pin must move in the same change.");

// The count above cannot catch a tag that was declared but never added
// to the roster: it compares the roster against itself. Enumerating
// the namespace and counting what derives from the marker base closes
// that path, because the two counts then diverge.

namespace detail::roster_completeness {

[[nodiscard]] consteval std::size_t count_policy_tags_in_namespace() noexcept {
    std::size_t found = 0;
    // An expansion statement synthesizes a fresh binding per iteration
    // in the same enclosing scope, so each iteration shadows the one
    // before it and the build reds on shadowing. The suppression
    // covers exactly the expansion's lexical extent.
    _Pragma("GCC diagnostic push")
        _Pragma("GCC diagnostic ignored \"-Wshadow\"") template for (constexpr auto m :
                                                                     std::define_static_array(std::meta::members_of(
                                                                         ^^::crucible::safety::secret_policy,
                                                                         std::meta::access_context::unchecked()))) {
        if constexpr (std::meta::is_type(m)) {
            using member_type = typename[:m:];
            if constexpr (std::is_class_v<member_type> && std::is_base_of_v<secret_policy_base, member_type>
                          && !std::is_same_v<secret_policy_base, member_type>) {
                ++found;
            }
        }
    }
    _Pragma("GCC diagnostic pop") return found;
}

inline constexpr std::size_t kNamespacePolicyTags = count_policy_tags_in_namespace();

static_assert(kNamespacePolicyTags == kCount, "The secret_policy namespace contains a different number of "
                                              "secret_policy_base-derived class types than roster::All "
                                              "enumerates. Either a tag was added to the namespace and not "
                                              "appended to the roster, which leaves a gap in the audit trail, "
                                              "or a tag was removed and the roster still names it. Reconcile "
                                              "by making roster::All enumerate every `struct X final : "
                                              "secret_policy_base {}` declared above.");

}  // namespace detail::roster_completeness

}  // namespace roster

}  // namespace secret_policy

// This is what makes the audit trail structural rather than a
// convention: an ad-hoc struct is rejected, so every declassification
// must name a tag from the closed set above.
template <typename Policy>
concept DeclassificationPolicy =
    std::is_class_v<Policy> && std::derived_from<Policy, secret_policy::secret_policy_base>;

template <typename T>
class [[nodiscard]] Secret {
public:
    using value_type = T;
    using lattice_type = ::crucible::algebra::lattices::ConfLattice::At<::crucible::algebra::lattices::Conf::Secret>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Comonad;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Comonad, lattice_type, T>;

private:
    graded_type impl_;

public:
    constexpr explicit Secret(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Secret(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                        && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    Secret(const Secret&) = delete("Secret<T> cannot be silently duplicated");
    Secret& operator=(const Secret&) = delete("Secret<T> cannot be silently duplicated");
    Secret(Secret&&) = default;
    Secret& operator=(Secret&&) = default;
    ~Secret() = default;

    // This is not a way out of classification: it derives a different
    // classified value from the original. The callable is trusted for
    // the duration of the call, so the intended use is a stateless
    // transformation such as a decode or a hash fold.
    //
    // The requires-clause turns an uncallable argument into a concept
    // failure at the call site rather than a deduction failure inside
    // the result-type computation.
    template <typename F>
        requires std::invocable<F, T&&>
    [[nodiscard]] constexpr auto transform(F&& f) && noexcept(std::is_nothrow_invocable_v<F, T&&>)
        -> Secret<std::invoke_result_t<F, T&&>> {
        using R = std::invoke_result_t<F, T&&>;
        static_assert(!std::is_reference_v<R>, "[Capture_Leak_Reference_Return] Secret::transform(f): f"
                                               " must return by value.  A reference return aliases either"
                                               " the moved-from secret storage (UAF) or a member of f's"
                                               " closure (silent declassification bypassing"
                                               " declassify<Policy>).  Change f's return type to a value,"
                                               " or — if the intent is to observe classified data — call"
                                               " declassify<Policy>() first to leave an audit trail.");
        static_assert(!std::is_void_v<R>, "[Capture_Leak_Void_Return] Secret::transform(f): f must"
                                          " return a value.  void → Secret<void> is meaningless; the"
                                          " likely intent is a side-effecting observation on the"
                                          " classified payload — that belongs in declassify<Policy>(),"
                                          " not transform().");
        return Secret<R>{std::forward<F>(f)(std::move(impl_).consume())};
    }

    [[nodiscard]] constexpr auto size() const noexcept
        requires requires(const T& t) { t.size(); }
    {
        return impl_.peek().size();
    }

    // The requires-clause already gates this, and the assertion below
    // duplicates it on purpose: a constraint failure reports only that
    // constraints were not satisfied, which says nothing about what to
    // do instead. The named diagnostic is what a reader and the
    // negative-compile harness both match on.
    template <DeclassificationPolicy Policy>
    [[nodiscard]] constexpr T declassify() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        static_assert(std::derived_from<Policy, ::crucible::safety::secret_policy::secret_policy_base>,
                      "crucible::safety::diagnostic [SecretPolicy_NotInBase]: "
                      "Secret::declassify<Policy>() requires Policy to derive "
                      "from crucible::safety::secret_policy::secret_policy_base. "
                      "Define new policies as `struct MyPolicy final : "
                      "secret_policy_base {};` inside the secret_policy:: "
                      "namespace so `grep \"declassify<secret_policy::\"` "
                      "enumerates every escape from classification.  Ad-hoc "
                      "policy structs anywhere else in the codebase would "
                      "silently bypass the audit trail.");
        return std::move(impl_).extract();
    }

    // Opt-in: overwrites the storage before destruction. Reaching the
    // mutable storage is admitted here because the substrate's
    // mutation gate also accepts an empty grade, which this
    // confidentiality lattice has, and the access stays internal.
    void zeroize() noexcept
        requires std::is_trivially_copyable_v<T>
    {
        // The writes are volatile so the optimizer cannot drop a clear
        // whose result is never read. The cast chain adds the volatile
        // qualifier by implicit conversion and then narrows through a
        // volatile void pointer, which keeps the qualifier all the way
        // down without reinterpreting or casting away a qualifier.
        volatile T* vp = std::addressof(impl_.peek_mut());
        volatile auto* p = static_cast<volatile unsigned char*>(static_cast<volatile void*>(vp));
        for (std::size_t i = 0; i < sizeof(T); ++i)
            p[i] = 0;
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};

template <typename T>
Secret(T) -> Secret<T>;

// This factory is not a stricter entrance than the public constructor
// and gates on the same predicate. It exists so a named site can be
// searched for. Classification is deliberately open to enter and
// audited to leave: declassification is the only way to get the value
// back out, deriving keeps the classification, and zeroizing destroys
// it. That asymmetry, not this factory, is the actual guarantee.
template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Secret<T> mint_secret(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return Secret<T>{std::in_place, std::forward<Args>(args)...};
}

static_assert(sizeof(Secret<int>) == sizeof(int));
static_assert(sizeof(Secret<unsigned long long>) == sizeof(unsigned long long));

// A public method handing back a mutable reference would let a caller
// read the classified payload through ordinary aliasing, which is
// exactly what the audited exit exists to prevent. The substrate does
// admit such an accessor for an empty grade, and this wrapper uses
// that internally to overwrite bytes before destruction, but it must
// never appear on the public face. The sentinels below turn "there is
// no such accessor today" into an invariant that reds if one is added.
namespace detail::secret_api_lock {

template <typename S>
concept ExposesPeekMut = requires(S& s) { s.peek_mut(); };
template <typename S>
concept ExposesValueMut = requires(S& s) { s.value_mut(); };
template <typename S>
concept ExposesMutableRef = requires(S& s) { s.mutable_ref(); };
template <typename S>
concept ExposesDataMut = requires(S& s) { s.data_mut(); };
template <typename S>
concept ExposesGetMut = requires(S& s) { s.get_mut(); };

}  // namespace detail::secret_api_lock

static_assert(!detail::secret_api_lock::ExposesPeekMut<Secret<int>>,
              "Secret<T>::peek_mut() must NOT exist publicly. The only escape "
              "from a classified value is declassify<Policy>(), and a public "
              "mutable accessor bypasses that audit trail. Write-only access "
              "for a secure-overwrite path stays internal, as zeroize() does.");
static_assert(!detail::secret_api_lock::ExposesValueMut<Secret<int>>,
              "Secret<T>::value_mut() must NOT exist. The same name on a "
              "provenance wrapper is intentional there, because mutating "
              "content preserves provenance, but on a classified value it "
              "leaks the payload. Derive a new Secret with transform(), or "
              "declassify and re-wrap, or use the internal zeroize() path.");
static_assert(!detail::secret_api_lock::ExposesMutableRef<Secret<int>>,
              "Secret<T>::mutable_ref() must NOT exist. Any public method "
              "returning a reference or pointer to the classified payload "
              "bypasses declassify<Policy>().");
static_assert(!detail::secret_api_lock::ExposesDataMut<Secret<int>>,
              "Secret<T>::data_mut() must NOT exist. A container-style raw "
              "pointer accessor leaks the classified payload through "
              "pointer-iterator idioms with no policy tag to discharge it.");
static_assert(!detail::secret_api_lock::ExposesGetMut<Secret<int>>,
              "Secret<T>::get_mut() must NOT exist. An optional-style mutable "
              "extractor is ergonomic, and on a classified value it bypasses "
              "declassify<Policy>().");

namespace detail::secret_self_test {

// Declassification routes through a substrate path distinct from the
// ordinary read and consume paths, so a divergence between the
// compile-time and run-time behaviour of that path would classify the
// wrong bytes without any assertion noticing.
inline void runtime_smoke_test() {
    int seed = 17;

    Secret<int> s{seed * 2};

    Secret<int> t = std::move(s).transform([](int&& v) { return v + 1; });
    int declassified = std::move(t).template declassify<secret_policy::AuditedLogging>();
    if (declassified != 35) std::abort();

    Secret<int> m = mint_secret<int>(seed);
    int m_out = std::move(m).template declassify<secret_policy::WireSerialize>();
    if (m_out != 17) std::abort();

    Secret<int> hp{seed};
    int hp_out = std::move(hp).template declassify<secret_policy::HashForCompare>();
    if (hp_out != 17) std::abort();

    Secret<unsigned long long> key{0xCAFEBABE12345678ULL};
    key.zeroize();
    // The zeroized value cannot be inspected without declassifying it,
    // so it is compared against a freshly zeroized one instead.
    Secret<unsigned long long> zero{0ULL};
    auto k_out = std::move(key).template declassify<secret_policy::HashForCompare>();
    auto z_out = std::move(zero).template declassify<secret_policy::HashForCompare>();
    if (k_out != z_out) std::abort();
}

}  // namespace detail::secret_self_test

}  // namespace crucible::safety
