#pragma once

// A cheat is a type or function pointer crafted to look like it
// satisfies a concept gate while violating one structural property. The
// probe asserts the gate rejects it, so the build succeeds only while the
// gate stays strict. A later relaxation that admits the cheat fails the
// build instead of passing silently.

#include <crucible/Platform.h>
#include <crucible/safety/_Diagnostic.h>

#include <type_traits>

namespace crucible::safety::diag {

// A gate author specializes this per category and sets `defined` to
// true. The unspecialized primary lets a cheat be registered before its
// gate exists. Probes against it are inert until the specialization
// lands, at which point they all start enforcing.

template <Category C>
struct concept_gate {
    static constexpr bool defined = false;

    template <typename T>
    static constexpr bool admits_type = false;

    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

template <Category C>
inline constexpr bool is_gate_defined_v = concept_gate<C>::defined;

// Instantiating the probe at namespace scope is the assertion.

template <typename T, Category C>
struct cheat_probe_type {
    static constexpr bool admits = concept_gate<C>::template admits_type<T>;
    static constexpr bool gate_defined = concept_gate<C>::defined;

    static_assert(!gate_defined || !admits, "[CheatProbe_TypeAdmitted] The concept gate for this category admits a "
                                            "type registered as a cheat.\n"
                                            "Recovery:\n"
                                            "  (a) The gate predicate is weaker than it was. Restore the\n"
                                            "      stricter form that rejected this type.\n"
                                            "  (b) The cheat is registered by mistake. Remove this probe.\n"
                                            "The gate is the concept_gate specialization for this category.");
};

// The cheat here is a function pointer whose signature looks like the
// shape the gate expects while violating one structural property.

template <auto FnPtr, Category C>
struct cheat_probe_function {
    static constexpr bool admits = concept_gate<C>::template admits_function<FnPtr>;
    static constexpr bool gate_defined = concept_gate<C>::defined;

    static_assert(!gate_defined || !admits, "[CheatProbe_FunctionAdmitted] The concept gate for this category "
                                            "admits a function pointer registered as a cheat.\n"
                                            "Recovery:\n"
                                            "  (a) The gate predicate is weaker than it was. Restore the\n"
                                            "      stricter form that rejected this function.\n"
                                            "  (b) The cheat is registered by mistake. Remove this probe.\n"
                                            "The gate is the concept_gate specialization for this category.");
};

// This header asserts nothing that instantiates concept_gate for a
// category a consumer might specialize. A class template cannot be
// specialized after it has been instantiated for the same arguments, so
// an eager probe or an eager is_gate_defined_v read here would foreclose
// on every later specialization. Sentinel translation units specialize
// first and exercise the probes second, which is the sound order.

namespace detail::cheat_probe_shape_check {

// Checking the shape costs one instantiation, which permanently blocks a
// specialization for whichever category pays it. EffectRowMismatch pays
// it because nothing specializes that gate.
using gate_template = decltype(&concept_gate<Category::EffectRowMismatch>::defined);

static_assert(std::is_same_v<gate_template, const bool*>, "concept_gate<C>::defined must be a static constexpr bool");

}  // namespace detail::cheat_probe_shape_check

}  // namespace crucible::safety::diag
