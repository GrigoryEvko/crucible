// Crucible Vessel C API — implementation.
//
// Every C-ABI thunk crosses the FFI boundary through the typed helpers
// in vessel_api_typed.h:
//   * `vessel::as_vigil_typed(h)` materializes a
//     `Tagged<Vigil*, source::ABIBoundary>` from the opaque handle.
//   * `vessel::as_meta_typed(metas)` materializes a
//     `Tagged<const CrucibleMeta*, source::ABIBoundary>` from the
//     CrucibleMeta array, and `vessel::decode_ffi_records` makes a
//     TensorMeta from each record through the checks of
//     `vessel::decode_ffi_meta`.
// The Tagged value is unwrapped (`.value()`) just before the actual
// Vigil method invocation, so provenance is type-visible across the
// entire thunk while the runtime cost stays at zero (regime-1 EBO
// collapse on `Tagged<T, Tag>`).
//
// Review-enforced rule: no raw `static_cast<Vigil*>(h)` is permitted in
// this file, and no code reads a CrucibleMeta record as a TensorMeta —
// grep for `as_vigil_typed` / `as_meta_typed` to find every ABI-
// crossing site in O(1).

#include "entry_slots.h"
#include "layout_note.h"
#include "vessel_api.h"
#include "vessel_api_typed.h"

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

// The handle check of vessel_api_typed.h reads the alignment of the Vigil
// from a constant, because that header only declares the class.  This file
// makes each Vigil, so the constant is checked here.
static_assert(alignof(crucible::Vigil) == crucible::vessel::detail::kVigilHandleAlignment,
              "vessel_api_typed.h: kVigilHandleAlignment must equal alignof(crucible::Vigil)");

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

}  // namespace

// The C answer for an op that the thunk refused before the Vigil saw it.
// Each byte is zero, as the action REFUSED is.
[[nodiscard]] static constexpr CrucibleDispatchResult refused_dispatch_result() noexcept {
    CrucibleDispatchResult refused{};
    refused.action = CRUCIBLE_ACTION_REFUSED;
    return refused;
}

// The C caller owns the Vigil of a handle from crucible_create to
// crucible_destroy, and it gives the texts and the arrays that the hashes read
// as raw pointers.  The functions up to crucible_hash_string take these
// pointers in an opt-out region of the class ABI.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")

// ── FNV-1a 64-bit ────────────────────────────────────────────────────

static constexpr uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;

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

CrucibleHandle crucible_create(uint32_t eager_numerics) noexcept {
    const crucible::EagerNumerics eager{.bits = eager_numerics};
    if (!crucible::is_named_eager_word(eager)) return nullptr;
    crucible::Vigil::Config config{};
    config.eager = eager;
    crucible::Vigil& vigil = *new crucible::Vigil(std::move(config));
    // The thread of the session claims the Vigil before the session writes a
    // structure of the process.  A claim on a second thread, while a Vigil of
    // another thread lives, ends the process here.
    static_cast<void>(vigil.mint_producer_context());
    return &vigil;
}

void crucible_destroy(CrucibleHandle h) noexcept {
    auto handle = crucible::vessel::as_vigil_typed(h);
    delete handle.value();
}

void crucible_release_claim(CrucibleHandle h) noexcept {
    crucible::vessel::as_vigil_typed(h).value()->release_producer_claim();
}

uint64_t crucible_hash_string(const char* s) noexcept {
    if (!s) return 0;
    uint64_t h = FNV_OFFSET;
    while (*s) {
        h ^= static_cast<uint8_t>(*s++);
        h *= 0x100000001b3ULL;
    }
    return h;
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

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

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
CrucibleDispatchResult crucible_dispatch_op(CrucibleHandle handle, uint64_t schema_hash, uint16_t num_inputs,
                                            uint16_t num_outputs, const CrucibleMeta* metas,
                                            uint32_t n_metas) noexcept {
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    auto vigil_typed = crucible::vessel::as_vigil_typed(handle);

    // C→C++ boundary: wrap raw uint64_t into strong hash types.  The guard
    // word comes from the decoded records below.
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema_hash};
    entry.num_inputs = num_inputs;
    entry.num_outputs = num_outputs;

    // The pointer and the count of the records come from the caller.  A
    // count above 0 with a null pointer, or with a pointer that does not have
    // the alignment of a CrucibleMeta, gives the action REFUSED in each build
    // (as_meta_typed).  A count of 0 reads no record.
    ::fixy::Option<crucible::vessel::TypedMeta> checked_metas = crucible::vessel::as_meta_typed(metas, n_metas);
    if (checked_metas.is_none()) return refused_dispatch_result();
    const crucible::vessel::TypedMeta metas_typed = std::move(checked_metas).expect("the test above found the records");

    // The trust ladder.  Every field above came from a caller this
    // process does not control, so the Entry starts at the first tag and
    // reaches the second only by passing the checks.  An op that a check
    // refuses gives the action REFUSED, so the caller knows that the Vigil
    // holds no record of it.
    //
    // The thunk first takes the context of the Vigil's producer claim.  The
    // mint claims the calling thread, or ends the process when another
    // thread holds the claim, which is the same gate that dispatch_op has.
    // This entry asks no is_producer_thread question, so that gate is its
    // only guard.  It runs before the decode, because the decode writes the
    // record slots that the producer thread owns.
    //
    // The op borrows its records, so the thunk opens a frame of n_metas slots
    // on the record stack of the Vigil and decodes the records of the caller
    // into it.  A count that does not fit the free slots, or a record that the
    // decode refuses, records nothing.
    //
    // The op runs no other op through this entry, so it opens no nest.  Its
    // depth is the count of the nests that are open when it arrives.
    const auto vigil = ::fixy::to(*vigil_typed.value());
    const crucible::VigilFgCtx fg = vigil->mint_producer_context();
    crucible::OpRecordStack& record_stack = vigil->op_record_stack(fg);
    const crucible::OpDepth depth = record_stack.depth();
    const crucible::OpRecordFrame frame{record_stack, n_metas};
    for (const ::fixy::View<crucible::TensorMeta> slots : frame.slots()) {
        for (const ::fixy::View<const crucible::TensorMeta> records :
             crucible::vessel::decode_ffi_records(metas_typed, slots)) {
            entry.shape_hash = guard_word_of(entry, records, depth);
            for (const crucible::ValidatedOp& op :
                 crucible::vessel::validate_ffi_op(crucible::mint_ffi_entry(entry), records)) {
                return encode_dispatch_result(
                    vigil->dispatch_op_pure(fg, op, crucible::ScopeHash{}, crucible::CallsiteHash{}, depth));
            }
        }
    }
    return refused_dispatch_result();
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
CrucibleDispatchResult crucible_dispatch_op_ex(CrucibleHandle handle, uint64_t schema_hash, uint16_t num_inputs,
                                               uint16_t num_outputs, const CrucibleMeta* metas, uint32_t n_metas,
                                               const int64_t* scalar_values, uint16_t num_scalars,
                                               uint16_t scalar_types, uint8_t data_slots, uint8_t grad_enabled,
                                               uint8_t inference_mode) noexcept {
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    auto vigil_typed = crucible::vessel::as_vigil_typed(handle);

    // The two arrays of the caller, tested as in crucible_dispatch_op.  A
    // scalar count above 0 with a null pointer, or with a pointer that does
    // not have the alignment of an int64_t, gives the action REFUSED.  A
    // count of 0 reads no value, as crucible_mode.py gives a buffer of five
    // with each count.
    ::fixy::Option<crucible::vessel::TypedArray<const int64_t>> checked_scalars =
        crucible::vessel::as_array_typed(scalar_values, num_scalars);
    if (checked_scalars.is_none()) return refused_dispatch_result();
    const crucible::vessel::TypedArray<const int64_t> scalars_typed =
        std::move(checked_scalars).expect("the test above found the scalar values");
    ::fixy::Option<crucible::vessel::TypedMeta> checked_metas = crucible::vessel::as_meta_typed(metas, n_metas);
    if (checked_metas.is_none()) return refused_dispatch_result();
    const crucible::vessel::TypedMeta metas_typed = std::move(checked_metas).expect("the test above found the records");

    // C→C++ boundary: wrap raw uint64_t into strong hash types.  The guard
    // word comes from the decoded records and the scalar slots below.
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{schema_hash};
    entry.num_inputs = num_inputs;
    entry.num_outputs = num_outputs;
    // A claim past the width of the field saturates, so the ladder below
    // refuses each claim past the inline slots.  The ladder refuses a data
    // slot past the claim too, and a bit past the inline slots.
    entry.num_scalar_args = static_cast<uint8_t>(::fixy::min(num_scalars, uint16_t{UINT8_MAX}));
    bool is_slot_word = false;
    for (const crucible::DataSlots data : crucible::DataSlots::from_bits(data_slots)) {
        entry.set_data_slots(data);
        is_slot_word = true;
    }
    if (!is_slot_word) return refused_dispatch_result();
    uint8_t flags = 0;
    if (inference_mode != 0) flags |= crucible::op_flag::INFERENCE_MODE;
    if (grad_enabled != 0) flags |= crucible::op_flag::GRAD_ENABLED;
    entry.op_flags = flags;

    // The count above is what the caller claims; the inline array is the
    // room there is for it. Writing past the array would be the first
    // corruption, so the copy takes the smaller of the two and the
    // ladder below rejects the operation outright when the claim does
    // not fit.
    constexpr uint16_t inline_scalars = decltype(entry.scalar_values)::size();
    // The door gave a pointer that is not null for each count above 0.
    const uint16_t stored_scalars = num_scalars < inline_scalars ? num_scalars : inline_scalars;
    for (uint16_t i = 0; i < stored_scalars; i++) {
        const int64_t value = scalars_typed.value()[i];
        entry.scalar_values.with_element(
            ::fixy::Index<inline_scalars>::check(i).expect("the copy takes at most the room"),
            [value](int64_t& held) noexcept { held = value; });
    }
    // The tags of the stored values, after the flags, because the tag of
    // slot 4 is in the two high bits of op_flags.  The bits of a slot past
    // the stored values are zero, as on the recording paths.
    entry.set_packed_scalar_types(
        static_cast<uint16_t>(scalar_types & ((uint32_t{1} << (crucible::kScalarTypeBits * stored_scalars)) - 1)));

    // The trust ladder of crucible_dispatch_op.  The producer claim gates
    // this entry and its decode as it gates crucible_dispatch_op, and an op
    // that a check refuses gives the action REFUSED.  The depth comes from
    // the record stack, as in crucible_dispatch_op.
    const auto vigil = ::fixy::to(*vigil_typed.value());
    const crucible::VigilFgCtx fg = vigil->mint_producer_context();
    crucible::OpRecordStack& record_stack = vigil->op_record_stack(fg);
    const crucible::OpDepth depth = record_stack.depth();
    const crucible::OpRecordFrame frame{record_stack, n_metas};
    for (const ::fixy::View<crucible::TensorMeta> slots : frame.slots()) {
        for (const ::fixy::View<const crucible::TensorMeta> records :
             crucible::vessel::decode_ffi_records(metas_typed, slots)) {
            entry.shape_hash = guard_word_of(entry, records, depth);
            for (const crucible::ValidatedOp& op :
                 crucible::vessel::validate_ffi_op(crucible::mint_ffi_entry(entry), records)) {
                return encode_dispatch_result(
                    vigil->dispatch_op_pure(fg, op, crucible::ScopeHash{}, crucible::CallsiteHash{}, depth));
            }
        }
    }
    return refused_dispatch_result();
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
int crucible_flush(CrucibleHandle h) noexcept {
    const ::fixy::InitLoadCtx waiter{::foundation::effects::host::InitOwner::mint_init_context()};
    return crucible::vessel::as_vigil_typed(h).value()->flush(waiter).is_ok() ? 1 : 0;
}

int crucible_is_compiled(CrucibleHandle h) noexcept {
    return crucible::vessel::as_vigil_typed(h).value()->is_compiled() ? 1 : 0;
}

uint64_t crucible_compiled_iterations(CrucibleHandle h) noexcept {
    return crucible::vessel::as_vigil_typed(h).value()->compiled_iterations();
}

uint64_t crucible_diverged_count(CrucibleHandle h) noexcept {
    return crucible::vessel::as_vigil_typed(h).value()->diverged_count();
}

// The context of the producer claim gates the change of the word, as it gates
// a dispatch.  A word that crucible_create refuses changes nothing.
int crucible_observe_eager_numerics(CrucibleHandle h, uint32_t eager_numerics) noexcept {
    const crucible::EagerNumerics eager{.bits = eager_numerics};
    if (!crucible::is_named_eager_word(eager)) return 0;
    const auto vigil = ::fixy::to(*crucible::vessel::as_vigil_typed(h).value());
    vigil->observe_eager_numerics(vigil->mint_producer_context(), eager);
    return 1;
}

uint64_t crucible_active_content_hash(CrucibleHandle h) noexcept {
    for (const ::fixy::Ref<const crucible::RegionNode> region :
         crucible::vessel::as_vigil_typed(h).value()->active_region()) {
        return region->content_hash.raw();
    }
    return 0;
}

uint32_t crucible_bg_iterations(CrucibleHandle h) noexcept {
    return crucible::vessel::as_vigil_typed(h).value()->bg_iterations_completed();
}

uint32_t crucible_ring_size(CrucibleHandle h) noexcept {
    // .peek(): unwrap the Stale<uint32_t> racy snapshot to the C-ABI scalar.
    return crucible::vessel::as_vigil_typed(h).value()->ring_size().peek();
}

uint32_t crucible_metalog_size(CrucibleHandle h) noexcept {
    // .peek(): unwrap the Stale<uint32_t> racy snapshot to the C-ABI scalar.
    return crucible::vessel::as_vigil_typed(h).value()->meta_log_size().peek();
}

// Each call proves that it runs on the thread that holds the producer claim.
// A call from any other thread ends the process, as a dispatch from one does.
// The C caller gets the pointer of the data address, or null for no
// address.  The C caller knows the device of its own tensor.
void* crucible_output_ptr(CrucibleHandle h, uint16_t j) noexcept {
    const auto vigil = ::fixy::to(*crucible::vessel::as_vigil_typed(h).value());
    const ::fixy::Option<::fixy::RunAddress> output = vigil->output_ptr(vigil->mint_producer_context(), j);
    return output.match(
        [](::fixy::RunAddress address) noexcept { return ::foundation::core::foreign_pointer(address); },
        [] noexcept -> void* { return nullptr; });
}

void* crucible_input_ptr(CrucibleHandle h, uint16_t j) noexcept {
    const auto vigil = ::fixy::to(*crucible::vessel::as_vigil_typed(h).value());
    const ::fixy::Option<::fixy::RunAddress> input = vigil->input_ptr(vigil->mint_producer_context(), j);
    return input.match([](::fixy::RunAddress address) noexcept { return ::foundation::core::foreign_pointer(address); },
                       [] noexcept -> void* { return nullptr; });
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
int crucible_register_schema_name(CrucibleHandle h, uint64_t schema_hash, const char* name) noexcept {
    // The C caller gives a C text.  A null name is refused here.  strnlen
    // reads at most kMaxSchemaNameBytes + 1 bytes, so a text with no zero
    // from a malformed caller cannot lead the walk into foreign memory.  The
    // typed door refuses the zero hash and a length of zero or past the
    // bound.
    if (name == nullptr) return CRUCIBLE_SCHEMA_NAME_REFUSED;
    const size_t len = ::strnlen(name, crucible::vessel::kMaxSchemaNameBytes + 1);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    // register_schema_name_typed checks the hash and the length, and its
    // checks are the sanitizer of the boundary.  A sealed global table sends
    // the name to the late table, so no seal drops it.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the C caller gives the name as a pointer, and strnlen gave its length")
    const ::fixy::TextView name_text = ::fixy::mint_foreign_text(name, len);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    // The tables take names only from the thread that holds the producer
    // claim of this Vigil.  The Vigil mints that context for the write, and
    // the mint ends the process on any other thread.  A different name of a
    // held hash comes back as a collision, and the table keeps its first name.
    // A new hash in a full table comes back as full, and the table keeps each
    // name that it holds.
    return crucible::vessel::register_schema_name_typed(*crucible::vessel::as_vigil_typed(h).value(), schema_hash,
                                                        name_text);
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
const char* crucible_schema_name(uint64_t schema_hash) noexcept {
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    // crucible::schema_name reads through both tables.  A caller cannot tell
    // which one accepted the registration, and should not have to.  An
    // unknown hash gives NULL.
    for (const crucible::SchemaTable::LookupName& name : crucible::schema_name(crucible::SchemaHash{schema_hash})) {
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: ctypes reads the name as a C text up to its zero")
        return ::foundation::core::foreign_c_str(name.value());
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    }
    return nullptr;
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
int crucible_export_crtrace(CrucibleHandle h, const char* path) noexcept {
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
    const ::fixy::TextView path_text = ::fixy::mint_foreign_text(path, path_len);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    // The path checks of the base refuse a ".." part, as each other open of
    // a path that a caller gives.
    using Sanitized = ::fixy::Path<::fixy::tags::source::Sanitized>;
    const ::fixy::Option<Sanitized> sanitized =
        ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(path_text))
            .match([](Sanitized checked) noexcept { return ::fixy::Option<Sanitized>::some(std::move(checked)); },
                   [](::fixy::PathTraversalError) noexcept { return ::fixy::Option<Sanitized>{::fixy::none}; });
    if (sanitized.is_none()) return 0;

    // The export runs on a thread of the caller, outside the hot path.  The
    // flush parks, and the open and the write wait on the disk.  The door of
    // the init context gives the Block and the Nondet of the wait, the
    // allocation of the buffer and the right to open and to write the file.
    // A background that stopped before the flush leaves the region unsure,
    // so the export writes nothing.
    const ::fixy::InitLoadCtx io{::foundation::effects::host::InitOwner::mint_init_context()};
    const auto vigil = ::fixy::to(*crucible::vessel::as_vigil_typed(h).value());
    if (vigil->flush(io).is_err()) return 0;

    ::fixy::Option<::fixy::Ref<const crucible::RegionNode>> active = vigil->active_region();
    if (active.is_none()) return 0;
    const ::fixy::Ref<const crucible::RegionNode> region =
        static_cast<::fixy::Option<::fixy::Ref<const crucible::RegionNode>>&&>(active).expect(
            "the test above found the region");
    if (region->num_ops() == 0) return 0;
    const uint32_t num_ops = region->num_ops();

    // Count total tensor metas across all ops.
    uint32_t total_metas = 0;
    for (const crucible::TraceEntry& te : region->ops) {
        total_metas += static_cast<uint32_t>(te.num_inputs() + te.num_outputs());
    }

    // Schema name table: the union of the two tables described above.
    // A hash registered before the seal and again after it appears in
    // both, so the late table's copy is skipped -- the reader keys by
    // hash and a second record for one hash is a malformed trace.
    const auto& table = crucible::global_schema_table();
    const auto& late = crucible::late_schema_table();
    const auto is_in_global = [&table](crucible::SchemaHash hash) noexcept { return table.lookup(hash).is_some(); };

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
        for (const crucible::TraceEntry& te : region->ops) {
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
                rec.scalar_values.with_element(
                    ::fixy::Index<5>::check(stored).expect("the test above keeps the lane below 5"),
                    [value](int64_t& held) noexcept { held = value; });
                ++stored;
            }
            cursor.put(crucible::encode_trace_op(rec));
        }

        // Meta records: TRACE_META_BYTES each (TensorMeta layout).
        for (const crucible::TraceEntry& te : region->ops) {
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
    // it onto the target and syncs the directory (THE DURABLE REPLACE of
    // fixy/os/Fs.h).  So a crash or a full disk leaves the old trace whole, and
    // each error of a step gives 0.
    const ::fixy::View<std::byte const> file_image = image.view();
    const bool is_written = sanitized.match(
        [&io, file_image](const Sanitized& checked) noexcept {
            return ::fixy::fs::replace_file(io, checked, file_image).is_ok();
        },
        [] noexcept { return false; });
    return is_written ? 1 : 0;
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
uint32_t crucible_active_num_ops(CrucibleHandle h) noexcept {
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    auto vigil_typed = crucible::vessel::as_vigil_typed(h);
    for (const ::fixy::Ref<const crucible::RegionNode> region : vigil_typed.value()->active_region()) {
        return region->num_ops();
    }
    return 0;
}

// The plan is in the memory of the active region, which lives while the
// Vigil keeps it active.  O(n) in the slots that the call writes.  A
// capacity above 0 with a null out, or with an out that does not have the
// alignment of a CrucibleSlot, gives 0, and the call writes nothing
// (as_array_typed).
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
uint32_t crucible_active_region_slots(CrucibleHandle h, CrucibleSlot* out, uint32_t capacity) noexcept {
    ::fixy::Option<crucible::vessel::TypedArray<CrucibleSlot>> checked_out =
        crucible::vessel::as_array_typed(out, capacity);
    if (checked_out.is_none()) return 0;
    CrucibleSlot* const first = std::move(checked_out).expect("the test above found the out array").value();
    auto vigil_typed = crucible::vessel::as_vigil_typed(h);
    for (const ::fixy::Ref<const crucible::RegionNode> region : vigil_typed.value()->active_region()) {
        for (const ::fixy::Ref<crucible::MemoryPlan const> plan : region->plan) {
            uint32_t written = 0;
            for (const crucible::TensorSlot& slot : plan->slots) {
                if (written == capacity) break;
                first[written] = encode_slot(slot);
                ++written;
            }
            return plan->num_slots();
        }
    }
    return 0;
}

// A capacity above 0 with a null out, or with an out that does not have the
// alignment of a CrucibleBoundScalar, gives 0, and the call writes nothing.
uint32_t crucible_bound_scalars(CrucibleHandle h, CrucibleBoundScalar* out, uint32_t capacity) noexcept {
    ::fixy::Option<crucible::vessel::TypedArray<CrucibleBoundScalar>> checked_out =
        crucible::vessel::as_array_typed(out, capacity);
    if (checked_out.is_none()) return 0;
    CrucibleBoundScalar* const first = std::move(checked_out).expect("the test above found the out array").value();
    const auto vigil = ::fixy::to(*crucible::vessel::as_vigil_typed(h).value());
    const crucible::VigilFgCtx fg = vigil->mint_producer_context();
    const ::fixy::View<const crucible::Vigil::BoundScalar> bound = vigil->bound_scalars(fg);
    uint32_t written = 0;
    for (const crucible::Vigil::BoundScalar& source : bound) {
        if (written == capacity) break;
        first[written] = CrucibleBoundScalar{.op_index = source.op.raw(),
                                             .slot = static_cast<uint32_t>(source.slot.value().raw()),
                                             .value = source.value};
        ++written;
    }
    return ::fixy::narrow<uint32_t>(bound.size()).expect("a region holds fewer data slots than 2^32");
}

uint64_t crucible_active_pool_bytes(CrucibleHandle h) noexcept {
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    auto vigil_typed = crucible::vessel::as_vigil_typed(h);
    for (const ::fixy::Ref<const crucible::RegionNode> region : vigil_typed.value()->active_region()) {
        for (const ::fixy::Ref<crucible::MemoryPlan const> plan : region->plan) {
            return plan->pool_bytes;
        }
    }
    return 0;
}

}  // extern "C"
