// Crucible native dispatcher fallback for DispatchKey::Crucible.
//
// Registers a boxed fallback that intercepts every ATen op when the key
// is enabled in TLS.  Reads CrucibleState to get the Vigil handle, extracts
// tensor metadata from the IValue stack, and feeds it to Vigil::dispatch_op().
//
// This is the C++ counterpart of crucible_mode.py (TorchDispatchMode).
// Where Python adds ~30-50us/op overhead, this path adds ~100ns/op.
//
// Build requirements:
//   - PyTorch fork with DispatchKey::Crucible + CrucibleState TLS
//   - Crucible headers (Vigil.h, TraceRing.h, MerkleDag.h, Types.h)
//
// Loaded via torch.ops.load_library() -- the TORCH_LIBRARY_IMPL registration
// fires on dlopen, no explicit init needed.

#include "dispatch_api.h"
#include "eager_numerics.h"
#include "layout_note.h"
#include "record_kernel.h"
#include "storage_generation.h"

#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <foundation/Quarantine.h>
#include <foundation/core/WindowStack.h>

#include <c10/core/CPUAllocator.h>
#include <c10/core/CrucibleState.h>
#include <c10/core/StorageImpl.h>

#include <ATen/core/dispatch/Dispatcher.h>
#include <ATen/core/ivalue.h>
#include <ATen/core/stack.h>
#include <torch/library.h>
#include <torch/csrc/autograd/function.h>
#include <torch/csrc/autograd/variable.h>

#include <crucible/MerkleDag.h>
#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/Types.h>
#include <crucible/Vigil.h>

#include "vessel_api_typed.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace {

// =====================================================================
// The dispatch-key layout that the PyTorch fork patch must produce
//
// The dispatcher gives the highest priority to the highest functionality
// key.  Crucible must therefore sit directly before EndOfFunctionalityKeys: a
// key that upstream appends after it demotes the recorder, and the build still
// succeeds and the fallback still fires, only later.  The patch also deletes
// DeferredInit and gives its slot to Crucible, so the functionality-key budget
// stays the same.  The compiler reads the patched enum here, so a re-indented
// header or an explicit enumerator value cannot hide a change.
//
// The checks name the enum of PyTorch, so they are in an ABI region.  The
// search of an enumerator by its identifier is the reflection door of the
// base (has_enumerator_named of foundation/core/Text.h).
// =====================================================================

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the checks read the dispatch keys that the PyTorch fork declares")
static_assert(std::to_underlying(c10::DispatchKey::Crucible) + 1
                  == std::to_underlying(c10::DispatchKey::EndOfFunctionalityKeys),
              "DispatchKey::Crucible is not the last functionality key: upstream added a key after it, so the "
              "recorder no longer has the top dispatch priority.  Move Crucible in "
              "patches/pytorch-crucible-integration.patch.");

// The positive control: a search that never finds a name would pass the next
// assertion for the wrong reason.
static_assert(::fixy::has_enumerator_named<c10::DispatchKey>("Crucible")
                  && ::fixy::has_enumerator_named<c10::DispatchKey>("EndOfFunctionalityKeys"),
              "the search of an enumerator of c10::DispatchKey finds no known key, so it proves nothing");

static_assert(!::fixy::has_enumerator_named<c10::DispatchKey>("DeferredInit"),
              "DispatchKey::DeferredInit survived the patch: the removal hunk did not apply, so Crucible widened "
              "the key budget instead of reusing the dead slot.  Repair "
              "patches/pytorch-crucible-integration.patch.");
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// =====================================================================
// The modes of the TLS state
//
// The PyTorch fork patch declares c10::CrucibleMode.  TlsMode of
// crucible_native.py mirrors its enumerators, and the loader compares the two
// after the load of this library through crucible_dispatch_mode_entry.  The
// table below reads the name and the value of each enumerator by reflection,
// so a change of the enum in the patch reaches that check with no edit here.
// crucible_dispatch_set_tls_mode refuses a value that names no enumerator,
// and the fallback ends the process on such a value.
//
// The table gives each name to ctypes as a C text, and it reads the enum of
// PyTorch, so it is in an ABI region.
// =====================================================================

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: ctypes reads the name and the value of each mode of the TLS state of PyTorch")

// One enumerator of c10::CrucibleMode: its name with a terminating zero, and
// its value.
struct ModeEnumerator {
    const char* name = nullptr;
    uint8_t value = 0;
};

constexpr std::size_t kModeCount = std::meta::enumerators_of(^^c10::CrucibleMode).size();

// The enumerators of c10::CrucibleMode in the order of their declarations.
consteval ::fixy::FixedArray<ModeEnumerator, kModeCount> read_mode_enumerators() {
    ::fixy::FixedArray<ModeEnumerator, kModeCount> table{};
    std::size_t position = 0;
    for (const std::meta::info enumerator : std::meta::enumerators_of(^^c10::CrucibleMode)) {
        const ModeEnumerator mode{.name = std::define_static_string(std::meta::identifier_of(enumerator)),
                                  .value = std::to_underlying(std::meta::extract<c10::CrucibleMode>(enumerator))};
        table.with_element(
            ::fixy::Index<kModeCount>::check(position).expect("the walk stays below the count of the modes"),
            [mode](ModeEnumerator& held) noexcept { held = mode; });
        ++position;
    }
    return table;
}

constexpr ::fixy::FixedArray<ModeEnumerator, kModeCount> kModeEnumerators = read_mode_enumerators();

// True when the value names an enumerator of c10::CrucibleMode.  Complexity:
// linear in the modes.
[[nodiscard]] constexpr bool is_named_mode(uint8_t raw) noexcept {
    for (const ModeEnumerator& mode : kModeEnumerators) {
        if (mode.value == raw) return true;
    }
    return false;
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// =====================================================================
// The bridge this path shares with the unboxed kernels
//
// FNV-1a, the metadata fill, the shape hash, the slot limits and the phase
// derivation all live in record_kernel.h. Two spellings of any of them would
// let the boxed and the unboxed path write different entries for one
// operation, which is the property the whole design rests on. The names are
// pulled in here so the call sites below read as they did when the definitions
// were local.
//
// The schema hash must also match vessel_api.cpp's crucible_hash_string, so
// that hash("aten::mm.default") is the same value in the Python ctypes path.
// The guard word (compute_shape_hash of entry_slots.h) is one function for
// the three paths.
// =====================================================================

using crucible::vessel::argument_position_at;
using crucible::vessel::ArgumentPosition;
using crucible::vessel::DataArguments;
using crucible::vessel::InputOverflow;
using crucible::vessel::is_structural_scalar;
using crucible::vessel::kMaxSchemaArguments;
using crucible::vessel::kAfterCrucibleKeyset;
using crucible::vessel::MAX_INLINE_METAS;
using crucible::vessel::MetaCount;
using crucible::vessel::ScalarArgs;
using crucible::vessel::scalar_result_meta;
using crucible::vessel::schema_hash_of_name;
using crucible::vessel::backward_depth;
using crucible::vessel::compute_shape_hash;
using crucible::vessel::fill_meta;
using crucible::vessel::is_foreach_op_name;
using crucible::vessel::phase_flag_bits;

// =====================================================================
// Schema hash cache
//
// One process-wide table: the key of an operator gives its schema hash and
// its two flags.  Each fact depends only on the operator, so each thread
// reads the same table.  The table is not thread-local, because the
// TLS block of this library goes into the static TLS of the process (the
// contract flag of foundation has the initial-exec model), and dlopen finds
// only a small surplus there with the default glibc tunables.
//
// A slot is empty, claimed by the one thread that writes it, ready, or
// retired.  The writer writes the key, the hash and the flags, then stores
// the state ready with release order.  A reader loads the state with acquire
// order, so it reads the facts only after they are complete.  The key, the
// hash and the flags of a ready slot never change again, and a retired slot
// never changes again.  A hit at the home slot of an operator is one acquire
// load and one comparison.  A lookup reads at most SCHEMA_CACHE_PROBES slots,
// and an operator with no ready slot in that window gets its facts computed
// again.
//
// is_foreach is a substring search over the operator name, which the unboxed
// path answers at compile time.  The table keeps the hot boxed path at one
// slot read.
//
// The key of an operator is the address of its c10::OperatorName, which
// OperatorHandle::operator_name() gives as a reference into the
// OperatorEntry of the dispatcher.  The dispatcher keeps one OperatorEntry
// for each operator while the operator has a registration.  The address
// names one schema only while that schema is registered.  When the last
// definition of an operator goes, the dispatcher can free its entry, and a
// later operator can get an entry at that address.  An implementation can
// also keep the entry, and a later definition then gives the same entry a
// different schema.  So the dispatcher calls SchemaCacheListener before it
// removes a schema, and the listener retires each slot of that key.  A
// lookup then reads no fact of the old schema.
//
// The address of the OperatorHandle itself is not a key.  A call through
// torch.ops gives the fallback a copy of the handle on the stack of the
// caller, so two operators that Python calls at one stack depth give one
// handle address.
//
// The key is the AddressKey of the operator name (operator_key_of).  A key
// compares and gives a hash, and no pointer comes back from it, so the cache
// never reads through the address.  A slot holds the full key and a lookup
// compares it.  An empty slot holds the key of the null address
// (AddressKey::of_null), and no operator name has the address zero.  Each
// address is a key, so an AddressKey has no niche, and an Option of it would
// add a word to the slot.  The constant keeps the slot at 32 bytes, so the
// cache stays in the first-level cache.

// The key of an operator: the address of its operator name.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the address of the operator name of PyTorch keys the cache")
[[nodiscard]] static ::fixy::AddressKey operator_key_of(const c10::OperatorName& name) noexcept {
    return ::fixy::mint_address_key(&name);
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The states of a slot, and the bits of its flags.  A retired slot held the
// facts of a schema that the dispatcher removed.  A lookup passes it as it
// passes a ready slot of a different key, and no writer claims it again.  A
// slot with FLAG_REFUSED holds an operator whose name the door of an outside
// name refused (record_schema_miss), and its other facts are zero.
static constexpr uint64_t SLOT_EMPTY = 0;
static constexpr uint64_t SLOT_CLAIMED = 1;
static constexpr uint64_t SLOT_READY = 2;
static constexpr uint64_t SLOT_RETIRED = 3;
static constexpr uint64_t FLAG_MUTABLE = 1;
static constexpr uint64_t FLAG_FOREACH = 2;
static constexpr uint64_t FLAG_REFUSED = 4;
// The data arguments of the schema (DataArguments of entry_slots.h) are in
// the high 32 bits of the flags.
static constexpr uint32_t FLAG_DATA_ARGUMENTS_SHIFT = 32;

struct SchemaHashSlot {
    ::fixy::Atomic<uint64_t> state{SLOT_EMPTY};
    ::fixy::AddressKey key = ::fixy::AddressKey::of_null();
    crucible::SchemaHash hash;
    uint64_t flags = 0;
};

static_assert(sizeof(SchemaHashSlot) == 32, "SchemaHashSlot holds four words and no padding.  An AddressKey "
                                            "holds one word.");

static constexpr uint32_t SCHEMA_CACHE_CAP = 2048;
static constexpr uint32_t SCHEMA_CACHE_MASK = SCHEMA_CACHE_CAP - 1;
static constexpr uint32_t SCHEMA_CACHE_PROBES = 8;
// constinit: the table has no dynamic initializer, so no access waits for one.
// The table is one object for the process with no CRUCIBLE_PROCESS_WIDE
// marker: only libcrucible_dispatch.so compiles this unit, and the object has
// internal linkage, on which the marker has no effect.  A copy in a second
// library would be correct too, because a cache that misses only computes the
// facts again and fills its own slot.
static constinit ::fixy::FixedArray<SchemaHashSlot, SCHEMA_CACHE_CAP> schema_cache{};

// The slot of a probe, for a read.  The mask keeps it below SCHEMA_CACHE_CAP,
// so the optimizer removes the check.
[[nodiscard]] static SchemaHashSlot const& read_schema_slot(uint64_t probe) noexcept {
    return schema_cache.at(::fixy::Index<SCHEMA_CACHE_CAP>::check(probe & SCHEMA_CACHE_MASK)
                               .expect("a masked probe is below SCHEMA_CACHE_CAP"));
}

// Lends the slot of a probe to body in place, and gives the result of body.
// The cache is process-wide and each thread probes it at the same time: the
// state of a slot is an Atomic, and a thread writes the other fields only
// after its claim of the state.
template <class Body>
static decltype(auto) with_schema_slot(uint64_t probe, Body&& body) noexcept {
    return schema_cache.with_element(::fixy::Index<SCHEMA_CACHE_CAP>::check(probe & SCHEMA_CACHE_MASK)
                                         .expect("a masked probe is below SCHEMA_CACHE_CAP"),
                                     static_cast<Body&&>(body));
}

// The first slot of the probe window of a key.  The hash of the key mixes
// each bit of the address into the low bits, and the mask takes them.  Each
// reader compares the key itself, and no integer of the address leaves the
// AddressKey.
[[nodiscard]] static uint64_t home_probe(::fixy::AddressKey const& key) noexcept {
    return key.hash() & SCHEMA_CACHE_MASK;
}

struct SchemaInfo {
    crucible::SchemaHash hash;
    bool is_mutable = false;
    bool is_foreach = false;
    // The arguments whose slots are data, with the structural scalars of
    // entry_slots.h taken out.
    DataArguments data_arguments{};
};

// The data arguments of a schema with the rows of kStructuralScalars of the
// schema taken out.  O(arguments * rows), once for each schema.
[[nodiscard]] static DataArguments without_structural_scalars(crucible::SchemaHash schema_hash,
                                                              DataArguments by_type) noexcept {
    DataArguments kept{};
    for (uint32_t position = 0; position < kMaxSchemaArguments; ++position) {
        for (const ArgumentPosition at : argument_position_at(position)) {
            if (by_type.role(position) == crucible::ScalarRole::DATA && !is_structural_scalar(schema_hash, at))
                kept = kept.with_data(at);
        }
    }
    return kept;
}

// The word of the flags of a slot: the facts of an operator, or FLAG_REFUSED
// for an operator with a refused name.
[[nodiscard]] static uint64_t flags_of(::fixy::Option<SchemaInfo> const& facts) noexcept {
    return facts.match(
        [](SchemaInfo const& info) noexcept {
            return (info.is_mutable ? FLAG_MUTABLE : 0) | (info.is_foreach ? FLAG_FOREACH : 0)
                 | (uint64_t{info.data_arguments.bits()} << FLAG_DATA_ARGUMENTS_SHIFT);
        },
        [] noexcept { return FLAG_REFUSED; });
}

// Keeps the facts of an operator, or its refusal, in the first empty slot of
// the window of its key.  The thread whose claim succeeds is the one writer of
// the slot.  Two threads that miss the same operator at one time can fill two
// slots, and each slot holds the same facts.  A window with no empty slot
// keeps nothing, and the next call of the operator misses again.
// O(SCHEMA_CACHE_PROBES).
static void keep_schema_facts(::fixy::AddressKey const& key, ::fixy::Option<SchemaInfo> const& facts) noexcept {
    const crucible::SchemaHash schema_hash = facts.match([](SchemaInfo const& info) noexcept { return info.hash; },
                                                         [] noexcept { return crucible::SchemaHash{}; });
    const uint64_t flags = flags_of(facts);
    const uint64_t idx = home_probe(key);
    for (uint32_t probe = 0; probe < SCHEMA_CACHE_PROBES; ++probe) {
        const bool is_claimed =
            with_schema_slot(idx + probe, [&key, schema_hash, flags](SchemaHashSlot& slot) noexcept {
                if (!slot.state.cas_acq_rel(SLOT_EMPTY, SLOT_CLAIMED).is_ok()) return false;
                slot.key = key;
                slot.hash = schema_hash;
                slot.flags = flags;
                slot.state.store_release(SLOT_READY);
                return true;
            });
        if (is_claimed) return;
    }
}

// The two parts of the name of an operator of PyTorch, as the dispatcher
// gives them: "namespace::name" and the overload, which can be empty.  The
// parts are outside data, so each one keeps the tag of its boundary.
using OperatorNamePart = ::fixy::Tagged<::fixy::TextView, ::fixy::tags::source::ABIBoundary>;

struct OperatorNameParts {
    OperatorNamePart base{};
    OperatorNamePart overload{};
};

// The reason of a refusal of the door of an outside name, for a report.
[[nodiscard]] static ::fixy::TextView refusal_text(crucible::vessel::OutsideNameRefusal refused) noexcept {
    switch (refused) {
        case crucible::vessel::OutsideNameRefusal::zero_hash:
            return "the schema hash of the name is zero";
        case crucible::vessel::OutsideNameRefusal::empty:
            return "the name has no character";
        case crucible::vessel::OutsideNameRefusal::too_long:
            return "the name is longer than SCHEMA_NAME_MAX characters";
        case crucible::vessel::OutsideNameRefusal::holds_zero:
            return "the name holds a zero byte";
        case crucible::vessel::OutsideNameRefusal::reserved:
            return "an operator of the generated table has the schema hash of the name";
        case crucible::vessel::OutsideNameRefusal::collision:
            return "the schema tables hold a different name of the schema hash";
        case crucible::vessel::OutsideNameRefusal::full:
            return "the schema table holds SCHEMA_TABLE_CAP names";
        default:
            ::fixy::unreachable();
    }
}

// The first characters of a part of a name, for a report.
[[nodiscard]] static ::fixy::TextView shown_part(::fixy::TextView part) noexcept {
    constexpr std::size_t kShownCharacters = 64;
    return part.window(0, ::fixy::min(part.size(), kShownCharacters)).expect("the window is inside the part");
}

// Keeps the refusal of an operator in the schema cache, and reports it.  The
// cache keeps the refusal, so the report comes one time for each operator,
// and the fallback runs the door one time.  Cold: a refused name is rare.
CRUCIBLE_COLD static void refuse_schema_name(::fixy::AddressKey const& key, OperatorNameParts const& name,
                                             crucible::vessel::OutsideNameRefusal refused) noexcept {
    keep_schema_facts(key, ::fixy::none);
    const ::fixy::TextView base = name.base.value();
    const ::fixy::TextView overload = name.overload.value();
    const bool has_overload = overload.size() != 0;
    const std::size_t characters = base.size() + (has_overload ? 1 + overload.size() : 0);
    ::fixy::report(::fixy::Sink::Err,
                   "crucible: the fallback records no operation of the operator {}{}{} ({} characters), and the "
                   "operator runs eagerly: {}\n",
                   shown_part(base), has_overload ? ::fixy::TextView{"."} : ::fixy::TextView{}, shown_part(overload),
                   characters, refusal_text(refused));
}

// The facts of an operator that the cache does not hold, from the two parts
// of its name, the mutability of its schema and the data arguments that the
// types of its schema give.  A library or a Python module can define an
// operator with any name at run time, so the name is outside data.  The door
// of an outside name (register_outside_schema_name of vessel_api_typed.h)
// checks the name and registers it for the trace reader.  A refused name
// gives no facts: the operator runs eagerly, and the fallback records no
// operation of it.  The first empty slot of the window of the key keeps the
// facts or the refusal, so the door runs one time for each operator.  The
// helper is cold, and the text of the name is on its stack only.
// Complexity: the length of the name, and O(log n) in the rows of the
// generated table.
CRUCIBLE_COLD static ::fixy::Option<SchemaInfo> record_schema_miss(crucible::Vigil& vigil,
                                                                   OperatorNameParts const& name, bool mutable_op,
                                                                   DataArguments data_by_type,
                                                                   ::fixy::AddressKey const& key) {
    // The name "namespace::name.overload", and its schema hash.  A name of
    // more than SCHEMA_NAME_MAX characters does not fit the text, and the
    // door refuses such a name too.  The door of the schema hash maps a fold
    // of zero, so a name never gives the sentinel of no schema
    // (schema_hash_of_name of entry_slots.h).
    const ::fixy::TextView overload = name.overload.value();
    ::fixy::FixedText<crucible::SCHEMA_NAME_MAX> full_name{};
    const bool is_joined =
        full_name.append(name.base.value()).is_ok()
        && (overload.size() == 0 || (full_name.append('.').is_ok() && full_name.append(overload).is_ok()));
    if (!is_joined) [[unlikely]] {
        refuse_schema_name(key, name, crucible::vessel::OutsideNameRefusal::too_long);
        return ::fixy::none;
    }
    const crucible::SchemaHash schema_hash = schema_hash_of_name(full_name.view()).value();

    // The tables take names only from a thread that holds a Vigil's producer
    // claim, which the caller checked.  The global table is sealed by now,
    // so the name goes to the late table, which the vessel library reads.
    // The joined copy of the parts is outside data too, so it takes the tag
    // of the boundary again.
    return crucible::vessel::register_outside_schema_name(
               vigil, schema_hash, ::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(full_name.view()))
        .match(
            [&name, &key, schema_hash, mutable_op, data_by_type](::fixy::Unit) noexcept {
                // Half of what the phase derivation reads to name an
                // operation an optimizer operation.  The overload never
                // carries the infix, so the base name is the whole question.
                const SchemaInfo info{schema_hash, mutable_op, is_foreach_op_name(name.base.value()),
                                      without_structural_scalars(schema_hash, data_by_type)};
                keep_schema_facts(key, ::fixy::some(info));
                return ::fixy::Option<SchemaInfo>::some(info);
            },
            [&name, &key](crucible::vessel::OutsideNameRefusal refused) noexcept {
                refuse_schema_name(key, name, refused);
                return ::fixy::Option<SchemaInfo>{::fixy::none};
            });
}

// The arguments of a schema whose slots are data by their types: an argument
// of the type Scalar or float, an optional of one, or a list of one (the role
// of a scalar argument, entry_slots.h).  The unboxed kernels read the same
// role from the C++ types of the arguments (role_of_kind of
// record_kernel.h).  The types are of PyTorch, so the read is in an ABI
// region.  O(arguments), once for each schema.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the role reads the argument types of the schema of PyTorch")
[[nodiscard]] static DataArguments data_arguments_of(const c10::FunctionSchema& schema) {
    DataArguments data{};
    const auto& arguments = schema.arguments();
    for (size_t position = 0; position < arguments.size(); ++position) {
        c10::TypePtr type = arguments[position].real_type();
        if (const auto optional = type->cast<c10::OptionalType>()) type = optional->getElementType();
        if (const auto list = type->cast<c10::ListType>()) type = list->getElementType();
        const bool is_data = type->kind() == c10::TypeKind::NumberType || type->kind() == c10::TypeKind::FloatType;
        if (!is_data) continue;
        for (const ArgumentPosition at : argument_position_at(position))
            data = data.with_data(at);
    }
    return data;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The lookup stays a call, and GCC can inline the whole body into
// crucibleFallback, which adds about 1000 instructions there.  A cold helper
// for the slow path with the probe inline gave the fallback 156 instructions
// more than this call.  The slow path is the cold record_schema_miss.  The
// handle and the schema are of PyTorch, so the reads of them are in ABI
// regions.  An operator whose name the door of an outside name refused gives
// no facts (record_schema_miss).
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the lookup takes the operator handle and the schema of PyTorch")
[[nodiscard, gnu::noinline]] static ::fixy::Option<SchemaInfo>
get_schema_info(crucible::Vigil& vigil, const c10::OperatorHandle& op, const c10::FunctionSchema& schema) {
    // The address of the operator name is the cache key.
    const c10::OperatorName& name = op.operator_name();
    const ::fixy::AddressKey key = operator_key_of(name);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    const uint64_t idx = home_probe(key);
    for (uint32_t probe = 0; probe < SCHEMA_CACHE_PROBES; ++probe) {
        SchemaHashSlot const& slot = read_schema_slot(idx + probe);
        const uint64_t state = slot.state.load_acquire();
        // The compare reads the whole key of the slot, not only its index.
        // Two operators whose keys give one index take two slots of the
        // window, and neither one reads the facts of the other.  A retired
        // slot of this key is not ready, so the lookup passes it.
        if (state == SLOT_READY && slot.key == key) [[likely]] {
            if ((slot.flags & FLAG_REFUSED) != 0) [[unlikely]]
                return ::fixy::none;
            return ::fixy::some(
                SchemaInfo{slot.hash, (slot.flags & FLAG_MUTABLE) != 0, (slot.flags & FLAG_FOREACH) != 0,
                           DataArguments::from_bits(static_cast<uint32_t>(slot.flags >> FLAG_DATA_ARGUMENTS_SHIFT))});
        }
        if (state == SLOT_EMPTY) break;
    }

    // Cache miss.  PyTorch gives the two parts of the name in std::string
    // objects.  Authoritative mutability from schema alias annotations.
    // Catches both in-place ops (add_.Tensor: self(a!)) and out= variants
    // (add.out: out(a!)). FunctionSchema::is_mutable() checks if any
    // argument has AliasInfo with isWrite() == true.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: PyTorch gives the operator name in two std::string parts")
    const OperatorNamePart base_name = ::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(
        ::fixy::mint_foreign_text(name.name.data(), name.name.size()));
    const OperatorNamePart overload = ::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(
        ::fixy::mint_foreign_text(name.overload_name.data(), name.overload_name.size()));
    const bool mutable_op = schema.is_mutable();
    const DataArguments data_by_type = data_arguments_of(schema);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return record_schema_miss(vigil, OperatorNameParts{.base = base_name, .overload = overload}, mutable_op,
                              data_by_type, key);
}

// Retires each ready slot of the key, so no lookup reads its facts again.
// O(SCHEMA_CACHE_PROBES).
//
// A writer claims the first empty slot of the window, and no slot turns
// empty again.  So each slot of the key comes before the first empty slot of
// its window, and the walk stops there.  The walk reads the key of a slot
// only when the slot is ready, so it reads no key that a writer writes.  A
// writer that fills a slot of this key at the same time runs the operator
// while the dispatcher removes its schema, which PyTorch does not permit.
static void retire_schema_slots(::fixy::AddressKey const& key) noexcept {
    const uint64_t idx = home_probe(key);
    for (uint32_t probe = 0; probe < SCHEMA_CACHE_PROBES; ++probe) {
        SchemaHashSlot const& slot = read_schema_slot(idx + probe);
        const uint64_t state = slot.state.load_acquire();
        if (state == SLOT_EMPTY) break;
        if (state == SLOT_READY && slot.key == key) {
            with_schema_slot(idx + probe,
                             [](SchemaHashSlot& held) noexcept { held.state.store_release(SLOT_RETIRED); });
        }
    }
}

// The dispatcher calls onOperatorDeregistered when the last definition of an
// operator goes, before it removes the schema and before it can free the
// entry.  The listener retires the slots of the operator, so a later schema
// at the address of the same operator name gets its facts computed again.
// The dispatcher holds its lock during each call, so the calls of the
// listener run one at a time.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the dispatcher of PyTorch calls a listener through its virtual functions")
class SchemaCacheListener final : public c10::OpRegistrationListener {
public:
    void onOperatorRegistered(const c10::OperatorHandle&) override {}
    void onOperatorDeregistered(const c10::OperatorHandle& op) override {
        retire_schema_slots(operator_key_of(op.operator_name()));
    }
};

// The handle keeps the listener in the dispatcher until this library unloads.
// The dispatcher exists before the first static object of this library, so
// it outlives the handle.
const c10::RegistrationHandleRAII schema_cache_listener =
    c10::Dispatcher::singleton().addRegistrationListener(std::make_unique<SchemaCacheListener>());
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// =====================================================================
// Scalar extraction
//
// Bitcast non-tensor scalars to int64_t for TraceRing::Entry.  An IValue is
// the boxed value of PyTorch, so the read is in an ABI region.
// =====================================================================

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the read takes one boxed value of PyTorch")
[[nodiscard]] static int64_t scalar_to_int64(const c10::IValue& iv) {
    if (iv.isInt()) return iv.toInt();
    if (iv.isBool()) return iv.toBool() ? 1 : 0;
    if (iv.isDouble()) return ::fixy::bit_cast<int64_t>(iv.toDouble());
    return 0;
}

// The tag of a boxed number (ScalarType2 of TraceRing.h): the tag of the
// IValue.  An enumerator boxes into an integer, so it takes INT here, as in
// the unboxed kernels.
[[nodiscard]] static crucible::ScalarType2 scalar_tag(const c10::IValue& iv) {
    if (iv.isBool()) return crucible::ScalarType2::BOOL;
    if (iv.isDouble()) return crucible::ScalarType2::FLOAT;
    return crucible::ScalarType2::INT;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// =====================================================================
// Stack extraction: pull TensorMeta + ScalarArgs from IValue stack
//
// Returns MetaCount (how many input metas were written).
// Output metas are appended AFTER redispatch (need actual results).
// =====================================================================

// The slots of the records of one operation of the fallback, the inputs and
// then the outputs.  The slots are a frame of the record stack of the Vigil,
// or the list of the cold path.  A slot of a frame holds the record of an
// earlier operation until the extraction writes it, and each reader reads
// only the slots that the extraction wrote.
using MetaSlots = ::fixy::View<crucible::TensorMeta, MAX_INLINE_METAS>;

// The slots of the cold path, on the stack of its call.
using InlineMetas = ::foundation::core::WindowStack<crucible::TensorMeta, MAX_INLINE_METAS>;

// The slot at position.  Each caller tests the position against
// MAX_INLINE_METAS first, so the optimizer removes the check.
[[nodiscard]] static crucible::TensorMeta& inline_slot(MetaSlots metas, uint32_t position) noexcept {
    return metas.at(
        ::fixy::Index<MAX_INLINE_METAS>::check(position).expect("a count below MAX_INLINE_METAS names a slot"));
}

// The first count records of the slots.  The extraction keeps each count at
// most MAX_INLINE_METAS.
[[nodiscard]] static ::fixy::View<const crucible::TensorMeta> first_records(MetaSlots metas, uint32_t count) noexcept {
    return ::fixy::View<const crucible::TensorMeta>{metas}.window(0, count).expect(
        "the extraction keeps each count at most MAX_INLINE_METAS");
}

struct ExtractionResult {
    MetaCount counts;
    ScalarArgs<true> scalars;
    // The input tensors past MAX_INLINE_METAS, which the overflow digest of
    // entry_slots.h folds.
    InputOverflow overflow;
    // False when fill_meta refuses a tensor: its rank or an extent is past the
    // bound of a meta, or an enumeration names no crucible enumerator.  The
    // fallback then records nothing.
    bool is_each_tensor_described = true;

    // One defined input tensor: a meta of the slots while they have room, and
    // a fold of the overflow digest after that.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the extraction takes one tensor of PyTorch")
    void add_input(const at::Tensor& tensor, MetaSlots metas) {
        if (counts.inputs < MAX_INLINE_METAS) {
            if (!fill_meta(inline_slot(metas, counts.inputs), tensor)) is_each_tensor_described = false;
            counts.inputs++;
            return;
        }
        crucible::TensorMeta meta{};
        if (!fill_meta(meta, tensor)) is_each_tensor_described = false;
        overflow.fold(meta);
    }
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
};

// The two extractions below read the boxed stack of PyTorch, so they are in
// an ABI region.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the extraction reads the boxed stack of an operator of PyTorch")
[[nodiscard]] static ExtractionResult extract_inputs(const torch::jit::Stack& stack, size_t args_begin, size_t num_args,
                                                     DataArguments data_arguments, MetaSlots metas) {
    ExtractionResult r{};

    for (size_t i = 0; i < num_args; i++) {
        const auto& iv = stack[args_begin + i];
        // The role of each slot of the argument comes from its type in the
        // schema, and not from the tag of its boxed value: an int and a
        // Scalar that holds an integer box into the same tag.
        const crucible::ScalarRole role = data_arguments.role(i);

        if (iv.isTensor()) {
            const auto& t = iv.toTensor();
            if (t.defined()) r.add_input(t, metas);
        } else if (iv.isTensorList()) {
            // Same proxy hazard as the OptionalTensorList branch below: the
            // c10::List iterator dereferences to a proxy whose conversion
            // returns by value, so `const at::Tensor&` binds to a temporary
            // and -Wdangling-reference fires. Take the element by value.
            for (const auto ref : iv.toTensorList()) {
                const at::Tensor t = ref;
                if (t.defined()) r.add_input(t, metas);
            }
        } else if (iv.isOptionalTensorList()) {
            // c10::List iterator dereferences to a proxy object (like
            // vector<bool>). Binding `const auto&` to it triggers
            // -Wrange-loop-bind-reference. Use value to avoid the warning.
            // A None element takes one slot with the tag NONE, as on the
            // unboxed path.
            for (const auto ref : iv.toOptionalTensorList()) {
                const auto opt = static_cast<std::optional<at::Tensor>>(ref);
                if (!opt.has_value()) {
                    r.scalars.push_absent();
                } else if (opt->defined()) {
                    r.add_input(*opt, metas);
                }
            }
        } else if (iv.isNone()) {
            // An absent optional, whatever its payload.
            r.scalars.push_absent();
        } else if (iv.isInt() || iv.isDouble() || iv.isBool()) {
            r.scalars.push(scalar_to_int64(iv), scalar_tag(iv), role);
        } else if (iv.isIntList()) {
            // IntList args (e.g., size=[3,4] in reshape, padding=[1,2] in conv).
            for (int64_t elem : iv.toIntList()) {
                r.scalars.push(elem, crucible::ScalarType2::INT, role);
            }
        } else if (iv.isDoubleList()) {
            for (double elem : iv.toDoubleList()) {
                r.scalars.push(::fixy::bit_cast<int64_t>(elem), crucible::ScalarType2::FLOAT, role);
            }
        } else if (iv.isList() && iv.toList().elementType()->kind() == c10::NumberType::Kind) {
            // A Scalar[] argument boxes into a list of numbers.  Each element
            // contributes as a Scalar argument does: a complex one gives
            // nothing.
            for (const auto ref : iv.toList()) {
                const c10::IValue element = ref;
                if (element.isInt() || element.isDouble() || element.isBool())
                    r.scalars.push(scalar_to_int64(element), scalar_tag(element), role);
            }
        }
    }

    return r;
}

static void extract_outputs(const torch::jit::Stack& stack, size_t num_returns, MetaSlots metas,
                            ExtractionResult& extracted) {
    if (num_returns == 0 || stack.size() < num_returns) return;
    const auto rets_begin = stack.size() - num_returns;
    MetaCount& counts = extracted.counts;

    for (size_t i = 0; i < num_returns; i++) {
        const auto& iv = stack[rets_begin + i];

        if (iv.isTensor()) {
            const auto& t = iv.toTensor();
            if (t.defined() && counts.total() < MAX_INLINE_METAS) {
                if (!fill_meta(inline_slot(metas, counts.total()), t)) extracted.is_each_tensor_described = false;
                counts.outputs++;
            }
        } else if (iv.isTensorList()) {
            // Proxy-by-value, as on the input side.
            for (const auto ref : iv.toTensorList()) {
                const at::Tensor t = ref;
                if (t.defined() && counts.total() < MAX_INLINE_METAS) {
                    if (!fill_meta(inline_slot(metas, counts.total()), t)) extracted.is_each_tensor_described = false;
                    counts.outputs++;
                }
            }
        } else if (iv.isDouble() || iv.isComplexDouble() || iv.isBool() || iv.isInt()) {
            // A scalar result, such as the value of _local_scalar_dense,
            // gets a meta of rank zero, as on the unboxed path.
            if (counts.total() < MAX_INLINE_METAS) {
                const crucible::ScalarType dtype = iv.isDouble()        ? crucible::ScalarType::Double
                                                 : iv.isComplexDouble() ? crucible::ScalarType::ComplexDouble
                                                 : iv.isBool()          ? crucible::ScalarType::Bool
                                                                        : crucible::ScalarType::Long;
                inline_slot(metas, counts.total()) = scalar_result_meta(dtype);
                counts.outputs++;
            }
        }
    }
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// =====================================================================
// The fallback
//
// The redispatch goes to the keys after the recorder, kAfterCrucibleKeyset
// of record_kernel.h, as for the unboxed kernels.  The three functions below
// take the operator handle, the boxed stack and the TLS state of PyTorch, so
// they are in an ABI region.
// =====================================================================

// Records one operation in the slots, runs it, and gives it to the Vigil.
// The slots hold the inputs from the extraction until the dispatch to the
// Vigil, and an operation that the redispatch runs can record other
// operations in between.  So each caller gives slots that no such operation
// writes: a frame of the record stack of the Vigil, or a list on the stack of
// the cold path.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the boxed fallback takes the operator handle and the stack of PyTorch")
static void record_boxed_op(const c10::OperatorHandle& op, c10::DispatchKeySet dispatch_keys, torch::jit::Stack& stack,
                            c10::CrucibleState& state, crucible::Vigil& vigil, crucible::VigilFgCtx const& fg,
                            crucible::OpDepth depth, MetaSlots metas) {
    const auto& schema = op.schema();
    // Runs the operation at the keys after the recorder, and records nothing.
    const auto run_unrecorded = [&op, dispatch_keys, &stack] {
        op.redispatchBoxed(dispatch_keys & kAfterCrucibleKeyset, &stack);
    };

    // -- Extract input tensor metadata + scalars ----------------------
    const auto num_args = schema.arguments().size();
    // Guard: schema.arguments().size() > stack.size() should never happen
    // with a well-formed dispatcher, but protect against underflow (UB).
    if (num_args > stack.size()) [[unlikely]] {
        run_unrecorded();
        return;
    }
    const auto args_begin = stack.size() - num_args;

    // -- Compute hashes, mutability and the data arguments -------------
    // An operator with a refused name runs eagerly, and the fallback records
    // nothing, as for a tensor that fill_meta refuses below.
    ::fixy::Option<SchemaInfo> facts = get_schema_info(vigil, op, schema);
    if (facts.is_none()) [[unlikely]] {
        run_unrecorded();
        return;
    }
    const auto [schema_hash, is_mutable, is_foreach, data_arguments] =
        static_cast<::fixy::Option<SchemaInfo>&&>(facts).expect("the test above");

    ExtractionResult extracted = extract_inputs(stack, args_begin, num_args, data_arguments, metas);
    MetaCount& counts = extracted.counts;

    // -- Execute eagerly (Tier 1: always redispatch) ------------------
    op.redispatchBoxed(dispatch_keys & kAfterCrucibleKeyset, &stack);

    // -- Extract output tensor metadata -------------------------------
    extract_outputs(stack, schema.returns().size(), metas, extracted);

    // A tensor that fill_meta refuses has no meta, so the operation stays out
    // of the ring.  It already ran eagerly above.
    if (!extracted.is_each_tensor_described) [[unlikely]]
        return;

    // -- Build TraceRing::Entry ---------------------------------------
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = schema_hash;
    entry.num_inputs = counts.inputs;
    entry.num_outputs = counts.outputs;

    // Pack op_flags: 5 bits of per-op context.  The flags go in before the
    // scalar slots, because the slots write the tag of slot 4 into the two
    // high bits of op_flags.
    uint8_t flags = 0;
    if (c10::InferenceMode::is_enabled()) flags |= crucible::op_flag::INFERENCE_MODE;
    if (c10::GradMode::is_enabled()) flags |= crucible::op_flag::GRAD_ENABLED;
    if (is_mutable) flags |= crucible::op_flag::IS_MUTABLE;
    flags |= phase_flag_bits(is_foreach);
    if (dispatch_keys.has(c10::DispatchKey::Python)) flags |= crucible::op_flag::TORCH_FUNCTION;
    entry.op_flags = flags;
    crucible::vessel::write_scalar_slots(entry, extracted.scalars, extracted.overflow);
    // The records of the inputs hold the inputs as they were before the
    // redispatch, and no operation of the redispatch wrote them.
    entry.shape_hash = compute_shape_hash(first_records(metas, counts.inputs), entry, depth);

    // -- Scope hash from TLS (set by Python module hooks) -------------
    const auto scope_hash = crucible::ScopeHash{state.scope_hash()};

    // -- Dispatch to Vigil --------------------------------------------
    //
    // COMPILED path: checks guards (~2ns), ignores metas.
    // RECORDING path: appends to ring + MetaLog (~15ns), uses metas.
    //
    // The trust ladder.  Every field of the Entry was read out of the
    // ATen stack and the live c10 query surface, which is a foreign
    // runtime, so the Entry starts at the first tag and reaches a
    // recording entry point only by passing the same checks the C ABI
    // thunks pass.  The operation already ran eagerly above, so a
    // rejected Entry costs the trace this operation and costs the caller
    // nothing.
    //
    // dispatch_op_pure takes the context of the producer claim, which the
    // caller took before the extraction.
    const auto validated =
        crucible::vessel::validate_ffi_op(crucible::mint_ffi_entry(entry), first_records(metas, counts.total()));
    for (const crucible::ValidatedOp& certified : validated) {
        const crucible::DispatchResult result =
            vigil.dispatch_op_pure(fg, certified, scope_hash, crucible::CallsiteHash{}, depth);
        // The read of the eager word at a region entry, as in
        // append_trace_entry of record_kernel.h.
        if (result.enters_region) [[unlikely]]
            crucible::vessel::observe_eager_numerics(vigil, fg);
    }
}

// Records one operation through a list on the stack of this call.  The
// fallback takes this path only when the record stack of the Vigil has fewer
// free slots than MAX_INLINE_METAS, which a deep nest of boxed operations
// needs.  The records are the records of the frame path, and the list costs a
// fill of each of its slots.  The list is in this function, so the frame of
// crucibleFallback holds no list.
CRUCIBLE_COLD static void record_boxed_op_on_stack(const c10::OperatorHandle& op, c10::DispatchKeySet dispatch_keys,
                                                   torch::jit::Stack& stack, c10::CrucibleState& state,
                                                   crucible::Vigil& vigil, crucible::VigilFgCtx const& fg,
                                                   crucible::OpDepth depth) {
    // The op can raise, so the slots lend their run through a loan, which an
    // unwind ends too, and not through a frame door, whose body cannot raise.
    InlineMetas slots{};
    const ::foundation::core::WindowLoan<crucible::TensorMeta, MAX_INLINE_METAS> loan{
        slots, ::foundation::core::WindowCount{MAX_INLINE_METAS}};
    for (const ::fixy::View<crucible::TensorMeta> window : loan.window()) {
        for (const MetaSlots metas : window.window<MAX_INLINE_METAS>(0)) {
            record_boxed_op(op, dispatch_keys, stack, state, vigil, fg, depth, metas);
        }
    }
}

void crucibleFallback(const c10::OperatorHandle& op, c10::DispatchKeySet dispatch_keys, torch::jit::Stack* stack) {
    auto& state = c10::CrucibleState::get_tls_state();

    // -- Fast path: INACTIVE -- pure passthrough, zero work -----------
    if (state.mode() == c10::CrucibleMode::INACTIVE) [[likely]] {
        op.redispatchBoxed(dispatch_keys & kAfterCrucibleKeyset, stack);
        return;
    }
    // The set entry refuses a value that names no mode, and the code of
    // PyTorch sets only an enumerator, so each other value is a defect.  The
    // check comes after the fast path, so the passthrough pays nothing for it.
    CRUCIBLE_FATAL_INVARIANT(is_named_mode(std::to_underlying(state.mode())));

    // -- Get Vigil from TLS context -----------------------------------
    //
    // state.context() crosses the same ABI boundary as CrucibleHandle —
    // a `void*` registered by the Vessel-side Python binding into
    // PyTorch's CrucibleState TLS.  Route through `as_vigil_typed` so
    // the TLS-stored Vigil pointer carries `source::ABIBoundary`
    // provenance just like a freshly-handed-in CrucibleHandle does.
    // The plausibility-check inside the typed helper is debug-only and
    // skipped when state.context() is null (caller branches below).
    void* tls_ctx = state.context();
    if (!tls_ctx) [[unlikely]] {
        op.redispatchBoxed(dispatch_keys & kAfterCrucibleKeyset, stack);
        return;
    }
    auto vigil_typed = crucible::vessel::as_vigil_typed(static_cast<CrucibleHandle>(tls_ctx));
    crucible::Vigil& vigil = *vigil_typed.value();

    // -- Foreign thread: execute, do not record -----------------------
    //
    // The ring behind Vigil is single-producer and the recorded op order is
    // the trace's identity: it fixes the region content hash, the memory
    // plan and the replay order.  Two threads appending to it produce a
    // torn ring, and two threads appending to any ring produce an op order
    // that depends on how they interleaved, which is a different trace on
    // every run.
    //
    // The autograd engine of this runtime runs a backward pass on a worker
    // thread of its own, one per device.  GraphTask captures the caller's
    // at::ThreadLocalState and the worker restores it before each node, and
    // c10::impl::LocalDispatchKeySet is a member of that state.  So the
    // Crucible key travels to the worker thread and this handler fires
    // there, for every backward op of a model on an accelerator.
    //
    // Whether c10::CrucibleState travels the same way is a property of the
    // fork this file builds against.  If it does not, the worker reads
    // INACTIVE and returns at the mode branch above, and this branch is
    // never reached.  If it does, this branch is what keeps the worker off
    // the ring.  Either way the outcome below is the one that holds.
    //
    // A foreign thread executes its op eagerly and leaves the ring alone.
    // The trace is then short by the ops of that thread, which is a trace
    // that is incomplete and reproducible.  Recording them would make it
    // complete and irreproducible, and the pipeline behind it — content
    // addressing, the memory plan, bit-exact replay — is built on the
    // reproducibility rather than on the completeness.
    //
    // A recording session closes that window upstream instead of widening
    // this door.  CrucibleNative holds the autograd engine on the thread
    // that calls backward() for every window, the replayed ones included,
    // so the backward pass has one producer and arrives here on that
    // thread.  A replayed dispatch records nothing but it advances the
    // replay cursor, so a window turned away here leaves the cursor
    // standing still until the next op fails its guard.  Refer to
    // the serialisation note in vessel/torch/crucible_native.py.  This
    // branch then carries the sessions that do not arm that guard, and any
    // thread the engine still owns outside a backward pass.
    //
    // Vigil refuses a second producer on its own and ends the process when
    // it sees one.  That guard stays as the last resort for a caller that
    // does not ask first.  This is the asking.
    if (!vigil.is_producer_thread()) [[unlikely]] {
        op.redispatchBoxed(dispatch_keys & kAfterCrucibleKeyset, stack);
        return;
    }

    // The check above confirmed that this thread can take the producer
    // claim, so the mint claims this thread or confirms its claim.  The
    // context gives the record stack of the Vigil and the dispatch to it.
    //
    // The nest holds the depth of the operation until the operation returns
    // or unwinds, as in the unboxed kernels of record_kernel.h.  Each
    // operation that the redispatch runs gets a depth one more.  When the
    // operation raises, or record_boxed_op refuses it, after an operation
    // inside it got to the Vigil, the close of the nest gives the closing
    // entry of the nest (VigilOpNest).
    //
    // The frame holds MAX_INLINE_METAS slots from the extraction of the
    // inputs to the dispatch.  An operation that the redispatch runs opens
    // its frame past these slots, and its frame closes before the redispatch
    // returns.  An unwind of a PyTorch error closes the frame too.
    const crucible::VigilFgCtx fg = vigil.mint_producer_context();
    // The CPU allocator follows the mode of the Vigil at the entry and at the
    // exit of the operation (storage_generation.h).  The scope comes before
    // the nest, so its exit check follows the close of the nest.
    const crucible::vessel::AllocatorFollowScope follow{vigil.context()};
    crucible::OpRecordStack& record_stack = vigil.op_record_stack(fg);
    const crucible::VigilOpNest nest{vigil, fg};
    const crucible::OpRecordFrame frame{record_stack, MAX_INLINE_METAS};
    for (const ::fixy::View<crucible::TensorMeta> slots : frame.slots()) {
        for (const MetaSlots metas : slots.window<MAX_INLINE_METAS>(0)) {
            record_boxed_op(op, dispatch_keys, *stack, state, vigil, fg, nest.depth(), metas);
            return;
        }
    }
    record_boxed_op_on_stack(op, dispatch_keys, *stack, state, vigil, fg, nest.depth());
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// =====================================================================
// Registration
//
// One line: catches every op in every namespace.  Fires on dlopen.  The
// blocks are the registration macro of PyTorch, so they are in an ABI region.
// =====================================================================

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the blocks of PyTorch register the fallback and the passthrough kernels")
TORCH_LIBRARY_IMPL(_, Crucible, m) { m.fallback(torch::CppFunction::makeFromBoxedFunction<&crucibleFallback>()); }

// Profiler ops: passthrough (do not record).
// These are injected by PyTorch's profiling infrastructure and would
// pollute the trace with zero-tensor ops that break iteration detection.
// A fallthrough covers one overload, so each overload of the namespace has
// its own line.  The record_function scope of an optimizer step calls
// _record_function_exit._RecordFunction.  test_profiler_ops.py asks the
// dispatcher for each overload of the namespace, so an overload that PyTorch
// adds later fails that test until it has a line here.
TORCH_LIBRARY_IMPL(profiler, Crucible, m) {
    m.impl("_record_function_enter", torch::CppFunction::makeFallthrough());
    m.impl("_record_function_enter_new", torch::CppFunction::makeFallthrough());
    m.impl("_record_function_exit", torch::CppFunction::makeFallthrough());
    m.impl("_record_function_exit._RecordFunction", torch::CppFunction::makeFallthrough());
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// =====================================================================
// Tracing completeness notes
//
// What IS captured by DispatchKey::Crucible fallback:
//   [x] Forward pass ATen ops (mm, conv2d, relu, etc.)
//   [~] Backward/autograd decomposed ops (mm, addmm, threshold_backward, etc.)
//       The autograd engine decomposes backward into native ATen ops that
//       all go through the dispatcher. DispatchKey::Crucible is above
//       AutogradCPU/AutogradCUDA in the priority table, so this handler
//       fires on them.
//
//       Whether they are RECORDED depends on which thread runs them, and
//       that is a property of the device rather than of the op:
//
//         - A backward pass over CPU tensors runs on the thread that called
//           backward(), because that thread serves the engine's CPU ready
//           queue itself. It is the recording thread, so these ops are
//           recorded.
//         - A backward pass over accelerator tensors runs on the engine's
//           per-device worker thread by default. GraphTask carries the
//           caller's at::ThreadLocalState to it, and LocalDispatchKeySet is
//           part of that state, so the Crucible key arrives and this
//           handler fires — on a thread that is not the recording thread.
//
//       Closing that gap is not a matter of admitting the worker thread to
//       the ring. Two threads have no defined emission order, so a trace
//       that contains both is a different trace on every run. It needs a
//       recording path that gives the backward window its own single
//       producer, and a recording session now arranges one: it holds the
//       engine on the thread that calls backward(), for every window the
//       Vigil sees rather than for the recorded ones alone.
//       Engine::ready_queue sends every node to the graph task's CPU ready
//       queue when multithreading is off, and the calling thread drives
//       that queue, so one thread produces the whole window. Refer to the
//       serialisation note in vessel/torch/crucible_native.py.
//
//       Measured on cuda:0 over 12 iterations of the MiniGPT in
//       examples/record_cuda.py, against a CPU control. Without the guard
//       the recorded region held 285 ops and 51 distinct schemas, and the
//       control recorded 7 backward schemas the accelerator arm did not.
//       With it the region holds 801 ops and 65 schemas and the count of
//       missing backward schemas is 0. A shape with the token table on the
//       host, which runs its backward pass on two threads at once by
//       default, reaches 898 ops and 0 missing backward schemas. Both
//       shapes replay that region from iteration 2 to iteration 11 with no
//       divergence, which is what says the region holds a whole period and
//       nothing else.
//
//       Holding the engine for the recorded windows alone is not enough,
//       and the measurement says so. A replayed dispatch records nothing,
//       but it advances the replay cursor one op at a time. With the guard
//       lifted under replay, the engine-driven window arrived on a worker
//       thread and reached no cursor, so the cursor stood still at index
//       286 of the 801-op region, holding aten::zero_ from that window,
//       while the op that arrived next was
//       aten::_foreach_mul_.Scalar from the optimizer. That is a schema
//       mismatch, the hard divergence tier. Over 12 iterations it cost 3
//       divergences on each accelerator shape and left the Vigil recording
//       rather than compiled. The published region was the whole 801 ops
//       either way. What was short was the stream that replayed it.
//
//       A session that does not arm that guard keeps the old behavior: the
//       recorded trace is forward plus optimizer, with the backward window
//       absent from it. That window is a gap in the trace, not a corruption
//       of it, because the op order that is recorded is still exactly the
//       order one thread produced.
//   [x] Optimizer ops (SGD: add_, mul_, addcdiv_; Adam: mul_, add_, etc.)
//       optimizer.step() wraps ops in torch.no_grad(), but ops still dispatch
//       through Crucible. no_grad() only affects autograd key, not Crucible.
//   [x] Loss function ops (nll_loss, cross_entropy decomposed, mse_loss, etc.)
//   [x] In-place ops (add_, relu_, copy_, etc.) — same dispatch path
//   [x] View ops (reshape, transpose, expand, slice, etc.) — same dispatch
//   [x] Gradient accumulation ops (add_ on .grad tensors)
//
// What IS filtered (passthrough, not recorded):
//   [x] profiler::_record_function_enter_new/exit — bookkeeping noise
//
// Also captured (no special handling required):
//   [x] aten::_foreach_* ops (fused optimizer ops like _foreach_add_,
//       _foreach_mul_) — go through the ATen dispatcher. TensorList args
//       are unpacked by extract_inputs() isTensorList() handler.
//   [x] aten::empty, aten::zeros, aten::ones — allocation ops that produce
//       fresh tensors. Recorded for correct DFG birth tracking.
//   [x] aten::t, aten::reshape, aten::view — view/alias ops. Recorded for
//       ALIAS edge construction and stride change tracking.
//   [x] aten::lift_fresh — compiler-inserted materialization op. Mostly a
//       no-op in eager mode. Recorded (harmless in iteration signature).
//   [x] AccumulateGrad — NOT an ATen op (autograd Node), but the resulting
//       add_/copy_ on .grad tensors IS dispatched and recorded.
//
// What requires attention for future phases:
//   [ ] aten::detach, aten::alias — metadata-only ops that create no
//       computation. Currently recorded (contributes to op count accuracy)
//       but could be filtered for cleaner traces. Keep for now: they carry
//       data_ptr for correct dataflow edge construction.
//   [x] Communication ops (all_reduce, broadcast, etc.) — a collective of a
//       ProcessGroup calls an operator of the c10d namespace through the
//       dispatcher.  ProcessGroup::allreduce of the fork at 8be2400 calls
//       c10d::allreduce_ (torch/csrc/distributed/c10d/ProcessGroup.hpp), and
//       the wildcard fallback above catches it as each other operator.  The
//       record holds the call of the collective and its tensors.  The
//       transfer that the backend (ProcessGroupNCCL, etc.) runs below the
//       dispatcher is not an operator, and no record holds it.
//   [ ] Custom autograd Functions (torch.autograd.Function) — the forward()
//       contains ATen ops that ARE dispatched. The backward() also contains
//       ATen ops that ARE dispatched. But the Function call itself is not
//       an ATen op. The ops inside both forward/backward are fully captured.
//   [ ] torch.compile / Inductor — if the model is compiled with
//       torch.compile, Inductor fuses ops into triton kernels. These fused
//       ops bypass the ATen dispatcher entirely. Crucible and torch.compile
//       are mutually exclusive execution strategies.
// =====================================================================

}  // anonymous namespace

// =====================================================================
// Exported TLS accessors (called from Python via ctypes or pybind11)
//
// libcrucible_dispatch.so links against libc10 (which defines the
// CrucibleState thread_local).  Python calls these after dlopen to
// set the Vigil handle and mode without additional fork modifications.
// =====================================================================

// The C entries of dispatch_api.h, which is the whole C ABI of this library.
// The layout note of the library holds the digest of their types
// (layout_note.cpp).
extern "C" {

// The C entries take and give the raw pointers of ctypes, and they read and
// write the TLS state of PyTorch, so the block is in an ABI region.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the C entries give ctypes the TLS state of PyTorch and the names of the schemas")

// The layout digest of the library, which its layout note holds too.  The
// loader calls it after the load, as the second guard of the note.
CRUCIBLE_API uint64_t crucible_dispatch_layout_digest() { return crucible::vessel::library_layout_digest(); }

// The eager word of the calling thread (EagerNumerics of
// crucible/NumericalRecipe.h): the bits that read_eager_numerics of
// eager_numerics.h gives.  The attach reads it, and gives it to
// crucible_create of the vessel library.
CRUCIBLE_API uint32_t crucible_dispatch_eager_numerics() { return crucible::vessel::read_eager_numerics().bits; }

// ── The generation allocator (storage_generation.h) ─────────────────
//
// A session attaches the allocator after it creates its Vigil, and detaches
// it at its end.  The attach gives 1 when the CPU allocator of PyTorch
// follows the Vigils of the sessions after the call.  The three addresses and
// the generation of a storage are reads for the tests of the chain: an
// address is 0 for no allocator.  The storage is the address of a
// c10::StorageImpl, which Python gives as UntypedStorage._cdata.  The read
// gives the word of crucible::StorageGeneration that fill_meta gives the
// storage at that time: a known generation, or the top bit and the period for
// a storage that the generation allocator did not make.

CRUCIBLE_API uint32_t crucible_dispatch_attach_allocator() {
    return crucible::vessel::attach_generation_allocator() ? 1U : 0U;
}

CRUCIBLE_API void crucible_dispatch_detach_allocator() { crucible::vessel::detach_generation_allocator(); }

CRUCIBLE_API uint64_t crucible_dispatch_cpu_allocator() { return std::bit_cast<uint64_t>(c10::GetCPUAllocator()); }

CRUCIBLE_API uint64_t crucible_dispatch_generation_allocator() {
    return crucible::vessel::generation_allocator_address();
}

CRUCIBLE_API uint64_t crucible_dispatch_inner_allocator() { return crucible::vessel::generation_inner_address(); }

CRUCIBLE_API uint32_t crucible_dispatch_storage_generation(void* storage) {
    if (storage == nullptr) return 0;
    return crucible::vessel::generation_of(static_cast<c10::StorageImpl*>(storage)->data_ptr()).raw();
}

// The enumerator i of c10::CrucibleMode, in the order of the declarations:
// its value and its name, a C text with a terminating zero.  Gives 1 for an
// enumerator, and 0 with no write when i is past the last one.  The loader
// compares the modes with TlsMode of crucible_native.py.
CRUCIBLE_API uint32_t crucible_dispatch_mode_entry(uint32_t i, uint8_t* out_value, const char** out_name) {
    if (i >= kModeCount) return 0;
    const ModeEnumerator& mode = kModeEnumerators.at(::fixy::Index<kModeCount>::check(i).expect("the test above"));
    if (out_value != nullptr) *out_value = mode.value;
    if (out_name != nullptr) *out_name = mode.name;
    return 1;
}

// Sets the mode of the TLS state of the calling thread and gives 1, or gives
// 0 and keeps the mode when the value names no enumerator of
// c10::CrucibleMode.
CRUCIBLE_API uint32_t crucible_dispatch_set_tls_mode(uint8_t mode) {
    if (!is_named_mode(mode)) return 0;
    c10::CrucibleState::get_tls_state().set_mode(static_cast<c10::CrucibleMode>(mode));
    return 1;
}

CRUCIBLE_API void crucible_dispatch_set_tls_context(void* ctx) { c10::CrucibleState::get_tls_state().set_context(ctx); }

CRUCIBLE_API uint8_t crucible_dispatch_get_tls_mode() {
    return static_cast<uint8_t>(c10::CrucibleState::get_tls_state().mode());
}

CRUCIBLE_API void* crucible_dispatch_get_tls_context() { return c10::CrucibleState::get_tls_state().context(); }

CRUCIBLE_API void crucible_dispatch_set_tls_scope(uint64_t scope_hash) {
    c10::CrucibleState::get_tls_state().set_scope_hash(scope_hash);
}

CRUCIBLE_API uint64_t crucible_dispatch_get_tls_scope() { return c10::CrucibleState::get_tls_state().scope_hash(); }

// ── The backward window ─────────────────────────────────────────────
//
// The wrapper CrucibleNative installs over Tensor.backward brackets each
// backward pass with these two calls, and record_kernel.h derives the BACKWARD
// phase from a non-zero depth. Lives in the dispatch lib — no PyTorch patch
// needed.
//
// The counter is owned here rather than in Python so that neither bound can be
// crossed from outside: `enter` saturates at the maximum instead of wrapping to
// zero and ending a window that is still open, and `exit` stops at zero instead
// of wrapping to the maximum and leaving every later operation labelled
// backward. Both bounds are unreachable from a matched bracket. They are here
// for the caller that is not matched, whose trace is then short of a phase bit
// rather than wrong about every one that follows.
//
// Not atomic: the counter is thread-local, one thread writes it, and that same
// thread is the only reader.

CRUCIBLE_API void crucible_dispatch_backward_enter() {
    if (backward_depth != UINT32_MAX) [[likely]]
        backward_depth++;
}

CRUCIBLE_API void crucible_dispatch_backward_exit() {
    if (backward_depth != 0) [[likely]]
        backward_depth--;
}

CRUCIBLE_API uint32_t crucible_dispatch_backward_depth() { return backward_depth; }

// ── Schema table accessors ──────────────────────────────────────────
//
// The names of the operators this process recorded, for a caller that
// lists them.  The two schema tables are one pair for the process
// (crucible/SchemaTable.h), shared with libcrucible_vessel.so.  The entries
// of the global table come first, then the entries of the late table.  A
// hash registered before the seal and again after it is listed two times.
// Call these on the thread that records, or after it stops.

CRUCIBLE_API uint32_t crucible_dispatch_schema_count() {
    return crucible::global_schema_table().count() + crucible::late_schema_table().count();
}

// Get the schema hash and name for the i-th entry.
// Returns 0 if i >= count.  Writes hash and name pointer.
CRUCIBLE_API int crucible_dispatch_schema_entry(uint32_t i, uint64_t* out_hash, const char** out_name) {
    const auto global_entries = crucible::global_schema_table().entries();
    const auto late_entries = crucible::late_schema_table().entries();
    const auto write_entry = [out_hash, out_name](const crucible::SchemaEntry& entry) noexcept {
        if (out_hash) *out_hash = entry.hash.raw();
        // The C caller reads the name as a C text up to its zero.
        if (out_name) *out_name = ::foundation::core::foreign_c_str(crucible::SchemaTable::name_of(entry).value());
        return 1;
    };
    if (i < global_entries.size()) {
        for (const auto& entry : global_entries.get(i))
            return write_entry(*entry);
    }
    for (const auto& entry : late_entries.get(i - global_entries.size()))
        return write_entry(*entry);
    return 0;
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // extern "C"
