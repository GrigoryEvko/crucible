// Negative-compile test. This file must fail to compile.
//
// The table keeps the characters of a name before its first zero, so an
// internal name with a zero would register other characters than it holds.
// The consteval constructor of an internal name checks its parts at compile
// time, and a part with a zero stops the build.
//
// Expected diagnostic: the constructor calls
// crucible::detail::internal_schema_name_holds_a_zero_byte, which no constant
// evaluation can call.

#include <crucible/SchemaTable.h>
#include <fixy/Core.h>

// A text of the program image with a zero after its first characters.
constexpr ::fixy::FixedText<16> kNameWithZero = [] {
    ::fixy::FixedText<16> name{"aten::"};
    static_cast<void>(name.append('\0').is_ok());
    static_cast<void>(name.append(::fixy::TextView{"mm"}).is_ok());
    return name;
}();

int main() {
    constexpr crucible::SchemaTable::InternalName name{kNameWithZero.view()};
    return static_cast<int>(name.parts().name().size());
}
