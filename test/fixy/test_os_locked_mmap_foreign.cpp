// A map of a descriptor of a C library in a LockedMmap, and the test of its
// object (the test of a foreign object of fixy/os/LockedMmap.h).
//   - Shared memory passes the test.  A discard of the lock drops its pages,
//     and the next read loads them again.  The map starts at its offset.
//   - Memory that is shorter than its map loses the last page.
//   - The verdict for a ring of io_uring, a BPF array and a perf ring agrees
//     with the oracle.  Releases of the kernel differ in how they map the three
//     objects, so the test pins no verdict for them.
//   - A kernel that has no advice for the test refuses the object.
//   - Each refusal gives the mapping, which still reads, and the exclusive,
//     which maps the next object.
// The oracle runs in a child process.  It maps the object, discards the first
// page with no test, and reads the page.  SIGBUS ends the child when the page
// is lost.

#include <fixy/os/LockedMmap.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Io.h>
#include <fixy/os/PerfEvent.h>
#include <fixy/os/Process.h>
#include <foundation/Linear.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>
#include <foundation/os/KernelFloor.h>
#include <foundation/os/MapWords.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;
namespace mmap = ::fixy::mmap;
using ::foundation::os::Errno;

constexpr ::fixy::TestRunnerCtx kTest{eff::testing::test()};

// One context for each door: the rows of a map and of an advice, and the row
// of a pool.
using MapCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                eff::Effect::Block, eff::Effect::Nondet>>;
constexpr MapCtx kCtx{eff::testing::test()};
constexpr eff::Alloc kAlloc = eff::testing::test().alloc;

struct PageRegion final {
    using permission_row = eff::Row<>;
};

using ReadOnly = ::fixy::atom::mmap::with_prot<mmap::prot::ReadOnly>;
using ReadWrite = ::fixy::atom::mmap::with_prot<mmap::prot::ReadWrite>;
using Shared = ::fixy::atom::mmap::with_share<mmap::share::Shared>;
using DontNeed = mmap::advice::DontNeed;

constexpr std::size_t kPageBytes = 4096;
constexpr ::fixy::ByteCount kPage{kPageBytes};
constexpr std::uint64_t kWord = 0x9E3779B97F4A7C15;

// The signal of a read of a page that does not come back, SIGBUS.
constexpr std::uint8_t kBusSignal = 7;

// The codes of a child: its page came back, the kernel did not discard the
// page, the host refuses the map of the object, and the host refuses the filter
// of the system calls.
constexpr std::uint8_t kCameBack = 0;
constexpr std::uint8_t kNotDiscarded = 2;
constexpr std::uint8_t kNoMap = 3;
constexpr std::uint8_t kNoFilter = 4;

// The limit of the wait for a child that reads one page or tests one object.
constexpr ::fixy::process::WaitLimit kChildLimit{60000};

// What the oracle saw of the first page of an object after a discard.
enum class PageFate : std::uint8_t {
    comes_back,
    is_lost,
    is_not_discarded,
};

// The answer that a filter gives to each madvise with the advice: an errno, or
// success for none.
struct AdviceAnswer final {
    int advice = 0;
    ::fixy::Option<Errno> answer{};
};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the test maps the raw descriptor of an object, and the oracle discards its page")
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {

// The descriptor of sealed memory, as a C library gives one.
[[nodiscard]] mmap::ForeignDescriptor foreign_of(::fixy::fs::SealedMemory const& memory) noexcept {
    return mmap::ForeignDescriptor{memory.ref().abi_descriptor()};
}

// The descriptor of the perf ring of an event.
[[nodiscard]] mmap::ForeignDescriptor foreign_of(::fixy::perf::PerfEventHandle const& event) noexcept {
    return mmap::ForeignDescriptor{event.ref().abi_descriptor()};
}

// The map of a BPF array of one page that a program can map, or no value when
// the host refuses it.  The process closes the descriptor at its end.
[[nodiscard]] ::fixy::Option<mmap::ForeignDescriptor> make_bpf_array() noexcept {
    ::bpf_attr attributes{};
    attributes.map_type = BPF_MAP_TYPE_ARRAY;
    attributes.key_size = 4;
    attributes.value_size = 8;
    attributes.max_entries = 512;
    attributes.map_flags = BPF_F_MMAPABLE;
    long const descriptor = ::syscall(SYS_bpf, BPF_MAP_CREATE, &attributes, sizeof(attributes));
    if (descriptor < 0) return ::fixy::none;
    return ::fixy::some(mmap::ForeignDescriptor{static_cast<int>(descriptor)});
}

// Maps the first page of the object, discards it with no test, and reads its
// first byte.  The process ends with a code, or with SIGBUS for a lost page.
// The default action of SIGBUS ends the process, also under ASan.
void discard_and_read(mmap::ForeignDescriptor descriptor) noexcept {
    ::signal(SIGBUS, SIG_DFL);
    void* const page = ::mmap(nullptr, kPageBytes, PROT_READ, MAP_SHARED, descriptor.abi_descriptor(), 0);
    if (page == MAP_FAILED) ::_exit(kNoMap);
    if (::madvise(page, 1, MADV_DONTNEED) != 0) ::_exit(kNotDiscarded);
    static_cast<void>(*static_cast<unsigned char const volatile*>(page));
    ::_exit(kCameBack);
}

// Makes each madvise of this process with the advice give the answer.  A host
// that refuses the filter ends the process with the code kNoFilter.
void filter_advice(AdviceAnswer const& given) noexcept {
    auto const advice = static_cast<std::uint32_t>(given.advice);
    auto const answer = given.answer.match([](Errno error) noexcept { return static_cast<std::uint32_t>(error); },
                                           []() noexcept { return std::uint32_t{0}; });
    ::sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(::seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_madvise, 0, 3),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(::seccomp_data, args) + 2 * sizeof(std::uint64_t)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, advice, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | answer),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    ::sock_fprog program{.len = 6, .filter = filter};
    if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) ::_exit(kNoFilter);
    if (::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0) ::_exit(kNoFilter);
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// What the oracle saw of the first page of the object, in a child process.
[[nodiscard]] PageFate ask_the_oracle(mmap::ForeignDescriptor descriptor) {
    ::fixy::process::Child child = ::fixy::process::spawn_child(kTest, [descriptor]() noexcept {
                                       discard_and_read(descriptor);
                                   }).expect("the test starts a child");
    ::fixy::process::ChildReport const report =
        ::fixy::process::wait_within(std::move(child), kChildLimit).expect("the child ends");
    if (report.is_signaled_by(kBusSignal)) return PageFate::is_lost;
    if (report.is_exit_with(kNotDiscarded)) return PageFate::is_not_discarded;
    CRUCIBLE_TEST_ASSERT(kTest, report.is_exit_with(kCameBack));
    return PageFate::comes_back;
}

// The first word of a shared mapping, or no value when it holds no whole word.
template <class Mapping>
[[nodiscard]] ::fixy::Option<std::uint64_t> first_word_of(Mapping const& mapping) {
    for (auto const words : mapping.template view_at<std::uint64_t const, 1>(0))
        return ::fixy::some(words.load_acquire(::fixy::index<0>()));
    return ::fixy::none;
}

// Ends the mapping and the exclusive of a refusal, and checks that the mapping
// is there and that it still reads.
template <class Refusal>
void end_refusal(Refusal&& refusal) {
    auto [mapping, exclusive] = std::move(refusal).parts();
    ::foundation::linear::drop(std::move(exclusive));
    auto const region = std::move(mapping).expect("the refusal gives the mapping back").consume();
    CRUCIBLE_TEST_ASSERT(kTest, first_word_of(region).is_some());
}

// Maps length bytes of the object, and expects the refusal.
template <class Brand>
void expect_refusal(perm::Permission<PageRegion, Brand>&& owner, mmap::ForeignDescriptor descriptor,
                    ::fixy::ByteCount length, mmap::ForeignMapFault cause, Errno error) {
    auto refused =
        mmap::mint_locked_mmap_foreign<ReadWrite, Shared>(kCtx, kAlloc, std::move(owner), descriptor, length);
    CRUCIBLE_TEST_ASSERT(kTest, refused.is_err());
    auto refusal = std::move(refused).err().expect("the mint refuses the object");
    CRUCIBLE_TEST_ASSERT(kTest, refusal.cause() == cause);
    CRUCIBLE_TEST_ASSERT(kTest, refusal.error() == error);
    end_refusal(std::move(refusal));
}

// Maps one page of the object.  The mint passes an object whose page comes
// back.  It refuses a lost page with efault, and a page that the kernel does
// not discard with einval.
template <class Brand>
void expect_the_verdict_of_the_oracle(perm::Permission<PageRegion, Brand>&& owner, mmap::ForeignDescriptor descriptor) {
    PageFate const fate = ask_the_oracle(descriptor);
    mmap::mint_locked_mmap_foreign<ReadOnly, Shared>(kCtx, kAlloc, std::move(owner), descriptor, kPage)
        .match(
            [fate](auto locked) noexcept {
                CRUCIBLE_TEST_ASSERT(kTest, fate == PageFate::comes_back);
                locked = ::fixy::with_mut(std::move(locked), [](auto& object) noexcept {
                    CRUCIBLE_TEST_ASSERT(kTest, object.template advise_release_aware<DontNeed>(kCtx).is_ok());
                });
            },
            [fate](auto refusal) noexcept {
                bool const is_lost = fate == PageFate::is_lost
                                  && refusal.cause() == mmap::ForeignMapFault::first_page_lost
                                  && refusal.error() == Errno::efault;
                bool const is_not_discarded = fate == PageFate::is_not_discarded
                                           && refusal.cause() == mmap::ForeignMapFault::no_discard
                                           && refusal.error() == Errno::einval;
                CRUCIBLE_TEST_ASSERT(kTest, is_lost || is_not_discarded);
                end_refusal(std::move(refusal));
            });
}

// The test passes for shared memory.  The map starts at its offset, and the
// word that a discard drops comes back.
void shared_memory_passes_the_test() {
    auto memory = ::fixy::fs::mint_sealed_memory(kCtx, ::fixy::fs::FileOffset{std::int64_t{3 * kPageBytes}})
                      .expect("sealed memory of three pages")
                      .consume();
    CRUCIBLE_TEST_ASSERT(kTest, ask_the_oracle(foreign_of(memory)) == PageFate::comes_back);
    auto locked = mmap::mint_locked_mmap_foreign<ReadWrite, Shared>(
                      kCtx, kAlloc, perm::mint_permission_root<PageRegion>(), foreign_of(memory),
                      ::fixy::ByteCount{2 * kPageBytes}, ::fixy::fs::FileOffset{std::int64_t{kPageBytes}})
                      .expect("a shared mapping of the last two pages of the memory");
    locked = ::fixy::with_mut(std::move(locked), [](auto& object) noexcept {
        ::fixy::Option<bool> const is_written = object.with_write(kCtx, [](auto& region) noexcept {
            bool is_stored = false;
            for (auto const words : region.template view_at<std::uint64_t, 1>(::fixy::ByteCount{0})) {
                words.store_release(::fixy::index<0>(), kWord);
                is_stored = true;
            }
            return is_stored;
        });
        CRUCIBLE_TEST_ASSERT(kTest, is_written == ::fixy::some(true));
        CRUCIBLE_TEST_ASSERT(kTest, object.template advise_release_aware<DontNeed>(kCtx).is_ok());
        ::fixy::Option<std::uint64_t> const read = object.with_read(kCtx, [](auto const& region) noexcept {
            std::uint64_t value = 0;
            for (auto const words : region.template view_at<std::uint64_t const, 1>(::fixy::ByteCount{0}))
                value = words.load_acquire(::fixy::index<0>());
            return value;
        });
        CRUCIBLE_TEST_ASSERT(kTest, read == ::fixy::some(kWord));
    });

    // The word is in the second page of the memory, and not in the first.
    auto whole = mmap::mint_locked_mmap<ReadWrite, Shared>(kCtx, kAlloc, perm::mint_permission_root<PageRegion>(),
                                                           memory.ref(), ::fixy::ByteCount{3 * kPageBytes})
                     .expect("a shared mapping of the memory");
    whole = ::fixy::with_mut(std::move(whole), [](auto& object) noexcept {
        ::fixy::Option<bool> const is_in_the_second_page = object.with_read(kCtx, [](auto const& region) noexcept {
            std::uint64_t first = kWord;
            std::uint64_t second = 0;
            for (auto const words : region.template view_at<std::uint64_t const, 1>(::fixy::ByteCount{0}))
                first = words.load_acquire(::fixy::index<0>());
            for (auto const words : region.template view_at<std::uint64_t const, 1>(kPage))
                second = words.load_acquire(::fixy::index<0>());
            return first == 0 && second == kWord;
        });
        CRUCIBLE_TEST_ASSERT(kTest, is_in_the_second_page == ::fixy::some(true));
    });
}

// Memory of one page that the caller maps as two pages: the first page comes
// back, and the last page does not exist.
void short_memory_loses_its_last_page() {
    auto memory = ::fixy::fs::mint_sealed_memory(kCtx, ::fixy::fs::FileOffset{std::int64_t{kPageBytes}})
                      .expect("sealed memory of one page")
                      .consume();
    CRUCIBLE_TEST_ASSERT(kTest, ask_the_oracle(foreign_of(memory)) == PageFate::comes_back);
    expect_refusal(perm::mint_permission_root<PageRegion>(), foreign_of(memory), ::fixy::ByteCount{2 * kPageBytes},
                   mmap::ForeignMapFault::last_page_lost, Errno::efault);
}

// A negative descriptor gives no mapping.  The exclusive of the refusal maps
// the next object.
void a_negative_descriptor_gives_the_exclusive_back() {
    auto refused = mmap::mint_locked_mmap_foreign<ReadWrite, Shared>(
        kCtx, kAlloc, perm::mint_permission_root<PageRegion>(), mmap::ForeignDescriptor{-1}, kPage);
    CRUCIBLE_TEST_ASSERT(kTest, refused.is_err());
    auto refusal = std::move(refused).err().expect("a negative descriptor names no object");
    CRUCIBLE_TEST_ASSERT(kTest, refusal.cause() == mmap::ForeignMapFault::map_refused);
    CRUCIBLE_TEST_ASSERT(kTest, refusal.error() == Errno::ebadf);
    auto [mapping, exclusive] = std::move(refusal).parts();
    bool const has_mapping = std::move(mapping).match(
        [](auto held) noexcept {
            drop(std::move(held));
            return true;
        },
        []() noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, !has_mapping);

    auto memory = ::fixy::fs::mint_sealed_memory(kCtx, ::fixy::fs::FileOffset{std::int64_t{kPageBytes}})
                      .expect("sealed memory of one page")
                      .consume();
    auto locked =
        mmap::mint_locked_mmap_foreign<ReadWrite, Shared>(kCtx, kAlloc, std::move(exclusive), foreign_of(memory), kPage)
            .expect("the exclusive of the refusal maps the memory");
    locked = ::fixy::with_mut(std::move(locked), [](auto& object) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, object.template advise_release_aware<DontNeed>(kCtx).is_ok());
    });
}

// The mint of shared memory in a child process whose kernel gives the answer to
// the advice.  The mint refuses the memory with the error, and it does not skip
// the test.
void a_kernel_with_no_advice_refuses_the_object(AdviceAnswer const& given, Errno error) {
    ::fixy::process::Child child =
        ::fixy::process::spawn_child(kTest, [given, error]() noexcept {
            filter_advice(given);
            auto memory = ::fixy::fs::mint_sealed_memory(kCtx, ::fixy::fs::FileOffset{std::int64_t{kPageBytes}})
                              .expect("sealed memory of one page")
                              .consume();
            expect_refusal(perm::mint_permission_root<PageRegion>(), foreign_of(memory), kPage,
                           mmap::ForeignMapFault::no_populate_read, error);
        }).expect("the test starts a child");
    ::fixy::process::ChildReport const report =
        ::fixy::process::wait_within(std::move(child), kChildLimit).expect("the child ends");
    if (report.is_exit_with(kNoFilter)) {
        ::fixy::report(::fixy::Sink::Err, "[skipped] the host refuses a filter of the system calls of a child\n");
        return;
    }
    CRUCIBLE_TEST_ASSERT(kTest, report.is_exit_with(0));
}

void check_an_io_uring_ring() {
    using Engine = ::fixy::atom::io::engine<::fixy::io::engine::IoUring>;
    using Sq8 = ::fixy::atom::io::sq_entries<8>;
    using Cq16 = ::fixy::atom::io::cq_entries<16>;
    ::fixy::io::mint_io_uring_ring<Engine, Sq8, Cq16>(kCtx).match(
        [](::fixy::Linear<::fixy::io::IoUringRing> made) noexcept {
            ::fixy::io::IoUringRing const ring = std::move(made).consume();
            expect_the_verdict_of_the_oracle(perm::mint_permission_root<PageRegion>(),
                                             mmap::ForeignDescriptor{ring.ring_fd()});
        },
        [](Errno error) noexcept {
            ::fixy::report(::fixy::Sink::Err, "[skipped] io_uring_setup is unavailable here (errno {})\n",
                           ::foundation::os::error_number(error));
        });
}

void check_a_bpf_array() {
    ::fixy::Option<mmap::ForeignDescriptor> const made = make_bpf_array();
    if (made.is_none()) {
        ::fixy::report(::fixy::Sink::Err, "[skipped] the host refuses the map of a BPF array to this process\n");
    }
    for (mmap::ForeignDescriptor const descriptor : made)
        expect_the_verdict_of_the_oracle(perm::mint_permission_root<PageRegion>(), descriptor);
}

void check_a_perf_ring() {
    ::fixy::perf::Event const migrations = ::fixy::perf::software_event(::fixy::perf::SoftwareEvent::cpu_migrations);
    ::fixy::perf::mint_perf_event(kCtx, migrations, 0, ::fixy::process::current_thread(kCtx))
        .match(
            [](::fixy::Linear<::fixy::perf::PerfEventHandle> opened) noexcept {
                ::fixy::perf::PerfEventHandle const event = std::move(opened).consume();
                expect_the_verdict_of_the_oracle(perm::mint_permission_root<PageRegion>(), foreign_of(event));
            },
            [](Errno error) noexcept {
                ::fixy::report(::fixy::Sink::Err, "[skipped] perf_event_open is unavailable here (errno {})\n",
                               ::foundation::os::error_number(error));
            });
}

// True when the kernel of the host has the advice of the test.  A kernel that
// does not have it refuses shared memory with the error of its kind: eopnotsupp
// for an emulator that gives success for each advice, and einval for a release
// that lacks the advice.
[[nodiscard]] bool has_kernel_the_advice() {
    auto memory = ::fixy::fs::mint_sealed_memory(kCtx, ::fixy::fs::FileOffset{std::int64_t{kPageBytes}})
                      .expect("sealed memory of one page")
                      .consume();
    return mmap::mint_locked_mmap_foreign<ReadWrite, Shared>(kCtx, kAlloc, perm::mint_permission_root<PageRegion>(),
                                                             foreign_of(memory), kPage)
        .match([](auto /*locked*/) noexcept { return true; },
               [](auto refusal) noexcept {
                   namespace os = ::foundation::os;
                   bool const is_old_release =
                       os::judge_flag(os::KernelFlag::advise_populate_read, os::running_kernel())
                       == os::FlagVerdict::fallback;
                   bool const is_expected =
                       refusal.error() == Errno::eopnotsupp || (is_old_release && refusal.error() == Errno::einval);
                   CRUCIBLE_TEST_ASSERT(kTest, refusal.cause() == mmap::ForeignMapFault::no_populate_read);
                   CRUCIBLE_TEST_ASSERT(kTest, is_expected);
                   end_refusal(std::move(refusal));
                   return false;
               });
}

}  // namespace

int main() {
    if (!has_kernel_the_advice()) {
        ::fixy::check::pass(kTest, "test_os_locked_mmap_foreign: [skipped] this kernel has no advice for the test\n");
        return 0;
    }
    shared_memory_passes_the_test();
    short_memory_loses_its_last_page();
    a_negative_descriptor_gives_the_exclusive_back();
    // A kernel below 5.14 has no MADV_POPULATE_READ, and it gives einval.
    a_kernel_with_no_advice_refuses_the_object(
        AdviceAnswer{.advice = ::foundation::os::kAdvisePopulateRead, .answer = ::fixy::some(Errno::einval)},
        Errno::einval);
    // An emulator gives success for each advice, also for one that does not
    // exist.
    a_kernel_with_no_advice_refuses_the_object(AdviceAnswer{.advice = -1, .answer = ::fixy::none}, Errno::eopnotsupp);
    check_an_io_uring_ring();
    check_a_bpf_array();
    check_a_perf_ring();
    ::fixy::check::pass(kTest, "test_os_locked_mmap_foreign: PASSED\n");
    return 0;
}
