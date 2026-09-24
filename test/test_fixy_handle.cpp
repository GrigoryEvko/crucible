// Including the umbrella header is itself part of the claim.  The
// sentinels it carries are never compiled under the project warning
// flags until some translation unit pulls it in.

#include <crucible/fixy/Handle.h>

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace safe = ::crucible::safety;
namespace fhand = ::crucible::fixy::handle;

namespace test_fixy_handle {
struct ProbeT {};
struct ProbeProto {};
struct ProbeResource {};
}  // namespace test_fixy_handle

namespace th = test_fixy_handle;

static_assert(std::is_same_v<fhand::Fd, safe::Fd>);
static_assert(std::is_same_v<fhand::FileHandle, safe::FileHandle>);
static_assert(std::is_same_v<fhand::Once, safe::Once>);
static_assert(std::is_same_v<fhand::Lazy<th::ProbeT>, safe::Lazy<th::ProbeT>>);
static_assert(std::is_same_v<fhand::SetOnce<th::ProbeT>, safe::SetOnce<th::ProbeT>>);
static_assert(std::is_same_v<fhand::OneShotFlag, safe::OneShotFlag>);
static_assert(std::is_same_v<fhand::PublishOnce<th::ProbeT>, safe::PublishOnce<th::ProbeT>>);
static_assert(std::is_same_v<fhand::PublishSlot<th::ProbeT>, safe::PublishSlot<th::ProbeT>>);
static_assert(std::is_same_v<fhand::LazyEstablishedChannel<th::ProbeProto, th::ProbeResource>,
                             safe::LazyEstablishedChannel<th::ProbeProto, th::ProbeResource>>);

static_assert(std::is_same_v<fhand::AlignedBuffer<th::ProbeT>, safe::AlignedBuffer<th::ProbeT>>);
static_assert(std::is_same_v<fhand::PublishCommitCell<th::ProbeT, th::ProbeProto>,
                             safe::PublishCommitCell<th::ProbeT, th::ProbeProto>>);

// The structural contracts below are stated again here at the alias
// path, not only at the substrate.  A weakening that the substrate's own
// test misses still reddens one of these.
static_assert(alignof(fhand::PublishCommitCell<th::ProbeT, th::ProbeProto>) == 64,
              "fixy::handle::PublishCommitCell must be alignas(64) — without it the "
              "foreground acquire-load and the background release-store share one "
              "cache line and contend on every publish.");
static_assert(!std::is_move_constructible_v<fhand::PublishCommitCell<th::ProbeT, th::ProbeProto>>,
              "fixy::handle::PublishCommitCell must not be move-constructible "
              "— channel identity is the cell's atomic storage address.");
static_assert(!std::is_copy_constructible_v<fhand::PublishCommitCell<th::ProbeT, th::ProbeProto>>,
              "fixy::handle::PublishCommitCell must not be copy-constructible "
              "— two cells claiming one channel identity would break the "
              "single-writer BorrowSafe invariant.");
static_assert(std::is_trivially_destructible_v<fhand::PublishCommitCell<th::ProbeT, th::ProbeProto>>,
              "fixy::handle::PublishCommitCell must be trivially destructible "
              "— a non-trivial dtor means someone added a managed resource "
              "without updating the Pinned discipline.");

static_assert(std::is_same_v<decltype(&fhand::open_read), decltype(&safe::open_read)>);
static_assert(std::is_same_v<decltype(&fhand::open_write_truncate), decltype(&safe::open_write_truncate)>);

static_assert(std::is_same_v<fhand::HugePageBuffer<th::ProbeT>, safe::HugePageBuffer<th::ProbeT>>);
static_assert(fhand::HugePageBuffer<th::ProbeT>::huge_page_bytes == ::crucible::warden::kHugePageBytes,
              "fixy::handle::HugePageBuffer<T>::huge_page_bytes must equal "
              "warden::kHugePageBytes — madvise(MADV_HUGEPAGE) requires "
              "the allocation to be aligned to the kernel huge-page boundary.");

// The write-side fixture sits at namespace scope so its identity is
// stable as a template argument.  The cell instantiated with it grants
// it friendship, and the call below compiles only if that friendship
// survives the alias path.  A shim wrapper inserted in the middle that
// forgot to re-emit the friend declaration would fail here.
namespace test_fixy_handle {
struct PubCommitWriteAuth {
    template <typename Cell>
    static auto bump_once(Cell& cell) noexcept {
        return cell.bump();
    }
};
struct PubCommitTag {};
}  // namespace test_fixy_handle

static_assert(std::is_same_v<fhand::OwnedFile, safe::OwnedFile>);

static_assert(!std::is_copy_constructible_v<fhand::OwnedFile>,
              "fixy::handle::OwnedFile must not be copy-constructible — copying "
              "a FILE* would double-close at destruction (LeakSafe + MemSafe).");
static_assert(!std::is_copy_assignable_v<fhand::OwnedFile>,
              "fixy::handle::OwnedFile must not be copy-assignable — copy-assign "
              "leaks the LHS's existing FILE* and double-closes the RHS's.");
static_assert(std::is_nothrow_move_constructible_v<fhand::OwnedFile>,
              "fixy::handle::OwnedFile must have a nothrow move constructor — "
              "it appears inside std::expected<>-shaped return channels that "
              "demand noexcept move for the strong exception guarantee.");
static_assert(std::is_nothrow_move_assignable_v<fhand::OwnedFile>,
              "fixy::handle::OwnedFile must have a nothrow move-assign for "
              "symmetric strong-guarantee handling.");
static_assert(std::is_nothrow_destructible_v<fhand::OwnedFile>,
              "fixy::handle::OwnedFile destructor must be noexcept — it runs on "
              "every early-return path and stdio errors must not propagate.");
static_assert(sizeof(fhand::OwnedFile) == sizeof(std::FILE*),
              "fixy::handle::OwnedFile must be sizeof(std::FILE*) — any inflation is "
              "paid again at every stack frame that holds one.");

// The exact pin on the alias count lives beside the constant itself.
// This is only the floor, which catches the other direction: an alias
// removed without anyone updating the pin.  Growth is silent here.
static_assert(::crucible::fixy::handle::self_test::handle_alias_cardinality >= 13,
              "floor: fixy::handle:: alias cardinality dropped below 13 — an alias "
              "was removed without updating both the exact pin colocated with the "
              "constant and this floor.");

int main() {
    {
        // The default handle owns no descriptor.  This block exists to
        // construct and destroy one through the alias, nothing more.
        fhand::FileHandle fh;
        (void)fh;
    }
    {
        fhand::OneShotFlag flag;
        flag.signal();
        bool seen = flag.peek();
        (void)seen;
    }
    {
        // The allocation is an alignment request, not a request for
        // huge-page backing, so it succeeds on a host with no huge pages
        // configured.  The madvise hint is applied later at the call site
        // and falls back to small pages silently.
        fhand::HugePageBuffer<int> buf = fhand::HugePageBuffer<int>::allocate(1);
        if (buf.data() == nullptr) return 1;
        if (buf.size() != 1) return 2;
        if (buf.bytes() < fhand::HugePageBuffer<int>::huge_page_bytes) return 3;
        // The static_assert above proves the constant is right.  This
        // proves the allocation actually honours it.  The cast goes
        // through bit_cast because the project bans reinterpret_cast.
        const auto addr = std::bit_cast<std::uintptr_t>(buf.data());
        if ((addr & (fhand::HugePageBuffer<int>::huge_page_bytes - 1)) != 0) return 4;
        // Any non-zero value works as a write-then-read witness.  The
        // habitual 0xDEADBEEF does not, because it does not fit the
        // positive half of a signed int and the sign-conversion warning
        // is an error here.
        buf[0] = 0x12345678;
        if (buf[0] != 0x12345678) return 5;
    }
    {
        using Cell = fhand::PublishCommitCell<th::PubCommitTag, th::PubCommitWriteAuth>;
        Cell cell;
        if (cell.load_acquire() != 0) return 10;
        if (cell.peek_relaxed() != 0) return 11;
        if (cell.get() != 0) return 12;
        if (cell.load(std::memory_order_relaxed) != 0) return 13;
        // bump returns the counter value from before the advance, the way
        // a ticket dispenser does.
        const auto previous = th::PubCommitWriteAuth::bump_once(cell);
        if (previous != 0) return 14;
        if (cell.load_acquire() != 1) return 15;
        if (cell.peek_relaxed() != 1) return 16;
        // A second bump separates a counter from a one-shot flag.
        if (th::PubCommitWriteAuth::bump_once(cell) != 1) return 17;
        if (cell.load_acquire() != 2) return 18;
    }
    {
        // A temporary file deletes itself and collides with no path, so
        // the round-trip stays deterministic and leaves nothing behind.
        std::FILE* raw = std::tmpfile();
        if (raw == nullptr) {
            // A sandbox with no writable temporary directory refuses.
            // That is a skip, not a failure: the static_asserts above
            // already prove the alias identity, and this block only adds
            // a runtime exercise on top.
            return 0;
        }
        fhand::OwnedFile a{raw};
        if (!a.is_open()) return 20;
        if (!a) return 21;
        if (a.get() != raw) return 22;
        constexpr int sentinel = 0x42;
        if (std::fputc(sentinel, a.get()) != sentinel) return 23;
        if (std::fflush(a.get()) != 0) return 24;
        std::rewind(a.get());
        if (std::fgetc(a.get()) != sentinel) return 25;
        fhand::OwnedFile b{std::move(a)};
        if (a.is_open()) return 26;
        if (!b.is_open()) return 27;
        if (b.get() != raw) return 28;
        // Nothing asserts that the destructor closed the file, because
        // nothing can from inside the program.  The leak sanitizer in the
        // default preset is the witness.
    }
    return 0;
}
