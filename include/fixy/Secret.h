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
// The policy tags live in fixy/Tags.h.  The fail-closed namespace below
// is the roster of the admitted policies, and the assertions derived
// from it check that the roster is complete.

#include <fixy/GradedFacade.h>
#include <fixy/Tags.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/ConfLattice.h>
#include <foundation/diag/FailClosed.h>
#include <foundation/reflect/Instance.h>

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdlib>
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

// Every read counts the members against this seal, so a member that
// another file adds stops the build rather than admitting a new exit
// from the Secret tier.  The count is the six edges and `classified`.
inline constexpr ::foundation::fail_closed::seal sealed{.members = 7};

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

// The length of a classified container is classified too.  A length can
// tell one kind of key from another, or give the number of records in a
// set.  So the LengthOnly policy is the one exit for a length, and that
// policy releases the length and never the payload.  Each other admitted
// policy releases the payload.  A search for
// `declassify<secret_policy::LengthOnly>` finds each release of a length.
template <typename Policy>
concept LengthDeclassification =
    AdmittedDeclassification<Policy> && std::same_as<Policy, tags::secret_policy::LengthOnly>;

template <typename Policy>
concept PayloadDeclassification = AdmittedDeclassification<Policy> && !LengthDeclassification<Policy>;

// The three rules transform() places on its callable.  Each is its
// own concept, so a refusal names the rule that was broken instead of
// the bare fact that constraints were not satisfied.
//
// A reference return would alias the payload.  The storage is moved
// from by the time the caller reads the alias, and the alias carries
// no policy tag, which is the one thing declassify<Policy>() exists to
// require.  A void return has nothing to rewrap.  The likely intent is
// an observation of the payload, and an observation also belongs
// behind a policy.
template <typename F, typename T>
concept TransformReturnsByValue = !std::is_reference_v<std::invoke_result_t<F, T&&>>;

template <typename F, typename T>
concept TransformReturnsNonVoid = !std::is_void_v<std::invoke_result_t<F, T&&>>;

template <typename F, typename T>
concept SecretTransformer = std::invocable<F, T&&> && TransformReturnsByValue<F, T> && TransformReturnsNonVoid<F, T>;

template <typename T>
class Secret;

// The constructors are private, so this is the door.
template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Secret<T> mint_secret(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>);

template <typename T>
class [[nodiscard]] Secret
    : public graded_facade<
          ::foundation::algebra::ModalityKind::Comonad,
          ::foundation::algebra::lattices::ConfLattice::At<::foundation::algebra::lattices::Conf::Secret>, T> {
public:
    // value_type, modality and the two name forwarders arrive from
    // graded_facade.  The base is dependent, so the two names this
    // class body uses unqualified are re-declared here rather than
    // found by lookup.
    using facade_ =
        graded_facade<::foundation::algebra::ModalityKind::Comonad,
                      ::foundation::algebra::lattices::ConfLattice::At<::foundation::algebra::lattices::Conf::Secret>,
                      T>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;

    static_assert(std::is_same_v<lattice_type, tags::secret_policy::admitted_policies::classified>,
                  "The From end of every admitted_policies edge must be the lattice position "
                  "Secret<T> occupies, or declassify<Policy>() consults edges out of a tier "
                  "this wrapper does not sit at.");

private:
    graded_type impl_;
    // No byte route builds a Secret, so no byte route reads a classified
    // value out with no declassify<Policy>() and no audit entry.
    // std::bit_cast refuses the class, and -Wclass-memaccess refuses a
    // memcpy.  The move constructor stays trivial, so a Secret still passes
    // in a register.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    using key_ = ::foundation::algebra::grade_key<Secret>;

    constexpr explicit Secret(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{key_{}, std::move(v), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Secret(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                        && std::is_nothrow_move_constructible_v<T>)
        : impl_{key_{}, T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

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

    // A classified value cannot steer a branch, an address or an order.
    // Each of these turns the value into timing or into a memory access
    // pattern, which is the leak that fixy::ct exists to prevent.  The
    // operators are deleted, not absent, so a refusal gives its reason
    // and a requires-expression reads false.
    //
    // operator bool takes the branch condition and the copy to bool.  The
    // integral template takes an index, an offset added to a pointer and
    // any other integer use: a subscript or a pointer sum converts its
    // operand to std::ptrdiff_t or std::size_t, and the template matches
    // that type exactly.  The comparison operators are hidden friends over
    // any second operand, so `secret == x`, `x == secret`, `secret < x`
    // and the rewritten forms all select them.
    //
    // Compare classified data with a fixy::ct primitive inside
    // transform().  The mask it returns stays classified until
    // declassify<HashForCompare>() releases it.
    operator bool() const = delete("[Secret_BranchCondition] a classified value cannot be a branch condition. "
                                   "Select with fixy::ct::select inside transform(), or release the value "
                                   "with declassify<Policy>() first.");

    template <std::integral I>
        requires(!std::same_as<I, bool>)
    operator I() const = delete("[Secret_IndexOrArithmetic] a classified value cannot become an integer, "
                                "an index or a pointer offset, because the address then depends on the "
                                "secret.  Derive with fixy::ct inside transform(), or release the value "
                                "with declassify<Policy>() first.");

    template <typename U>
    friend constexpr bool operator==(Secret const&, U const&) =
        delete("[Secret_Compare] a classified value cannot be compared with ==, because the compare "
               "exits early.  Compare with fixy::ct::eq inside transform(), and release the mask "
               "with declassify<HashForCompare>().");

    template <typename U>
    friend constexpr std::strong_ordering operator<=>(Secret const&, U const&) =
        delete("[Secret_Order] a classified value cannot be ordered with <, <=, >, >= or <=>.  "
               "Order with fixy::ct::less inside transform(), and release the mask with "
               "declassify<HashForCompare>().");

    // This is not a way out of classification: it derives a different
    // classified value from the original.  The callable is trusted for
    // the duration of the call, so the intended use is a stateless
    // transformation such as a decode or a hash fold.
    //
    // The rules on the callable are constraints, not assertions in the
    // body.  A callable that breaks one never reaches the trailing
    // return type, so Secret<T&> is never named, and a caller's
    // requires-expression reads false instead of stopping the build.
    // The two assertions below restate the rules for a reader of this
    // body, as declassify() does, and cannot fire.
    template <typename F>
        requires SecretTransformer<F, T>
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

    // The length of a classified value is classified.  The deleted
    // accessor gives the reason at a call, and a requires-expression over
    // size() reads false.
    std::size_t size() const = delete("[Secret_LengthRelease] the length of a classified value is classified.  "
                                      "Release it with declassify<secret_policy::LengthOnly>(), so that a "
                                      "search finds each release.");

    // The payload leaves through each admitted policy except LengthOnly,
    // and only from an rvalue.  The requires-clause gates the call, and
    // the assertion below restates the rule in words on purpose: a
    // constraint failure reports only that constraints were not
    // satisfied, which says nothing about what to do instead.  The named
    // diagnostic is what a reader of this body finds.
    template <PayloadDeclassification Policy>
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

    // The length leaves only through the LengthOnly policy.  The call
    // does not consume the Secret, because the payload stays classified.
    // An rvalue also binds here, so LengthOnly never releases the payload.
    template <LengthDeclassification Policy>
        requires requires(T const& payload) { payload.size(); }
    [[nodiscard]] constexpr auto declassify() const& noexcept(noexcept(std::declval<T const&>().size())) {
        return impl_.peek().size();
    }

    // Opt-in: overwrites the storage before destruction.  Reaching the
    // mutable storage is admitted here because the substrate's
    // mutation gate also accepts an empty grade, which this
    // confidentiality lattice has, and this class holds the key.  The
    // zeroed bytes are still secret, so the grade stays true.
    void zeroize() noexcept
        requires std::is_trivially_copyable_v<T>
    {
        // The writes are volatile so the optimizer cannot drop a clear
        // whose result is never read.  The cast chain adds the volatile
        // qualifier by implicit conversion and then narrows through a
        // volatile void pointer, which keeps the qualifier all the way
        // down without reinterpreting or casting away a qualifier.
        volatile T* vp = __builtin_addressof(impl_.peek_mut(key_{}));
        volatile auto* p = static_cast<volatile unsigned char*>(static_cast<volatile void*>(vp));
        for (std::size_t i = 0; i < sizeof(T); ++i)
            p[i] = 0;
    }
};

template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Secret<T> mint_secret(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return Secret<T>{std::in_place, std::forward<Args>(args)...};
}

// True when T, with its references and qualifiers removed, is a Secret.
// One reflection query answers it, so there is no primary-plus-
// specialization ladder to keep in step with the class.  The concept is
// the question.  The value spelling is derived from it and read by
// nothing that gates, because a variable template can be explicitly
// specialized from any translation unit and a concept cannot.

template <typename T>
concept IsSecret = ::foundation::reflect::IsInstanceOf<T, ^^Secret>;

template <typename T>
inline constexpr bool is_secret_v = IsSecret<T>;

// The number of edges of the policy relation, derived from the
// namespace.  The check file of this header pins it, so a new policy is
// a reviewed edit.
inline constexpr std::size_t admitted_policy_count =
    ::foundation::fail_closed::edge_count<^^tags::secret_policy::admitted_policies>();

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

}  // namespace detail::secret_policy_relation

}  // namespace fixy
