#pragma once

// The reason that a loader gives when it refuses an image: deserialize_region
// and deserialize_branch of crucible/Serialize.h, and load_content_addressed
// of crucible/Cipher.h.  A report of a damaged cache names the reason, so a
// person can act on it.  Each value is a condition that a loader tests.

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
};

}  // namespace crucible
