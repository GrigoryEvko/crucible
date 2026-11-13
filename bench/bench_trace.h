#pragma once

// The metadata of a recorded trace, for the benches that run build_trace on
// a .crtrace file.

#include <crucible/MetaLog.h>
#include <crucible/TraceLoader.h>

#include <cstdint>

namespace bench {

// Empties the metadata log and appends the metas of each op of the trace, in
// the order of the ops.  An op with no metas, or with metas past the end of
// LoadedTrace::metas, appends nothing.  build_trace reads the metas at the
// indices of LoadedTrace::meta_starts, so the function ignores the index that
// each append returns.  O(number of metas).
inline void refill_meta_log(crucible::MetaLog& meta_log, const crucible::LoadedTrace& trace) noexcept {
    meta_log.reset();
    uint32_t cursor = 0;
    for (const crucible::TraceRing::Entry& entry : trace.entries) {
        const uint16_t meta_count = static_cast<uint16_t>(entry.num_inputs + entry.num_outputs);
        if (meta_count > 0 && cursor + meta_count <= trace.num_metas) {
            (void)meta_log.try_append(&*trace.metas.get(cursor).expect("the run of the op fits the metas"), meta_count);
            cursor += meta_count;
        }
    }
}

}  // namespace bench
