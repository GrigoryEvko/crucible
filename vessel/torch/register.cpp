// register.cpp — registers one unboxed recording kernel per ATen operator for
// DispatchKey::Crucible, and publishes the compile-time CKernelId table.
//
// The boxed fallback in crucible_fallback.cpp stays. A per-operator kernel
// takes precedence over a fallback, so every operator named here moves to the
// unboxed path and every other operator, in every namespace, keeps reaching
// the fallback. Both build the same TraceRing::Entry from the bridge functions
// in record_kernel.h.
//
// The operator list is CRUCIBLE_ATEN_OP_LIST from aten_op_table.h, which is
// the one place the generator emits tokens rather than values: a type cannot be
// spelled as data, so the operator struct arrives as a macro argument. Each
// expansion pairs a struct with its table index, and the kernel derives
// everything else — the signature by reflection, the schema hash, the arity,
// the masks and the mutability from the row at that index.
//
// THE SHARDS
//
// One compile of the kernels of all 3110 operators used more than the hard
// limit of 600 s CPU of a compile job (COMPILE_CPU_LIMIT_S of
// utils/assay/loop/step.py).  So the build compiles this file one time for
// each shard.  vessel/torch/CMakeLists.txt
// gives each compile CRUCIBLE_VESSEL_REGISTER_SHARDS, the number of shards, and
// CRUCIBLE_VESSEL_REGISTER_SHARD, the index of its shard.  shard_of() gives
// each operator exactly one shard.  A shard instantiates the kernels of its
// own operators only.  Shard 0 also publishes the CKernelId table.

#if !defined(CRUCIBLE_VESSEL_REGISTER_SHARD) || !defined(CRUCIBLE_VESSEL_REGISTER_SHARDS)
#error "a compile of register.cpp needs CRUCIBLE_VESSEL_REGISTER_SHARD and CRUCIBLE_VESSEL_REGISTER_SHARDS"
#endif

#include "record_kernel.h"

#include <ATen/Operators.h>
#include <fixy/FixedArray.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace crucible::vessel {
namespace {

inline constexpr uint32_t kShardCount = CRUCIBLE_VESSEL_REGISTER_SHARDS;
inline constexpr uint32_t kShard = CRUCIBLE_VESSEL_REGISTER_SHARD;

static_assert(kShard < kShardCount, "the index of the shard of register.cpp is not less than the number of shards");

// Shard k holds the table indices k, k + S, k + 2 * S and so on, for S
// shards.  The table keeps the overloads of one operator adjacent, and the
// kernels of some overload sets are expensive.  The interleave spreads each set
// over the shards, so the cost of the shards stays near their average.  Each
// shard holds N / S of the N operators, rounded down or up.
[[nodiscard]] consteval uint32_t shard_of(uint32_t table_index) { return table_index % kShardCount; }

// =====================================================================
// The compile-time CKernelId table
//
// CKernelTable is filled one register_op call at a time and capped at
// CKERNEL_TABLE_CAP, which is 256. The generated table classifies 344
// operators, so registering them would abort before the first iteration
// finished. They are published instead, as one sorted array that
// crucible::classify_kernel reads before the run-time table.
//
// The array is built in two consteval passes because a FixedArray needs its
// size as a constant: the first counts the classified rows and the second
// fills and sorts them. std::define_static_array would do it in one pass, but
// it requires a structural type and CKernelEntry is not one — SchemaHash keeps
// its payload private, which is the property that stops a raw uint64_t being
// passed for a schema hash.
// =====================================================================

[[nodiscard]] consteval uint32_t classified_row_count() {
    uint32_t count = 0;
    for (const OpEntry& row : aten_op_table)
        if (row.kernel_id != crucible::CKernelId::OPAQUE) count++;
    return count;
}

// Sorted by schema hash, which is what crucible::classify_static bisects.
template <uint32_t Count>
[[nodiscard]] consteval ::fixy::FixedArray<crucible::CKernelEntry, Count> classified_rows() {
    ::fixy::FixedArray<crucible::CKernelEntry, Count> entries{};
    uint32_t next = 0;
    for (const OpEntry& row : aten_op_table)
        if (row.kernel_id != crucible::CKernelId::OPAQUE)
            entries[next++] =
                crucible::CKernelEntry{.schema_hash = crucible::SchemaHash{row.schema_hash}, .id = row.kernel_id};
    std::ranges::sort(entries, {}, &crucible::CKernelEntry::schema_hash);
    return entries;
}

inline constexpr auto kClassifiedRows = classified_rows<classified_row_count()>();

// Static storage duration, which is what publish_static_ckernel_table needs of
// the array it takes the address of.
inline constexpr crucible::StaticCKernelTable kStaticCKernelTable{.entries = kClassifiedRows.view()};

static_assert(kClassifiedRows.size() > crucible::CKERNEL_TABLE_CAP,
              "if the classified set fits the run-time cap again, registering it is the simpler route and this "
              "publication can go");

// The sort is what classify_static relies on, so it is checked rather than
// assumed.
static_assert(std::ranges::is_sorted(kClassifiedRows, {}, &crucible::CKernelEntry::schema_hash));

// A hash the generator's own witnesses pin, so a re-generation that changed the
// hash function reddens here rather than at replay.
static_assert(std::ranges::find_if(kClassifiedRows,
                                   [](const crucible::CKernelEntry& row) {
                                       return row.schema_hash == crucible::SchemaHash{0x983b9200d566222dULL};
                                   })
                  != kClassifiedRows.end(),
              "aten::mm's schema hash is absent from the classified set");

// =====================================================================
// Registration
//
// The name Library::impl takes is relative to the namespace of its block, so
// it is the qualified name record_kernel.h builds minus the "aten::" prefix.
// That is also why every row must be an aten row.
// =====================================================================

inline constexpr std::string_view kAtenNamespacePrefix = "aten::";

[[nodiscard]] consteval bool every_row_is_aten() {
    for (const OpEntry& row : aten_op_table)
        if (!std::string_view{row.name}.starts_with(kAtenNamespacePrefix)) return false;
    return true;
}

static_assert(every_row_is_aten(),
              "a row outside the aten namespace cannot be registered by the TORCH_LIBRARY_IMPL(aten, ...) block "
              "below; give it a block of its own");

template <uint32_t TableIndex, class Op>
void register_kernel(torch::Library& library) {
    using Kernel = RecordKernelFor<TableIndex, Op>;
    const QualifiedOpName name{aten_op_table[TableIndex]};
    library.impl(name.c_str() + kAtenNamespacePrefix.size(), Kernel::as_cpp_function());
}

// The condition depends on TableIndex, so the kernel of an operator of a
// different shard is not instantiated.
template <uint32_t TableIndex, class Op>
void register_kernel_of_shard(torch::Library& library) {
    if constexpr (shard_of(TableIndex) == kShard) register_kernel<TableIndex, Op>(library);
}

}  // namespace
}  // namespace crucible::vessel

// The classification is published from here rather than from a separate
// initializer, so it is in place before any operator that a shard registers can
// run and therefore before the background thread can classify one.  dlopen of
// the library runs the block of each shard before it returns.
TORCH_LIBRARY_IMPL(aten, Crucible, m) {
    if constexpr (crucible::vessel::kShard == 0)
        crucible::publish_static_ckernel_table(crucible::vessel::kStaticCKernelTable);

#define CRUCIBLE_REGISTER_ATEN_OP(index, op_struct) \
    ::crucible::vessel::register_kernel_of_shard<index##u, ::at::_ops::op_struct>(m);
    CRUCIBLE_ATEN_OP_LIST(CRUCIBLE_REGISTER_ATEN_OP)
#undef CRUCIBLE_REGISTER_ATEN_OP
}
