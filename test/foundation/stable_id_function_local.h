// The classes that test_stable_id_function_local compares across two
// translation units.
//
// Each unit declares its own class UnitLocal in an unnamed namespace, so
// the two classes have one printed name and are two types.  local_of<T>
// declares a class inside its body.  The class of local_of<UnitLocal>
// in the first unit is not the class of local_of<UnitLocal> in the
// second unit, and each holds the number of its own unit.  The two
// print one name, so a stable id that read that name would give the two
// classes one id.
//
// view_of<Unit>() takes the class of the unit as its argument.  An
// instantiation over a class with internal linkage has internal linkage,
// so each unit computes its values in its own context.

#pragma once

#include <foundation/reflect/Hash.h>

#include <cstdint>

namespace stable_id_function_local {

// A class with a declared name at namespace scope, the positive control.
struct Named {};

// A class declared in the body of a function template.  It holds the
// number of the unit whose class is the template argument.
template <class Unit>
inline auto local_of() noexcept {
    struct Local {
        int unit_number = Unit::unit_number;
    };
    return Local{};
}

// A class declared in the body of an inline function.  Every unit sees
// the one definition.
inline auto local_of_inline() noexcept {
    struct Local {};
    return Local{};
}

// What one unit sees.  An id of zero means that the stable id refuses
// the class.
struct UnitView {
    int unit_number = 0;
    std::uint64_t template_local_id = 0;
    std::uint64_t inline_local_id = 0;
    std::uint64_t named_id = 0;
};

// The stable id of T, or zero when T has no stable identity.
template <class T>
[[nodiscard]] constexpr std::uint64_t id_or_zero() noexcept {
    if constexpr (::foundation::reflect::HasStableIdentity<T>) {
        return ::foundation::reflect::stable_type_id<T>;
    } else {
        return 0;
    }
}

template <class Unit>
[[nodiscard]] UnitView view_of() noexcept {
    using TemplateLocal = decltype(local_of<Unit>());
    using InlineLocal = decltype(local_of_inline());
    return UnitView{TemplateLocal{}.unit_number, id_or_zero<TemplateLocal>(), id_or_zero<InlineLocal>(),
                    id_or_zero<Named>()};
}

// Defined in the second unit.
UnitView view_of_second_unit() noexcept;

}  // namespace stable_id_function_local
