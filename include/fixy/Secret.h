#pragma once

// A classified value cannot silently duplicate, so copy is deleted and
// a move is the only transfer.  Deriving a new value keeps the
// classification, and the single exit is a named declassification
// whose policy tag is the audit trail.
//
// The one door is mint_secret<T>(args...).  Classification is
// deliberately open to enter and audited to leave: declassification is
// the only way to get the value back out, deriving keeps the
// classification, and zeroizing destroys it.  That asymmetry, not the
// factory, is the actual guarantee; the factory exists so a named site
// can be searched for.
//
// Old spelling: include/crucible/safety/Secret.h and the detection
// surface of include/crucible/safety/IsSecret.h.  The policy tags live
// in fixy/Tags.h; the roster and its completeness check of the old
// header are the fail-closed namespace below and the assertions
// derived from it.

#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/ConfLattice.h>
#include <foundation/diag/FailClosed.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

// Searching for a declassification by its policy tag only enumerates
// every escape from classification if the policy universe is closed.
// The marker base in fixy/Tags.h closes the set of tags; the namespace
// below closes the set of tags a value can leave through.  A tag that
// derives from the base and has no edge here is declared and not
// admitted, and declassify<Tag>() rejects it.
//
// Every edge starts at `classified`, the lattice position a Secret
// occupies (Secret<T>::lattice_type, pinned in the class body).  The
// From end is the wrapper's own grade rather than an ad-hoc marker so
// that the edge reads as "out of the Secret tier, through this
// channel", and a wrapper at another confidentiality tier cannot
// borrow these edges.
namespace fixy::tags::secret_policy::admitted_policies {

using classified = ::foundation::algebra::lattices::conf::SecretTier;

inline constexpr ::foundation::fail_closed::edge<classified, AuditedLogging> audited_logging{};
inline constexpr ::foundation::fail_closed::edge<classified, WireSerialize> wire_serialize{};
inline constexpr ::foundation::fail_closed::edge<classified, HashForCompare> hash_for_compare{};
inline constexpr ::foundation::fail_closed::edge<classified, LengthOnly> length_only{};
inline constexpr ::foundation::fail_closed::edge<classified, UserDisplay> user_display{};
inline constexpr ::foundation::fail_closed::edge<classified, AuthorizedReplay> authorized_replay{};

}  // namespace fixy::tags::secret_policy::admitted_policies

namespace fixy {

// This is what makes the audit trail structural rather than a
// convention: an ad-hoc struct is rejected, so every declassification
// must name a tag from the closed set in fixy/Tags.h.
template <typename Policy>
concept DeclassificationPolicy =
    std::is_class_v<Policy> && std::derived_from<Policy, tags::secret_policy::secret_policy_base>;

// A policy leaves classification only along its edge.
template <typename Policy>
concept AdmittedDeclassification =
    DeclassificationPolicy<Policy>
    && ::foundation::fail_closed::Admitted<^^tags::secret_policy::admitted_policies,
                                           tags::secret_policy::admitted_policies::classified, Policy>;

template <typename T>
class Secret;

// The constructors are private, so this is the door.
template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Secret<T> mint_secret(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>);

template <typename T>
class [[nodiscard]] Secret {
public:
    using value_type = T;
    using lattice_type =
        ::foundation::algebra::lattices::ConfLattice::At<::foundation::algebra::lattices::Conf::Secret>;
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Comonad;
    using graded_type = ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Comonad, lattice_type, T>;

    static_assert(std::is_same_v<lattice_type, tags::secret_policy::admitted_policies::classified>,
                  "The From end of every admitted_policies edge must be the lattice position "
                  "Secret<T> occupies, or declassify<Policy>() consults edges out of a tier "
                  "this wrapper does not sit at.");

private:
    graded_type impl_;

    constexpr explicit Secret(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Secret(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                        && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    template <typename U, typename... Args>
        requires std::is_constructible_v<U, Args...>
    friend constexpr Secret<U> mint_secret(Args&&... args) noexcept(std::is_nothrow_constructible_v<U, Args...>);

    // transform() derives a Secret<R> from a Secret<T>, so each
    // specialization constructs its siblings.
    template <typename U>
    friend class Secret;

public:
    Secret(const Secret&) = delete("Secret<T> cannot be silently duplicated");
    Secret& operator=(const Secret&) = delete("Secret<T> cannot be silently duplicated");
    Secret(Secret&&) = default;
    Secret& operator=(Secret&&) = default;
    ~Secret() = default;

    // This is not a way out of classification: it derives a different
    // classified value from the original.  The callable is trusted for
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

    // The requires-clause gates the call, and the assertion below
    // restates the rule in words on purpose: a constraint failure
    // reports only that constraints were not satisfied, which says
    // nothing about what to do instead.  The named diagnostic is what
    // a reader of this body finds.
    template <AdmittedDeclassification Policy>
    [[nodiscard]] constexpr T declassify() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        static_assert(std::derived_from<Policy, tags::secret_policy::secret_policy_base>,
                      "fixy::diagnostic [SecretPolicy_NotInBase]: "
                      "Secret::declassify<Policy>() requires Policy to derive "
                      "from fixy::tags::secret_policy::secret_policy_base and to "
                      "have an edge in fixy::tags::secret_policy::admitted_policies. "
                      "Define new policies as `struct MyPolicy final : "
                      "secret_policy_base {};` inside the secret_policy:: "
                      "namespace of fixy/Tags.h and admit each with one edge in "
                      "fixy/Secret.h, so `grep \"declassify<secret_policy::\"` "
                      "enumerates every escape from classification.  Ad-hoc "
                      "policy structs anywhere else in the codebase would "
                      "silently bypass the audit trail.");
        return std::move(impl_).extract();
    }

    // Opt-in: overwrites the storage before destruction.  Reaching the
    // mutable storage is admitted here because the substrate's
    // mutation gate also accepts an empty grade, which this
    // confidentiality lattice has, and the access stays internal.
    void zeroize() noexcept
        requires std::is_trivially_copyable_v<T>
    {
        // The writes are volatile so the optimizer cannot drop a clear
        // whose result is never read.  The cast chain adds the volatile
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

template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Secret<T> mint_secret(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return Secret<T>{std::in_place, std::forward<Args>(args)...};
}

static_assert(sizeof(Secret<int>) == sizeof(int));
static_assert(sizeof(Secret<unsigned long long>) == sizeof(unsigned long long));

// The detection surface of the old IsSecret.h.  One reflection query
// answers it, and the value type is read off the wrapper's own typedef,
// so there is no primary-plus-specialization ladder to keep in step
// with the class.

template <typename T>
inline constexpr bool is_secret_v = ::foundation::reflect::is_instance_of_v<T, ^^Secret>;

template <typename T>
concept IsSecret = is_secret_v<T>;

template <typename T>
    requires is_secret_v<T>
using secret_value_t = typename std::remove_cvref_t<T>::value_type;

namespace detail::secret_self_test {

struct payload {};
struct LookalikeSecret {
    using value_type = int;
    int payload;
};

using S_int = Secret<int>;
using S_payload = Secret<payload>;

static_assert(is_secret_v<S_int>);
static_assert(is_secret_v<S_payload>);
static_assert(is_secret_v<S_int&>);
static_assert(is_secret_v<S_int&&>);
static_assert(is_secret_v<S_int const>);
static_assert(is_secret_v<S_int const&>);
static_assert(is_secret_v<S_int volatile>);
static_assert(!is_secret_v<int>);
static_assert(!is_secret_v<int*>);
static_assert(!is_secret_v<S_int*>);
static_assert(!is_secret_v<void>);
static_assert(!is_secret_v<LookalikeSecret>);
static_assert(IsSecret<S_int>);
static_assert(IsSecret<S_payload const&>);
static_assert(!IsSecret<int>);
static_assert(std::is_same_v<secret_value_t<S_int>, int>);
static_assert(std::is_same_v<secret_value_t<S_payload const&>, payload>);

}  // namespace detail::secret_self_test

// The policy relation's discipline, derived from the namespace.  The
// count is the one place a new policy must be acknowledged by hand.
// Adding a tag takes three steps: declare `struct NewTag final :
// secret_policy_base {}` in the secret_policy namespace of
// fixy/Tags.h, admit it with one edge above, and move this pin.  A
// downstream consumer that pins its own cardinality against
// admitted_policy_count moves in the same change.
inline constexpr std::size_t admitted_policy_count =
    ::foundation::fail_closed::edge_count<^^tags::secret_policy::admitted_policies>();

static_assert(admitted_policy_count == 6, "fixy::tags::secret_policy::admitted_policies holds a different "
                                          "number of edges than this pin.  A policy was added or removed: "
                                          "review it as the audit-trail change it is, then move the pin.");
static_assert(::foundation::fail_closed::every_edge_is_admitted<^^tags::secret_policy::admitted_policies>(),
              "every edge declared in fixy::tags::secret_policy::admitted_policies must be admitted by "
              "the fail-closed check that reads the same namespace");

// The count above cannot catch a tag that was declared but never
// admitted: it counts the edges against themselves.  Enumerating the
// tag namespace and asking for an edge per class closes that path.
static_assert(::foundation::fail_closed::every_class_in_has_edge<
                  ^^tags::secret_policy::admitted_policies, ^^tags::secret_policy,
                  ::foundation::fail_closed::EdgeEnd::To, tags::secret_policy::secret_policy_base>(),
              "The secret_policy namespace of fixy/Tags.h declares a policy tag that has no edge in "
              "fixy::tags::secret_policy::admitted_policies.  A declared tag that is not admitted "
              "leaves a gap in the audit trail: declassify<Tag>() rejects it, and nothing says why.  "
              "Admit it with one `inline constexpr edge<classified, Tag>` in fixy/Secret.h, or "
              "remove the declaration.");

namespace detail::secret_policy_relation {

// The converse of the completeness check: every edge starts at the
// Secret tier and ends at a final tag derived from the marker base, so
// the relation admits nothing that DeclassificationPolicy rejects.
[[nodiscard]] consteval bool every_edge_is_a_policy_exit() noexcept {
    for (const auto m :
         std::meta::members_of(^^tags::secret_policy::admitted_policies, std::meta::access_context::unchecked())) {
        if (!::foundation::fail_closed::is_edge(m)) continue;
        const auto ends = ::foundation::fail_closed::ends_of(m);
        if (ends.from != std::meta::dealias(^^tags::secret_policy::admitted_policies::classified)) return false;
        if (!std::meta::is_base_of_type(^^tags::secret_policy::secret_policy_base, ends.to)) return false;
        if (ends.to == ^^tags::secret_policy::secret_policy_base) return false;
        if (!std::meta::is_final(ends.to)) return false;
    }
    return true;
}

static_assert(every_edge_is_a_policy_exit(), "Every edge in fixy::tags::secret_policy::admitted_policies must "
                                             "run from `classified` to a final class derived from "
                                             "secret_policy_base.  Any other edge is inert for "
                                             "declassify<Policy>() and misleads a reader of the catalog.");

}  // namespace detail::secret_policy_relation

// A public method handing back a mutable reference would let a caller
// read the classified payload through ordinary aliasing, which is
// exactly what the audited exit exists to prevent.  The substrate does
// admit such an accessor for an empty grade, and this wrapper uses
// that internally to overwrite bytes before destruction, but it must
// never appear on the public face.  The sentinels below turn "there is
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
              "Secret<T>::value_mut() must NOT exist. A provenance wrapper once "
              "carried that name because mutating content preserves provenance; "
              "on a classified value it leaks the payload. Derive a new Secret "
              "with transform(), or declassify and re-wrap, or use the internal "
              "zeroize() path.");
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

    Secret<int> s = mint_secret<int>(seed * 2);

    Secret<int> t = std::move(s).transform([](int&& v) { return v + 1; });
    int declassified = std::move(t).template declassify<tags::secret_policy::AuditedLogging>();
    if (declassified != 35) std::abort();

    Secret<int> m = mint_secret<int>(seed);
    int m_out = std::move(m).template declassify<tags::secret_policy::WireSerialize>();
    if (m_out != 17) std::abort();

    Secret<int> hp = mint_secret<int>(seed);
    int hp_out = std::move(hp).template declassify<tags::secret_policy::HashForCompare>();
    if (hp_out != 17) std::abort();

    Secret<unsigned long long> key = mint_secret<unsigned long long>(0xCAFEBABE12345678ULL);
    key.zeroize();
    // The zeroized value cannot be inspected without declassifying it,
    // so it is compared against a freshly zeroized one instead.
    Secret<unsigned long long> zero = mint_secret<unsigned long long>(0ULL);
    auto k_out = std::move(key).template declassify<tags::secret_policy::HashForCompare>();
    auto z_out = std::move(zero).template declassify<tags::secret_policy::HashForCompare>();
    if (k_out != z_out) std::abort();

    if (!is_secret_v<S_int>) std::abort();
    if (!is_secret_v<S_payload const&>) std::abort();
    if (is_secret_v<int>) std::abort();
    if (is_secret_v<LookalikeSecret>) std::abort();
    if (!IsSecret<S_int&&>) std::abort();
}

}  // namespace detail::secret_self_test

}  // namespace fixy
