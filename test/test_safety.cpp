// Smoke test: every safety wrapper compiles and does not introduce
// unexpected runtime bloat.  `sizeof` assertions verify zero-cost
// wrapping at compile time.

#include <crucible/safety/Safety.h>

#include <atomic>
#include <cassert>
#include <contracts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <inplace_vector>
#include <latch>
#include <string>
#include <thread>
#include <utility>

// Weak default handle_contract_violation comes from libcrucible.a
// (src/ContractHandler.cpp).  Nothing to do here.

using namespace crucible::safety;

// ═══════════════════════════════════════════════════════════════════
// Linear<T>
// ═══════════════════════════════════════════════════════════════════

struct FakeFd {
    int value;
    FakeFd(int v) : value{v} {}
};

static void test_linear() {
    static_assert(sizeof(Linear<int>) == sizeof(int),
                  "Linear<T> must be zero-cost in layout");
    static_assert(sizeof(Linear<FakeFd>) == sizeof(FakeFd),
                  "Linear<T> must be zero-cost in layout");

    Linear<FakeFd> a{FakeFd{42}};
    // Linear<FakeFd> b = a;  // COMPILE ERROR (copy deleted) — good.
    Linear<FakeFd> b = std::move(a);
    assert(b.peek().value == 42);

    FakeFd raw = std::move(b).consume();
    assert(raw.value == 42);
    std::printf("  Linear:         ok\n");
}

// ═══════════════════════════════════════════════════════════════════
// Refined<Pred, T>
// ═══════════════════════════════════════════════════════════════════

using PositiveInt = Refined<positive, int>;
using NonNullPtr  = Refined<non_null, int*>;

static void test_refined() {
    static_assert(sizeof(PositiveInt) == sizeof(int),
                  "Refined<P, T> must be zero-cost in layout");

    int stack_var = 7;
    PositiveInt p{5};
    NonNullPtr  q{&stack_var};

    assert(p.value() == 5);
    assert(*q.value() == 7);
    int extracted = std::move(p).into();
    assert(extracted == 5);

    // Composition: Linear<Refined<Pred, T>> — owned move-only handle
    // to a value that satisfies a compile-time invariant.  The common
    // ordering for resource handles.
    {
        // Predicate: fd >= 0.  This is a "load-bearing" invariant — the
        // rule says name it (code_guide §XVI).  We inline here because
        // the demo is self-contained; real call sites would use a named
        // alias near the resource type.
        constexpr auto is_open_fd = [](int fd) constexpr noexcept {
            return fd >= 0;
        };

        static_assert(sizeof(LinearRefined<is_open_fd, int>) == sizeof(int),
                      "LinearRefined<P, T> must collapse to sizeof(T)");

        // Move-only by Linear's rule; construction proves the invariant.
        LinearRefined<is_open_fd, int> handle{Refined<is_open_fd, int>{42}};
        assert(handle.peek().value() == 42);

        // Consume yields the Refined; extract the raw int via into().
        int raw_fd = std::move(handle).consume().into();
        assert(raw_fd == 42);
    }

    // Composition: Refined<Pred, Linear<T>> — predicate about the
    // Linear wrapper state (rare case).  The predicate runs at
    // construction and gets a const Linear<T>& via peek().
    {
        struct Sentinel { int token{0}; };
        constexpr auto token_is_valid = [](const Linear<Sentinel>& l) constexpr noexcept {
            return l.peek().token > 0;
        };
        RefinedLinear<token_is_valid, Sentinel> rl{Linear<Sentinel>{Sentinel{7}}};
        assert(rl.value().peek().token == 7);
    }

    std::printf("  Refined:        ok\n");
}

// ═══════════════════════════════════════════════════════════════════
// Secret<T>
// ═══════════════════════════════════════════════════════════════════

struct CardNumber {
    std::uint64_t digits;
    std::size_t size() const noexcept { return 16; }
};

static void test_secret() {
    static_assert(sizeof(Secret<CardNumber>) == sizeof(CardNumber),
                  "Secret<T> must be zero-cost in layout");

    Secret<CardNumber> card{CardNumber{4242424242424242ULL}};
    // Secret<CardNumber> copy = card;  // COMPILE ERROR — good.

    // Transform stays Secret.
    auto hashed = std::move(card).transform([](CardNumber c) -> std::uint64_t {
        return c.digits ^ std::uint64_t{0xDEADBEEFCAFEBABEULL};
    });
    static_assert(std::is_same_v<decltype(hashed), Secret<std::uint64_t>>);

    // Declassify requires a policy tag.
    std::uint64_t raw = std::move(hashed).declassify<secret_policy::HashForCompare>();
    (void)raw;
    std::printf("  Secret:         ok\n");
}

// ═══════════════════════════════════════════════════════════════════
// Tagged<T, Tag>
// ═══════════════════════════════════════════════════════════════════

struct Config { int port; };

static void apply_sanitized(Tagged<Config, source::Sanitized> cfg) {
    assert(cfg.value().port == 8080);
}

static void test_tagged() {
    static_assert(sizeof(Tagged<Config, source::Sanitized>) == sizeof(Config),
                  "Tagged<T, Tag> must be zero-cost in layout");

    Tagged<Config, source::External> raw{Config{8080}};
    // apply_sanitized(raw);  // COMPILE ERROR — type mismatch — good.
    auto sanitized = std::move(raw).retag<source::Sanitized>();
    apply_sanitized(std::move(sanitized));

    Tagged<int, trust::Verified>    v{42};
    Tagged<int, access::RO>         ro{99};
    Tagged<int, version::V<3>>      vv{7};
    (void)v; (void)ro; (void)vv;
    std::printf("  Tagged:         ok\n");
}

// ═══════════════════════════════════════════════════════════════════
// Session<Resource, Steps...>
// ═══════════════════════════════════════════════════════════════════

struct FakePipe {
    int fd{0};
    int recorded_send{0};
    int recorded_recv{0};
};

struct Request  { int id; };
struct Response { int status; };

static void test_session() {
    // Combinators + factory live in the proto:: sub-namespace; bring
    // them in here rather than at file scope so the rest of the file
    // (which only uses the value-level safety wrappers) stays free of
    // the protocol-DSL names.
    using namespace crucible::safety::proto;

    // Canonical Honda-style nested combinators: a single Proto template
    // argument whose continuation chain encodes the protocol's full
    // sequence.  (The legacy parameter-pack form
    // `make_session<Send<Request>, Recv<Response>, End>(...)` is no
    // longer the API — both `Send` and `Recv` are now two-arg
    // combinators with explicit continuations, and the factory is
    // `make_session_handle<Proto>`.)
    auto s = make_session_handle<Send<Request, Recv<Response, End>>>(FakePipe{});

    auto s1 = std::move(s).send(Request{7},
        [](FakePipe& p, Request&& req) noexcept {
            p.recorded_send = req.id;
        });
    // std::move(s1).send(...);  // COMPILE ERROR — no send on Recv head.

    auto [resp, s2] = std::move(s1).recv(
        [](FakePipe& p) noexcept -> Response {
            p.recorded_recv = 1;
            return Response{200};
        });
    assert(resp.status == 200);

    auto pipe = std::move(s2).close();
    assert(pipe.recorded_recv == 1);
    assert(pipe.recorded_send == 7);
    std::printf("  Session:        ok\n");
}

// ═══════════════════════════════════════════════════════════════════
// Machine<State>
// ═══════════════════════════════════════════════════════════════════

namespace {
namespace conn {
    struct Disconnected {};
    struct Connecting { std::string host; std::uint32_t attempt; };
    struct Connected  { int sock_fd; };

    [[nodiscard]] Machine<Connecting>
    connect(Machine<Disconnected>&& m, std::string host) {
        (void)std::move(m).extract();
        return Machine<Connecting>{Connecting{std::move(host), 0}};
    }

    [[nodiscard]] Machine<Connected>
    established(Machine<Connecting>&& m, int fd) {
        (void)std::move(m).extract();
        return Machine<Connected>{Connected{fd}};
    }

    [[nodiscard]] Machine<Disconnected>
    close(Machine<Connected>&& m) {
        (void)std::move(m).extract();
        return Machine<Disconnected>{Disconnected{}};
    }
}
}

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
    // conn::connect(std::move(m2), ...);   // COMPILE ERROR — good.
    auto m3 = conn::close(std::move(m2));
    (void)m3;
    std::printf("  Machine:        ok\n");
}

// ═══════════════════════════════════════════════════════════════════
// Checked arithmetic
// ═══════════════════════════════════════════════════════════════════

static void test_checked() {
    auto a = checked_add<std::uint32_t>(0xFFFFFFFFu, 1u);
    assert(!a.has_value());

    auto b = checked_add<std::uint32_t>(100u, 200u);
    assert(b && *b == 300u);

    auto w = wrapping_add<std::uint8_t>(250u, 10u);
    assert(w == 4u);  // wrapped

    auto t = trapping_add<std::uint32_t>(1u, 2u);
    assert(t == 3u);
    std::printf("  Checked:        ok\n");
}

// ═══════════════════════════════════════════════════════════════════
// AppendOnly and Monotonic
// ═══════════════════════════════════════════════════════════════════

static void test_mutation() {
    AppendOnly<int> log;
    log.append(1);
    log.append(2);
    log.append(3);
    // log.data_.erase(...);   // not possible — no erase exposed.
    assert(log.size() == 3);
    assert(log[2] == 3);

    Monotonic<std::uint64_t> epoch{0};
    epoch.advance(1);
    epoch.advance(5);
    // epoch.advance(3);  // contract violation in debug build.
    assert(epoch.get() == 5);
    assert(!epoch.try_advance(2));
    assert(epoch.try_advance(10));
    assert(epoch.get() == 10);

    // OrderedAppendOnly — nested composition of AppendOnly + per-emplace
    // key monotonicity.  Default KeyFn = std::identity, Cmp = std::less<>.
    static_assert(sizeof(OrderedAppendOnly<std::uint64_t>)
                  == sizeof(AppendOnly<std::uint64_t>),
                  "stateless KeyFn/Cmp must collapse to zero layout cost");
    {
        OrderedAppendOnly<std::uint64_t> timeline;
        timeline.append(0ULL);
        timeline.append(1ULL);
        timeline.append(1ULL); // equal — non-decreasing, OK.
        timeline.append(2ULL);
        timeline.emplace(5ULL); // forwarding — must take the same path.
        // timeline.append(3);  // WOULD contract-violate (3 < 5).
        assert(timeline.size() == 5);
        assert(timeline.back() == 5);
        assert(timeline[0] == 0);
    }
    // Projected key: struct-with-step_id log, ordered by step_id.
    {
        struct Entry { std::uint64_t step; int payload; };
        struct ByStep {
            constexpr std::uint64_t operator()(const Entry& e) const noexcept {
                return e.step;
            }
        };
        OrderedAppendOnly<Entry, ByStep> log_by_step;
        log_by_step.append({.step = 10, .payload = 100});
        log_by_step.append({.step = 10, .payload = 101});  // duplicate step OK
        log_by_step.append({.step = 20, .payload = 200});
        // log_by_step.append({.step = 15, .payload = 150}); // would fail pre
        assert(log_by_step.size() == 3);
        assert(log_by_step.back().step == 20);
    }

    // BoundedMonotonic — Monotonic + compile-time upper bound.
    static_assert(sizeof(BoundedMonotonic<std::uint32_t, 1024U>) == sizeof(std::uint32_t),
                  "BoundedMonotonic must collapse to underlying T");
    {
        BoundedMonotonic<std::uint32_t, 10U> counter{0U};
        assert(counter.get() == 0);
        counter.advance(1U);
        counter.advance(5U);
        counter.advance(10U);                 // at bound — still OK (<=)
        // counter.advance(11U);              // would fire pre (> Max)
        // counter.advance(5U);               // would fire Monotonic's pre
        assert(counter.get() == 10);
        assert((BoundedMonotonic<std::uint32_t, 10U>::max() == 10U));

        BoundedMonotonic<std::uint32_t, 3U> bumper{0U};
        bumper.bump();
        bumper.bump();
        bumper.bump();                         // now at bound
        // bumper.bump();                      // would fire pre (get() == Max)
        assert(bumper.get() == 3U);

        // try_advance rejects out-of-bound silently, as documented.
        BoundedMonotonic<std::uint32_t, 5U> cnt{2U};
        assert(!cnt.try_advance(6U));          // over bound — rejected
        assert(cnt.get() == 2U);
        assert(!cnt.try_advance(1U));          // goes backward — rejected
        assert(cnt.try_advance(4U));           // fine
        assert(cnt.get() == 4U);
    }

    // AtomicMonotonic — fetch_max fast path on canonical std::less<T>.
    // Single-threaded behavior must match the non-atomic Monotonic.
    {
        AtomicMonotonic<std::uint64_t> step{0};
        assert(step.get() == 0ULL);
        assert(step.try_advance(1ULL));        // 0 → 1: advanced
        assert(step.get() == 1ULL);
        assert(!step.try_advance(1ULL));       // equal: no-op, returns false
        assert(!step.try_advance(0ULL));       // backward: no-op, returns false
        assert(step.try_advance(7ULL));        // jump forward
        assert(step.get() == 7ULL);
        assert(!step.try_advance(5ULL));       // backward again
        assert(step.get() == 7ULL);

        // Strict advance: contract checks for non-monotonic input.
        step.advance(8ULL);
        assert(step.get() == 8ULL);
    }

    // AtomicMonotonic with std::greater — fetch_min fast path.
    {
        AtomicMonotonic<std::uint64_t, std::greater<std::uint64_t>> floor{100ULL};
        assert(floor.get() == 100ULL);
        assert(floor.try_advance(50ULL));      // 100 → 50: advanced
        assert(floor.get() == 50ULL);
        assert(!floor.try_advance(50ULL));     // equal
        assert(!floor.try_advance(60ULL));     // backward (greater than current)
        assert(floor.try_advance(0ULL));       // jump down
        assert(floor.get() == 0ULL);
    }

    // MaxObserved alias — same underlying impl, intent-revealing name.
    {
        MaxObserved<std::uint32_t> high_water{0U};
        assert(high_water.try_advance(50U));
        assert(high_water.try_advance(100U));
        assert(!high_water.try_advance(75U));   // not a new high
        assert(high_water.get() == 100U);
    }

    // Concurrent stress: N threads each push an exclusive range of values.
    // Final state must be the global max.  Total successful try_advance
    // returns must equal the number of strict-improvement events
    // (every thread always sees a higher max from someone else partway).
    //
    // Uses std::latch as a starting-line gate so every worker hits
    // try_advance simultaneously — without this, threads constructed
    // earlier in the loop finish thousands of calls before later
    // threads even start, and the CAS contention the test is meant
    // to exercise never materializes.
    {
        constexpr int kThreads = 4;
        constexpr std::uint64_t kPerThread = 4096;

        AtomicMonotonic<std::uint64_t> shared_high{0};
        std::atomic<std::uint64_t> total_advances{0};

        std::latch start_gate{kThreads + 1};

        {
            std::inplace_vector<std::jthread, kThreads> workers;
            for (int tid = 0; tid < kThreads; ++tid) {
                workers.emplace_back([tid, &shared_high,
                                      &total_advances, &start_gate]{
                    start_gate.arrive_and_wait();       // synchronize all workers
                    std::uint64_t local_advances = 0;
                    const std::uint64_t base =
                        static_cast<std::uint64_t>(tid) * kPerThread;
                    for (std::uint64_t v = 1; v <= kPerThread; ++v) {
                        if (shared_high.try_advance(base + v))
                            ++local_advances;
                    }
                    total_advances.fetch_add(local_advances,
                                             std::memory_order_relaxed);
                });
            }
            // Main thread arrives last, releasing all workers at once.
            start_gate.arrive_and_wait();
        }  // jthreads join

        // Final value must be the highest base+v any thread tried.
        const std::uint64_t expected_max =
            static_cast<std::uint64_t>(kThreads - 1) * kPerThread + kPerThread;
        assert(shared_high.get() == expected_max);
        // At least kPerThread advances must have stuck (the winning thread's
        // monotonic stride alone), and no more than kThreads*kPerThread.
        const std::uint64_t advances =
            total_advances.load(std::memory_order_relaxed);
        assert(advances >= kPerThread);
        assert(advances <= kThreads * kPerThread);
    }

    // ── WriteOnceNonNull<T*> (#77) ─────────────────────────────────
    //
    // Pointer-specialized single-set with nullptr as the "unset"
    // sentinel — zero tag overhead relative to WriteOnce<T*>'s
    // std::optional<T*>.  Covers: sizeof collapse, unset-state
    // queries, set() + try_set() idempotence, get() retrieval,
    // dereference paths on typed pointee.
    {
        // Zero-cost: sizeof matches the raw pointer exactly.
        static_assert(sizeof(WriteOnceNonNull<int*>)    == sizeof(int*));
        static_assert(sizeof(WriteOnceNonNull<double*>) == sizeof(double*));
        static_assert(sizeof(WriteOnceNonNull<void*>)   == sizeof(void*));

        // Participates in the is_writeoncenonnull trait used by
        // AppendOnly's redundancy rejection.
        static_assert(is_writeoncenonnull_v<WriteOnceNonNull<int*>>);
        static_assert(!is_writeoncenonnull_v<int*>);
        static_assert(!is_writeoncenonnull_v<WriteOnce<int*>>);

        int payload = 7;

        WriteOnceNonNull<int*> slot;
        assert(!slot.has_value());
        assert(!static_cast<bool>(slot));

        // First try_set on null → no-op, stays unset.
        assert(!slot.try_set(nullptr));
        assert(!slot.has_value());

        // First try_set on non-null → accepted.
        assert(slot.try_set(&payload));
        assert(slot.has_value());
        assert(static_cast<bool>(slot));
        assert(slot.get() == &payload);
        assert(*slot == 7);

        // Subsequent try_set → refused (idempotent).
        int other = 13;
        assert(!slot.try_set(&other));
        assert(slot.get() == &payload);

        // Operator-> reaches the pointee without extra unwrap.
        struct Thing { int x; };
        Thing t{42};
        WriteOnceNonNull<Thing*> thing_slot;
        thing_slot.set(&t);
        assert(thing_slot->x == 42);

        // void-pointee path: must compile without operator* / operator->
        // (SFINAE'd away for void).
        int raw = 99;
        WriteOnceNonNull<void*> vslot;
        vslot.set(&raw);
        assert(vslot.has_value());
        assert(vslot.get() == &raw);
    }
    std::printf("  Mutation:       ok\n");
}

// ═══════════════════════════════════════════════════════════════════
// Assertion triad — Platform.h CRUCIBLE_ASSERT/DEBUG_ASSERT/INVARIANT
//
// Smoke test: each macro accepts a true expression without firing,
// verifies the <debugging> shim links, and confirms the
// is_debugger_present() probe returns a defined bool.  We don't try
// to trigger contract violations here — those go through abort and
// would terminate the test binary.
// ═══════════════════════════════════════════════════════════════════

static void test_assertion_triad() {
    // CRUCIBLE_ASSERT: contract-backed boundary precondition.
    int x = 42;
    CRUCIBLE_ASSERT(x == 42);
    CRUCIBLE_DEBUG_ASSERT(x > 0);
    CRUCIBLE_INVARIANT(x > 0);

    // The <debugging> shim must link.  is_debugger_present() returns
    // false on unattended CI; under gdb/lldb returns true.  Either way
    // it's a defined bool, not a link error.
    bool dbg = ::crucible::detail::is_debugger_present();
    (void)dbg;

    // breakpoint_if_debugging() must link too.  On unattended CI it
    // no-ops; under a debugger it would trap, but the test runner
    // would not be running under a debugger so the call is safe.
    ::crucible::detail::breakpoint_if_debugging();

    std::printf("  AssertionTriad: ok (debugger_present=%s)\n",
                dbg ? "true" : "false");
}

// ═══════════════════════════════════════════════════════════════════
// FileHandle — RAII posix fd wrapper
// ═══════════════════════════════════════════════════════════════════

static void test_file_handle() {
    static_assert(sizeof(FileHandle) == sizeof(int),
                  "FileHandle must be a zero-cost int wrapper");
    static_assert(!std::is_copy_constructible_v<FileHandle>);
    static_assert(!std::is_copy_assignable_v<FileHandle>);
    static_assert(std::is_move_constructible_v<FileHandle>);
    static_assert(std::is_move_assignable_v<FileHandle>);

    char tmpl[] = "/tmp/crucible_fh_XXXXXX";
    int raw_fd = ::mkstemp(tmpl);
    assert(raw_fd >= 0);

    // Wrap: FileHandle now owns raw_fd.  Scope-exit closes it.
    const std::string path = tmpl;
    {
        FileHandle h{raw_fd};
        assert(h.is_open());
        assert(h.get() == raw_fd);

        // write_full with all-or-nothing semantics.
        const std::byte buf[] = {
            std::byte{'A'}, std::byte{'B'}, std::byte{'C'}, std::byte{'\n'}
        };
        const int wrc = write_full(h, std::span<const std::byte>{buf, 4});
        assert(wrc == 0);

        // fstat via file_size — we wrote 4 bytes.
        const auto sz = file_size(h);
        assert(sz == 4);
    }  // dtor closes raw_fd here

    // Read back via open_read + read_full.  Factory returns a FileHandle
    // whose fd is a brand-new one (different from raw_fd above).
    {
        FileHandle r = open_read(path.c_str());
        assert(r.is_open());
        std::byte rbuf[16]{};
        const ssize_t n = read_full(r, std::span<std::byte>{rbuf, 16});
        assert(n == 4);
        assert(rbuf[0] == std::byte{'A'});
        assert(rbuf[3] == std::byte{'\n'});
    }

    // Move semantics: moved-from is closed-state.
    {
        FileHandle a = open_read(path.c_str());
        assert(a.is_open());
        const int a_fd = a.get();
        FileHandle b = std::move(a);
        assert(!a.is_open());      // source is reset
        assert(b.is_open());
        assert(b.get() == a_fd);
    }

    // Explicit close: lets caller observe the close() return code.
    {
        FileHandle c = open_read(path.c_str());
        assert(c.is_open());
        const int rc = c.close_explicit();
        assert(rc == 0);
        assert(!c.is_open());
        // Second explicit close is a no-op (already closed).
        assert(c.close_explicit() == 0);
    }

    ::unlink(path.c_str());
    std::printf("  FileHandle:     ok\n");
}

// ═══════════════════════════════════════════════════════════════════
// ConstantTime primitives
// ═══════════════════════════════════════════════════════════════════

static void test_constant_time() {
    using namespace crucible::safety::ct;

    auto a = select<std::uint32_t>(1u, 0xAAAAu, 0xBBBBu);
    assert(a == 0xAAAAu);
    auto b = select<std::uint32_t>(0u, 0xAAAAu, 0xBBBBu);
    assert(b == 0xBBBBu);

    std::byte buf1[4] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    std::byte buf2[4] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    std::byte buf3[4] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{9}};
    assert(eq(buf1, buf2, 4));
    assert(!eq(buf1, buf3, 4));

    assert(less<std::uint32_t>(5u, 10u) == 1u);
    assert(less<std::uint32_t>(10u, 5u) == 0u);
    assert(is_zero<std::uint32_t>(0u) == 1u);
    assert(is_zero<std::uint32_t>(1u) == 0u);

    std::uint32_t x = 100u, y = 200u;
    cswap<std::uint32_t>(1u, x, y);
    assert(x == 200u && y == 100u);
    cswap<std::uint32_t>(0u, x, y);
    assert(x == 200u && y == 100u);
    std::printf("  ConstantTime:   ok\n");
}

// ═══════════════════════════════════════════════════════════════════

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
    std::printf("All safety wrappers compile and pass smoke test.\n");
    return 0;
}
