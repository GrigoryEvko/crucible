#pragma once

// The doors of the C entries of the vessel library.  A C caller gives the
// records of an op as CrucibleMeta records, the claims of an op as raw
// integers, each array as a pointer and a count, and a schema name as a C
// text.  Each door gives a checked value or a refusal.  The harness holds each
// answer against an oracle that it computes on its own:
//
//   decode     decode_ffi_meta gives a TensorMeta exactly when the record holds
//              each claim of TensorMeta, and the TensorMeta holds the fields of
//              the record
//   validate   validate_ffi_op gives an op exactly when the entry holds each
//              bound of the trust ladder and the count of the records is the
//              sum of the inputs and the outputs
//   array      as_array_typed gives a pointer exactly for a count of 0, or for a
//              pointer that is not null and that has the alignment of its
//              element.  No code reads through a pointer of the input
//   name       register_schema_name_typed refuses exactly a hash of 0, an empty
//              name, a name past SCHEMA_NAME_MAX and a name with a zero, a
//              refusal leaves the tables as they were, and each other code
//              agrees with the lookup of the tables and with the reserved
//              hashes of the generated table
//
// The input is a list of commands.  Its first byte picks the state of the
// schema tables at the start of the input: both empty, and the global table
// open or sealed.  So the result of an input does not depend on the inputs
// before it.  The name command takes its text as the C entry takes it, up to
// the first zero, and the raw name command gives each byte to the door.

#include "../harness.h"
#include "tensor_meta_claims.h"

#include "../../../../vessel/torch/vessel_api_typed.h"

#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/ValidatedOp.h>
#include <crucible/Vigil.h>
#include <fixy/Core.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::fuzz::boundary {

namespace vessel_entries {

inline constexpr ::fixy::TextView kName{"vessel_entries"};

// The most commands of one input, and the most records of one op.  An op of
// more records takes the same path of the ladder.
inline constexpr std::size_t kMaxCommands = 64;
inline constexpr std::size_t kMaxRecordsOfAnOp = 8;

// The longest name of a command, past the bound of the door, so the fuzzer
// reaches the refusal of a long name.
inline constexpr std::size_t kMaxNameBytes = SCHEMA_NAME_MAX + 64;

enum class Command : std::uint8_t {
    Decode,
    Validate,
    Array,
    Name,
    RawName,
};
inline constexpr std::uint8_t kCommandCount = 5;

// The form of the text of a name command: as the C entry reads a C text, up
// to the first zero, or each byte of the input.
enum class NameForm : std::uint8_t {
    CText,
    Raw,
};

// The state of the schema tables at the start of an input.
enum class TableState : std::uint8_t {
    GlobalOpen,
    GlobalSealed,
};

// The Vigil whose producer claim the registration takes.  The harness makes
// it once, on the thread of the harness, before the first input clears the
// tables.  The Vigil holds a value of one owner thread, so it is thread_local,
// and no other thread reaches it.  Its start seals the global table, and the
// clear of each input opens it again.  No thread of the Vigil reads a schema
// table.
[[nodiscard]] inline Vigil& harness_vigil() noexcept {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the harness keeps one Vigil of its thread for each input, as the vessel does")
    thread_local Vigil vigil{};
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return vigil;
}

// The fields of a CrucibleMeta in the order of the C struct, as the writer of
// the seeds puts them.  A CrucibleMeta holds C arrays and a pointer, so the
// reads and the writes of its fields are in an ABI region.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the harness reads and writes the fields of a CrucibleMeta of a C caller")

[[nodiscard]] inline CrucibleMeta take_record(InputCursor& cursor) noexcept {
    CrucibleMeta record{};
    for (std::size_t lane = 0; lane < kMaxTensorNDim; ++lane)
        record.sizes[lane] = cursor.take<std::int64_t>();
    for (std::size_t lane = 0; lane < kMaxTensorNDim; ++lane)
        record.strides[lane] = cursor.take<std::int64_t>();
    record.data_ptr = std::bit_cast<void*>(cursor.take<std::uintptr_t>());
    record.ndim = cursor.take<std::uint8_t>();
    record.dtype = cursor.take<std::int8_t>();
    record.device_type = cursor.take<std::int8_t>();
    record.device_idx = cursor.take<std::int8_t>();
    record.layout = cursor.take<std::int8_t>();
    record.requires_grad = cursor.take<std::uint8_t>();
    record.flags = cursor.take<std::uint8_t>();
    record.output_nr = cursor.take<std::uint8_t>();
    record.storage_offset = cursor.take<std::int64_t>();
    record.version = cursor.take<std::uint32_t>();
    record.pad0 = cursor.take<std::uint32_t>();
    record.storage_nbytes = cursor.take<std::uint64_t>();
    record.grad_fn_hash = cursor.take<std::uint64_t>();
    return record;
}

inline void append_record(SeedWriter& seed, CrucibleMeta const& record) noexcept {
    for (std::size_t lane = 0; lane < kMaxTensorNDim; ++lane)
        seed.append_raw(record.sizes[lane]);
    for (std::size_t lane = 0; lane < kMaxTensorNDim; ++lane)
        seed.append_raw(record.strides[lane]);
    seed.append_raw(std::bit_cast<std::uintptr_t>(record.data_ptr));
    seed.append_raw(record.ndim);
    seed.append_raw(record.dtype);
    seed.append_raw(record.device_type);
    seed.append_raw(record.device_idx);
    seed.append_raw(record.layout);
    seed.append_raw(record.requires_grad);
    seed.append_raw(record.flags);
    seed.append_raw(record.output_nr);
    seed.append_raw(record.storage_offset);
    seed.append_raw(record.version);
    seed.append_raw(record.pad0);
    seed.append_raw(record.storage_nbytes);
    seed.append_raw(record.grad_fn_hash);
}

// The oracle of the decode: true when the record holds each claim of a
// TensorMeta (decode_tensor_meta of crucible/TensorMeta.h states them).
[[nodiscard]] inline bool holds_each_claim(CrucibleMeta const& record) noexcept {
    if (record.ndim > kMaxTensorNDim) return false;
    for (std::size_t lane = 0; lane < kMaxTensorNDim; ++lane) {
        const std::int64_t size = record.sizes[lane];
        const std::int64_t stride = record.strides[lane];
        const bool is_held = lane < record.ndim ? size <= kMaxTensorDimExtent && stride <= kMaxTensorDimExtent
                                                : size == 0 && stride == 0;
        if (!is_held) return false;
    }
    const bool is_each_enumerator = ::fixy::enum_from<ScalarType>(record.dtype).is_some()
                                 && ::fixy::enum_from<DeviceType>(record.device_type).is_some()
                                 && ::fixy::enum_from<Layout>(record.layout).is_some();
    constexpr std::uint8_t kNamedFlags = meta_flags::IS_LEAF | meta_flags::IS_CONTIGUOUS | meta_flags::HAS_GRAD_FN
                                       | meta_flags::IS_VIEW | meta_flags::IS_NEG | meta_flags::IS_CONJ;
    return is_each_enumerator && record.device_idx >= -1 && record.requires_grad <= 1
        && (record.flags & ~kNamedFlags) == 0;
}

// The claims of a TensorMeta that the decode gave for the record: the claims
// of its type, and each field of the record.
inline void claim_decoded(CrucibleMeta const& record, TensorMeta const& meta) noexcept {
    claim_meta_in_range(as_harness(kName), meta);
    CRUCIBLE_FUZZ_CLAIM(kName, meta.ndim == record.ndim);
    for (std::size_t lane = 0; lane < kMaxTensorNDim; ++lane) {
        CRUCIBLE_FUZZ_CLAIM(kName, raw_tensor_dim(meta.sizes[lane]) == record.sizes[lane]);
        CRUCIBLE_FUZZ_CLAIM(kName, raw_tensor_dim(meta.strides[lane]) == record.strides[lane]);
    }
    CRUCIBLE_FUZZ_CLAIM(kName, meta.data_ptr.value() == record.data_ptr);
    CRUCIBLE_FUZZ_CLAIM(kName, static_cast<std::int8_t>(meta.dtype) == record.dtype);
    CRUCIBLE_FUZZ_CLAIM(kName, static_cast<std::int8_t>(meta.device_type) == record.device_type);
    CRUCIBLE_FUZZ_CLAIM(kName, meta.device_idx == normalize_device_index(meta.device_type, record.device_idx));
    CRUCIBLE_FUZZ_CLAIM(kName, static_cast<std::int8_t>(meta.layout) == record.layout);
    CRUCIBLE_FUZZ_CLAIM(kName, meta.requires_grad == (record.requires_grad == 1));
    CRUCIBLE_FUZZ_CLAIM(kName, meta.flags == record.flags);
    CRUCIBLE_FUZZ_CLAIM(kName, meta.output_nr == record.output_nr);
    CRUCIBLE_FUZZ_CLAIM(kName, meta.storage_offset == record.storage_offset);
    CRUCIBLE_FUZZ_CLAIM(kName, meta.version == record.version);
    CRUCIBLE_FUZZ_CLAIM(kName, meta.storage_nbytes == record.storage_nbytes);
    CRUCIBLE_FUZZ_CLAIM(kName, raw_grad_fn_hash(meta) == record.grad_fn_hash);
    CRUCIBLE_FUZZ_CLAIM(kName, meta.storage_generation == StorageGeneration{});
}

// The array door for one element type, over the bits of a pointer of the
// input.  The pointer is a value only, and no code reads through it.
template <class Element>
inline void check_array_of(std::uint64_t bits, std::uint32_t count) noexcept {
    Element* const first = std::bit_cast<Element*>(bits);
    const ::fixy::Option<vessel::TypedArray<Element>> typed = vessel::as_array_typed(first, count);
    const bool is_expected = count == 0 || (bits != 0 && bits % alignof(Element) == 0);
    CRUCIBLE_FUZZ_CLAIM(kName, typed.is_some() == is_expected);
    for (const vessel::TypedArray<Element> pointer : typed) {
        CRUCIBLE_FUZZ_CLAIM(kName, count == 0 ? pointer.value() == nullptr : pointer.value() == first);
    }
}

inline void check_array(InputCursor& cursor) noexcept {
    const std::uint64_t bits = cursor.take<std::uint64_t>();
    const std::uint32_t count = cursor.take<std::uint32_t>();
    switch (cursor.take<std::uint8_t>() % 4U) {
        case 0:
            check_array_of<const CrucibleMeta>(bits, count);
            break;
        case 1:
            check_array_of<const std::int64_t>(bits, count);
            break;
        case 2:
            check_array_of<CrucibleSlot>(bits, count);
            break;
        default:
            check_array_of<CrucibleBoundScalar>(bits, count);
            break;
    }
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

inline void check_decode(InputCursor& cursor) noexcept {
    const CrucibleMeta record = take_record(cursor);
    const ::fixy::Option<TensorMeta> decoded = vessel::decode_ffi_meta(record);
    CRUCIBLE_FUZZ_CLAIM(kName, decoded.is_some() == holds_each_claim(record));
    for (const TensorMeta& meta : decoded)
        claim_decoded(record, meta);
}

// One op of the C entries: the claims of its entry and its records.  The
// thunk refuses a word of data slots past the inline slots and a record that
// the decode refuses before the ladder, so the command stops there too.
inline void check_validate(InputCursor& cursor) noexcept {
    TraceRing::Entry entry{};
    entry.schema_hash = SchemaHash{cursor.take<std::uint64_t>()};
    entry.num_inputs = cursor.take<std::uint16_t>();
    entry.num_outputs = cursor.take<std::uint16_t>();
    entry.num_scalar_args = cursor.take<std::uint8_t>();
    const std::uint8_t slot_bits = cursor.take<std::uint8_t>();
    bool is_slot_word = false;
    for (const DataSlots data : DataSlots::from_bits(slot_bits)) {
        entry.set_data_slots(data);
        is_slot_word = true;
    }
    const std::size_t count = cursor.take<std::uint8_t>() % (kMaxRecordsOfAnOp + 1);
    ::fixy::FixedList<TensorMeta, kMaxRecordsOfAnOp> records{};
    for (std::size_t position = 0; position < count; ++position) {
        ::fixy::Option<TensorMeta> decoded = vessel::decode_ffi_meta(take_record(cursor));
        if (decoded.is_none()) return;
        if (records.append(std::move(decoded).expect("the test above found the meta")).is_err()) return;
    }
    if (!is_slot_word) return;

    const ::fixy::Option<ValidatedOp> op = vessel::validate_ffi_op(mint_ffi_entry(entry), records.view());
    const bool is_entry_held = entry.schema_hash.raw() != 0 && entry.num_inputs <= kMaxOpInputs
                            && entry.num_outputs <= kMaxOpOutputs && entry.num_scalar_args <= 5
                            && (slot_bits >> entry.num_scalar_args) == 0;
    const bool is_count_held = std::size_t{entry.num_inputs} + std::size_t{entry.num_outputs} == count;
    CRUCIBLE_FUZZ_CLAIM(kName, op.is_some() == (is_entry_held && is_count_held));
    for (const ValidatedOp& certified : op) {
        CRUCIBLE_FUZZ_CLAIM(kName, certified.metas().size() == count);
        CRUCIBLE_FUZZ_CLAIM(kName, certified.entry().schema_hash == entry.schema_hash);
    }
}

// The count of the names of the two schema tables.
[[nodiscard]] inline std::size_t names_held() noexcept {
    return std::size_t{global_schema_table().count()} + std::size_t{late_schema_table().count()};
}

inline void check_name(InputCursor& cursor, NameForm form) noexcept {
    const std::uint64_t hash = cursor.take<std::uint64_t>();
    const std::size_t length = cursor.take<std::uint16_t>() % (kMaxNameBytes + 1);
    ::fixy::FixedText<kMaxNameBytes> text{};
    for (const std::byte byte : cursor.take_bytes(length)) {
        const char character = static_cast<char>(byte);
        // The C entry reads its name with strnlen, up to the first zero.
        if (form == NameForm::CText && character == '\0') break;
        text.append(character).expect("the text holds kMaxNameBytes characters");
    }
    const ::fixy::TextView name = text.view();
    const std::size_t held_before = names_held();
    const int code = vessel::register_schema_name_typed(harness_vigil(), hash,
                                                        ::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(name));

    const bool is_refused =
        hash == 0 || name.size() == 0 || name.size() > SCHEMA_NAME_MAX || ::fixy::find(name, '\0').is_some();
    CRUCIBLE_FUZZ_CLAIM(kName, (code == CRUCIBLE_SCHEMA_NAME_REFUSED) == is_refused);
    const ::fixy::Option<InternedSchemaName> held = schema_name(SchemaHash{hash});
    const bool is_held_name =
        held.match([name](InternedSchemaName const& found) noexcept { return found.value().view() == name; },
                   [] noexcept { return false; });
    switch (code) {
        case CRUCIBLE_SCHEMA_NAME_REGISTERED:
            CRUCIBLE_FUZZ_CLAIM(kName, is_held_name);
            CRUCIBLE_FUZZ_CLAIM(kName, names_held() <= held_before + 1);
            break;
        case CRUCIBLE_SCHEMA_NAME_COLLISION:
            // A different name of a held hash, or of the hash of an operator
            // of the generated table (the reserved hashes).
            CRUCIBLE_FUZZ_CLAIM(kName,
                                (held.is_some() && !is_held_name)
                                    || vessel::is_reserved_for_other_name(SchemaHash{hash}, SchemaNameParts{name}));
            CRUCIBLE_FUZZ_CLAIM(kName, names_held() == held_before);
            break;
        case CRUCIBLE_SCHEMA_NAME_FULL:
            CRUCIBLE_FUZZ_CLAIM(kName, held.is_none());
            CRUCIBLE_FUZZ_CLAIM(kName, names_held() == held_before);
            break;
        case CRUCIBLE_SCHEMA_NAME_REFUSED:
            CRUCIBLE_FUZZ_CLAIM(kName, names_held() == held_before);
            break;
        default:
            CRUCIBLE_FUZZ_CLAIM(kName, code >= CRUCIBLE_SCHEMA_NAME_REGISTERED && code <= CRUCIBLE_SCHEMA_NAME_FULL);
            break;
    }
}

// Writes one command of each kind into seed.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the harness reads and writes the fields of a CrucibleMeta of a C caller")
inline void append_commands(SeedWriter& seed) noexcept {
    CrucibleMeta held{};
    held.sizes[0] = 3;
    held.sizes[1] = 5;
    held.strides[0] = 5;
    held.strides[1] = 1;
    held.data_ptr = std::bit_cast<void*>(std::uintptr_t{0x2000});
    held.ndim = 2;
    held.dtype = static_cast<std::int8_t>(ScalarType::Float);
    held.device_idx = -1;
    held.flags = meta_flags::IS_CONTIGUOUS;
    held.storage_nbytes = 60;
    CrucibleMeta wide = held;
    wide.ndim = kMaxTensorNDim + 1;

    seed.append_raw(Command::Decode);
    append_record(seed, held);
    seed.append_raw(Command::Decode);
    append_record(seed, wide);

    seed.append_raw(Command::Validate);
    seed.append_raw(std::uint64_t{0xABCD});
    seed.append_raw(std::uint16_t{1});
    seed.append_raw(std::uint16_t{1});
    seed.append_raw(std::uint8_t{1});
    seed.append_raw(std::uint8_t{1});
    seed.append_raw(std::uint8_t{2});
    append_record(seed, held);
    append_record(seed, held);

    seed.append_raw(Command::Array);
    seed.append_raw(std::uint64_t{0x1000});
    seed.append_raw(std::uint32_t{3});
    seed.append_raw(std::uint8_t{0});
    seed.append_raw(Command::Array);
    seed.append_raw(std::uint64_t{0x1001});
    seed.append_raw(std::uint32_t{1});
    seed.append_raw(std::uint8_t{1});
    seed.append_raw(Command::Array);
    seed.append_raw(std::uint64_t{0});
    seed.append_raw(std::uint32_t{0});
    seed.append_raw(std::uint8_t{2});

    const auto append_name = [&seed](Command command, std::uint64_t hash, ::fixy::TextView name) noexcept {
        seed.append_raw(command);
        seed.append_raw(hash);
        seed.append_raw(static_cast<std::uint16_t>(name.size()));
        seed.append_text(name);
    };
    append_name(Command::Name, 0xC1, "aten::first");
    append_name(Command::Name, 0xC1, "aten::first");
    append_name(Command::Name, 0xC1, "aten::second");
    append_name(Command::Name, 0, "aten::zero");
    // A raw name with a zero before its last letter: the C entry would cut
    // it, and the door refuses it.
    seed.append_raw(Command::RawName);
    seed.append_raw(std::uint64_t{0xC2});
    seed.append_raw(std::uint16_t{3});
    seed.append_raw(std::uint8_t{'a'});
    seed.append_raw(std::uint8_t{0});
    seed.append_raw(std::uint8_t{'b'});
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace vessel_entries

// Two seeds: one command of each kind with the global schema table open, and
// the same commands with the global table sealed, so the names go to the late
// table, as for the C entry of a live Vigil.
[[nodiscard]] inline SeedList seeds_vessel_entries() noexcept {
    SeedList seeds = empty_seed_list();
    const ::fixy::FixedArray<vessel_entries::TableState, 2> states{vessel_entries::TableState::GlobalOpen,
                                                                   vessel_entries::TableState::GlobalSealed};
    for (const vessel_entries::TableState state : states) {
        SeedWriter seed{};
        seed.append_raw(state);
        vessel_entries::append_commands(seed);
        add_seed(seeds, seed.finish());
    }
    return seeds;
}

inline void run_vessel_entries(::fixy::View<std::byte const> bytes) noexcept {
    using vessel_entries::Command;
    InputCursor cursor{bytes};
    // The Vigil starts before the clear, so its seal of the global table
    // comes before the state of each input.
    static_cast<void>(vessel_entries::harness_vigil());
    global_schema_table().clear(::foundation::effects::testing::test());
    late_schema_table().clear(::foundation::effects::testing::test());
    if (cursor.take<std::uint8_t>() % 2U == std::to_underlying(vessel_entries::TableState::GlobalSealed))
        global_schema_table().seal(kHarnessCtx);

    for (std::size_t step = 0; step < vessel_entries::kMaxCommands && !cursor.is_empty(); ++step) {
        switch (static_cast<Command>(cursor.take<std::uint8_t>() % vessel_entries::kCommandCount)) {
            case Command::Decode:
                vessel_entries::check_decode(cursor);
                break;
            case Command::Validate:
                vessel_entries::check_validate(cursor);
                break;
            case Command::Array:
                vessel_entries::check_array(cursor);
                break;
            case Command::Name:
                vessel_entries::check_name(cursor, vessel_entries::NameForm::CText);
                break;
            case Command::RawName:
                vessel_entries::check_name(cursor, vessel_entries::NameForm::Raw);
                break;
            default:
                ::fixy::unreachable();
        }
    }
}

}  // namespace crucible::fuzz::boundary
