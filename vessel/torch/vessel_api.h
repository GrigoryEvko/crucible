// Crucible Vessel C API — ABI boundary for Python/ctypes.
//
// Wraps crucible::Vigil behind checked handles and C-compatible structs.
// TorchDispatchMode (Python) calls these functions to feed real ATen ops
// into Crucible's recording and dispatch pipeline.  A handle is a word of the
// table of handles (vessel/torch/vigil_handles.h), and each entry that takes
// one gives a CRUCIBLE_HANDLE_* status: a stale handle, a forged handle and a
// call on a wrong thread give a status, never an end of the process.
//
// CrucibleMeta has the layout of crucible::TensorMeta (176 bytes).  The C++
// side reads each record through CrucibleMeta and decodes it into a
// TensorMeta.  All enum types use the same int8_t ordinals as
// c10::ScalarType etc.
//
// ── Typed-boundary discipline ────────────────────────────────────────
//
// The C ABI fields below remain raw (`void*`, `const char*`) because
// ctypes consumers cannot interpret C++ wrapper types.  Inside C++,
// every value crossing this boundary is immediately re-typed via the
// helpers in vessel_api_typed.h and vigil_handles.h:
//
//   * CrucibleHandle          → HeldVigil, the proof that the calling thread
//                               holds the live Vigil of the handle, via
//                               `vigil_handles.mint_held_vigil(h)`.
//   * const CrucibleMeta*     → Tagged<const CrucibleMeta*, ABIBoundary>
//                               via `as_meta_typed(metas, n)`, and each
//                               record → TensorMeta via
//                               `decode_ffi_meta(record)`.
//   * each other array        → Tagged<E*, ABIBoundary> via
//                               `as_array_typed(first, n)`.  The door
//                               tests the pointer and the count, and the
//                               entry gives its refusal for a pair that
//                               breaks the rule of the arrays below.
//
// ── The arrays of a caller ───────────────────────────────────────────
//
// Each array is a pointer and a count.  A count of 0 reads and writes no
// element, so the pointer can then have any value.  A count above 0 needs a
// pointer that is not null and that has the alignment of the element type.
// An entry that gets another pair gives its refusal: the action REFUSED, or
// 0 with no write for an entry that writes into an array of the caller.
//   * CrucibleMeta::data_ptr  → Option<TensorDataKey>
//                               via `data_ptr_typed(typed_meta, i)`
//                               — provenance threads into
//                               TensorMeta-consuming code without
//                               changing the wire-struct layout.
//   * schema name lookup      → InternedSchemaName
//                               via `schema_name_typed(schema_hash)`
//                               — distinguishes the
//                               post-validation pointer from raw FFI
//                               input.
//
// ── The layout of the library ────────────────────────────────────────
//
// The layout note of the library holds the digest of the layout of the
// structs and of the function types below, and of the Vigil (the note of
// layout_note.h).  The build writes the digest and the layout of each struct
// below into the layout table beside the library, and the loader of
// crucible_native.py compares the note and its ctypes Structures with that
// table before the load (the layout of a library).  No version is written by
// hand.

#pragma once

// The opt-out region below exists only for the C++ compile, which loads the
// quarantine plugin.  A C compiler reads the declarations with no region.
#ifdef __cplusplus
#include <foundation/Quarantine.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Symbol visibility — must be visible for ctypes/dlsym.
#ifdef _MSC_VER
#define CRUCIBLE_VESSEL_API __declspec(dllexport)
#else
#define CRUCIBLE_VESSEL_API __attribute__((visibility("default")))
#endif

// Exception transparency for the C ABI boundary.  Every extern "C"
// function is noexcept, because a throw across a C ABI is undefined
// behavior.  If a callee of a thunk throws, the noexcept specification
// calls std::terminate before the exception goes across the boundary.
// The build has no -fno-exceptions.  Nothing in the tree throws, and
// the rule artifact-symbols does that check on the crucible and foundation
// artifacts, but not on this library.  The macro is empty
// under a C compiler, because `noexcept` is C++-only syntax.
#ifdef __cplusplus
#define CRUCIBLE_VESSEL_NOEXCEPT noexcept
#else
#define CRUCIBLE_VESSEL_NOEXCEPT
#endif

#ifdef __cplusplus
CRUCIBLE_I_KNOW_WHAT_IM_DOING("C-HEADER: a C caller and ctypes read these declarations, so they hold only C types")
#endif

// The handle of a Vigil: a word that crucible_create gives, and no address.
// The table of the handles checks each handle that an entry takes
// (vessel/torch/vigil_handles.h).  The word 0 never names a Vigil.
typedef uint64_t CrucibleHandle;

// The most live Vigils of one process.  A crucible_create past them gives
// CRUCIBLE_HANDLE_FULL.
enum {
    CRUCIBLE_VIGIL_SLOTS = 64
};

// The status of an entry that takes or gives a handle.  An entry with another
// status than LIVE changes nothing and writes no out value.
enum {
    // The entry ran on the live Vigil of the handle.
    CRUCIBLE_HANDLE_LIVE = 0,
    // No live Vigil has the handle: crucible_destroy ended it, no
    // crucible_create gave it, or it is 0 or forged.
    CRUCIBLE_HANDLE_STALE = 1,
    // Another thread holds the Vigil, or holds the producer role of another
    // live Vigil.  The first entry on a handle that no thread holds claims the
    // handle for its thread, and crucible_release_claim gives it up.
    CRUCIBLE_HANDLE_WRONG_THREAD = 2,
    // An op of the Vigil runs on this thread, so crucible_destroy and
    // crucible_release_claim cannot end it or give it up.
    CRUCIBLE_HANDLE_BUSY = 3,
    // The process holds CRUCIBLE_VIGIL_SLOTS live Vigils, so crucible_create
    // makes no Vigil.
    CRUCIBLE_HANDLE_FULL = 4,
    // An argument other than the handle breaks its rule: an eager word that
    // EagerNumerics does not name, an out pointer that is null or not
    // aligned for its type, or a position of crucible_output_ptr or
    // crucible_input_ptr that the replay cannot answer (a position past the
    // op, or no op matched since the start of the replay of a region).
    CRUCIBLE_HANDLE_REFUSED = 5,
    // The Vigil records, and it does not replay a compiled region, so
    // crucible_output_ptr and crucible_input_ptr have no address to give.
    CRUCIBLE_HANDLE_NOT_REPLAYING = 6,
    // PyTorch raised an error in an entry of the dispatch library, and
    // crucible_dispatch_last_error gives its text.  The entry wrote no value.
    CRUCIBLE_HANDLE_FOREIGN_ERROR = 7
};

// Binary-compatible with crucible::TensorMeta (176 bytes).
// Field order and types must match exactly — verified via static_assert
// in the implementation.
typedef struct {
    int64_t sizes[8];
    int64_t strides[8];
    void* data_ptr;
    uint8_t ndim;
    int8_t dtype;  // crucible::ScalarType ordinal
    int8_t device_type;  // crucible::DeviceType ordinal
    int8_t device_idx;
    int8_t layout;  // crucible::Layout ordinal
    uint8_t requires_grad;  // tensor.requires_grad
    uint8_t flags;  // packed: is_leaf|is_contiguous|has_grad_fn|is_view|is_neg|is_conj
    uint8_t output_nr;  // autograd output number (which output of multi-output op)
    int64_t storage_offset;  // offset into underlying storage
    uint32_t version;  // tensor data version counter
    uint32_t pad0;  // zero
    uint64_t storage_nbytes;  // the size of the storage in bytes
    uint64_t grad_fn_hash;  // FNV-1a hash of grad_fn class name
} CrucibleMeta;

// The action of a CrucibleDispatchResult: what the Vigil did with the op.
// A result with all bytes zero is a refused op, so no result claims a record
// that the Vigil did not take.
enum {
    // The thunk refused the op before the Vigil saw it: a count that the
    // records do not hold, a record that the decode refuses, or more records
    // than the free slots of the record stack.  The Vigil holds no record of
    // the op.  The caller runs the op eagerly.  The status and the op index
    // are zero.
    CRUCIBLE_ACTION_REFUSED = 0,
    // The Vigil recorded the op.  The caller runs the op eagerly.  The status
    // and the op index are zero.
    CRUCIBLE_ACTION_RECORD = 1,
    // The Vigil replays a region and checked the guard of the op.  The status
    // gives the result of the guard, and the op index gives the position of
    // the op in the region.  An op with the status COMPLETE is the last op of
    // the region.
    CRUCIBLE_ACTION_COMPILED = 2
};

// The status of a CrucibleDispatchResult with the action COMPILED.
enum {
    // The op matches the region, and its outputs are ready.
    CRUCIBLE_STATUS_MATCH = 0,
    // A guard failed.  The Vigil records again.
    CRUCIBLE_STATUS_DIVERGED = 1,
    // The op ends the region, which ends the iteration.
    CRUCIBLE_STATUS_COMPLETE = 2
};

// Result from crucible_dispatch_op (8 bytes).
typedef struct {
    uint8_t action;  // CRUCIBLE_ACTION_*
    uint8_t status;  // CRUCIBLE_STATUS_*, zero for an action other than COMPILED
    // 1 when the next op starts an iteration of a region: this op completed a
    // region, or its dispatch activated one.  The caller then reads its eager
    // word again and gives it to crucible_observe_eager_numerics.  Zero for
    // each other op.
    uint8_t enters_region;
    // CRUCIBLE_HANDLE_*: LIVE when the handle names a live Vigil that this
    // thread holds.  Another status comes with the action REFUSED.
    uint8_t handle_status;
    uint32_t op_index;  // the position of the op in the region for COMPILED, zero for another action
} CrucibleDispatchResult;

// ── Layout digest ────────────────────────────────────────────────────

// The layout digest of the library: the digest of the layout of the types and
// of the C entries that the two vessel libraries share, which the layout note
// of the library holds too (the note of layout_note.h).  The loader reads the
// note before the load, and calls this entry after the load as its second
// guard.  A constant return.
CRUCIBLE_VESSEL_API uint64_t crucible_layout_digest(void) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Lifecycle ────────────────────────────────────────────────────────

// Makes a Vigil, starts its background thread, and writes its handle to out.
// eager_numerics: the eager word of the front end at the attach (EagerNumerics
// of crucible/NumericalRecipe.h): bits 0 and 1 the precision of a float32
// matmul (0 highest, 1 high, 2 medium), bit 2 TF32 for a cuBLAS matmul, bit 3
// TF32 for a cuDNN convolution, bit 4 the flush of denormal floats.  The Vigil
// records each region under this word.  A word with another bit, or with the
// precision 3, gives REFUSED.
// The calling thread holds the Vigil: it is the producer thread of each op of
// the Vigil.  Each live Vigil of the process has one producer thread, so a
// create on a second thread, while a Vigil of another thread lives, gives
// WRONG_THREAD.  With CRUCIBLE_VIGIL_SLOTS live Vigils, the call gives FULL.
CRUCIBLE_VESSEL_API uint32_t crucible_create(uint32_t eager_numerics, CrucibleHandle* out) CRUCIBLE_VESSEL_NOEXCEPT;

// Ends the Vigil of the handle, and joins its background thread.  The handle
// is then stale, also for a second call.  BUSY while an op of the Vigil runs
// on this thread.
CRUCIBLE_VESSEL_API uint32_t crucible_destroy(CrucibleHandle h) CRUCIBLE_VESSEL_NOEXCEPT;

// The calling thread gives up its hold of the Vigil and the producer role.
// The next entry or op of the handle, on any thread, holds it again.  BUSY
// while an op of the Vigil runs on this thread.  CrucibleMode calls it at the
// exit of the mode, so a live mode object outside its context holds no
// thread.
CRUCIBLE_VESSEL_API uint32_t crucible_release_claim(CrucibleHandle h) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Hashing ──────────────────────────────────────────────────────────

// The schema hash of the op name of a null-terminated string: the FNV-1a
// 64-bit fold of its bytes, with the fold zero mapped to the fold of the
// empty text (schema_hash_of_fold of entry_slots.h).  So a name never gives
// zero, the sentinel of no schema.  A null pointer gives zero, which each C
// entry refuses.
CRUCIBLE_VESSEL_API uint64_t crucible_hash_string(const char* s) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Dispatch ─────────────────────────────────────────────────────────

// Unified dispatch: records in RECORDING mode, checks guards in COMPILED.
// metas: array of CrucibleMeta, [inputs..., outputs...].
// n_metas: num_inputs + num_outputs.
// The thunk computes the guard word of the op from the records of its inputs
// and from its scalars, as the native recording paths do.  The caller gives no
// guard word.
// An op that the checks of the thunk refuse gives the action REFUSED, such as
// a pair of metas and n_metas that breaks the rule of the arrays above.  A
// handle that names no live Vigil that this thread holds gives the action
// REFUSED and its status in handle_status.
CRUCIBLE_VESSEL_API CrucibleDispatchResult crucible_dispatch_op(CrucibleHandle h, uint64_t schema_hash,
                                                                uint16_t num_inputs, uint16_t num_outputs,
                                                                const CrucibleMeta* metas,
                                                                uint32_t n_metas) CRUCIBLE_VESSEL_NOEXCEPT;

// The most scalars of one op that crucible_dispatch_op_ex reads from its
// caller.  A _foreach_ op over the parameter tensors of a model gives one
// scalar for each tensor, so the bound holds the scalar lists of large
// models.  It also bounds the read of the three scalar arrays of a caller
// that gives a wrong count.
enum {
    CRUCIBLE_MAX_FFI_SCALARS = 4096
};

// Extended dispatch with scalar arguments and op flags.  The entry writes the
// entry that the native recording paths write for the same op.
//
// metas holds n_metas records: num_inputs inputs, then num_outputs outputs.
// The entry keeps the first 32 records (MAX_INLINE_METAS of entry_slots.h):
// each input while there is room, then each output while there is room.  The
// inputs past them fold into the overflow digest, and the outputs past them
// have no record, as on the native paths.  n_metas other than num_inputs plus
// num_outputs, or a record that the decode refuses, gives the action REFUSED.
//
// The op gives each of its scalars, in the order of the schema, as three
// arrays of num_scalars entries:
//   scalar_values: the value (a double as its bits)
//   scalar_tags:   the tag (ScalarType2 of crucible/TraceRing.h): 0 an integer,
//                  1 a double, 2 a bool, 3 an argument that boxes into None
//   scalar_roles:  the role (ScalarRole of crucible/Types.h): 1 a data scalar,
//                  whose argument has the type Scalar or float in the schema
//                  and for which crucible_is_structural_scalar gives 0, and 0
//                  a structural scalar
// The entry folds the scalars as the native recording paths do
// (write_scalar_slots of entry_slots.h): the first five stay inline, and the
// scalars past them fold into the overflow digest.  A count above
// CRUCIBLE_MAX_FFI_SCALARS, a null array with a count above 0, a tag or a role
// that names no value, gives the action REFUSED.  The guard word reads the tag
// of a data scalar but not its value, and the replay binds its value.
//
// op_flags holds the byte of OpFlags of crucible/TraceRing.h with the five
// fields that the native paths set: the inference mode at bit 0, the
// mutability at bit 1, the training phase in bits 2 and 3, the torch function
// mode at bit 4 and the gradient mode at bit 5.  A bit of the tag of slot 4 in
// bits 6 and 7, which the entry writes, gives the action REFUSED.
CRUCIBLE_VESSEL_API CrucibleDispatchResult crucible_dispatch_op_ex(
    CrucibleHandle h, uint64_t schema_hash, uint16_t num_inputs, uint16_t num_outputs, const CrucibleMeta* metas,
    uint32_t n_metas, const int64_t* scalar_values, const uint8_t* scalar_tags, const uint8_t* scalar_roles,
    uint16_t num_scalars, uint8_t op_flags) CRUCIBLE_VESSEL_NOEXCEPT;

// 1 when the argument at position of the operator of schema_hash is a
// structural scalar by the list of the vessel (kStructuralScalars of
// vessel/torch/entry_slots.h), although its type is Scalar or float: its
// value changes the shape of an output or picks the arm of the kernel.  0 for
// each other argument.  A caller of crucible_dispatch_op_ex reads the role of
// each slot through it, as the native recording paths do.
CRUCIBLE_VESSEL_API int crucible_is_structural_scalar(uint64_t schema_hash, uint32_t position) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Control and queries ──────────────────────────────────────────────
//
// Each entry below that takes a handle gives a CRUCIBLE_HANDLE_* status, and
// it writes its value to out only with the status LIVE.  An out pointer that
// is null or not aligned for its type gives REFUSED.

// Waits until the background thread completes the work for each ring entry
// that exists at the call.  That work includes the build and the publication
// of a region.  The wait spins, and then it parks in the kernel.  out: 1 when
// the work is complete, and 0 when the background thread stopped before it
// completed the work.  The call does not wait for a stopped background.
CRUCIBLE_VESSEL_API uint32_t crucible_flush(CrucibleHandle h, uint32_t* out) CRUCIBLE_VESSEL_NOEXCEPT;

// out: 1 while the Vigil replays a region, 0 while it records.  A region that
// the background thread published and the foreground has not aligned to yet
// reads 0.
CRUCIBLE_VESSEL_API uint32_t crucible_is_compiled(CrucibleHandle h, uint32_t* out) CRUCIBLE_VESSEL_NOEXCEPT;

// out: the count of the complete iterations in COMPILED mode.
CRUCIBLE_VESSEL_API uint32_t crucible_compiled_iterations(CrucibleHandle h, uint64_t* out) CRUCIBLE_VESSEL_NOEXCEPT;

// out: the count of the divergences.
CRUCIBLE_VESSEL_API uint32_t crucible_diverged_count(CrucibleHandle h, uint64_t* out) CRUCIBLE_VESSEL_NOEXCEPT;

// Give the eager word of the front end, in the layout of crucible_create.  The
// caller reads the word when a dispatch result has enters_region, and gives it
// here.  A changed word stops the replay of a region of the old word, which
// counts one divergence, and the Vigil records the next regions under the new
// word.  A word that crucible_create refuses gives REFUSED, and it changes
// nothing.
CRUCIBLE_VESSEL_API uint32_t crucible_observe_eager_numerics(CrucibleHandle h,
                                                             uint32_t eager_numerics) CRUCIBLE_VESSEL_NOEXCEPT;

// out: the content hash of the active region, or 0 when the Vigil has no
// region.
CRUCIBLE_VESSEL_API uint32_t crucible_active_content_hash(CrucibleHandle h, uint64_t* out) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Schema name registration ─────────────────────────────────────────

// The result of crucible_register_schema_name.
enum {
    // The table holds the name for the hash: a first registration, or the
    // same name again.
    CRUCIBLE_SCHEMA_NAME_REGISTERED = 0,
    // The call gives no name to register: a hash of zero, a null name, or a
    // name that is empty or longer than 65535 bytes (SCHEMA_NAME_MAX of
    // crucible/SchemaTable.h).  The table is as it was.
    CRUCIBLE_SCHEMA_NAME_REFUSED = 1,
    // The table holds a different name for the hash, or an operator of the
    // generated table of the vessel has the hash and a different name.  The
    // tables keep each name that they hold, and they refuse this one.
    CRUCIBLE_SCHEMA_NAME_COLLISION = 2,
    // The table holds the most names that it can hold, and the hash is not
    // one of them.  It keeps each name, and it refuses this one.
    CRUCIBLE_SCHEMA_NAME_FULL = 3
};

// Register an op name for a schema hash, for the labels of trace
// visualization and diagnostics.  out: a CRUCIBLE_SCHEMA_NAME_* code.  The
// tables take names only from the thread that holds the Vigil h.
CRUCIBLE_VESSEL_API uint32_t crucible_register_schema_name(CrucibleHandle h, uint64_t schema_hash, const char* name,
                                                           uint32_t* out) CRUCIBLE_VESSEL_NOEXCEPT;

// Lookup the registered name for a schema hash. Returns NULL if unknown.
CRUCIBLE_VESSEL_API const char* crucible_schema_name(uint64_t schema_hash) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Diagnostics ──────────────────────────────────────────────────────

// out: the count of the iteration boundaries that the background thread found.
CRUCIBLE_VESSEL_API uint32_t crucible_bg_iterations(CrucibleHandle h, uint32_t* out) CRUCIBLE_VESSEL_NOEXCEPT;

// out: the count of the entries in the ring buffer (approximate).
CRUCIBLE_VESSEL_API uint32_t crucible_ring_size(CrucibleHandle h, uint32_t* out) CRUCIBLE_VESSEL_NOEXCEPT;

// out: the count of the entries in the MetaLog (approximate).
CRUCIBLE_VESSEL_API uint32_t crucible_metalog_size(CrucibleHandle h, uint32_t* out) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Compiled mode accessors ──────────────────────────────────────────

// out: the pre-allocated output pointer for output j of the current op: the
// first byte of the storage of the output.  A view at an offset starts
// storage_offset elements later.  Null when the output has no slot.  While
// the Vigil records, the status is NOT_REPLAYING.  A j past the outputs of
// the op, or a call before the first match of a region, gives REFUSED.  A
// status other than LIVE writes nothing.
CRUCIBLE_VESSEL_API uint32_t crucible_output_ptr(CrucibleHandle h, uint16_t j, void** out) CRUCIBLE_VESSEL_NOEXCEPT;

// out: the pre-allocated input pointer for input j of the current op, or
// null, with the statuses of crucible_output_ptr for the inputs.
CRUCIBLE_VESSEL_API uint32_t crucible_input_ptr(CrucibleHandle h, uint16_t j, void** out) CRUCIBLE_VESSEL_NOEXCEPT;

// ── Trace export ────────────────────────────────────────────────────

// Export the active compiled region as .crtrace binary.
// Flushes the ring first, then serializes the RegionNode's TraceEntry
// data to disk.  Format: 16B header + 88B/op + 176B/meta + schema table.
// The file goes to a temporary file in the directory of path, and a rename
// puts it at path, so a reader of path sees the whole old file or the whole
// new file.
// out: 1 on success, 0 on failure (no active region, file I/O error).  The
// tables hold only names that the loader takes (sanitize_schema_name of
// crucible/SchemaTable.h).  A test build checks each name again, and a name
// that the loader refuses then gives 0, and the export writes nothing.
CRUCIBLE_VESSEL_API uint32_t crucible_export_crtrace(CrucibleHandle h, const char* path,
                                                     uint32_t* out) CRUCIBLE_VESSEL_NOEXCEPT;

// out: the count of the ops of the active compiled region, 0 with no region.
CRUCIBLE_VESSEL_API uint32_t crucible_active_num_ops(CrucibleHandle h, uint32_t* out) CRUCIBLE_VESSEL_NOEXCEPT;

// ── The memory plan of the active region ─────────────────────────────

// One slot of the memory plan of the active region (crucible::TensorSlot of
// crucible/MerkleDag.h), 40 bytes.  An external slot keeps the storage that it
// had before the region, so it takes no bytes of the pool.  An op index is the
// position of the op in the region, in the order of crucible_export_crtrace.
typedef struct {
    uint64_t offset_bytes;  // the offset of the slot in the pool, zero for an external slot
    uint64_t nbytes;  // the bytes of the storage of the slot
    uint32_t birth_op;  // the first op of the region that uses the slot
    uint32_t death_op;  // the last op of the region that uses the slot
    uint32_t slot_id;  // the id of the slot in the region
    int8_t dtype;  // crucible::ScalarType ordinal
    int8_t device_type;  // crucible::DeviceType ordinal
    int8_t device_idx;
    int8_t layout;  // crucible::Layout ordinal
    uint8_t is_external;  // 1 for a slot whose storage lives before the region
    uint8_t pad0;  // zero
    uint16_t pad1;  // zero
    uint32_t pad2;  // zero
} CrucibleSlot;

// The slots of the memory plan of the active region, in the order of the
// plan.  The call writes the first slots into out, at most capacity of them,
// and writes the count of the slots of the plan to out_count: 0 when the
// Vigil has no active region or the region has no plan.  out is null only with
// a capacity of 0, for a read of the count.  A pair of out and capacity that
// breaks the rule of the arrays above gives REFUSED, and the call writes
// nothing.
CRUCIBLE_VESSEL_API uint32_t crucible_active_region_slots(CrucibleHandle h, CrucibleSlot* out, uint32_t capacity,
                                                          uint32_t* out_count) CRUCIBLE_VESSEL_NOEXCEPT;

// out: the bytes of the pool of the memory plan of the active region, or 0
// when the Vigil has no active region or the region has no plan.
CRUCIBLE_VESSEL_API uint32_t crucible_active_pool_bytes(CrucibleHandle h, uint64_t* out) CRUCIBLE_VESSEL_NOEXCEPT;

// ── The data scalars of the replayed region ──────────────────────────

// One data slot of the region of the last activation or switch (BoundScalar of
// crucible/Vigil.h), 16 bytes.  A data slot holds a scalar of the type Scalar,
// float or double, such as a step size, and the replay binds its value at each
// iteration.  An op index is the position of the op in the region, in the
// order of crucible_export_crtrace.
typedef struct {
    uint32_t op_index;
    uint32_t slot;  // the scalar slot of the op, 0 to 4
    int64_t value;  // the bits of the value that the replay bound last, as a slot of a trace holds them
} CrucibleBoundScalar;

// The data slots of the region of the last activation or switch, in the order
// of the ops and of the slots.  The call writes the first slots into out, at
// most capacity of them, and writes the count of the data slots to out_count.
// out is null only with a capacity of 0, for a read of the count.  A pair of
// out and capacity that breaks the rule of the arrays above gives REFUSED,
// and the call writes nothing.
CRUCIBLE_VESSEL_API uint32_t crucible_bound_scalars(CrucibleHandle h, CrucibleBoundScalar* out, uint32_t capacity,
                                                    uint32_t* out_count) CRUCIBLE_VESSEL_NOEXCEPT;

#ifdef __cplusplus
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}
#endif
