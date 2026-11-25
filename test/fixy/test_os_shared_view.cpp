// The shared view of a shared mapping: each read and each write is atomic, and
// a copy of a record through its commit word never mixes two writes.
//
// The race case: a writer thread writes one slot of records through
// write_committed, and a reader thread copies the slot through read_committed
// at the same time, on a shared mapping.  Each write gives a record whose two
// words agree, so a copy that mixes two writes shows a pair that does not
// agree, and the test counts no such copy.  The case runs a record of four
// words with the commit word third, one with it first, and one of 16 words with
// it first.  A commit that a writer stores before the other words shows only
// with the commit word first.  ThreadSanitizer sees only atomic accesses to the
// mapping, so the tsan preset reports no race.  A plain View of the same mapping
// reads and writes with plain loads and stores, and the same test through it is
// a data race.
//
// The single-thread cases read a word, a field and a record of each door, and
// a record that no write committed gives no value.  The round trip of each
// word runs records of 2, 4, 8, 9, 16 and 32 words, with the commit word first
// and last, because a copy of at most eight words has other code than a longer
// one.  A write must change no word of the records beside it.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/OwnedMmap.h>
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

// The same record with the commit word first, so a store of the commit before
// the other words is seen.
struct FrontProbe {
    std::uint64_t stamp = 0;
    std::uint64_t value = 0;
    std::uint64_t complement = 0;
    std::uint64_t pad = 0;
};

// The same record with 16 words, which the copy and the store walk with a loop.
struct WideFrontProbe {
    std::uint64_t stamp = 0;
    std::uint64_t value = 0;
    std::uint64_t complement = 0;
    fixy::FixedArray<std::uint64_t, 13> pad{};
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

// Sealed memory of one page, or an empty handle when the kernel gives none.
[[nodiscard]] fixy::fs::SealedMemory page_memory(IoBlockCtx const& ctx) {
    return fixy::fs::mint_sealed_memory(ctx, fixy::fs::FileOffset{static_cast<std::int64_t>(kPageBytes)})
        .match([](fixy::Linear<fixy::fs::SealedMemory> made) { return std::move(made).consume(); },
               [](foundation::os::Errno) { return fixy::fs::SealedMemory{}; });
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

// A record of Words words whose commit word is the first word, and one whose
// commit word is the last word.  copy_words_ and store_words_ expand the words
// of a record of at most eight words, and walk the words of a longer one.
template <std::size_t Words>
struct FrontCommit {
    std::uint64_t stamp = 0;
    fixy::FixedArray<std::uint64_t, Words - 1> rest{};
};
template <std::size_t Words>
struct BackCommit {
    fixy::FixedArray<std::uint64_t, Words - 1> rest{};
    std::uint64_t stamp = 0;
};

// The record with the stamp and a word of value in each place, each value
// distinct in the record.
template <class Record>
[[nodiscard]] Record record_of(std::uint64_t stamp) {
    Record made{};
    made.stamp = stamp;
    for (auto const index : decltype(made.rest)::indices())
        made.rest.set(index, stamp * 1000 + index.raw() + 1);
    return made;
}

// Two records hold the same words.
template <class Record>
[[nodiscard]] bool is_same_record(Record const& left, Record const& right) {
    bool is_same = left.stamp == right.stamp;
    for (auto const index : decltype(left.rest)::indices())
        is_same = is_same && left.rest.at(index) == right.rest.at(index);
    return is_same;
}

// A write of one record of a run gives each word of it back, and it changes no
// word of the record before it or after it.
template <class Record, class Mapping>
[[nodiscard]] int each_word_round_trips(Mapping& mapping) {
    constexpr std::size_t kCount = kPageBytes / sizeof(Record);
    for (auto const records : mapping.template view_at<Record, kCount>(0)) {
        fixy::Index<kCount> const before = fixy::index<0>();
        fixy::Index<kCount> const middle = fixy::index<1>();
        fixy::Index<kCount> const after = fixy::index<2>();
        records.template write_committed<&Record::stamp>(before, record_of<Record>(3));
        records.template write_committed<&Record::stamp>(middle, record_of<Record>(5));
        records.template write_committed<&Record::stamp>(after, record_of<Record>(7));
        records.template write_committed<&Record::stamp>(middle, record_of<Record>(11));
        bool const is_middle = records.template read_committed<&Record::stamp>(middle)
                                   .map([](Record copy) { return is_same_record(copy, record_of<Record>(11)); })
                                   .value_or(false);
        bool const are_neighbors = records.template read_committed<&Record::stamp>(before)
                                       .map([](Record copy) { return is_same_record(copy, record_of<Record>(3)); })
                                       .value_or(false)
                                && records.template read_committed<&Record::stamp>(after)
                                       .map([](Record copy) { return is_same_record(copy, record_of<Record>(7)); })
                                       .value_or(false);
        if (!is_middle || !are_neighbors) {
            fixy::report(fixy::Sink::Err, "a record of {} bytes gave middle {} neighbors {}\n", sizeof(Record),
                         is_middle, are_neighbors);
            return 1;
        }
        return 0;
    }
    fixy::report(fixy::Sink::Err, "the mapping gave no run of records of {} bytes\n", sizeof(Record));
    return 1;
}

// Each form of a record in the pack.
template <class... Records, class Mapping>
[[nodiscard]] int words_round_trip(Mapping& mapping) {
    return (each_word_round_trips<Records>(mapping) + ... + 0);
}

// The race case: a writer and a reader on one slot at the same time, of a
// record whose commit word is the third word or the first.  Each record takes
// its own slot, because a slot that holds a record of the other type before the
// first write looks like a copy that mixes two writes.
template <class Record, std::size_t Slot, class Mapping>
[[nodiscard]] int copies_mix_no_two_writes(Mapping& mapping) {
    std::uint64_t torn = 0;
    std::uint64_t copied = 0;
    constexpr std::size_t kCount = kPageBytes / sizeof(Record);
    for (auto const records : mapping.template view_at<Record, kCount>(0)) {
        fixy::Index<kCount> const slot = fixy::index<Slot>();
        auto writer = [records, slot]() noexcept {
            for (std::uint64_t write = 1; write <= kWrites; ++write) {
                Record made{};
                made.value = write;
                made.complement = ~write;
                made.stamp = write;
                records.template write_committed<&Record::stamp>(slot, made);
            }
        };
        auto reader = [records, slot, &torn, &copied]() noexcept {
            for (std::uint64_t read = 0; read < kWrites; ++read) {
                for (Record const& copy : records.template read_committed<&Record::stamp>(slot)) {
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

// Each case on a shared mapping of a page of sealed memory.  The mapping takes
// the permission of its region, which the caller holds and ends after the
// mapping ends.
template <class Brand>
[[nodiscard]] int check_shared_page(IoBlockCtx const& ctx, perm::Permission<ProbeRegion, Brand> const& owner) {
    fixy::fs::SealedMemory const memory = page_memory(ctx);
    if (!memory.is_open()) {
        fixy::report(fixy::Sink::Out, "[skipped] the kernel gives no sealed memory\n");
        return 0;
    }
    auto mapped = region_of(fixy::mmap::mint_mmap<ReadWrite, Shared>(ctx, owner, memory.ref(), kPageBytes));
    if (mapped.is_err()) {
        fixy::report(fixy::Sink::Err, "a shared mapping of a page of the memory failed\n");
        return 1;
    }
    auto mapping = std::move(mapped).expect("the mint gave a mapping");
    if (int const rc = doors_read_what_they_wrote(mapping); rc != 0) return rc;
    if (int const rc = copies_mix_no_two_writes<Probe, 2>(mapping); rc != 0) return rc;
    if (int const rc = copies_mix_no_two_writes<FrontProbe, 5>(mapping); rc != 0) return rc;
    if (int const rc = copies_mix_no_two_writes<WideFrontProbe, 9>(mapping); rc != 0) return rc;
    // The round trip writes records of other types in the page, so it runs
    // last: a slot of the race case holds no record of the round trip.
    return words_round_trip<FrontCommit<2>, FrontCommit<4>, FrontCommit<8>, FrontCommit<9>, FrontCommit<16>,
                            FrontCommit<32>, BackCommit<2>, BackCommit<4>, BackCommit<8>, BackCommit<9>, BackCommit<16>,
                            BackCommit<32>>(mapping);
}

}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    auto owner = perm::mint_permission_root<ProbeRegion>();
    int const rc = check_shared_page(ctx, owner);
    foundation::linear::drop(std::move(owner));
    return rc;
}
