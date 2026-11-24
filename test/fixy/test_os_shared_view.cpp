// The shared view of a shared mapping: each read and each write is atomic, and
// a copy of a record through its commit word never mixes two writes.
//
// The race case: a writer thread writes one slot of records through
// write_committed, and a reader thread copies the slot through read_committed
// at the same time, on a shared mapping.  Each write gives a record whose two
// words agree, so a copy that mixes two writes shows a pair that does not
// agree, and the test counts no such copy.  ThreadSanitizer sees only atomic
// accesses to the mapping, so the tsan preset reports no race.  A plain View
// of the same mapping reads and writes with plain loads and stores, and the
// same test through it is a data race.
//
// The single-thread cases read a word, a field and a record of each door, and
// a record that no write committed gives no value.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/OwnedMmap.h>
#include <fixy/os/Path.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace eff = foundation::effects;
namespace mm = fixy::mmap;
namespace perm = foundation::permissions;

namespace {

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using WorkerCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;
using ReadWrite = fixy::atom::mmap::with_prot<mm::prot::ReadWrite>;
using Shared = fixy::atom::mmap::with_share<mm::share::Shared>;

// A record of the test: two words that a write keeps in agreement, the stamp
// of the write as its commit word, and a word of padding.
struct Probe {
    std::uint64_t value = 0;
    std::uint64_t complement = 0;
    std::uint64_t stamp = 0;
    std::uint64_t pad = 0;
};

struct ProbeRegion {
    using permission_row = eff::Row<>;
};

constexpr std::size_t kPageBytes = 4096;
constexpr std::size_t kProbes = kPageBytes / sizeof(Probe);
constexpr std::uint64_t kWrites = 20000;

using Probes = mm::SharedView<Probe, kProbes>;

// The region of a mint, or the error of the mint.
template <class Region>
[[nodiscard]] fixy::Result<Region, foundation::os::Errno>
region_of(fixy::Result<fixy::Linear<Region>, foundation::os::Errno> minted) {
    CRUCIBLE_TRY(made, std::move(minted));
    return std::move(made).consume();
}

// A file with no name under /tmp of one page, or an empty handle when /tmp
// gives no such file.
[[nodiscard]] fixy::fs::TemporaryFile page_file(IoBlockCtx const& ctx) {
    auto const tmp = fixy::sanitize_path(fixy::mint_tagged<fixy::tags::source::External>(fixy::TextView{"/tmp"}))
                         .expect("/tmp passes the check");
    fixy::fs::DirectoryHandle const directory = fixy::fs::open_directory(ctx, tmp).expect("/tmp opens");
    fixy::fs::TemporaryFile file =
        fixy::fs::mint_temporary_file_at(ctx, directory.ref())
            .match([](fixy::Linear<fixy::fs::TemporaryFile> opened) { return std::move(opened).consume(); },
                   [](foundation::os::Errno) { return fixy::fs::TemporaryFile{}; });
    if (!file.is_open()) return file;
    if (fixy::fs::set_file_size(ctx, file.ref(), fixy::fs::FileOffset{static_cast<std::int64_t>(kPageBytes)})
            .is_err()) {
        return fixy::fs::TemporaryFile{};
    }
    return file;
}

// The single-thread cases of each door.
template <class Mapping>
[[nodiscard]] int doors_read_what_they_wrote(Mapping& mapping) {
    for (Probes const probes : mapping.template view_at<Probe, kProbes>(0)) {
        fixy::Index<kProbes> const first = fixy::index<0>();
        fixy::Index<kProbes> const second = fixy::index<1>();
        if (probes.read_committed<&Probe::stamp>(first).is_some()) {
            fixy::report(fixy::Sink::Err, "a record that no write committed gave a value\n");
            return 1;
        }
        probes.write_committed<&Probe::stamp>(first, Probe{.value = 7, .complement = ~std::uint64_t{7}, .stamp = 9});
        bool const is_copied =
            probes.read_committed<&Probe::stamp>(first)
                .map([](Probe copy) noexcept {
                    return copy.value == 7 && copy.complement == ~std::uint64_t{7} && copy.stamp == 9;
                })
                .value_or(false);
        probes.store_release<&Probe::value>(second, 11);
        bool const is_field_read = probes.load_acquire<&Probe::value>(second) == 11;
        if (!is_copied || !is_field_read) {
            fixy::report(fixy::Sink::Err, "a door of the shared view gave copied {} field {}\n", is_copied,
                         is_field_read);
            return 1;
        }
    }
    for (mm::SharedView<std::uint64_t, 1> const words : mapping.template view_at<std::uint64_t, 1>(kPageBytes - 8)) {
        words.store_release(fixy::index<0>(), 13);
        if (words.load_acquire(fixy::index<0>()) != 13) {
            fixy::report(fixy::Sink::Err, "a word of the shared view did not read what it wrote\n");
            return 1;
        }
    }
    return 0;
}

// The race case: a writer and a reader on one slot at the same time.
template <class Mapping>
[[nodiscard]] int copies_mix_no_two_writes(Mapping& mapping) {
    std::uint64_t torn = 0;
    std::uint64_t copied = 0;
    for (Probes const probes : mapping.template view_at<Probe, kProbes>(0)) {
        fixy::Index<kProbes> const slot = fixy::index<2>();
        auto writer = [probes, slot]() noexcept {
            for (std::uint64_t write = 1; write <= kWrites; ++write) {
                probes.write_committed<&Probe::stamp>(slot,
                                                      Probe{.value = write, .complement = ~write, .stamp = write});
            }
        };
        auto reader = [probes, slot, &torn, &copied]() noexcept {
            for (std::uint64_t read = 0; read < kWrites; ++read) {
                for (Probe const copy : probes.read_committed<&Probe::stamp>(slot)) {
                    ++copied;
                    if (copy.complement != ~copy.value || copy.stamp != copy.value) ++torn;
                }
            }
        };
        fixy::thread::run_joined_raw_probe(WorkerCtx{eff::testing::bg()}, ::fixy::TestRunnerCtx{eff::testing::test()},
                                           "a probe of the shared view: a writer and a reader of one slot of a shared "
                                           "mapping at the same time",
                                           fixy::thread::ThreadOptions{}, std::move(writer), std::move(reader))
            .expect("the probe starts two threads");
    }
    if (torn != 0) {
        fixy::report(fixy::Sink::Err, "{} of {} copies of the shared view mixed two writes\n", torn, copied);
        return 1;
    }
    return 0;
}

// Each case on a shared mapping of a page of a file.  The mapping takes the
// permission of its region, which the caller holds and ends after the
// mapping ends.
template <class Brand>
[[nodiscard]] int check_shared_page(IoBlockCtx const& ctx, perm::Permission<ProbeRegion, Brand> const& owner) {
    fixy::fs::TemporaryFile const file = page_file(ctx);
    if (!file.is_open()) {
        fixy::report(fixy::Sink::Out, "[skipped] the file system of /tmp has no file with no name\n");
        return 0;
    }
    auto mapped = region_of(fixy::mmap::mint_mmap<ReadWrite, Shared>(ctx, owner, file.ref(), kPageBytes));
    if (mapped.is_err()) {
        fixy::report(fixy::Sink::Err, "a shared mapping of a page of the file failed\n");
        return 1;
    }
    auto mapping = std::move(mapped).expect("the mint gave a mapping");
    if (int const rc = doors_read_what_they_wrote(mapping); rc != 0) return rc;
    return copies_mix_no_two_writes(mapping);
}

}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    auto owner = perm::mint_permission_root<ProbeRegion>();
    int const rc = check_shared_page(ctx, owner);
    foundation::linear::drop(std::move(owner));
    return rc;
}
