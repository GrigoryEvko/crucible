#pragma once

#include <crucible/fixy/Reject.h>

namespace crucible::fixy {

// A translation unit not wired to the build system leaves the macro
// undefined. Default to strict, which is the safe direction.
#ifndef CRUCIBLE_FIXY_STRICT
#define CRUCIBLE_FIXY_STRICT 1
#endif

inline constexpr bool fixy_is_strict = (CRUCIBLE_FIXY_STRICT != 0);

// The Grants pack is deliberately not inspected. Grant permissivity is the
// point of sketch mode, and the parameter list must still match IsAccepted
// so the toggle below can alias either one.
template <typename Type, typename... Grants>
concept IsAcceptedSketch = detail::accept::type_is_accepted_payload<Type>();

// Swapping in the permissive concept is safe because grant well-formedness
// and the collision rules are asserted in the function class body whatever
// this toggle says. Only the engagement and corpus checks relax.
template <typename Type, typename... Grants>
concept IsAcceptedActive =
#if CRUCIBLE_FIXY_STRICT
    IsAccepted<Type, Grants...>;
#else
    IsAcceptedSketch<Type, Grants...>;
#endif

namespace detail::profile_self_test {

#if CRUCIBLE_FIXY_STRICT
static_assert(fixy_is_strict, "Profile.h: fixy_is_strict must be true when "
                              "CRUCIBLE_FIXY_STRICT=1.");
#else
static_assert(!fixy_is_strict, "Profile.h: fixy_is_strict must be false when "
                               "CRUCIBLE_FIXY_STRICT=0.");
#endif

static_assert(IsAcceptedSketch<int>, "IsAcceptedSketch<int> must accept the empty Grants pack — sketch "
                                     "mode is permissive on the Grants axis.");
static_assert(IsAcceptedSketch<int*>, "IsAcceptedSketch<int*> must accept — object pointers are accepted "
                                      "payloads.");
static_assert(IsAcceptedSketch<int (*)(int)>, "IsAcceptedSketch<int(*)(int)> must accept — function POINTERS are "
                                              "object types, hence accepted payloads.");

static_assert(!IsAcceptedSketch<void>, "IsAcceptedSketch<void> must reject — Fn<void, ...> "
                                       "has no value-category semantics; sketch mode does not bypass the "
                                       "Type-axis floor.");
static_assert(!IsAcceptedSketch<const int>, "top-level const-qualified Type must reject — silently "
                                            "deletes Fn's defaulted assignment ops.");
static_assert(!IsAcceptedSketch<volatile int>, "top-level volatile-qualified Type must reject for "
                                               "the same reason as const.");
static_assert(!IsAcceptedSketch<int&>, "lvalue-reference Type must reject — Fn<int&, ...> "
                                       "has no clear copy/move semantics.");
static_assert(!IsAcceptedSketch<int&&>, "rvalue-reference Type must reject for the same "
                                        "reason as lvalue-reference.");
static_assert(!IsAcceptedSketch<int[5]>, "array Type must reject — Fn(Type) would silently "
                                         "decay to pointer instead of copy by value.");
static_assert(!IsAcceptedSketch<int(int)>, "bare function-type Type must reject — wrap as "
                                           "function pointer or callable before instantiating fixy::fn.");

namespace not_a_grant_tag {
struct Tag {};
}  // namespace not_a_grant_tag
static_assert(IsAcceptedSketch<int, not_a_grant_tag::Tag>,
              "IsAcceptedSketch ignores Grants-pack shape — even a non-grant "
              "type in the pack accepts as long as Type is structurally valid. "
              "(Tier-2 AllGrantsWellFormed in Fn's class body still rejects the "
              "binding downstream, but THIS concept does not.)");

}  // namespace detail::profile_self_test

}  // namespace crucible::fixy
