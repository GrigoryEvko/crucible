// Negative-compile test.  This file must fail to compile.
//
// The publish stage gives the record of a region to the region callback, and
// the owner hands the record to its foreground through a slot.  No code
// writes a published region, and a reader of the record on another thread
// reads the region at the same time.  So the record names a const region,
// and a write through it does not compile.

#include <crucible/BackgroundThread.h>
#include <crucible/MerkleDag.h>

#include <utility>

namespace {

void write_through_the_record(crucible::BackgroundThread::PublishedRegion const& published) noexcept {
    auto link = published.region;
    std::move(link).expect("the publish stage gives a record of a region")->content_hash = crucible::ContentHash{};
}

}  // namespace

int main() {
    crucible::BackgroundThread::PublishedRegion const published{};
    write_through_the_record(published);
    return 0;
}
