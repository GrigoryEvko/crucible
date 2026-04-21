// Smoke test: every safety wrapper compiles and does not introduce
// unexpected runtime bloat.  `sizeof` assertions verify zero-cost
// wrapping at compile time.

#include <crucible/safety/Safety.h>

#include <cassert>
#include <contracts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
    auto s = make_session<Send<Request>, Recv<Response>, End>(FakePipe{});
    auto s1 = std::move(s).send(Request{7}, [](FakePipe& p, Request&& req) {
        p.recorded_send = req.id;
    });
    // std::move(s1).send(...);  // COMPILE ERROR — no send on Recv head.
    auto [resp, s2] = std::move(s1).recv([](FakePipe& p) {
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
    std::printf("  Mutation:       ok\n");
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
    std::printf("All safety wrappers compile and pass smoke test.\n");
    return 0;
}
