// The compile-time checks of crucible/SchemaTable.h.

#include <crucible/SchemaTable.h>
#include <foundation/Transfer.h>
#include <foundation/effects/Effect.h>

#include <type_traits>
#include <utility>

namespace crucible {

// An entry owns the storage of its name, so the table moves it and never
// copies it.
static_assert(std::is_nothrow_move_constructible_v<SchemaEntry> && !std::is_copy_constructible_v<SchemaEntry>);

static_assert(::fixy::no_scoped_view_field_check<SchemaTable>());

// The two tables state synchronized{}, and each door of each table holds the
// check of the statement (foundation/Transfer.h).
static_assert(::foundation::transfer::Synchronized<SchemaTable>);
static_assert(::foundation::transfer::Synchronized<LateSchemaTable>);

// The section is the one token of a write section: no default constructor,
// no copy and no assignment.  It moves, and the move leaves the source with
// no binding.
static_assert(!std::is_default_constructible_v<WriteSection<SchemaTable>>);
static_assert(!std::is_copy_constructible_v<WriteSection<SchemaTable>>);
static_assert(!std::is_copy_assignable_v<WriteSection<SchemaTable>>
              && !std::is_move_assignable_v<WriteSection<SchemaTable>>);
static_assert(std::is_nothrow_move_constructible_v<WriteSection<SchemaTable>>);

// The claim door gives an Option of a section, which is empty for a sealed
// table.
static_assert(
    std::is_same_v<decltype(std::declval<SchemaTable&>().try_enter_write_section(std::declval<VigilFgCtx const&>())),
                   ::fixy::Option<WriteSection<SchemaTable>>>);

// The proof of a reopen comes only from the test context.  The doors that
// take a section or the proof are pinned in test/test_schema_table.cpp: a
// concept of this file that reads a member of the table template reads the
// open templates of each signature of the table.
static_assert(std::is_constructible_v<TableReopen<SchemaTable>, ::foundation::effects::Test const&>);
static_assert(!std::is_default_constructible_v<TableReopen<SchemaTable>>);
static_assert(!std::is_constructible_v<TableReopen<SchemaTable>, VigilFgCtx const&>);

}  // namespace crucible
