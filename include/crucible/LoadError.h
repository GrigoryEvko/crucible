#pragma once

// The reason that a loader gives when it refuses an image: deserialize_region
// and deserialize_branch of crucible/Serialize.h, and load_content_addressed
// and load_session_events of crucible/Cipher.h.  A report of a damaged cache
// names the reason, so a person can act on it.  Each value is a condition
// that a loader tests.
// load_error_text gives the text of a reason for report() of the Report
// family.

#include <fixy/Core.h>

#include <cstdint>

namespace crucible {

enum class LoadError : std::uint8_t {
    // The image ends before a field that the header or a count names.
    truncated,
    // The first four bytes are not the CDAG magic number.
    bad_magic,
    // The image has a format version that this build does not read.
    version_mismatch,
    // The header names a node kind that is not the kind of the loader.
    wrong_kind,
    // A count is above its ceiling: the ops, the slots, the inputs, the
    // outputs, the scalar arguments or the arms.
    count_above_limit,
    // A byte is outside the values of its field: a bool, an enumerator, a
    // pad byte, an op flag, a kernel id, a dimension or a time.
    field_out_of_range,
    // The memory plan names a pool size, a count of external slots or a slot
    // that the pool allocator cannot serve.
    plan_unservable,
    // An op names a slot past the slots of the memory plan.
    slot_out_of_range,
    // The ops do not fold to the content hash that the header names.
    content_hash_mismatch,
    // The schema of the first op is not the schema that the image names.
    first_schema_mismatch,
    // The arms of a branch are not in the order of their values.
    arms_unsorted,
    // The caller asked the Cipher for the none hash, which names no object.
    none_hash,
    // The file of the object does not open: it is missing, or the open fails
    // for another reason, such as a permission.
    file_open_failed,
    // The file of the object is empty or above its limit, or it gives fewer
    // bytes than its size.
    file_size,
    // The object of the hash holds a region with a different content hash.
    object_hash_mismatch,
    // The federation entry of a session-event batch does not decode.
    batch_entry_malformed,
    // The entry of a session-event batch names a content hash or a row hash
    // that is not the hash of its index line, or its payload does not hash
    // to that content hash.
    batch_hash_mismatch,
    // A session-event batch holds a count of events or a range of steps that
    // is not the count or the range of its index line.
    batch_count_mismatch,
    // An event of a session-event batch belongs to a different session, its
    // steps are not consecutive, or the batch names a step that the load
    // holds already.
    batch_steps_mismatch,
    // The region image does not end with the checksum of the bytes before
    // it: the checksum differs, or bytes come between the last field and the
    // checksum.
    checksum_mismatch,
};

// The text of a reason, for a report to a person.  Each reason has its own
// text.  A value that names no reason ends the process.
[[nodiscard]] constexpr ::fixy::TextView load_error_text(LoadError reason) noexcept {
    switch (reason) {
        case LoadError::truncated:
            return ::fixy::TextView{"the image ends before a field that its header or a count names"};
        case LoadError::bad_magic:
            return ::fixy::TextView{"the first four bytes of the image are not the CDAG magic number"};
        case LoadError::version_mismatch:
            return ::fixy::TextView{"the image has a format version that this build does not read"};
        case LoadError::wrong_kind:
            return ::fixy::TextView{"the header names a node kind that is not the kind of the loader"};
        case LoadError::count_above_limit:
            return ::fixy::TextView{"a count of the image is above its ceiling"};
        case LoadError::field_out_of_range:
            return ::fixy::TextView{"a byte of the image is outside the values of its field"};
        case LoadError::plan_unservable:
            return ::fixy::TextView{"the memory plan names a pool or a slot that the pool allocator cannot serve"};
        case LoadError::slot_out_of_range:
            return ::fixy::TextView{"an op names a slot past the slots of the memory plan"};
        case LoadError::content_hash_mismatch:
            return ::fixy::TextView{"the ops do not fold to the content hash that the header names"};
        case LoadError::first_schema_mismatch:
            return ::fixy::TextView{"the schema of the first op is not the schema that the image names"};
        case LoadError::arms_unsorted:
            return ::fixy::TextView{"the arms of the branch are not in the order of their values"};
        case LoadError::none_hash:
            return ::fixy::TextView{"the load asks for the none hash, which names no object"};
        case LoadError::file_open_failed:
            return ::fixy::TextView{"the file of the object does not open"};
        case LoadError::file_size:
            return ::fixy::TextView{"the file of the object is empty, above its limit, or shorter than its size"};
        case LoadError::object_hash_mismatch:
            return ::fixy::TextView{"the object of the hash holds a region with a different content hash"};
        case LoadError::batch_entry_malformed:
            return ::fixy::TextView{"the federation entry of the session-event batch does not decode"};
        case LoadError::batch_hash_mismatch:
            return ::fixy::TextView{"the session-event batch does not have the hashes that its index line names"};
        case LoadError::batch_count_mismatch:
            return ::fixy::TextView{"the session-event batch does not hold the events that its index line counts"};
        case LoadError::batch_steps_mismatch:
            return ::fixy::TextView{
                "an event of the session-event batch has another session, a gap in its steps, or a loaded step"};
        case LoadError::checksum_mismatch:
            return ::fixy::TextView{"the region image does not end with the checksum of the bytes before it"};
        default:
            ::fixy::unreachable();
    }
}

}  // namespace crucible
