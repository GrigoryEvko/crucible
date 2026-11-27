// The compile-time checks of crucible/CKernel.h.

#include <crucible/CKernel.h>
#include <foundation/Transfer.h>
#include <foundation/effects/Effect.h>

#include <type_traits>

namespace crucible {

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(CKernelEntry);

static_assert(::fixy::no_scoped_view_field_check<CKernelTable>());

// The table states synchronized{}, and each door of the table holds the
// check of the statement (foundation/Transfer.h).
static_assert(::foundation::transfer::Synchronized<CKernelTable>);

// register_op takes the write section of the table and no context, so a
// writer with no section does not compile.
template <class Writer>
concept registers_op_with = requires(CKernelTable& table, Writer& writer, CKernelTable::MutableView const& view) {
    table.register_op(writer, view, SchemaHash{1}, CKernelId::GEMM_MM);
};
static_assert(registers_op_with<WriteSection<CKernelTable>>);
static_assert(!registers_op_with<VigilFgCtx const>);
static_assert(!registers_op_with<::foundation::effects::Test const>);

// The section is the one token of a write section: no default constructor,
// no copy and no assignment.  It moves, and the move leaves the source with
// no binding.
static_assert(!std::is_default_constructible_v<WriteSection<CKernelTable>>);
static_assert(!std::is_copy_constructible_v<WriteSection<CKernelTable>>);
static_assert(!std::is_copy_assignable_v<WriteSection<CKernelTable>>
              && !std::is_move_assignable_v<WriteSection<CKernelTable>>);
static_assert(std::is_nothrow_move_constructible_v<WriteSection<CKernelTable>>);

// The clear takes the proof of a reopen, which only the test context builds,
// and no context.
template <class Proof>
concept clears_with = requires(CKernelTable& table, Proof&& proof) { table.clear(static_cast<Proof&&>(proof)); };
static_assert(clears_with<TableReopen<CKernelTable>>);
static_assert(!clears_with<::foundation::effects::Test const&>);

static_assert(sizeof(CKernelTableSingleton) == sizeof(CKernelTable*));

// Each byte below ckernel_id_count names a kernel, and the count names none,
// so enum_from widens exactly the bytes of the ids.
[[nodiscard]] consteval bool names_each_byte_below_the_count() {
    for (uint8_t raw = 0; std::size_t{raw} < ckernel_id_count; ++raw) {
        if (::fixy::enum_from<CKernelId>(raw).is_none()) return false;
    }
    return ::fixy::enum_from<CKernelId>(static_cast<uint8_t>(ckernel_id_count)).is_none();
}
static_assert(names_each_byte_below_the_count());

}  // namespace crucible
