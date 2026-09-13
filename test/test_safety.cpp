#include <crucible/safety/Safety.h>
#include <crucible/permissions/Permissions.h>
#include <crucible/handles/Handles.h>
#include <crucible/sessions/Sessions.h>
#include <crucible/bridges/Bridges.h>

#include "test_assert.h"

#include <atomic>
#include <contracts>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <inplace_vector>
#include <latch>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

// The contract-violation handler comes from the library this test links
// against, so this translation unit defines none.

using namespace crucible::safety;

struct FakeFd {
    int value;
    FakeFd(int v) : value{v} {}
};

static void test_linear() {
    static_assert(sizeof(Linear<int>) == sizeof(int), "Linear<T> must be zero-cost in layout");
    static_assert(sizeof(Linear<FakeFd>) == sizeof(FakeFd), "Linear<T> must be zero-cost in layout");

    Linear<FakeFd> a{FakeFd{42}};
    // Copy is deleted, so only the move below compiles.
    Linear<FakeFd> b = std::move(a);
    assert(b.peek().value == 42);

    FakeFd raw = std::move(b).consume();
    assert(raw.value == 42);
    std::printf("  Linear:         ok\n");
}

using PositiveInt = Refined<positive, int>;
using NonNullPtr = Refined<non_null, int*>;

static void test_refined() {
    static_assert(sizeof(PositiveInt) == sizeof(int), "Refined<P, T> must be zero-cost in layout");

    int stack_var = 7;
    PositiveInt p{5};
    NonNullPtr q{&stack_var};

    assert(p.value() == 5);
    assert(*q.value() == 7);
    int extracted = std::move(p).into();
    assert(extracted == 5);

    // Linear outside Refined is the usual ordering for a resource handle: an
    // owned move-only value that already satisfies its invariant.
    {
        // A real call site names this predicate next to the resource type.
        // It is inline here because the example is self-contained.
        constexpr auto is_open_fd = [](int fd) constexpr noexcept { return fd >= 0; };

        static_assert(sizeof(LinearRefined<is_open_fd, int>) == sizeof(int),
                      "LinearRefined<P, T> must collapse to sizeof(T)");

        LinearRefined<is_open_fd, int> handle{Refined<is_open_fd, int>{42}};
        assert(handle.peek().value() == 42);

        int raw_fd = std::move(handle).consume().into();
        assert(raw_fd == 42);
    }

    // The reverse nesting is the rare case: the predicate now speaks about
    // the Linear wrapper state and receives it by const reference.
    {
        struct Sentinel {
            int token{0};
        };
        constexpr auto token_is_valid = [](const Linear<Sentinel>& l) constexpr noexcept { return l.peek().token > 0; };
        RefinedLinear<token_is_valid, Sentinel> rl{Linear<Sentinel>{Sentinel{7}}};
        assert(rl.value().peek().token == 7);
    }

    std::printf("  Refined:        ok\n");
}

struct CardNumber {
    std::uint64_t digits;
    std::size_t size() const noexcept { return 16; }
};

static void test_secret() {
    static_assert(sizeof(Secret<CardNumber>) == sizeof(CardNumber), "Secret<T> must be zero-cost in layout");

    Secret<CardNumber> card{CardNumber{4242424242424242ULL}};
    // Copy is deleted, so the card can only be moved out of.

    auto hashed = std::move(card).transform(
        [](CardNumber c) -> std::uint64_t { return c.digits ^ std::uint64_t{0xDEADBEEFCAFEBABEULL}; });
    static_assert(std::is_same_v<decltype(hashed), Secret<std::uint64_t>>);

    std::uint64_t raw = std::move(hashed).declassify<secret_policy::HashForCompare>();
    (void)raw;
    std::printf("  Secret:         ok\n");
}

struct Config {
    int port;
};

static void apply_sanitized(Tagged<Config, source::Sanitized> cfg) { assert(cfg.value().port == 8080); }

static void test_tagged() {
    static_assert(sizeof(Tagged<Config, source::Sanitized>) == sizeof(Config),
                  "Tagged<T, Tag> must be zero-cost in layout");

    Tagged<Config, source::External> raw{Config{8080}};
    // Passing raw straight to apply_sanitized does not compile: the tags
    // differ, so the retag below is the only route in.
    auto sanitized = std::move(raw).retag<source::Sanitized>();
    apply_sanitized(std::move(sanitized));

    Tagged<int, trust::Verified> v{42};
    Tagged<int, access::RO> ro{99};
    Tagged<int, version::V<3>> vv{7};
    (void)v;
    (void)ro;
    (void)vv;
    std::printf("  Tagged:         ok\n");
}

struct FakePipe {
    int fd{0};
    int recorded_send{0};
    int recorded_recv{0};
};

struct Request {
    int id;
};
struct Response {
    int status;
};

static void test_session() {
    // The protocol combinators are brought in function-locally rather than at
    // file scope, so the rest of the file keeps its own names visible.
    using namespace crucible::safety::proto;

    auto s = mint_session_handle<Send<Request, Recv<Response, End>>>(FakePipe{});

    auto s1 = std::move(s).send(Request{7}, [](FakePipe& p, Request&& req) noexcept { p.recorded_send = req.id; });
    // A second send does not compile: the protocol head is now a receive.

    auto [resp, s2] = std::move(s1).recv([](FakePipe& p) noexcept -> Response {
        p.recorded_recv = 1;
        return Response{200};
    });
    assert(resp.status == 200);

    auto pipe = std::move(s2).close();
    assert(pipe.recorded_recv == 1);
    assert(pipe.recorded_send == 7);
    std::printf("  Session:        ok\n");
}

namespace {
namespace conn {
struct Disconnected {};
struct Connecting {
    std::string host;
    std::uint32_t attempt;
};
struct Connected {
    int sock_fd;
};

[[nodiscard]] Machine<Connecting> connect(Machine<Disconnected>&& m, std::string host) {
    (void)std::move(m).extract();
    return Machine<Connecting>{Connecting{std::move(host), 0}};
}

[[nodiscard]] Machine<Connected> established(Machine<Connecting>&& m, int fd) {
    (void)std::move(m).extract();
    return Machine<Connected>{Connected{fd}};
}

[[nodiscard]] Machine<Disconnected> close(Machine<Connected>&& m) {
    (void)std::move(m).extract();
    return Machine<Disconnected>{Disconnected{}};
}
}  // namespace conn
}  // namespace

static void test_machine() {
    static_assert(sizeof(Machine<conn::Disconnected>) == sizeof(conn::Disconnected),
                  "Machine<S> must be zero-cost in layout");
    static_assert(sizeof(Machine<conn::Connected>) == sizeof(conn::Connected),
                  "Machine<S> must be zero-cost in layout");

    Machine<conn::Disconnected> m0{conn::Disconnected{}};
    auto m1 = conn::connect(std::move(m0), "example.com");
    assert(m1.data().host == "example.com");
    auto m2 = conn::established(std::move(m1), 42);
    assert(m2.data().sock_fd == 42);
    // Connecting again from the connected state does not compile.
    auto m3 = conn::close(std::move(m2));
    (void)m3;
    std::printf("  Machine:        ok\n");
}

static void test_checked() {
    auto a = checked_add<std::uint32_t>(0xFFFFFFFFu, 1u);
    assert(!a.has_value());

    auto b = checked_add<std::uint32_t>(100u, 200u);
    assert(b && *b == 300u);

    auto w = wrapping_add<std::uint8_t>(250u, 10u);
    assert(w == 4u);  // 260 wrapped into eight bits

    auto t = trapping_add<std::uint32_t>(1u, 2u);
    assert(t == 3u);
    std::printf("  Checked:        ok\n");
}

static void test_mutation() {
    AppendOnly<int> log;
    log.append(1);
    log.append(2);
    log.append(3);
    // No erase is exposed, so the only way out of the log is reading it.
    assert(log.size() == 3);
    assert(log[2] == 3);

    Monotonic<std::uint64_t> epoch{0};
    epoch.advance(1);
    epoch.advance(5);
    // A backward advance would fire the monotonicity contract.
    assert(epoch.get() == 5);
    assert(!epoch.try_advance(2));
    assert(epoch.try_advance(10));
    assert(epoch.get() == 10);

    // The default key function is the identity and the default comparison is
    // less, so the appended values themselves must not decrease.
    static_assert(sizeof(OrderedAppendOnly<std::uint64_t>) == sizeof(AppendOnly<std::uint64_t>),
                  "stateless KeyFn/Cmp must collapse to zero layout cost");
    {
        OrderedAppendOnly<std::uint64_t> timeline;
        timeline.append(0ULL);
        timeline.append(1ULL);
        timeline.append(1ULL);  // equal keys are non-decreasing
        timeline.append(2ULL);
        timeline.emplace(5ULL);  // emplace must take the ordering path too
        // Appending 3 here would violate the ordering contract.
        assert(timeline.size() == 5);
        assert(timeline.back() == 5);
        assert(timeline[0] == 0);
    }
    // With a projected key the ordering runs on one field, so the payload is
    // free to move in any direction.
    {
        struct Entry {
            std::uint64_t step;
            int payload;
        };
        struct ByStep {
            constexpr std::uint64_t operator()(const Entry& e) const noexcept { return e.step; }
        };
        OrderedAppendOnly<Entry, ByStep> log_by_step;
        log_by_step.append({.step = 10, .payload = 100});
        log_by_step.append({.step = 10, .payload = 101});  // duplicate step OK
        log_by_step.append({.step = 20, .payload = 200});
        // A step of 15 appended here would violate the ordering contract.
        assert(log_by_step.size() == 3);
        assert(log_by_step.back().step == 20);
    }

    static_assert(sizeof(BoundedMonotonic<std::uint32_t, 1024U>) == sizeof(std::uint32_t),
                  "BoundedMonotonic must collapse to underlying T");
    {
        BoundedMonotonic<std::uint32_t, 10U> counter{0U};
        assert(counter.get() == 0);
        counter.advance(1U);
        counter.advance(5U);
        counter.advance(10U);  // the bound itself is admissible
        // 11 would exceed the bound; 5 would go backwards. Either fires a
        // precondition.
        assert(counter.get() == 10);
        assert((BoundedMonotonic<std::uint32_t, 10U>::max() == 10U));

        BoundedMonotonic<std::uint32_t, 3U> bumper{0U};
        bumper.bump();
        bumper.bump();
        bumper.bump();
        // A fourth bump would fire the bound precondition.
        assert(bumper.get() == 3U);

        // try_advance rejects rather than firing a contract. The two
        // rejections below have different causes.
        BoundedMonotonic<std::uint32_t, 5U> cnt{2U};
        assert(!cnt.try_advance(6U));  // over the bound
        assert(cnt.get() == 2U);
        assert(!cnt.try_advance(1U));  // backwards
        assert(cnt.try_advance(4U));
        assert(cnt.get() == 4U);
    }

    // Single-threaded behaviour must match the non-atomic Monotonic.
    {
        AtomicMonotonic<std::uint64_t> step{0};
        assert(step.get() == 0ULL);
        assert(step.try_advance(1ULL));
        assert(step.get() == 1ULL);
        assert(!step.try_advance(1ULL));  // equal
        assert(!step.try_advance(0ULL));  // backwards
        assert(step.try_advance(7ULL));
        assert(step.get() == 7ULL);
        assert(!step.try_advance(5ULL));  // backwards
        assert(step.get() == 7ULL);

        step.advance(8ULL);
        assert(step.get() == 8ULL);
    }

    // Under std::greater the whole direction inverts: advancing walks the
    // value down, and a larger value counts as backwards.
    {
        AtomicMonotonic<std::uint64_t, std::greater<std::uint64_t>> floor{100ULL};
        assert(floor.get() == 100ULL);
        assert(floor.try_advance(50ULL));
        assert(floor.get() == 50ULL);
        assert(!floor.try_advance(50ULL));  // equal
        assert(!floor.try_advance(60ULL));  // backwards
        assert(floor.try_advance(0ULL));
        assert(floor.get() == 0ULL);
    }

    {
        MaxObserved<std::uint32_t> high_water{0U};
        assert(high_water.try_advance(50U));
        assert(high_water.try_advance(100U));
        assert(!high_water.try_advance(75U));
        assert(high_water.get() == 100U);
    }

    // bump returns the value it replaced, which is the slot index the caller
    // has just claimed. A producer reserves ring slots this way.
    {
        AtomicMonotonic<std::uint64_t> ring_head{0};
        assert(ring_head.bump() == 0ULL);
        assert(ring_head.get() == 1ULL);
        assert(ring_head.bump() == 1ULL);
        assert(ring_head.bump_by(5ULL) == 2ULL);  // claims slots 2 through 6
        assert(ring_head.get() == 7ULL);
        assert(ring_head.bump_by(0ULL) == 7ULL);
        assert(ring_head.get() == 7ULL);
    }

    // Under std::greater, bump walks the counter down instead.
    {
        AtomicMonotonic<std::uint64_t, std::greater<std::uint64_t>> ttl{1000ULL};
        assert(ttl.bump() == 1000ULL);
        assert(ttl.get() == 999ULL);
        assert(ttl.bump_by(99ULL) == 999ULL);
        assert(ttl.get() == 900ULL);
    }

    // A single producer owns the head, so it may read its own value relaxed
    // and publish the successor with a release store. get() is the acquire
    // load a reader on another thread would use.
    {
        AtomicMonotonic<std::uint64_t> spsc_head{0};
        const std::uint64_t h0 = spsc_head.peek_relaxed();
        assert(h0 == 0ULL);
        spsc_head.advance(h0 + 1);
        assert(spsc_head.peek_relaxed() == 1ULL);
        assert(spsc_head.get() == 1ULL);

        for (std::uint64_t i = 1; i < 16; ++i) {
            const std::uint64_t h = spsc_head.peek_relaxed();
            assert(h == i);
            spsc_head.advance(h + 1);
        }
        assert(spsc_head.get() == 16ULL);
    }

    // Reset moves the counter backwards, which advance forbids. The caller
    // carries the obligation that no other thread is writing at the time.
    {
        AtomicMonotonic<std::uint64_t> ring_head{0};
        ring_head.advance(1);
        ring_head.advance(2);
        ring_head.advance(100);
        assert(ring_head.get() == 100ULL);

        ring_head.reset_under_quiescence();
        assert(ring_head.get() == 0ULL);

        ring_head.advance(1);  // monotonic from the new baseline
        assert(ring_head.get() == 1ULL);

        ring_head.reset_under_quiescence(42ULL);
        assert(ring_head.get() == 42ULL);
    }

    // The default orderings are acquire on load and release on store. The
    // explicit-order overloads exist for callers that need seq_cst, and they
    // keep the monotonicity contract on the store side.
    {
        AtomicMonotonic<std::uint64_t> counter{0};

        assert(counter.load() == 0ULL);

        assert(counter.load(std::memory_order_seq_cst) == 0ULL);
        assert(counter.load(std::memory_order_relaxed) == 0ULL);

        counter.store(7ULL);
        assert(counter.get() == 7ULL);

        counter.store(42ULL, std::memory_order_seq_cst);
        assert(counter.get() == 42ULL);

        // A backward store breaks the same contract advance does. Only the
        // forward path is exercised here, because a firing contract aborts
        // the process.
    }

    // A failed compare-exchange writes the value it actually observed back
    // through `expected`, which is how the owner and a thief resolve their
    // race over the last element in a work-stealing deque.
    {
        AtomicMonotonic<std::uint64_t> top{5};

        std::uint64_t observed = 5ULL;
        bool ok = top.compare_exchange_advance(observed, 6ULL);
        assert(ok);
        assert(top.get() == 6ULL);

        observed = 5ULL;  // deliberately stale guess
        ok = top.compare_exchange_advance(observed, 7ULL);
        assert(!ok);
        assert(observed == 6ULL);

        // Retrying with the value just reported back succeeds.
        ok = top.compare_exchange_advance(observed, 7ULL);
        assert(ok);
        assert(top.get() == 7ULL);

        // The explicit-order form with seq_cst on success is what a
        // work-stealing pop needs.
        observed = 7ULL;
        ok = top.compare_exchange_advance(observed, 8ULL, std::memory_order_seq_cst, std::memory_order_relaxed);
        assert(ok);
        assert(top.get() == 8ULL);
    }

    // The weak form may fail spuriously, so the caller must loop on the
    // return value rather than treat one failure as contention.
    {
        AtomicMonotonic<std::uint64_t> seq{100};

        std::uint64_t observed = seq.get();
        std::uint64_t target;
        bool advanced = false;
        for (int retries = 0; retries < 16 && !advanced; ++retries) {
            target = observed + 1;
            advanced = seq.compare_exchange_advance_weak(observed, target);
        }
        assert(advanced);
        assert(seq.get() == 101ULL);
    }

    // A work-stealing pop needs a bare seq_cst fence between the owner's
    // bottom store and its load of top. The fence sits on the counter type so
    // that it reads next to the two calls it separates.
    {
        AtomicMonotonic<std::uint64_t> bottom{0};
        AtomicMonotonic<std::uint64_t> top{0};

        // One thread only. No race is exercised here, just the API shape.
        bottom.store(5ULL);
        AtomicMonotonic<std::uint64_t>::fence_seq_cst();
        const auto t = top.load(std::memory_order_relaxed);
        assert(t == 0ULL);
        assert(bottom.get() == 5ULL);
    }

    // Compare-exchange follows the same inverted direction under greater.
    {
        AtomicMonotonic<std::uint64_t, std::greater<std::uint64_t>> ttl{1000ULL};

        std::uint64_t observed = 1000ULL;
        bool ok = ttl.compare_exchange_advance(observed, 999ULL);
        assert(ok);
        assert(ttl.get() == 999ULL);

        observed = 1000ULL;  // deliberately stale guess
        ok = ttl.compare_exchange_advance(observed, 998ULL);
        assert(!ok);
        assert(observed == 999ULL);
    }

    // Each thread pushes its own exclusive range of values, so the final
    // value is the global maximum.
    //
    // The latch is a starting line. Without it the threads constructed early
    // in the loop finish thousands of calls before the later ones start, and
    // the contention this test exists to exercise never happens.
    {
        constexpr int kThreads = 4;
        constexpr std::uint64_t kPerThread = 4096;

        AtomicMonotonic<std::uint64_t> shared_high{0};
        std::atomic<std::uint64_t> total_advances{0};

        std::latch start_gate{kThreads + 1};

        {
            std::inplace_vector<std::jthread, kThreads> workers;
            for (int tid = 0; tid < kThreads; ++tid) {
                workers.emplace_back([tid, &shared_high, &total_advances, &start_gate] {
                    start_gate.arrive_and_wait();
                    std::uint64_t local_advances = 0;
                    const std::uint64_t base = static_cast<std::uint64_t>(tid) * kPerThread;
                    for (std::uint64_t v = 1; v <= kPerThread; ++v) {
                        if (shared_high.try_advance(base + v)) ++local_advances;
                    }
                    total_advances.fetch_add(local_advances, std::memory_order_relaxed);
                });
            }
            // The main thread counts for the extra slot in the latch and
            // arrives last, releasing every worker at once.
            start_gate.arrive_and_wait();
        }  // the jthreads join here

        const std::uint64_t expected_max = static_cast<std::uint64_t>(kThreads - 1) * kPerThread + kPerThread;
        assert(shared_high.get() == expected_max);
        // The thread holding the top range advances on every one of its own
        // steps, which sets the lower bound. No advance can happen twice, so
        // the total count is the upper bound.
        const std::uint64_t advances = total_advances.load(std::memory_order_relaxed);
        assert(advances >= kPerThread);
        assert(advances <= kThreads * kPerThread);
    }

    // nullptr is the unset sentinel here, which costs nothing. Wrapping the
    // pointer in an optional instead would add a tag byte and its padding.
    {
        static_assert(sizeof(WriteOnceNonNull<int*>) == sizeof(int*));
        static_assert(sizeof(WriteOnceNonNull<double*>) == sizeof(double*));
        static_assert(sizeof(WriteOnceNonNull<void*>) == sizeof(void*));

        // AppendOnly reads this trait to reject a redundant wrapping.
        static_assert(is_writeoncenonnull_v<WriteOnceNonNull<int*>>);
        static_assert(!is_writeoncenonnull_v<int*>);
        static_assert(!is_writeoncenonnull_v<WriteOnce<int*>>);

        int payload = 7;

        WriteOnceNonNull<int*> slot;
        assert(!slot.has_value());
        assert(!static_cast<bool>(slot));

        // Setting nullptr writes the sentinel, so it is refused and the slot
        // stays unset.
        assert(!slot.try_set(nullptr));
        assert(!slot.has_value());

        assert(slot.try_set(&payload));
        assert(slot.has_value());
        assert(static_cast<bool>(slot));
        assert(slot.get() == &payload);
        assert(*slot == 7);

        // A second try_set is refused even with a valid pointer.
        int other = 13;
        assert(!slot.try_set(&other));
        assert(slot.get() == &payload);

        struct Thing {
            int x;
        };
        Thing t{42};
        WriteOnceNonNull<Thing*> thing_slot;
        thing_slot.set(&t);
        assert(thing_slot->x == 42);

        // A void pointee has no dereference operators, so this path only
        // checks that the rest of the interface still compiles.
        int raw = 99;
        WriteOnceNonNull<void*> vslot;
        vslot.set(&raw);
        assert(vslot.has_value());
        assert(vslot.get() == &raw);
    }
    std::printf("  Mutation:       ok\n");
}

// Every macro here is given a true expression. A violation aborts, which
// would take the test binary with it, so the firing side is not exercised.
static void test_assertion_triad() {
    int x = 42;
    CRUCIBLE_ASSERT(x == 42);
    CRUCIBLE_DEBUG_ASSERT(x > 0);
    CRUCIBLE_INVARIANT(x > 0);

    // The answer depends on how the binary was started, so only the fact
    // that the probe links and returns a defined bool is checked.
    bool dbg = ::crucible::detail::is_debugger_present();
    (void)dbg;

    // The call traps only under a debugger, and the test runner attaches
    // none, so it is a no-op here.
    ::crucible::detail::breakpoint_if_debugging();

    std::printf("  AssertionTriad: ok (debugger_present=%s)\n", dbg ? "true" : "false");
}

static void test_file_handle() {
    static_assert(sizeof(FileHandle) == sizeof(int), "FileHandle must be a zero-cost int wrapper");
    static_assert(!std::is_copy_constructible_v<FileHandle>);
    static_assert(!std::is_copy_assignable_v<FileHandle>);
    static_assert(std::is_move_constructible_v<FileHandle>);
    static_assert(std::is_move_assignable_v<FileHandle>);

    char tmpl[] = "/tmp/crucible_fh_XXXXXX";
    int raw_fd = ::mkstemp(tmpl);
    assert(raw_fd >= 0);

    // The handle takes ownership of raw_fd, so nothing else may close it.
    const std::string path = tmpl;
    {
        FileHandle h{raw_fd};
        assert(h.is_open());
        assert(h.get() == raw_fd);

        // write_full is all or nothing: a short write is an error, not a
        // smaller success.
        const std::byte buf[] = {std::byte{'A'}, std::byte{'B'}, std::byte{'C'}, std::byte{'\n'}};
        const auto wrc = write_full(h, std::span<const std::byte>{buf, 4});
        assert(wrc.has_value());

        const auto sz = file_size(h);
        assert(sz.has_value());
        assert(*sz == 4);
    }  // the destructor closes raw_fd here

    {
        auto r_e = open_read(path.c_str());
        assert(r_e.has_value());
        FileHandle& r = *r_e;
        assert(r.is_open());
        std::byte rbuf[16]{};
        const auto n_e = read_full(r, std::span<std::byte>{rbuf, 16});
        assert(n_e.has_value());
        assert(*n_e == 4);
        assert(rbuf[0] == std::byte{'A'});
        assert(rbuf[3] == std::byte{'\n'});
    }

    {
        auto a_e = open_read(path.c_str());
        assert(a_e.has_value());
        FileHandle a = std::move(*a_e);
        assert(a.is_open());
        const int a_fd = a.get();
        FileHandle b = std::move(a);
        // The moved-from handle is left closed, so the descriptor has exactly
        // one owner and is closed once.
        assert(!a.is_open());
        assert(b.is_open());
        assert(b.get() == a_fd);
    }

    // Closing explicitly is the way a caller sees the return code, which the
    // destructor path discards.
    {
        auto c_e = open_read(path.c_str());
        assert(c_e.has_value());
        FileHandle c = std::move(*c_e);
        assert(c.is_open());
        const int rc = c.close_explicit();
        assert(rc == 0);
        assert(!c.is_open());
        // Closing a second time is a no-op rather than an error.
        assert(c.close_explicit() == 0);
    }

    // A failure to open carries the POSIX errno out through the error
    // channel, so the caller never receives an open-looking closed handle.
    {
        auto missing_e = open_read("/tmp/crucible_nonexistent_path_XXXXXX_a1_013");
        assert(!missing_e.has_value());
        assert(missing_e.error().value() == ENOENT);
        assert(missing_e.error().category() == std::system_category());
    }

    // A closed handle reports EBADF on the error channel. Returning a
    // negative errno inside an ssize_t would alias a legitimate byte count.
    {
        FileHandle closed{};
        assert(!closed.is_open());

        std::byte rbuf[1]{};
        const auto r_err = read_full(closed, std::span<std::byte>{rbuf, 1});
        assert(!r_err.has_value());
        assert(r_err.error().value() == EBADF);
        assert(r_err.error().category() == std::system_category());

        const std::byte wbuf[1] = {std::byte{0}};
        const auto w_err = write_full(closed, std::span<const std::byte>{wbuf, 1});
        assert(!w_err.has_value());
        assert(w_err.error().value() == EBADF);

        const auto sz_err = file_size(closed);
        assert(!sz_err.has_value());
        assert(sz_err.error().value() == EBADF);
    }

    ::unlink(path.c_str());
    std::printf("  FileHandle:     ok\n");
}

static void test_constant_time() {
    using namespace crucible::safety::ct;

    auto a = select<std::uint32_t>(1u, 0xAAAAu, 0xBBBBu);
    assert(a == 0xAAAAu);
    auto b = select<std::uint32_t>(0u, 0xAAAAu, 0xBBBBu);
    assert(b == 0xBBBBu);

    std::byte buf1[4] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    std::byte buf2[4] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    std::byte buf3[4] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{9}};
    // eq takes spans only. A pointer-and-length pair would let a caller pass
    // a null pointer with a non-zero length.
    assert(eq(std::span<const std::byte>{buf1}, std::span<const std::byte>{buf2}));
    assert(!eq(std::span<const std::byte>{buf1}, std::span<const std::byte>{buf3}));

    assert(less<std::uint32_t>(5u, 10u) == 1u);
    assert(less<std::uint32_t>(10u, 5u) == 0u);
    // is_zero is qualified because the file-scope using-directive also brings
    // in a safety predicate of that name.
    assert(ct::is_zero<std::uint32_t>(0u) == 1u);
    assert(ct::is_zero<std::uint32_t>(1u) == 0u);

    std::uint32_t x = 100u, y = 200u;
    cswap<std::uint32_t>(1u, x, y);
    assert(x == 200u && y == 100u);
    cswap<std::uint32_t>(0u, x, y);
    assert(x == 200u && y == 100u);
    std::printf("  ConstantTime:   ok\n");
}

namespace not_inherited_test {
struct Plain {};
struct Sealed final {};

static_assert(!NotInherited<Plain>, "NotInherited<Plain> must be false — Plain is not final");
static_assert(NotInherited<Sealed>, "NotInherited<Sealed> must be true — Sealed is final");

// assert_not_inherited must be invocable from a consteval context when the
// type is final.
consteval bool prove_sealed() {
    assert_not_inherited<Sealed>();
    return true;
}
static_assert(prove_sealed());

// A type that inherits FinalBy is not itself `final`, so the NotInherited
// concept does not hold on it, yet a subclass is still rejected. The two
// properties are separate and are checked separately.
class Shielded : public virtual FinalBy<Shielded> {
public:
    Shielded() = default;
    int value = 7;
};

// Virtual inheritance puts a base pointer in the derived class, but the base
// itself still contributes nothing.
static_assert(sizeof(FinalBy<Shielded>) == sizeof(char), "FinalBy<T> must be empty so the base contributes zero bytes");
static_assert(std::is_empty_v<FinalBy<Shielded>>, "FinalBy<T> must be std::is_empty_v");

static_assert(std::is_default_constructible_v<Shielded>,
              "Shielded must be default-constructible via its own (friend) ctor");
}  // namespace not_inherited_test

static void test_not_inherited() {
    using namespace not_inherited_test;
    Shielded s;
    assert(s.value == 7);

    // The witnesses fire at compile time. Naming them here only confirms the
    // types instantiate in a runtime context as well.
    constexpr bool plain_is_not_final = !NotInherited<Plain>;
    constexpr bool sealed_is_final = NotInherited<Sealed>;
    (void)plain_is_not_final;
    (void)sealed_is_final;

    std::printf("  NotInherited:   ok\n");
}

static void test_publish_slot() {
    static_assert(alignof(PublishSlot<int>) >= 64, "PublishSlot<T> is cache-line aligned. Publish and exchange "
                                                   "traffic invalidates the consumer's line every iteration, so "
                                                   "the slot must not share a line with unrelated state in the "
                                                   "embedder.");
    static_assert(sizeof(PublishSlot<int>) >= 64, "PublishSlot<T> occupies a full cache line by design.");
    static_assert(!std::is_copy_constructible_v<PublishSlot<int>>,
                  "PublishSlot<T> is the identity of a publication cell");
    static_assert(!std::is_move_constructible_v<PublishSlot<int>>, "PublishSlot<T> must not move its atomic cell");

    int first = 1;
    int second = 2;
    PublishSlot<int> slot;

    assert(slot.observe() == nullptr);
    assert(slot.consume() == nullptr);

    slot.publish(&first);
    assert(slot.has_pending());
    assert(slot.observe() == &first);
    assert(slot.consume() == &first);
    assert(!slot.has_pending());
    assert(slot.observe() == nullptr);

    // Publication is latest-wins: a newer value replaces an older unconsumed
    // one. The producer keeps both pointed-to objects alive, so dropping the
    // older pointer leaks nothing.
    slot.publish(&first);
    slot.publish(&second);
    assert(slot.observe() == &second);
    assert(slot.consume() == &second);
    assert(slot.consume() == nullptr);

    std::printf("  PublishSlot:    ok\n");
}

int main() {
    std::printf("Safety wrappers smoke test:\n");
    test_linear();
    test_refined();
    test_secret();
    test_tagged();
    test_session();
    test_machine();
    test_checked();
    test_mutation();
    test_file_handle();
    test_constant_time();
    test_assertion_triad();
    test_not_inherited();
    test_publish_slot();
    std::printf("All safety wrappers compile and pass smoke test.\n");
    return 0;
}
