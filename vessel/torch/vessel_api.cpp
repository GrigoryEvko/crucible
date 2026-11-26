// Crucible Vessel C API — implementation.
//
// Every C-ABI thunk crosses the FFI boundary through the typed doors of
// vigil_handles.h and vessel_api_typed.h:
//   * `vigil_handles.mint_held_vigil(h)` checks the handle against the table
//     of live handles, and gives the proof that the calling thread holds the
//     Vigil, or a refusal that the thunk gives as its status.
//   * `vessel::as_meta_typed(metas)` materializes a
//     `Tagged<const CrucibleMeta*, source::ABIBoundary>` from the
//     CrucibleMeta array, and `vessel::decode_ffi_records` makes a
//     TensorMeta from each record through the checks of
//     `vessel::decode_ffi_meta`.
//
// Review-enforced rule: no code reads a CrucibleMeta record as a TensorMeta,
// and each thunk that takes a handle reaches its Vigil only through the table
// of handles — grep for `mint_held_vigil` / `as_meta_typed` to find every
// ABI-crossing site in O(1).

#include "entry_slots.h"
#include "layout_note.h"
#include "vessel_api.h"
#include "vessel_api_typed.h"
#include "vigil_handles.h"

#include <crucible/SchemaTable.h>
#include <crucible/TraceLoader.h>
#include <crucible/Vigil.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstring>
#include <utility>

// CrucibleDispatchResult is a small POD; size-pin is local to this
// file because the struct is only consumed at this boundary.
static_assert(sizeof(CrucibleDispatchResult) == 8);

// The C enumerators name the same statuses as crucible::ReplayStatus, so the
// encode below copies the status ordinal.
static_assert(CRUCIBLE_STATUS_MATCH == static_cast<int>(crucible::ReplayStatus::MATCH));
static_assert(CRUCIBLE_STATUS_DIVERGED == static_cast<int>(crucible::ReplayStatus::DIVERGED));
static_assert(CRUCIBLE_STATUS_COMPLETE == static_cast<int>(crucible::ReplayStatus::COMPLETE));

// CrucibleSlot has the 40 bytes and CrucibleBoundScalar the 16 bytes that
// vessel_api.h states, so a C caller and ctypes lay them out the same.
static_assert(sizeof(CrucibleSlot) == 40);
static_assert(sizeof(CrucibleBoundScalar) == 16);

// The C form of one slot of a memory plan.
[[nodiscard]] static CrucibleSlot encode_slot(const crucible::TensorSlot& slot) noexcept {
    CrucibleSlot encoded{};
    encoded.offset_bytes = slot.offset_bytes;
    encoded.nbytes = slot.nbytes;
    encoded.birth_op = slot.birth_op.raw();
    encoded.death_op = slot.death_op.raw();
    encoded.slot_id = slot.slot_id.raw();
    encoded.dtype = std::to_underlying(slot.dtype);
    encoded.device_type = std::to_underlying(slot.device_type);
    encoded.device_idx = slot.device_idx;
    encoded.layout = std::to_underlying(slot.layout);
    encoded.is_external = slot.is_external ? 1 : 0;
    return encoded;
}

// The C form of the answer of a dispatch.  The Vigil saw the op, so the
// action is RECORD or COMPILED, never REFUSED.
[[nodiscard]] static CrucibleDispatchResult encode_dispatch_result(const crucible::DispatchResult& result) noexcept {
    CrucibleDispatchResult encoded{};
    if (result.is_compiled()) {
        encoded.action = CRUCIBLE_ACTION_COMPILED;
        encoded.status = static_cast<uint8_t>(result.compiled_status());
        encoded.op_index = result.compiled_op_index().raw();
    } else {
        encoded.action = CRUCIBLE_ACTION_RECORD;
    }
    encoded.enters_region = result.enters_region ? uint8_t{1} : uint8_t{0};
    return encoded;
}

namespace {

// The bytes of the file of an export, written in order from the first byte
// through the write lease of the image.  Each put writes the next bytes.  The
// size pass of the export counts each byte of the file, so each put fits, and
// the last put fills the image.  The mint of the image ends the process when
// a put does not fit or a byte has no write.
class ExportCursor {
public:
    explicit ExportCursor(::fixy::WriteLease<std::byte>& lease) noexcept : lease_{lease} {}

    // The bytes of a record.
    void put(::fixy::View<std::byte const> bytes) noexcept { lease_.put_run(bytes); }

    // The bytes of an encoded record, which the call can take as a temporary.
    template <std::size_t Count>
    void put(::fixy::FixedArray<std::byte, Count> const& record) noexcept {
        put(record.view());
    }

    // The bytes of one integer, little-endian, as the platform holds it.
    template <class Field>
    void put_field(Field value) noexcept {
        const auto field = ::fixy::bit_cast<::fixy::FixedArray<std::byte, sizeof(Field)>>(value);
        put(field.view());
    }

    // The characters of a text, one byte each.
    void put_text(::fixy::TextView text) noexcept {
        for (const char character : text) {
            lease_.put(static_cast<std::byte>(static_cast<unsigned char>(character)));
        }
    }

private:
    ::fixy::WriteLease<std::byte>& lease_;
};

// The reason of a refusal of sanitize_schema_name, for a report.
[[nodiscard]] ::fixy::TextView schema_name_error_text(crucible::SchemaNameError error) noexcept {
    switch (error) {
        case crucible::SchemaNameError::zero_hash:
            return "the hash is zero";
        case crucible::SchemaNameError::empty:
            return "the name is empty";
        case crucible::SchemaNameError::too_long:
            return "the name is longer than SCHEMA_NAME_MAX";
        case crucible::SchemaNameError::holds_zero:
            return "the name holds a zero";
        default:
            ::fixy::unreachable();
    }
}

// True in a test build, which arms the export check of each name of the
// tables as a test oracle.  A build with a sanitizer is a test build, and it
// runs the test oracles of the base (has_test_oracles of
// foundation/Platform.h).  A table takes a name only as an internal name or a
// Sanitized name, and each of the two types proves the checks of the validator
// of the loader.  So a Release build exports with no check.  Only the test
// door of a Sanitized name (UncheckedSchemaNameDoor) plants a name that the
// validator refuses.
constexpr bool kChecksExportedNames = ::foundation::has_test_oracles;

// True when the name of entry passes sanitize_schema_name, the validator that
// the loader runs on each name of a file.  The export of a test build writes
// the name into a file only after this check.  A refused name gets a report
// with its hash and the reason.  Complexity: the length of the name.
[[nodiscard]] bool is_exportable_name(const crucible::SchemaEntry& entry) noexcept {
    const ::fixy::TextView name = crucible::SchemaTable::name_of(entry).value().view();
    return crucible::sanitize_schema_name(entry.hash, ::fixy::mint_tagged<::fixy::tags::source::External>(name))
        .match([](crucible::SanitizedSchemaName) noexcept { return true; },
               [&entry](crucible::SchemaNameError error) noexcept {
                   ::fixy::report(::fixy::Sink::Err,
                                  "crucible_export_crtrace: the schema table holds a name of the schema hash {} that "
                                  "a trace cannot hold ({}), so the export writes nothing\n",
                                  entry.hash.raw(), schema_name_error_text(error));
                   return false;
               });
}

}  // namespace

// The C answer for an op that the thunk refused before the Vigil saw it.
// Each byte is zero, as the action REFUSED and the handle status LIVE are.
[[nodiscard]] static constexpr CrucibleDispatchResult refused_dispatch_result() noexcept {
    CrucibleDispatchResult refused{};
    refused.action = CRUCIBLE_ACTION_REFUSED;
    return refused;
}

// The C answer for an op whose handle the table refused.
[[nodiscard]] static constexpr CrucibleDispatchResult
refused_dispatch_result(crucible::vessel::HandleRefusal refused) noexcept {
    CrucibleDispatchResult result = refused_dispatch_result();
    result.handle_status = static_cast<uint8_t>(crucible::vessel::handle_code_of(refused));
    return result;
}

// The status of an entry that runs body on the Vigil of a handle: the status
// that body gives on the live Vigil that the calling thread holds, or the
// status of the refusal of the table.  body takes the Vigil, and it gives a
// CRUCIBLE_HANDLE_* status.
template <class Body>
[[nodiscard]] static uint32_t on_held_vigil(CrucibleHandle handle, Body&& body) noexcept {
    return crucible::vessel::vigil_handles.mint_held_vigil(crucible::vessel::VigilHandle{handle})
        .match(
            [&body](crucible::vessel::HeldVigil held) noexcept -> uint32_t {
                return body(*crucible::vessel::vigil_handles.vigil_of(held));
            },
            [](crucible::vessel::HandleRefusal refused) noexcept { return crucible::vessel::handle_code_of(refused); });
}

// The C caller owns the Vigil of a handle from crucible_create to
// crucible_destroy, and it gives the texts, the arrays and the out values as
// raw pointers.  The functions up to crucible_hash_string take these pointers
// in an opt-out region of the class ABI.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")

// The status of an entry that writes one value of the Vigil of a handle to
// out: the value that read gives on the live Vigil that the calling thread
// holds.  An out that is null or not aligned for Value gives REFUSED, and no
// entry writes out with another status than LIVE.
template <class Value, class Read>
[[nodiscard]] static uint32_t answer(CrucibleHandle handle, Value* out, Read&& read) noexcept {
    ::fixy::Option<crucible::vessel::TypedArray<Value>> checked = crucible::vessel::as_array_typed(out, 1);
    if (checked.is_none()) return CRUCIBLE_HANDLE_REFUSED;
    Value* const target = std::move(checked).expect("the test above found the out value").value();
    return on_held_vigil(handle, [target, &read](crucible::Vigil& vigil) noexcept -> uint32_t {
        *target = read(vigil);
        return CRUCIBLE_HANDLE_LIVE;
    });
}

// The status of a refusal of an address door of the Vigil.  A Vigil that
// records gives NOT_REPLAYING.  A position past the op, and a call before the
// first match of a region, are arguments that break their rule, so they give
// REFUSED.  Each enumerator has its own case, so -Wswitch-enum refuses a new
// enumerator with no status.  The default arm is for a value that names no
// enumerator, which no door of the replay gives.
[[nodiscard]] static constexpr uint32_t status_of_refusal(crucible::AddressRefusal refusal) noexcept {
    switch (refusal) {
        case crucible::AddressRefusal::NotReplaying:
            return CRUCIBLE_HANDLE_NOT_REPLAYING;
        case crucible::AddressRefusal::NoMatchedOp:
            return CRUCIBLE_HANDLE_REFUSED;
        case crucible::AddressRefusal::NoPosition:
            return CRUCIBLE_HANDLE_REFUSED;
        default:
            ::fixy::unreachable();
    }
}

// The status of an entry that writes one address of the replay of the Vigil
// of a handle to out.  read gives the answer of an address door of the Vigil
// (Vigil::output_ptr).  The entry writes the address, or null for a slot with
// none, and gives LIVE.  A refusal writes nothing and gives its status
// (status_of_refusal).  Only the thread that holds the Vigil changes its mode,
// and that thread runs this entry, so the mode stays the same for the read.
template <class Read>
[[nodiscard]] static uint32_t answer_in_replay(CrucibleHandle handle, void** out, Read&& read) noexcept {
    ::fixy::Option<crucible::vessel::TypedArray<void*>> checked = crucible::vessel::as_array_typed(out, 1);
    if (checked.is_none()) return CRUCIBLE_HANDLE_REFUSED;
    void** const target = std::move(checked).expect("the test above found the out value").value();
    return on_held_vigil(handle, [target, &read](crucible::Vigil& vigil) noexcept -> uint32_t {
        return read(vigil).match(
            [target](::fixy::Option<::fixy::RunAddress> address) noexcept -> uint32_t {
                *target = address.match(
                    [](::fixy::RunAddress found) noexcept { return ::foundation::core::foreign_pointer(found); },
                    [] noexcept -> void* { return nullptr; });
                return CRUCIBLE_HANDLE_LIVE;
            },
            [](crucible::AddressRefusal refusal) noexcept { return status_of_refusal(refusal); });
    });
}

// ── FFI entry validation ─────────────────────────────────────────────
//
// The checks and the retag that carries an Entry from the first trust
// tag to the second live in vessel_api_typed.h, beside the rest of the
// boundary's rules and shared with the two PyTorch adapters.  The two
// thunks below build an Entry, mint the first tag over it, and reach a
// recording entry point only through validate_ffi_op.
//
// ── Schema names ─────────────────────────────────────────────────────
//
// crucible_create() constructs a Vigil, whose constructor calls
// BackgroundThread::start(), whose first statement seals the global
// SchemaTable.  A name arrives when its operator first runs, which is after
// that, so crucible::register_schema_name puts it in the late table
// (crucible/SchemaTable.h).  The two tables are one pair for the process, so
// the names that libcrucible_dispatch.so registers while it records are here
// too.  crucible_export_crtrace() writes the union of the two tables, and
// crucible_schema_name() reads through both.

// ── C API implementation ─────────────────────────────────────────────

extern "C" {

uint64_t crucible_layout_digest(void) noexcept { return crucible::vessel::library_layout_digest(); }

// The thread of the session claims the Vigil before the session writes a
// structure of the process.  Two cheap tests refuse a wrong thread and a full
// table before the start of a Vigil: a thread that holds the producer role of
// another live Vigil, and no free slot.  The admit of the table makes the two
// refusals sure, and its refusal ends the new Vigil.
uint32_t crucible_create(uint32_t eager_numerics, CrucibleHandle* out) noexcept {
    ::fixy::Option<crucible::vessel::TypedArray<CrucibleHandle>> checked = crucible::vessel::as_array_typed(out, 1);
    if (checked.is_none()) return CRUCIBLE_HANDLE_REFUSED;
    CrucibleHandle* const target = std::move(checked).expect("the test above found the out value").value();
    const crucible::EagerNumerics eager{.bits = eager_numerics};
    if (!crucible::is_named_eager_word(eager)) return CRUCIBLE_HANDLE_REFUSED;
    if (!::foundation::effects::host::ProducerClaim<crucible::Vigil>::can_caller_use_brand())
        return CRUCIBLE_HANDLE_WRONG_THREAD;
    if (!crucible::vessel::vigil_handles.has_free_slot()) return CRUCIBLE_HANDLE_FULL;
    crucible::Vigil::Config config{};
    config.eager = eager;
    // The create runs at the start of a session, so it opens the init door
    // one time, and the mint builds the Vigil in the storage of its Box.
    const ::fixy::InitLoadCtx init{::foundation::effects::host::InitOwner::mint_init_context()};
    ::fixy::Box<crucible::Vigil> vigil = ::fixy::mint_box_built<crucible::Vigil>(
        init.cap().alloc, [&init, &config]() noexcept { return crucible::mint_vigil(init, std::move(config)); });
    return crucible::vessel::vigil_handles.admit(std::move(vigil))
        .match(
            [target](crucible::vessel::VigilHandle handle) noexcept -> uint32_t {
                *target = handle.raw();
                return CRUCIBLE_HANDLE_LIVE;
            },
            [](crucible::vessel::HandleRefusal refused) noexcept { return crucible::vessel::handle_code_of(refused); });
}

// The end of the Box that withdraw gives ends the Vigil and joins its
// background thread.
uint32_t crucible_destroy(CrucibleHandle h) noexcept {
    return crucible::vessel::vigil_handles.mint_held_vigil(crucible::vessel::VigilHandle{h})
        .match(
            [](crucible::vessel::HeldVigil held) noexcept {
                return crucible::vessel::vigil_handles.withdraw(std::move(held))
                    .match(
                        [](::fixy::Box<crucible::Vigil> ended) noexcept -> uint32_t {
                            static_cast<void>(ended);
                            return CRUCIBLE_HANDLE_LIVE;
                        },
                        [](crucible::vessel::HandleRefusal refused) noexcept {
                            return crucible::vessel::handle_code_of(refused);
                        });
            },
            [](crucible::vessel::HandleRefusal refused) noexcept { return crucible::vessel::handle_code_of(refused); });
}

uint32_t crucible_release_claim(CrucibleHandle h) noexcept {
    return crucible::vessel::vigil_handles.mint_held_vigil(crucible::vessel::VigilHandle{h})
        .match(
            [](crucible::vessel::HeldVigil held) noexcept {
                return crucible::vessel::vigil_handles.release(std::move(held))
                    .match([](::fixy::Unit) noexcept -> uint32_t { return CRUCIBLE_HANDLE_LIVE; },
                           [](crucible::vessel::HandleRefusal refused) noexcept {
                               return crucible::vessel::handle_code_of(refused);
                           });
            },
            [](crucible::vessel::HandleRefusal refused) noexcept { return crucible::vessel::handle_code_of(refused); });
}

uint64_t crucible_hash_string(const char* s) noexcept {
    if (!s) return 0;
    uint64_t fold = crucible::vessel::kFnvOffset;
    while (*s)
        fold = crucible::vessel::fnv1a_step(fold, static_cast<uint8_t>(*s++));
    return crucible::vessel::schema_hash_of_fold(crucible::SchemaHash{fold}).value().raw();
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // extern "C"

// The guard word of the entry of a C caller, from the records of its inputs
// that the decode gave and from the scalar slots of the entry.  The thunk
// computes the word, so the guard of an op of the C path never trusts a word
// of the caller that its records do not show.  An op whose count of inputs is
// more than its records gives the word of the records that it has, and the
// trust ladder then refuses the op.
[[nodiscard]] static crucible::ShapeHash guard_word_of(crucible::TraceRing::Entry const& entry,
                                                       ::fixy::View<const crucible::TensorMeta> records,
                                                       crucible::OpDepth depth) noexcept {
    const size_t input_count = ::fixy::min(size_t{entry.num_inputs}, records.size());
    return crucible::vessel::compute_shape_hash(
        records.window(0, input_count).expect("the count of the inputs is at most the count of the records"), entry,
        depth);
}

// The answer of an entry of a dispatch on the Vigil of a handle: the answer
// that body gives on the live Vigil that the calling thread holds, or the
// action REFUSED with the status of the refusal of the table.  The check of
// the handle comes first, so a result with the status LIVE names a live
// handle.
template <class Body>
[[nodiscard]] static CrucibleDispatchResult on_dispatch_vigil(CrucibleHandle handle, Body&& body) noexcept {
    return crucible::vessel::vigil_handles.mint_held_vigil(crucible::vessel::VigilHandle{handle})
        .match(
            [&body](crucible::vessel::HeldVigil held) noexcept -> CrucibleDispatchResult {
                return body(*crucible::vessel::vigil_handles.vigil_of(held));
            },
            [](crucible::vessel::HandleRefusal refused) noexcept { return refused_dispatch_result(refused); });
}

// The trust ladder of the two entries of a dispatch.  Every field of the
// entry comes from a caller this process does not control, so the Entry
// starts at the first tag and reaches the second only by passing the checks.
// An op that a check refuses gives the action REFUSED, so the caller knows
// that the Vigil holds no record of it.
//
// The calling thread holds the Vigil, so it holds the producer role, and the
// mint of the context takes its fast path.  The mint runs before the decode,
// because the decode writes the record slots that the producer thread owns.
//
// The op borrows its records, so the thunk opens a frame of n_metas slots on
// the record stack of the Vigil and decodes the records of the caller into
// it.  The decode and the mint take the one writer of the slots by move, so
// no code writes the records after the mint.  A count that does not fit the
// free slots, or a record that the decode refuses, records nothing.
//
// The arm builds the entry in the storage of its seal: fill writes the fields
// of the caller, and the arm writes the guard word from the decoded records.
// The entry is the one named return of the build, so the seal copies no
// entry, and no code writes the entry after the checks.
//
// The op runs no other op through this entry, so it opens no nest.  Its depth
// is the count of the nests that are open when it arrives.
//
// The two entries share the ladder, and each entry gives its own fill.  So the
// frame body of each instance has one caller, and the compiler inlines the
// ladder, the frame door and the body into each entry.  The record of one op
// then makes no call that the inline code of one entry did not make.
template <class Fill>
[[nodiscard]] CRUCIBLE_INLINE static CrucibleDispatchResult
dispatch_records(crucible::Vigil& vigil, Fill const& fill, crucible::vessel::TypedMeta metas_typed,
                 ::foundation::core::WindowCount count) noexcept {
    const crucible::VigilFgCtx fg = vigil.mint_producer_context();
    crucible::OpRecordStack& record_stack = vigil.op_record_stack(fg);
    const crucible::OpDepth depth = record_stack.depth();
    // The arm dispatches to the Vigil, which the store check of a frame door
    // refuses as a capture, so the frame is a foreign frame of the C entry.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the frame of a C entry lends its slots to the decode and the dispatch")
    return record_stack.with_foreign_frame(
        count,
        [&] [[gnu::always_inline]] (auto slots) noexcept {
            return crucible::vessel::decode_ffi_records(metas_typed, std::move(slots))
                .match(
                    [&] [[gnu::always_inline]] (auto records) noexcept {
                        const crucible::TraceRing::SealedEntry sealed{::fixy::build_sealed, [&]() noexcept {
                                                                          crucible::TraceRing::Entry built{};
                                                                          fill(built);
                                                                          built.shape_hash = guard_word_of(
                                                                              built, records.view(), depth);
                                                                          return built;
                                                                      }};
                        return crucible::vessel::validate_ffi_op(crucible::mint_ffi_entry(sealed), std::move(records))
                            .match(
                                [&] [[gnu::always_inline]] (crucible::ValidatedOp op) noexcept {
                                    return encode_dispatch_result(vigil.dispatch_op_pure(
                                        fg, op, crucible::ScopeHash{}, crucible::CallsiteHash{}, depth));
                                },
                                [] noexcept { return refused_dispatch_result(); });
                    },
                    [] noexcept { return refused_dispatch_result(); });
        },
        [] noexcept { return refused_dispatch_result(); });
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

// The scalars of a caller of crucible_dispatch_op_ex, folded as the native
// recording paths fold them (ScalarArgs<true> of entry_slots.h).  So the
// three paths write one entry for one op: the first five scalars stay inline,
// and the scalars past them fold into the overflow digest.  The door of the
// arrays checked each pointer, so each array holds count elements.  A tag
// that names no ScalarType2, or a role that names no ScalarRole, gives no
// value.  O(count).
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the fold reads the three scalar arrays of a C caller")
[[nodiscard]] static ::fixy::Option<crucible::vessel::ScalarArgs<true>>
fold_ffi_scalars(crucible::vessel::TypedArray<const int64_t> values, crucible::vessel::TypedArray<const uint8_t> tags,
                 crucible::vessel::TypedArray<const uint8_t> roles, uint16_t count) noexcept {
    const ::fixy::View<const int64_t> value_view = ::fixy::mint_foreign_view(values.value(), count);
    const ::fixy::View<const uint8_t> tag_view = ::fixy::mint_foreign_view(tags.value(), count);
    const ::fixy::View<const uint8_t> role_view = ::fixy::mint_foreign_view(roles.value(), count);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    crucible::vessel::ScalarArgs<true> scalars{};
    std::size_t position = 0;
    for (const int64_t value : value_view) {
        ::fixy::Option<crucible::ScalarType2> tag =
            ::fixy::enum_from<crucible::ScalarType2>(*tag_view.get(position).expect("the tags hold count elements"));
        ::fixy::Option<crucible::ScalarRole> role =
            ::fixy::enum_from<crucible::ScalarRole>(*role_view.get(position).expect("the roles hold count elements"));
        if (tag.is_none() || role.is_none()) return ::fixy::none;
        scalars.push(value, std::move(tag).expect("the test above found the tag"),
                     std::move(role).expect("the test above found the role"));
        ++position;
    }
    return ::fixy::Option<crucible::vessel::ScalarArgs<true>>::some(scalars);
}

// The overflow digest of the input records of a caller of
// crucible_dispatch_op_ex at the positions from first to end, folded as the
// native recording paths fold the inputs past their records
// (InputOverflow::fold of entry_slots.h).  The door of the records checked the
// pointer, and the caller gives each position below the count of the records.
// No value when the decode refuses a record.  An op with more inputs than the
// kept records is rare, so the fold is cold.  O(end - first).
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the fold reads the records of a C caller past the kept records")
[[nodiscard]] CRUCIBLE_COLD static ::fixy::Option<crucible::vessel::InputOverflow>
fold_ffi_input_overflow(crucible::vessel::TypedMeta metas, uint32_t first, uint32_t end) noexcept {
    crucible::vessel::InputOverflow overflow{};
    for (uint32_t position = first; position < end; ++position) {
        ::fixy::Option<crucible::TensorMeta> decoded =
            crucible::vessel::decode_ffi_meta(crucible::vessel::metas_from_typed(metas)[position]);
        if (decoded.is_none()) return ::fixy::none;
        overflow.fold(std::move(decoded).expect("the test above found the record"));
    }
    return ::fixy::Option<crucible::vessel::InputOverflow>::some(overflow);
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The op flags that a caller of crucible_dispatch_op_ex gives: each bit that
// the native recording paths set.  The tag of slot 4 is the entry's own.
constexpr uint8_t kCallerOpFlags = crucible::op_flag::INFERENCE_MODE | crucible::op_flag::IS_MUTABLE
                                 | crucible::op_flag::PHASE_MASK | crucible::op_flag::TORCH_FUNCTION
                                 | crucible::op_flag::GRAD_ENABLED;

// The records that an entry keeps, as the native recording paths keep them.
constexpr uint16_t kKeptRecords = static_cast<uint16_t>(crucible::vessel::MAX_INLINE_METAS);

extern "C" {

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
CrucibleDispatchResult crucible_dispatch_op(CrucibleHandle handle, uint64_t schema_hash, uint16_t num_inputs,
                                            uint16_t num_outputs, const CrucibleMeta* metas,
                                            uint32_t n_metas) noexcept {
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return on_dispatch_vigil(handle, [&](crucible::Vigil& vigil) noexcept {
        // The pointer and the count of the records come from the caller.  A
        // count above 0 with a null pointer, or with a pointer that does not
        // have the alignment of a CrucibleMeta, gives the action REFUSED in
        // each build (as_meta_typed).  A count of 0 reads no record.
        ::fixy::Option<crucible::vessel::TypedMeta> checked_metas = crucible::vessel::as_meta_typed(metas, n_metas);
        if (checked_metas.is_none()) return refused_dispatch_result();
        // C→C++ boundary: the raw uint64_t becomes a strong hash type.  The
        // ladder writes the guard word from the decoded records.
        return dispatch_records(
            vigil,
            [&](crucible::TraceRing::Entry& entry) noexcept {
                entry.schema_hash = crucible::SchemaHash{schema_hash};
                entry.num_inputs = num_inputs;
                entry.num_outputs = num_outputs;
            },
            std::move(checked_metas).expect("the test above found the records"),
            ::foundation::core::WindowCount{n_metas});
    });
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
CrucibleDispatchResult crucible_dispatch_op_ex(CrucibleHandle handle, uint64_t schema_hash, uint16_t num_inputs,
                                               uint16_t num_outputs, const CrucibleMeta* metas, uint32_t n_metas,
                                               const int64_t* scalar_values, const uint8_t* scalar_tags,
                                               const uint8_t* scalar_roles, uint16_t num_scalars,
                                               uint8_t op_flags) noexcept {
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return on_dispatch_vigil(handle, [&](crucible::Vigil& vigil) noexcept -> CrucibleDispatchResult {
        // The counts and the flags come from the caller, so a count past the
        // bound, counts that do not add up to the records, or a flag that the
        // caller does not give, gives the action REFUSED before a read of an
        // array.
        if (num_scalars > uint16_t{CRUCIBLE_MAX_FFI_SCALARS}) return refused_dispatch_result();
        if (uint32_t{num_inputs} + uint32_t{num_outputs} != n_metas) return refused_dispatch_result();
        if ((op_flags & ~kCallerOpFlags) != 0) return refused_dispatch_result();
        // The four arrays of the caller, tested as in crucible_dispatch_op.  A
        // count above 0 with a null pointer, or with a pointer that does not
        // have the alignment of its element, gives the action REFUSED.  A
        // count of 0 reads no element.
        ::fixy::Option<crucible::vessel::TypedArray<const int64_t>> checked_values =
            crucible::vessel::as_array_typed(scalar_values, num_scalars);
        ::fixy::Option<crucible::vessel::TypedArray<const uint8_t>> checked_tags =
            crucible::vessel::as_array_typed(scalar_tags, num_scalars);
        ::fixy::Option<crucible::vessel::TypedArray<const uint8_t>> checked_roles =
            crucible::vessel::as_array_typed(scalar_roles, num_scalars);
        if (checked_values.is_none() || checked_tags.is_none() || checked_roles.is_none())
            return refused_dispatch_result();
        ::fixy::Option<crucible::vessel::ScalarArgs<true>> folded =
            fold_ffi_scalars(std::move(checked_values).expect("the test above found the values"),
                             std::move(checked_tags).expect("the test above found the tags"),
                             std::move(checked_roles).expect("the test above found the roles"), num_scalars);
        if (folded.is_none()) return refused_dispatch_result();
        const crucible::vessel::ScalarArgs<true> scalars = std::move(folded).expect("the test above found the fold");
        ::fixy::Option<crucible::vessel::TypedMeta> checked_metas = crucible::vessel::as_meta_typed(metas, n_metas);
        if (checked_metas.is_none()) return refused_dispatch_result();
        const crucible::vessel::TypedMeta metas_typed =
            std::move(checked_metas).expect("the test above found the records");

        // The entry keeps the records of the native paths (extract_inputs and
        // extract_outputs of crucible_fallback.cpp): each input while there is
        // room, then each output while there is room.  So the kept records are
        // the first records of the caller.  The inputs past them fold into the
        // overflow digest, and the outputs past them have no record.
        const uint16_t kept_inputs = ::fixy::min(num_inputs, kKeptRecords);
        const uint16_t kept_outputs = ::fixy::min(num_outputs, static_cast<uint16_t>(kKeptRecords - kept_inputs));
        crucible::vessel::InputOverflow overflow{};
        if (num_inputs > kept_inputs) [[unlikely]] {
            ::fixy::Option<crucible::vessel::InputOverflow> folded_inputs =
                fold_ffi_input_overflow(metas_typed, kept_inputs, num_inputs);
            if (folded_inputs.is_none()) return refused_dispatch_result();
            overflow = std::move(folded_inputs).expect("the test above found the fold");
        }

        // The trust ladder of crucible_dispatch_op (dispatch_records).  The
        // fill holds the small values by value.
        return dispatch_records(
            vigil,
            [&, op_flags, kept_inputs, kept_outputs](crucible::TraceRing::Entry& entry) noexcept {
                // C→C++ boundary: the raw uint64_t becomes a strong hash type.
                // The ladder writes the guard word from the decoded records
                // and the scalar slots.
                entry.schema_hash = crucible::SchemaHash{schema_hash};
                entry.num_inputs = kept_inputs;
                entry.num_outputs = kept_outputs;
                // The flags come before the scalar slots, because the tag of
                // slot 4 is in the two high bits of op_flags.
                entry.op_flags = op_flags;
                crucible::vessel::write_scalar_slots(entry, scalars, overflow);
            },
            metas_typed, ::foundation::core::WindowCount{uint32_t{kept_inputs} + uint32_t{kept_outputs}});
    });
}

// The position of the C caller is a number, and a position past the
// arguments of a schema is no structural scalar.
int crucible_is_structural_scalar(uint64_t schema_hash, uint32_t position) noexcept {
    for (const crucible::vessel::ArgumentPosition at : crucible::vessel::argument_position_at(position)) {
        return crucible::vessel::is_structural_scalar(crucible::SchemaHash{schema_hash}, at) ? 1 : 0;
    }
    return 0;
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")

// The flush waits on a thread of the caller, outside the hot path, and it
// parks in the kernel after its spin.  The door of the init context gives the
// Block and the Nondet of the wait.  1 when the background processed each op
// that the ring held, and 0 when the background stopped first.
uint32_t crucible_flush(CrucibleHandle h, uint32_t* out) noexcept {
    return answer(h, out, [](crucible::Vigil& vigil) noexcept -> uint32_t {
        const ::fixy::InitLoadCtx waiter{::foundation::effects::host::InitOwner::mint_init_context()};
        return vigil.flush(waiter).is_ok() ? 1U : 0U;
    });
}

uint32_t crucible_is_compiled(CrucibleHandle h, uint32_t* out) noexcept {
    return answer(h, out, [](crucible::Vigil& vigil) noexcept -> uint32_t { return vigil.is_compiled() ? 1U : 0U; });
}

uint32_t crucible_compiled_iterations(CrucibleHandle h, uint64_t* out) noexcept {
    return answer(h, out, [](crucible::Vigil& vigil) noexcept -> uint64_t { return vigil.compiled_iterations(); });
}

uint32_t crucible_diverged_count(CrucibleHandle h, uint64_t* out) noexcept {
    return answer(h, out, [](crucible::Vigil& vigil) noexcept -> uint64_t { return vigil.diverged_count(); });
}

// The context of the producer claim gates the change of the word, as it gates
// a dispatch.  A word that crucible_create refuses changes nothing.
uint32_t crucible_observe_eager_numerics(CrucibleHandle h, uint32_t eager_numerics) noexcept {
    const crucible::EagerNumerics eager{.bits = eager_numerics};
    return on_held_vigil(h, [eager](crucible::Vigil& vigil) noexcept -> uint32_t {
        if (!crucible::is_named_eager_word(eager)) return CRUCIBLE_HANDLE_REFUSED;
        vigil.observe_eager_numerics(vigil.mint_producer_context(), eager);
        return CRUCIBLE_HANDLE_LIVE;
    });
}

uint32_t crucible_active_content_hash(CrucibleHandle h, uint64_t* out) noexcept {
    return answer(h, out, [](crucible::Vigil& vigil) noexcept -> uint64_t {
        for (const crucible::DagRef<const crucible::RegionNode> region : vigil.active_region()) {
            return region->content_hash.raw();
        }
        return 0;
    });
}

uint32_t crucible_bg_iterations(CrucibleHandle h, uint32_t* out) noexcept {
    return answer(h, out, [](crucible::Vigil& vigil) noexcept -> uint32_t { return vigil.bg_iterations_completed(); });
}

// .peek() unwraps the Stale<uint32_t> racy snapshot to the C-ABI scalar.
uint32_t crucible_ring_size(CrucibleHandle h, uint32_t* out) noexcept {
    return answer(h, out, [](crucible::Vigil& vigil) noexcept -> uint32_t { return vigil.ring_size().peek(); });
}

uint32_t crucible_metalog_size(CrucibleHandle h, uint32_t* out) noexcept {
    return answer(h, out, [](crucible::Vigil& vigil) noexcept -> uint32_t { return vigil.meta_log_size().peek(); });
}

// A call while the Vigil records gives NOT_REPLAYING, and a position past the
// outputs of the op gives REFUSED.  Each reads nothing.  The C caller knows
// the device of its own tensor.
uint32_t crucible_output_ptr(CrucibleHandle h, uint16_t j, void** out) noexcept {
    return answer_in_replay(h, out, [j](crucible::Vigil& vigil) noexcept {
        return vigil.output_ptr(vigil.mint_producer_context(), crucible::OutputPosition{j});
    });
}

uint32_t crucible_input_ptr(CrucibleHandle h, uint16_t j, void** out) noexcept {
    return answer_in_replay(h, out, [j](crucible::Vigil& vigil) noexcept {
        return vigil.input_ptr(vigil.mint_producer_context(), crucible::InputPosition{j});
    });
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
uint32_t crucible_register_schema_name(CrucibleHandle h, uint64_t schema_hash, const char* name,
                                       uint32_t* out) noexcept {
    // The C caller gives a C text.  A null name is refused here.  strnlen
    // reads at most SCHEMA_NAME_MAX + 1 bytes, so a text with no zero from a
    // malformed caller cannot lead the walk into foreign memory.  The typed
    // door refuses the zero hash and a length of zero or past the bound.
    if (name == nullptr)
        return answer(h, out, [](crucible::Vigil&) noexcept -> uint32_t { return CRUCIBLE_SCHEMA_NAME_REFUSED; });
    const size_t len = ::strnlen(name, crucible::SCHEMA_NAME_MAX + 1);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    // register_schema_name_typed gives the name to the door of an outside
    // name (register_outside_schema_name of vessel_api_typed.h), which runs
    // sanitize_schema_name, the sanitizer of the boundary.  A sealed global
    // table sends the name to the late table, so no seal drops it.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the C caller gives the name as a pointer, and strnlen gave its length")
    const auto name_text = ::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(::fixy::mint_foreign_text(name, len));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    // The tables take names only from the thread that holds this Vigil.  A
    // different name of a held hash comes back as a collision, and the table
    // keeps its first name.  A different name of the hash of an operator of the generated
    // table comes back as a collision too, and the tables stay as they were.
    // A new hash in a full table comes back as full, and the table keeps each
    // name that it holds.
    return answer(h, out, [schema_hash, &name_text](crucible::Vigil& vigil) noexcept -> uint32_t {
        return static_cast<uint32_t>(crucible::vessel::register_schema_name_typed(vigil, schema_hash, name_text));
    });
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
const char* crucible_schema_name(uint64_t schema_hash) noexcept {
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    // crucible::schema_name reads through both tables.  A caller cannot tell
    // which one accepted the registration, and should not have to.  An
    // unknown hash gives NULL.
    for (const crucible::InternedSchemaName& name : crucible::schema_name(crucible::SchemaHash{schema_hash})) {
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: ctypes reads the name as a C text up to its zero")
        return ::foundation::core::foreign_c_str(name.value());
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    }
    return nullptr;
}

// The export of crucible_export_crtrace on the Vigil that the calling thread
// holds: 1 when it wrote the file, and 0 when it wrote nothing.  The C entry
// gives the init context of the wait and of the file.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
[[nodiscard]] static uint32_t export_trace_of(crucible::Vigil& held, const char* path,
                                              const ::fixy::InitLoadCtx& io) noexcept {
    // FFI boundary: path validation before the read of the path and the open.
    //   - path != null
    //   - strnlen(path, PATH_MAX+1) ≤ PATH_MAX
    // A missing NUL-terminator from a malformed caller could otherwise
    // walk into foreign memory; PATH_MAX=4096 is Linux's filesystem
    // component-path ceiling.  Cap the walk to bound the failure mode.
    constexpr size_t MAX_PATH_LEN = 4096;
    if (path == nullptr) return 0;
    const size_t path_len = ::strnlen(path, MAX_PATH_LEN + 1);
    if (path_len == 0 || path_len > MAX_PATH_LEN) return 0;
    const auto path_text =
        ::fixy::mint_tagged<::fixy::tags::source::External>(::fixy::mint_foreign_text(path, path_len));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    // The path checks of the base refuse a ".." part, as each other open of
    // a path that a caller gives.
    using Sanitized = ::fixy::Path<::fixy::tags::source::Sanitized>;
    const ::fixy::Option<Sanitized> sanitized = ::fixy::sanitize_path(path_text).match(
        [](Sanitized checked) noexcept { return ::fixy::Option<Sanitized>::some(std::move(checked)); },
        [](::fixy::PathTraversalError) noexcept { return ::fixy::Option<Sanitized>{::fixy::none}; });
    if (sanitized.is_none()) return 0;

    // A background that stopped before the flush leaves the region unsure,
    // so the export writes nothing.
    const auto vigil = ::fixy::to(held);
    if (vigil->flush(io).is_err()) return 0;

    ::fixy::Option<crucible::DagRef<const crucible::RegionNode>> active = vigil->active_region();
    if (active.is_none()) return 0;
    const crucible::DagRef<const crucible::RegionNode> region =
        static_cast<::fixy::Option<crucible::DagRef<const crucible::RegionNode>>&&>(active).expect(
            "the test above found the region");
    if (region->num_ops() == 0) return 0;
    const uint32_t num_ops = region->num_ops();

    // Count total tensor metas across all ops.
    uint32_t total_metas = 0;
    for (const crucible::TraceEntry& te : region->ops.view()) {
        total_metas += static_cast<uint32_t>(te.num_inputs() + te.num_outputs());
    }

    // Schema name table: the union of the two tables described above.
    // A hash registered before the seal and again after it appears in
    // both, so the late table's copy is skipped -- the reader keys by
    // hash and a second record for one hash is a malformed trace.
    const auto& table = crucible::global_schema_table();
    const auto& late = crucible::late_schema_table();
    const auto is_in_global = [&table](crucible::SchemaHash hash) noexcept { return table.lookup(hash).is_some(); };

    // Each name of the tables passed the validator of the loader, or the
    // consteval check of an internal name, on its own hash.  So the export
    // writes no file that the loader refuses.  A test build checks each name
    // again before the first byte (kChecksExportedNames), and a refused name
    // makes the export write nothing.
    if (kChecksExportedNames) {
        for (const crucible::SchemaEntry& entry : table.entries()) {
            if (!is_exportable_name(entry)) return 0;
        }
        for (const crucible::SchemaEntry& entry : late.entries()) {
            if (!is_in_global(entry.hash) && !is_exportable_name(entry)) return 0;
        }
    }

    // The count is written before the records, so it has to be the
    // post-dedup count, not the sum of the two sizes.  The size pass counts
    // each byte of the file, so one buffer holds the file.
    constexpr size_t kNameHead = sizeof(uint64_t) + sizeof(uint16_t);
    uint32_t num_names = table.count();
    size_t name_bytes = 0;
    for (const crucible::SchemaEntry& entry : table.entries())
        name_bytes += kNameHead + crucible::SchemaTable::name_of(entry).value().size();
    for (const crucible::SchemaEntry& entry : late.entries()) {
        if (is_in_global(entry.hash)) continue;
        num_names++;
        name_bytes += kNameHead + crucible::SchemaTable::name_of(entry).value().size();
    }
    const size_t file_bytes = crucible::TRACE_HEADER_BYTES + size_t{num_ops} * sizeof(crucible::TraceOpRecord)
                            + size_t{total_metas} * crucible::TRACE_META_BYTES + sizeof(uint32_t) + name_bytes;

    // The cursor writes each byte of the image one time, in order, through the
    // write lease of a new buffer.  The mint ends the process when the writer
    // leaves a byte with no write, so the image is full when the mint returns.
    const auto write_image = [&](::fixy::WriteLease<std::byte>& lease) noexcept {
        ExportCursor cursor{lease};

        // Header: "CRTR" + version + num_ops + num_metas = 16B.
        cursor.put(crucible::encode_trace_header(num_ops, total_metas));

        // Op records: 88B each (TraceOpRecord layout).
        for (const crucible::TraceEntry& te : region->ops.view()) {
            crucible::TraceOpRecord rec{};
            rec.schema_hash = te.schema_hash;
            rec.shape_hash = te.shape_hash;
            rec.scope_hash = te.scope_hash;
            rec.callsite_hash = te.callsite_hash;
            rec.depth = te.depth;
            rec.scalar_types = te.scalar_kinds.tags();
            rec.data_slots = te.scalar_kinds.data_slots().bits();
            rec.num_inputs = te.num_inputs();
            rec.num_outputs = te.num_outputs();
            rec.num_scalars = te.num_scalar_args();
            rec.grad_enabled = te.grad_enabled ? 1 : 0;
            // Pack all op_flags back into one byte for on-disk format.
            uint8_t flags = 0;
            if (te.inference_mode) flags |= crucible::op_flag::INFERENCE_MODE;
            if (te.is_mutable) flags |= crucible::op_flag::IS_MUTABLE;
            flags |= (static_cast<uint8_t>(te.training_phase) & 0x3) << crucible::op_flag::PHASE_SHIFT;
            if (te.torch_function) flags |= crucible::op_flag::TORCH_FUNCTION;
            rec.inference_mode = flags;
            // The record stores the first five scalars.  An absent array leaves
            // the zeros of the record.
            uint16_t stored = 0;
            for (const int64_t value : te.scalar_args()) {
                if (stored == 5) break;
                rec.scalar_values.set(::fixy::Index<5>::check(stored).expect("the test above keeps the lane below 5"),
                                      value);
                ++stored;
            }
            cursor.put(crucible::encode_trace_op(rec));
        }

        // Meta records: TRACE_META_BYTES each (TensorMeta layout).
        for (const crucible::TraceEntry& te : region->ops.view()) {
            for (const auto& m : te.input_metas())
                cursor.put(crucible::encode_trace_meta(m));
            for (const auto& m : te.output_metas())
                cursor.put(crucible::encode_trace_meta(m));
        }

        cursor.put_field(num_names);
        const auto write_entry = [&cursor](const crucible::SchemaEntry& entry) noexcept {
            const ::fixy::TextView name = crucible::SchemaTable::name_of(entry).value().view();
            cursor.put_field(entry.hash.raw());
            cursor.put_field(static_cast<uint16_t>(name.size()));
            cursor.put_text(name);
        };
        for (const crucible::SchemaEntry& entry : table.entries())
            write_entry(entry);
        for (const crucible::SchemaEntry& entry : late.entries())
            if (!is_in_global(entry.hash)) write_entry(entry);
    };
    const ::fixy::HeapArray<std::byte> image =
        ::fixy::mint_heap_array_written<std::byte>(io.cap().alloc, file_bytes, write_image);

    // The door writes a temporary file beside the target, syncs it, renames
    // it onto the target and syncs the directory (the durable replace of
    // fixy/os/Fs.h).  So a crash or a full disk leaves the old trace whole, and
    // each error of a step gives 0.
    const ::fixy::View<std::byte const> file_image = image.view();
    const bool is_written = sanitized.match(
        [&io, file_image](const Sanitized& checked) noexcept {
            return ::fixy::fs::replace_file(io, checked, file_image).is_ok();
        },
        [] noexcept { return false; });
    return is_written ? 1U : 0U;
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")

// The export runs on a thread of the caller, outside the hot path.  The flush
// parks, and the open and the write wait on the disk.  The door of the init
// context gives the Block and the Nondet of the wait, the allocation of the
// buffer and the right to open and to write the file.
uint32_t crucible_export_crtrace(CrucibleHandle h, const char* path, uint32_t* out) noexcept {
    return answer(h, out, [path](crucible::Vigil& vigil) noexcept {
        const ::fixy::InitLoadCtx io{::foundation::effects::host::InitOwner::mint_init_context()};
        return export_trace_of(vigil, path, io);
    });
}

uint32_t crucible_active_num_ops(CrucibleHandle h, uint32_t* out) noexcept {
    return answer(h, out, [](crucible::Vigil& vigil) noexcept -> uint32_t {
        for (const crucible::DagRef<const crucible::RegionNode> region : vigil.active_region()) {
            return region->num_ops();
        }
        return 0;
    });
}

// The plan is in the memory of the active region, which lives while the
// Vigil keeps it active.  O(n) in the slots that the call writes.  A
// capacity above 0 with a null out, or with an out that does not have the
// alignment of a CrucibleSlot, gives REFUSED, and the call writes nothing
// (as_array_typed).
uint32_t crucible_active_region_slots(CrucibleHandle h, CrucibleSlot* out, uint32_t capacity,
                                      uint32_t* out_count) noexcept {
    ::fixy::Option<crucible::vessel::TypedArray<CrucibleSlot>> checked_out =
        crucible::vessel::as_array_typed(out, capacity);
    if (checked_out.is_none()) return CRUCIBLE_HANDLE_REFUSED;
    CrucibleSlot* const first = std::move(checked_out).expect("the test above found the out array").value();
    return answer(h, out_count, [first, capacity](crucible::Vigil& vigil) noexcept -> uint32_t {
        for (const crucible::DagRef<const crucible::RegionNode> region : vigil.active_region()) {
            for (const crucible::DagRef<crucible::MemoryPlan const> plan : region->plan) {
                uint32_t written = 0;
                for (const crucible::TensorSlot& slot : plan->slots.view()) {
                    if (written == capacity) break;
                    first[written] = encode_slot(slot);
                    ++written;
                }
                return plan->num_slots();
            }
        }
        return 0;
    });
}

// A capacity above 0 with a null out, or with an out that does not have the
// alignment of a CrucibleBoundScalar, gives REFUSED, and the call writes
// nothing.
uint32_t crucible_bound_scalars(CrucibleHandle h, CrucibleBoundScalar* out, uint32_t capacity,
                                uint32_t* out_count) noexcept {
    ::fixy::Option<crucible::vessel::TypedArray<CrucibleBoundScalar>> checked_out =
        crucible::vessel::as_array_typed(out, capacity);
    if (checked_out.is_none()) return CRUCIBLE_HANDLE_REFUSED;
    CrucibleBoundScalar* const first = std::move(checked_out).expect("the test above found the out array").value();
    return answer(h, out_count, [first, capacity](crucible::Vigil& vigil) noexcept -> uint32_t {
        const crucible::VigilFgCtx fg = vigil.mint_producer_context();
        const ::fixy::View<const crucible::Vigil::BoundScalar> bound = vigil.bound_scalars(fg);
        uint32_t written = 0;
        for (const crucible::Vigil::BoundScalar& source : bound) {
            if (written == capacity) break;
            first[written] = CrucibleBoundScalar{.op_index = source.op.raw(),
                                                 .slot = static_cast<uint32_t>(source.slot.value().raw()),
                                                 .value = source.value};
            ++written;
        }
        return ::fixy::narrow<uint32_t>(bound.size()).expect("a region holds fewer data slots than 2^32");
    });
}

uint32_t crucible_active_pool_bytes(CrucibleHandle h, uint64_t* out) noexcept {
    return answer(h, out, [](crucible::Vigil& vigil) noexcept -> uint64_t {
        for (const crucible::DagRef<const crucible::RegionNode> region : vigil.active_region()) {
            for (const crucible::DagRef<crucible::MemoryPlan const> plan : region->plan) {
                return plan->pool_bytes;
            }
        }
        return 0;
    });
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // extern "C"
