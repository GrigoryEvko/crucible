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
// Shards
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
#include <fixy/Core.h>
#include <foundation/Quarantine.h>

#include <cstddef>
#include <cstdint>

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
    for_each_aten_op_row([&count](const OpEntry& row) {
        if (row.kernel_id != crucible::CKernelId::OPAQUE) count++;
    });
    return count;
}

// Sorted by schema hash, which is what crucible::classify_static bisects.
// The first pass takes the hash and the kernel of each classified row in the
// order of the table.  Then each row goes to the slot of its rank: the count
// of the rows with a lower hash, or with the same hash at an earlier place.
// So the result is sorted, and two rows of one hash keep the order of the
// table.  The rank loop compares the raw hashes, so the constant evaluation
// stays below its operation limit.  Complexity: O(Count^2) compares, at
// compile time.
template <uint32_t Count>
[[nodiscard]] consteval ::fixy::FixedArray<crucible::CKernelEntry, Count> classified_rows() {
    using Slot = ::fixy::Index<Count>;
    ::fixy::FixedArray<uint64_t, Count> hashes{};
    ::fixy::FixedArray<crucible::CKernelId, Count> kernels{};
    uint32_t next = 0;
    for_each_aten_op_row([&](const OpEntry& row) {
        if (row.kernel_id == crucible::CKernelId::OPAQUE) return;
        const Slot slot = Slot::check(next).expect("the count pass counted each classified row");
        hashes.with_element(slot, [&row](uint64_t& hash) noexcept { hash = row.schema_hash; });
        kernels.with_element(slot, [&row](crucible::CKernelId& kernel) noexcept { kernel = row.kernel_id; });
        ++next;
    });
    ::fixy::FixedArray<crucible::CKernelEntry, Count> sorted{};
    uint32_t place = 0;
    for (const uint64_t hash : hashes) {
        uint32_t rank = 0;
        uint32_t other_place = 0;
        for (const uint64_t other : hashes) {
            if (other < hash || (other == hash && other_place < place)) ++rank;
            ++other_place;
        }
        const crucible::CKernelEntry entry{
            .schema_hash = crucible::SchemaHash{hash},
            .id = kernels.at(Slot::check(place).expect("a place is below the count of the rows"))};
        sorted.with_element(Slot::check(rank).expect("a rank is below the count of the rows"),
                            [&entry](crucible::CKernelEntry& held) noexcept { held = entry; });
        ++place;
    }
    return sorted;
}

// True when each row has a schema hash no lower than the hash of the row
// before it.  Complexity: O(n).
[[nodiscard]] consteval bool is_sorted_by_schema_hash(::fixy::View<const crucible::CKernelEntry> rows) {
    for (const ::fixy::View<const crucible::CKernelEntry, 2> pair : rows.windows<2>()) {
        if (pair.at(::fixy::index<1>()).schema_hash < pair.at(::fixy::index<0>()).schema_hash) return false;
    }
    return true;
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
static_assert(is_sorted_by_schema_hash(kClassifiedRows.view()));

// A hash the generator's own witnesses pin, so a re-generation that changed the
// hash function reddens here rather than at replay.
static_assert(::fixy::find_if(kClassifiedRows.view(),
                              [](const crucible::CKernelEntry& row) noexcept {
                                  return row.schema_hash == crucible::SchemaHash{0x983b9200d566222dULL};
                              })
                  .is_some(),
              "aten::mm's schema hash is absent from the classified set");

// True when a row of the table has the schema hash.  Complexity: O(n) in
// the rows, at compile time.
[[nodiscard]] consteval bool has_row_with_schema_hash(crucible::SchemaHash hash) {
    bool is_found = false;
    for_each_aten_op_row([&](const OpEntry& row) { is_found |= crucible::SchemaHash{row.schema_hash} == hash; });
    return is_found;
}

// The build refuses each region that holds the closing entry of a nest, so no
// op of the table may have its schema hash.
static_assert(!has_row_with_schema_hash(crucible::kUnrecordedNestSchema),
              "an op of the table has the schema hash of the closing entry of a nest");

// =====================================================================
// Registration
//
// The name Library::impl takes is relative to the namespace of its block, so
// it is the qualified name record_kernel.h builds minus the "aten::" prefix.
// That is also why every row must be an aten row.
// =====================================================================

inline constexpr ::fixy::TextView kAtenNamespacePrefix{"aten::"};

[[nodiscard]] consteval bool every_row_is_aten() {
    bool is_each_aten = true;
    for_each_aten_op_row([&is_each_aten](const OpEntry& row) {
        if (!::fixy::starts_with(row.name, kAtenNamespacePrefix)) is_each_aten = false;
    });
    return is_each_aten;
}

static_assert(every_row_is_aten(),
              "a row outside the aten namespace cannot be registered by the TORCH_LIBRARY_IMPL(aten, ...) block "
              "below; give it a block of its own");

// The name of the operator of a row after the namespace prefix.  Each row is
// an aten row (every_row_is_aten), so the name starts with the prefix.
[[nodiscard]] constexpr ::fixy::TextView name_in_aten_namespace(const OpEntry& row) noexcept {
    return row.name.window(kAtenNamespacePrefix.size(), row.name.size() - kAtenNamespacePrefix.size())
        .expect("each row of the table is an aten row");
}

// The two functions take the library of PyTorch and give it a kernel.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: Library::impl of PyTorch registers the kernel of one operator")
template <uint32_t TableIndex, class Op>
void register_kernel(torch::Library& library) {
    using Kernel = RecordKernelFor<TableIndex, Op>;
    const OpEntry& row = aten_op_row<TableIndex>();
    const QualifiedOpName name{name_in_aten_namespace(row), row.overload};
    library.impl(name.c_str(), Kernel::as_cpp_function());
}

// The condition depends on TableIndex, so the kernel of an operator of a
// different shard is not instantiated.
template <uint32_t TableIndex, class Op>
void register_kernel_of_shard(torch::Library& library) {
    if constexpr (shard_of(TableIndex) == kShard) register_kernel<TableIndex, Op>(library);
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace
}  // namespace crucible::vessel

// The classification is published from here rather than from a separate
// initializer, so it is in place before any operator that a shard registers can
// run and therefore before the background thread can classify one.  dlopen of
// the library runs the block of each shard before it returns.
//
// The block is the registration macro of PyTorch, and the expansion of the
// list names each operator struct of at::_ops.  So the block is in an opt-out
// region of the class ABI.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the block of PyTorch registers the kernel of each operator struct of at::_ops")
TORCH_LIBRARY_IMPL(aten, Crucible, m) {
    if constexpr (crucible::vessel::kShard == 0)
        crucible::publish_static_ckernel_table(crucible::vessel::kStaticCKernelTable);

#define CRUCIBLE_REGISTER_ATEN_OP(index, op_struct) \
    ::crucible::vessel::register_kernel_of_shard<index##u, ::at::_ops::op_struct>(m);
    CRUCIBLE_ATEN_OP_LIST(CRUCIBLE_REGISTER_ATEN_OP)
#undef CRUCIBLE_REGISTER_ATEN_OP
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
