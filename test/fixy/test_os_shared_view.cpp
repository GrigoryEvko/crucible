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
//
// The cases of the ring ticket write a ring of records through write_ticket.
// The single-thread cases put a writer in the middle of its slot by hand, as a
// BPF program that stops there: each later write to the slot drops until that
// writer ends, and a writer that starts late drops.  A reader of a ticket gets
// the record of that ticket or no value: not the record of the lap before in a
// slot that its writer claimed and did not write, and not a later one when the
// ring overtook the reader.  In the race case, some writers claim the tickets
// of a ring of one slot and of four slots, so they meet in their slots all the
// time, and some readers copy the newest tickets.  Each word of a record tells
// its ticket, so a copy that mixes two writes, or that holds another ticket,
// shows.  The writes and the drops add up to the claims.

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

// A record of a ring: two words that tell the ticket of the write, its commit
// word, and a word that the test writes by hand.
struct RingProbe {
    std::uint64_t claim = 0;
    std::uint64_t complement = 0;
    std::uint64_t commit = 0;
    std::uint64_t extra = 0;
};

// The slots of the ring of the cases, and the ring.
constexpr std::size_t kRingSlots = 4;
using ProbeRing = mm::SharedView<RingProbe, kRingSlots>;
using mm::RingTicket;
using mm::RingWrite;

[[nodiscard]] RingTicket ticket_of(std::uint64_t claim) {
    return RingTicket::check(claim).expect("a claim of the test is a ticket");
}

// The record of a write of ticket.
[[nodiscard]] RingProbe probe_of(RingTicket ticket) {
    return RingProbe{.claim = ticket.raw(), .complement = ~std::uint64_t{ticket.raw()}, .extra = ticket.raw() + 1};
}

// The slot of ticket in the ring.
[[nodiscard]] fixy::Index<kRingSlots> slot_of(RingTicket ticket) { return fixy::Index<kRingSlots>::mask(ticket.raw()); }

// What a reader of ticket gets: the record of that ticket, another record, or
// no value.
enum class Found : std::uint8_t {
    record,
    other,
    nothing,
};

template <std::size_t Extent>
[[nodiscard]] Found read_probe(mm::SharedView<RingProbe, Extent> ring, RingTicket ticket) {
    return ring.template read_ticket<&RingProbe::commit>(ticket)
        .map([ticket](RingProbe copy) noexcept {
            bool const is_record = copy.claim == ticket.raw() && copy.complement == ~std::uint64_t{ticket.raw()}
                                && copy.extra == ticket.raw() + 1;
            return is_record ? Found::record : Found::other;
        })
        .value_or(Found::nothing);
}

// Reports the case that failed.
[[nodiscard]] int ring_case_failed(fixy::TextView name) {
    fixy::report(fixy::Sink::Err, "the ring ticket: {}\n", name);
    return 1;
}

// A writer that stops in its slot keeps it.  The test writes the steps of the
// writer of the ticket 1 by hand: the commit word of a busy slot and one field,
// and later the rest and the whole commit.
[[nodiscard]] int a_stopped_writer_keeps_its_slot(ProbeRing ring) {
    RingTicket const stopped = ticket_of(1);
    RingTicket const lapped = ticket_of(1 + kRingSlots);
    RingTicket const twice_lapped = ticket_of(1 + 2 * kRingSlots);
    ring.store_release<&RingProbe::commit>(slot_of(stopped), 2 * stopped.raw() + 1);
    ring.store_release<&RingProbe::claim>(slot_of(stopped), stopped.raw());
    if (ring.write_ticket<&RingProbe::commit>(lapped, probe_of(lapped)) != RingWrite::dropped) {
        return ring_case_failed("a write took the slot of a writer that is in it");
    }
    if (read_probe(ring, stopped) != Found::nothing || read_probe(ring, lapped) != Found::nothing
        || ring.has_ticket<&RingProbe::commit>(stopped) || ring.has_ticket<&RingProbe::commit>(lapped)) {
        return ring_case_failed("a reader got a record of a slot that a writer is in");
    }
    ring.store_release<&RingProbe::complement>(slot_of(stopped), ~std::uint64_t{stopped.raw()});
    ring.store_release<&RingProbe::extra>(slot_of(stopped), stopped.raw() + 1);
    ring.store_release<&RingProbe::commit>(slot_of(stopped), 2 * stopped.raw() + 2);
    if (read_probe(ring, stopped) != Found::record || read_probe(ring, lapped) != Found::nothing) {
        return ring_case_failed("the writer that stopped did not end its write");
    }
    // The slot holds the ticket two laps before, because the write between
    // dropped, so the write takes it from the commit word that it saw.
    if (ring.write_ticket<&RingProbe::commit>(twice_lapped, probe_of(twice_lapped)) != RingWrite::written
        || read_probe(ring, twice_lapped) != Found::record) {
        return ring_case_failed("a slot after a dropped write took no later write");
    }
    return 0;
}

// A writer that starts after a later ticket wrote its slot drops, and the later
// record stays.
[[nodiscard]] int a_late_writer_drops(ProbeRing ring) {
    RingTicket const late = ticket_of(2);
    RingTicket const later = ticket_of(2 + kRingSlots);
    if (ring.write_ticket<&RingProbe::commit>(later, probe_of(later)) != RingWrite::written) {
        return ring_case_failed("a write of a later lap did not take an empty slot");
    }
    if (ring.write_ticket<&RingProbe::commit>(late, probe_of(late)) != RingWrite::dropped) {
        return ring_case_failed("a late writer wrote over a later record");
    }
    if (read_probe(ring, later) != Found::record || read_probe(ring, late) != Found::nothing) {
        return ring_case_failed("a late writer changed the later record");
    }
    return 0;
}

// A reader of a ticket whose writer claimed and did not write yet gets nothing,
// also when the slot holds a whole record of the lap before.
[[nodiscard]] int a_claim_gives_no_older_record(ProbeRing ring) {
    RingTicket const written = ticket_of(3);
    RingTicket const claimed = ticket_of(3 + kRingSlots);
    if (ring.write_ticket<&RingProbe::commit>(written, probe_of(written)) != RingWrite::written) {
        return ring_case_failed("a write did not take an empty slot");
    }
    if (read_probe(ring, claimed) != Found::nothing || ring.has_ticket<&RingProbe::commit>(claimed)) {
        return ring_case_failed("a reader of a claim got the record of the lap before");
    }
    return read_probe(ring, written) == Found::record ? 0 : ring_case_failed("a written record is gone");
}

// A reader that the ring overtook gets nothing for its ticket.
[[nodiscard]] int a_lapped_reader_gets_nothing(ProbeRing ring) {
    std::uint64_t const first = 100;
    for (std::uint64_t claim = first; claim < first + 2 * kRingSlots; ++claim) {
        if (ring.write_ticket<&RingProbe::commit>(ticket_of(claim), probe_of(ticket_of(claim))) != RingWrite::written) {
            return ring_case_failed("a write of a later lap dropped");
        }
    }
    for (std::uint64_t claim = first; claim < first + kRingSlots; ++claim) {
        if (read_probe(ring, ticket_of(claim)) != Found::nothing) {
            return ring_case_failed("a lapped reader got a record");
        }
        if (read_probe(ring, ticket_of(claim + kRingSlots)) != Found::record) {
            return ring_case_failed("a reader of the last lap got no record");
        }
    }
    // A count below zero gives a ticket that no write has.
    if (RingTicket::check(std::uint64_t{0} - 1).is_some()) return ring_case_failed("a wrapped count is a ticket");
    if (read_probe(ring, RingTicket::mask(std::uint64_t{0} - 1)) != Found::nothing) {
        return ring_case_failed("a wrapped count gave a record");
    }
    return 0;
}

// A ring of the page for one case: each case takes its own four slots.
template <std::size_t Case, class Mapping, class Check>
[[nodiscard]] int on_ring(Mapping& mapping, Check check) {
    for (ProbeRing const ring :
         mapping.template view_at<RingProbe, kRingSlots>(Case * kRingSlots * sizeof(RingProbe))) {
        return check(ring);
    }
    return ring_case_failed("the mapping gave no ring");
}

// The claims of one writer of the race case, and the copies of one reader.
constexpr std::uint64_t kClaims = 20000;

// The race case: three writers claim the tickets of one ring of Extent slots,
// and two readers copy the four newest tickets.
template <std::size_t Extent>
[[nodiscard]] int ring_copies_hold_their_ticket(mm::SharedView<RingProbe, Extent> ring) {
    foundation::core::Atomic<std::uint64_t> claims{0};
    foundation::core::Atomic<std::uint64_t> written{0};
    foundation::core::Atomic<std::uint64_t> dropped{0};
    foundation::core::Atomic<std::uint64_t> copied{0};
    foundation::core::Atomic<std::uint64_t> wrong{0};
    auto write = [ring, &claims, &written, &dropped]() noexcept {
        for (std::uint64_t turn = 0; turn < kClaims; ++turn) {
            RingTicket const ticket = ticket_of(claims.fetch_add_acq_rel(1));
            RingWrite const end = ring.template write_ticket<&RingProbe::commit>(ticket, probe_of(ticket));
            static_cast<void>((end == RingWrite::written ? written : dropped).fetch_add_acq_rel(1));
        }
    };
    auto read = [ring, &claims, &copied, &wrong]() noexcept {
        for (std::uint64_t turn = 0; turn < kClaims; ++turn) {
            std::uint64_t const newest = claims.load_acquire();
            for (RingTicket const ticket : RingTicket::check(newest - 1 - turn % kRingSlots)) {
                Found const found = read_probe(ring, ticket);
                if (found == Found::nothing) continue;
                static_cast<void>(copied.fetch_add_acq_rel(1));
                if (found == Found::other) static_cast<void>(wrong.fetch_add_acq_rel(1));
            }
        }
    };
    fixy::thread::run_joined_raw_probe(WorkerCtx{eff::testing::bg()}, ::fixy::TestRunnerCtx{eff::testing::test()},
                                       "a probe of the ring ticket: three writers and two readers of one ring of a "
                                       "shared mapping at the same time",
                                       fixy::thread::ThreadOptions{}, auto(write), auto(write), auto(write), auto(read),
                                       auto(read))
        .expect("the probe starts five threads");
    std::uint64_t const ended = written.load_acquire() + dropped.load_acquire();
    if (wrong.load_acquire() != 0) {
        fixy::report(fixy::Sink::Err, "the ring ticket: {} of {} copies mixed two writes\n", wrong.load_acquire(),
                     copied.load_acquire());
        return 1;
    }
    if (claims.load_acquire() != 3 * kClaims || ended != 3 * kClaims || written.load_acquire() == 0) {
        fixy::report(fixy::Sink::Err, "the ring ticket: {} claims, {} written, {} dropped\n", claims.load_acquire(),
                     written.load_acquire(), dropped.load_acquire());
        return 1;
    }
    return 0;
}

// The race case on a ring of one slot, where the writers meet all the time.
[[nodiscard]] int one_slot_copies_hold_their_ticket(ProbeRing ring) {
    for (auto const slot : ring.window<1>(0)) {
        return ring_copies_hold_their_ticket(slot);
    }
    return ring_case_failed("the ring gave no slot");
}

// Each case of the ring ticket.
template <class Mapping>
[[nodiscard]] int ring_tickets_hold(Mapping& mapping) {
    if (int const rc = on_ring<0>(mapping, a_stopped_writer_keeps_its_slot); rc != 0) return rc;
    if (int const rc = on_ring<1>(mapping, a_late_writer_drops); rc != 0) return rc;
    if (int const rc = on_ring<2>(mapping, a_claim_gives_no_older_record); rc != 0) return rc;
    if (int const rc = on_ring<3>(mapping, a_lapped_reader_gets_nothing); rc != 0) return rc;
    if (int const rc = on_ring<4>(mapping, one_slot_copies_hold_their_ticket); rc != 0) return rc;
    return on_ring<5>(mapping, ring_copies_hold_their_ticket<kRingSlots>);
}

// Each case of the commit word.
template <class Mapping>
[[nodiscard]] int committed_records_hold(Mapping& mapping) {
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

// The check on a shared mapping of a page of sealed memory of its own, whose
// bytes are zero.  The mapping takes the permission of its region, which the
// caller holds and ends after the mapping ends.
template <class Brand, class Check>
[[nodiscard]] int check_shared_page(IoBlockCtx const& ctx, perm::Permission<ProbeRegion, Brand> const& owner,
                                    Check check) {
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
    return check(mapping);
}

}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    auto owner = perm::mint_permission_root<ProbeRegion>();
    int rc = check_shared_page(ctx, owner, [](auto& mapping) { return committed_records_hold(mapping); });
    if (rc == 0) rc = check_shared_page(ctx, owner, [](auto& mapping) { return ring_tickets_hold(mapping); });
    foundation::linear::drop(std::move(owner));
    return rc;
}
