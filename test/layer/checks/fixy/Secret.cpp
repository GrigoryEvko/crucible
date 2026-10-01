// The compile-time checks of fixy/Secret.h.

#include <fixy/Secret.h>

namespace fixy {

static_assert(sizeof(Secret<int>) == sizeof(int));
static_assert(sizeof(Secret<unsigned long long>) == sizeof(unsigned long long));
static_assert(!std::is_trivially_copyable_v<Secret<int>> && !std::is_trivially_copyable_v<Secret<unsigned long long>>,
              "std::bit_cast must not read a classified value out with no declassify<Policy>()");
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Secret<int>>,
              "std::start_lifetime_as must not build a classified value over bytes");
static_assert(std::is_trivially_move_constructible_v<Secret<int>> && std::is_trivially_destructible_v<Secret<int>>,
              "the move constructor stays trivial, so a Secret still passes in a register");

// The deleted operators refuse each flow of a classified value into a
// branch, an address or an order.  A requires-expression over each flow
// reads false, so an operator that a later edit defines stops the build.
namespace detail::secret_flow_lock {

template <typename S>
concept BranchesOn = requires(S const& s) { s ? 1 : 0; };
template <typename S>
concept IndexesWith = requires(S const& s, int (&table)[4]) { table[s]; };
template <typename S>
concept OffsetsPointerBy = requires(S const& s, int* p) { p + s; };
template <typename S>
concept IndexesContainerWith = requires(S const& s, std::array<int, 4>& table) { table[s]; };
template <typename S>
concept ReleasesLengthWithoutPolicy = requires(S const& s) { s.size(); };

static_assert(!BranchesOn<Secret<int>> && !BranchesOn<Secret<bool>>);
static_assert(!std::is_convertible_v<Secret<int>, bool> && !std::is_constructible_v<bool, Secret<int> const&>);
static_assert(!IndexesWith<Secret<std::size_t>> && !IndexesWith<Secret<int>>);
static_assert(!OffsetsPointerBy<Secret<std::size_t>> && !OffsetsPointerBy<Secret<int>>);
static_assert(!IndexesContainerWith<Secret<std::size_t>>);
static_assert(!std::is_convertible_v<Secret<std::size_t>, std::size_t>);
static_assert(!std::equality_comparable<Secret<int>> && !std::equality_comparable_with<Secret<int>, int>);
static_assert(!std::three_way_comparable<Secret<int>> && !std::totally_ordered<Secret<int>>);
static_assert(!std::totally_ordered_with<Secret<int>, int>);

// A length leaves only through its policy, and that policy gives a count
// from an rvalue too, never the payload.
static_assert(!ReleasesLengthWithoutPolicy<Secret<std::array<int, 4>>>);
static_assert(std::is_same_v<decltype(std::declval<Secret<std::array<int, 4>>&&>()
                                          .template declassify<tags::secret_policy::LengthOnly>()),
                             std::size_t>);

}  // namespace detail::secret_flow_lock

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

}  // namespace detail::secret_self_test

// The policy relation's discipline, derived from the namespace.  The
// seal is the one place a new policy must be acknowledged by hand.
// Adding a tag takes three steps: declare `struct NewTag final :
// secret_policy_base {}` in the secret_policy namespace of
// fixy/Tags.h, admit it with one edge in fixy/Secret.h, and move the seal
// and this pin.  A downstream consumer that pins its own cardinality
// against admitted_policy_count moves in the same change.
static_assert(::foundation::fail_closed::Sealed<^^tags::secret_policy::admitted_policies>);

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

}  // namespace fixy
