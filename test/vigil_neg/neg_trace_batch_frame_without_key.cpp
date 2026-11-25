// Negative-compile test.  This file must fail to compile.
//
// A second frame of the entries of a trace batch, inside the first one.  The
// two frames give two writers of one run.  Only the door of the drain stage
// gives the key of the brand of the entries, so a body with no key opens no
// frame of them.

#include <crucible/BackgroundThread.h>

namespace {

using Batch = crucible::BackgroundThread::BgTraceBatch;
using EntriesKey = ::foundation::core::LoanKey<crucible::BackgroundThread::BatchEntriesBrand>;

// The key is a parameter, so the fixture mints no key.
[[maybe_unused]] void open_two_frames(Batch& batch, EntriesKey& key) noexcept {
    batch.entries.with_mut(key, [&batch](auto first) noexcept {
        batch.entries.with_mut([&first](auto second) noexcept { static_cast<void>(first.size() + second.size()); });
    });
}

}  // namespace

int main() { return 0; }
