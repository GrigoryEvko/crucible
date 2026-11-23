// Negative-compile test. This file must fail to compile.
//
// The old route of the boxed fallback: the parts of a name of PyTorch, which
// a foreign ABI gave, joined into a buffer, and the buffer registered as an
// internal name.  The constructor of an internal name is consteval, so a text
// of the run time, from outside or copied from outside, is no internal name.
// The only route from outside to the table is sanitize_schema_name.
//
// Expected diagnostic: the call of the consteval constructor is no constant
// expression.

#include <crucible/SchemaTable.h>
#include <crucible/Vigil.h>
#include <fixy/Core.h>

// A text that mint_foreign_text gave at a foreign ABI.  The fixture never
// links, so the function has no body.
::fixy::ForeignText foreign_name() noexcept;

int main() {
    crucible::SchemaTable table;
    const auto view = table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>())
                          .expect("an open table mints a mutable view");
    ::fixy::FixedText<64> full_name{};
    static_cast<void>(full_name.append(foreign_name().characters()).is_ok());
    (void)table.register_name(::foundation::effects::testing::foreground<crucible::Vigil>(),
                              ::foundation::effects::testing::test().alloc, view,
                              crucible::InternalSchemaName{crucible::SchemaHash{0x42}, full_name.view()});
    return 0;
}
