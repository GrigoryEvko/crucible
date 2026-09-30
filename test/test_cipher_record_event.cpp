// Recording an event writes a file and blocks on a syscall, and it stamps
// the log entry with a reading of the monotonic clock.  So the caller has
// to hand in a context whose row admits IO and Block, and whose row owns
// Bg, Init or Test.  A caller on the foreground path holds no such
// context, and the call does not compile for it.  Everything here is the
// accepting side of that fence.  The negative-compile fixtures hold the
// rejecting side.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>
#include <fixy/os/Time.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include "test_assert.h"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

// Opening the store and its open view need a context whose row admits IO
// and Block.
[[nodiscard]] inline ::fixy::TestRunnerCtx store_ctx() {
    return ::fixy::TestRunnerCtx{::foundation::effects::testing::test()};
}

using crucible::Cipher;
using crucible::ContentHash;
namespace eff = ::foundation::effects;

[[nodiscard]] static Cipher open_cipher(const std::string& dir) {
    return Cipher::open(store_ctx(), ::fixy::mint_tagged<::fixy::tags::source::External>(std::filesystem::path{dir}));
}

// Records one event and requires that the commit happened.
template <typename Ctx>
static void record(Cipher& cipher, Ctx const& ctx, Cipher::OpenView const& view, ContentHash hash, std::uint64_t step) {
    const auto recorded = cipher.record_event(ctx, view, hash, step);
    assert(recorded.has_value() && "record_event must commit when the clock read succeeds");
}

// The whole content of a file, or an empty string when it does not open.
[[nodiscard]] static std::string read_file(const std::string& path) {
    std::ifstream file(path);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// The third field of each line of the log: the time of each commit, in
// the order of the commits.
[[nodiscard]] static std::vector<std::uint64_t> log_timestamps(const std::string& dir) {
    std::vector<std::uint64_t> timestamps;
    std::ifstream log(dir + "/log");
    std::string line;
    while (std::getline(log, line)) {
        const std::size_t second_comma = line.find(',', line.find(',') + 1);
        assert(second_comma != std::string::npos);
        std::uint64_t timestamp = 0;
        const char* const end = line.data() + line.size();
        const auto [stop, error] = std::from_chars(line.data() + second_comma + 1, end, timestamp);
        assert(error == std::errc{} && stop == end);
        timestamps.push_back(timestamp);
    }
    return timestamps;
}

// A reading of CLOCK_MONOTONIC in nanoseconds.
[[nodiscard]] static std::uint64_t monotonic_now_ns() {
    const auto clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(store_ctx());
    const auto reading = clock.read();
    assert(reading.has_value());
    return reading->peek();
}

static void test_t01_required_row_pinned() {
    static_assert(std::is_same_v<Cipher::record_event_required_row, eff::Row<eff::Effect::IO, eff::Effect::Block>>,
                  "Cipher::record_event_required_row MUST be exactly "
                  "Row<IO, Block>.  Adding an atom here tightens the API and "
                  "breaks every existing call site, so change the contract "
                  "first.");

    static_assert(eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::IO>);
    static_assert(eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Block>);
    static_assert(!eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Alloc>);
    static_assert(!eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Bg>);

    static_assert(eff::row_size_v<Cipher::record_event_required_row> == 2);

    std::printf("  T01 required_row_pinned:                 PASSED\n");
}

static void test_t02_subrow_accepted_shapes() {
    using Required = Cipher::record_event_required_row;

    static_assert(eff::Subrow<Required, eff::Row<eff::Effect::IO, eff::Effect::Block>>);

    // Containment is by membership, so the order the caller writes its
    // atoms in makes no difference.
    static_assert(eff::Subrow<Required, eff::Row<eff::Effect::Block, eff::Effect::IO>>);

    static_assert(
        eff::Subrow<Required, eff::Row<eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block, eff::Effect::Bg>>);

    static_assert(eff::Subrow<Required, eff::Row<eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block,
                                                 eff::Effect::Bg, eff::Effect::Init, eff::Effect::Test>>);

    // The gate reads the row of the context, so each named context whose
    // row holds IO and Block passes it.
    static_assert(crucible::CtxFitsCipherPersistence<::fixy::TestRunnerCtx>);
    static_assert(crucible::CtxFitsCipherPersistence<::fixy::BgLoadCtx>);
    static_assert(crucible::CtxFitsCipherPersistence<::fixy::InitLoadCtx>);

    // The same three rows also own Test, Bg or Init, so each one also fits
    // the clock reader and the commit.
    static_assert(crucible::cipher::CtxFitsCipherCommit<::fixy::TestRunnerCtx>);
    static_assert(crucible::cipher::CtxFitsCipherCommit<::fixy::BgLoadCtx>);
    static_assert(crucible::cipher::CtxFitsCipherCommit<::fixy::InitLoadCtx>);

    std::printf("  T02 subrow_accepted_shapes:              PASSED\n");
}

// These assert the containment predicate directly.  What happens at a
// call site that fails it is the negative-compile fixtures' business.

static void test_t03_subrow_rejected_shapes() {
    using Required = Cipher::record_event_required_row;

    static_assert(!eff::Subrow<Required, eff::Row<>>);

    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::IO>>);

    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Block>>);

    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Alloc>>);

    // A background context is a context tag and not a claim about
    // effects, so it implies neither of the two required atoms.
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Bg>>);

    // The named contexts whose rows lack one of the two atoms fail the
    // gate, and a value that is no context fails it too.
    static_assert(!crucible::CtxFitsCipherPersistence<::fixy::HotFgCtx>);
    static_assert(!crucible::CtxFitsCipherPersistence<::fixy::BgDrainCtx>);
    static_assert(!crucible::CtxFitsCipherPersistence<::fixy::BgCompileCtx>);
    static_assert(!crucible::CtxFitsCipherPersistence<::fixy::ColdInitCtx>);
    static_assert(!crucible::CtxFitsCipherPersistence<Required>);
    static_assert(!crucible::CtxFitsCipherPersistence<int>);

    // A context narrowed to IO and Block fits the store and not the
    // commit: its row owns none of Bg, Init and Test, so it cannot mint
    // the clock reader that stamps the log entry.
    using StoreOnly = decltype(store_ctx().in_row<Required>());
    static_assert(crucible::CtxFitsCipherPersistence<StoreOnly>);
    static_assert(!crucible::cipher::CtxFitsCipherCommit<StoreOnly>);
    static_assert(!crucible::cipher::CtxFitsCipherCommit<::fixy::HotFgCtx>);
    static_assert(!crucible::cipher::CtxFitsCipherCommit<::fixy::BgDrainCtx>);

    std::printf("  T03 subrow_rejected_shapes:              PASSED\n");
}

// The commit is durable: a Cipher opened again on the same directory
// reads the head and the step from the files that record_event wrote.

static void test_t04_record_event_is_durable(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t04";
    std::filesystem::create_directories(dir);

    constexpr ContentHash kHash{0xC0FFEEBA12345678ULL};
    constexpr std::uint64_t kStep = 42u;
    {
        auto cipher = open_cipher(dir);
        auto view = cipher.mint_open_view(store_ctx());
        record(cipher, store_ctx(), view, kHash, kStep);
    }

    // The head file holds sixteen hex digits and a newline.
    assert(read_file(dir + "/HEAD") == "c0ffeeba12345678\n");

    auto reopened = open_cipher(dir);
    auto view = reopened.mint_open_view(store_ctx());
    assert(reopened.head() == kHash);
    assert(reopened.hash_at_step(view, kStep) == kHash);

    std::printf("  T04 record_event_is_durable:             PASSED\n");
}

static void test_t05_round_trip(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t05";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());

    constexpr ContentHash kHash{0xDEADBEEFCAFEBABEULL};
    constexpr std::uint64_t kStep = 7u;

    record(cipher, store_ctx(), view, kHash, kStep);

    assert(cipher.head() == kHash);
    assert(cipher.hash_at_step(view, kStep) == kHash);

    // A query past the end answers with the last recorded step.
    assert(cipher.hash_at_step(view, kStep + 100) == kHash);

    std::printf("  T05 round_trip:                          PASSED\n");
}

// The context of a background thread carries more than the two required
// atoms, and is accepted because it carries them both.

static void test_t06_bg_superset_row(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t06";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());

    const ::fixy::BgLoadCtx bg_ctx{::foundation::effects::testing::bg()};
    static_assert(std::is_same_v<typename ::fixy::BgLoadCtx::row_type,
                                 eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>);

    constexpr ContentHash kHash{0x123456789ABCDEF0ULL};
    record(cipher, bg_ctx, view, kHash, 1u);

    assert(cipher.head() == kHash);
    std::printf("  T06 bg_superset_row:                     PASSED\n");
}

// The startup context carries Init on top of the two required atoms.

static void test_t07_startup_superset_row(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t07";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());

    const ::fixy::InitLoadCtx init_ctx{::foundation::effects::testing::init()};

    constexpr ContentHash kHash{0xFEDCBA9876543210ULL};
    record(cipher, init_ctx, view, kHash, 1u);

    assert(cipher.head() == kHash);
    std::printf("  T07 startup_superset_row:                PASSED\n");
}

// Recording an ordered sequence and querying it back shows that the
// precondition on step order holds on the recorded log.

static void test_t08_monotonic_steps(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t08";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());
    const auto ctx = store_ctx();

    record(cipher, ctx, view, ContentHash{0xAA}, 1u);
    record(cipher, ctx, view, ContentHash{0xBB}, 5u);
    record(cipher, ctx, view, ContentHash{0xCC}, 10u);

    // The query answers with the last entry at or before the step
    // asked for.
    assert(cipher.hash_at_step(view, 0u) == ContentHash{});  // before all
    assert(cipher.hash_at_step(view, 1u) == ContentHash{0xAA});
    assert(cipher.hash_at_step(view, 3u) == ContentHash{0xAA});  // gap
    assert(cipher.hash_at_step(view, 5u) == ContentHash{0xBB});
    assert(cipher.hash_at_step(view, 7u) == ContentHash{0xBB});  // gap
    assert(cipher.hash_at_step(view, 10u) == ContentHash{0xCC});
    assert(cipher.hash_at_step(view, 99u) == ContentHash{0xCC});  // future

    std::printf("  T08 monotonic_steps:                     PASSED\n");
}

static void test_t09_multiple_events_monotonic(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t09";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());
    const auto ctx = store_ctx();

    constexpr int N = 32;
    for (int i = 0; i < N; ++i) {
        record(cipher, ctx, view, ContentHash{std::uint64_t{0x1000u} + static_cast<std::uint64_t>(i)},
               static_cast<std::uint64_t>(i));
    }

    assert(cipher.head() == ContentHash{0x1000ULL + (N - 1)});

    for (int i = 0; i < N; ++i) {
        const auto h = cipher.hash_at_step(view, static_cast<std::uint64_t>(i));
        assert(h == ContentHash{std::uint64_t{0x1000u} + static_cast<std::uint64_t>(i)});
    }

    std::printf("  T09 multiple_events_monotonic:           PASSED\n");
}

// The context travels as the first argument, and the result says whether
// the commit happened.  Dropping the context would keep every call site
// compiling for a caller who holds no evidence and lose the fence, so the
// argument list and the result type are pinned here.

static void test_t10_api_surface_pinned(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t10";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());

    using Ctx = ::fixy::TestRunnerCtx;
    using Result = decltype(cipher.record_event(std::declval<Ctx const&>(), view, ContentHash{1u}, std::uint64_t{1u}));
    static_assert(std::is_same_v<Result, std::expected<void, std::error_code>>);

    // A context, the view and two runtime arguments.
    static_assert(std::is_invocable_r_v<std::expected<void, std::error_code>,
                                        decltype([](Cipher& c, Ctx const& ctx, Cipher::OpenView const& v, ContentHash h,
                                                    std::uint64_t s) { return c.record_event(ctx, v, h, s); }),
                                        Cipher&, Ctx const&, Cipher::OpenView const&, ContentHash, std::uint64_t>);

    record(cipher, store_ctx(), view, ContentHash{42u}, 0u);
    assert(cipher.head() == ContentHash{42u});

    std::printf("  T10 api_surface_pinned:                  PASSED\n");
}

// Each log line holds a reading of CLOCK_MONOTONIC.  So every recorded
// time lies between two readings taken before and after the records, and
// the times do not decrease from one commit to the next.

static void test_t11_log_holds_monotonic_readings(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t11";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());
    const auto ctx = store_ctx();

    constexpr int N = 8;
    const std::uint64_t before_ns = monotonic_now_ns();
    for (int i = 0; i < N; ++i) {
        record(cipher, ctx, view, ContentHash{std::uint64_t{0x2000u} + static_cast<std::uint64_t>(i)},
               static_cast<std::uint64_t>(i));
    }
    const std::uint64_t after_ns = monotonic_now_ns();

    const std::vector<std::uint64_t> timestamps = log_timestamps(dir);
    assert(timestamps.size() == static_cast<std::size_t>(N));
    for (std::size_t i = 0; i < timestamps.size(); ++i) {
        assert(before_ns <= timestamps[i] && timestamps[i] <= after_ns);
        if (i != 0) {
            assert(timestamps[i - 1] <= timestamps[i]);
        }
    }

    std::printf("  T11 log_holds_monotonic_readings:        PASSED\n");
}

// The header asserts the content of the required row where it
// declares it.  Restating those assertions here makes this binary's
// compilation depend on them as well, and puts them where someone
// reading the test can find them.

static void test_required_row_header_fence() {
    static_assert(std::is_same_v<Cipher::record_event_required_row, eff::Row<eff::Effect::IO, eff::Effect::Block>>);
    static_assert(eff::row_size_v<Cipher::record_event_required_row> == 2u);
    static_assert(eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::IO>);
    static_assert(eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Block>);
    static_assert(!eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Alloc>);
    static_assert(!eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Bg>);
    static_assert(!eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Init>);
    static_assert(!eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Test>);

    std::printf("  required_row_header_fence:              PASSED\n");
}

// One record is not enough to show that the files read back.  A defect
// that shows only after several records, or only on some hash values,
// needs a longer and more varied sequence than that.

static void test_multi_event_durability(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/multi_event";
    std::filesystem::create_directories(dir);

    constexpr int N = 16;
    constexpr std::uint64_t kHashes[N] = {
        0x0001000100010001ULL, 0xFFFE000200030004ULL, 0xC0FFEE00000000ULL,   0xDEADBEEFCAFEBABEULL,
        0x123456789ABCDEF0ULL, 0xFEDCBA9876543210ULL, 0x5555555555555555ULL, 0xAAAAAAAAAAAAAAAAULL,
        0x0F0F0F0F0F0F0F0FULL, 0xF0F0F0F0F0F0F0F0ULL, 0x1111222233334444ULL, 0x4444333322221111ULL,
        0x8000000000000001ULL, 0x7FFFFFFFFFFFFFFEULL, 0x0123456789ABCDEFULL, 0xFEDCBA9876543211ULL,
    };

    {
        auto cipher = open_cipher(dir);
        auto view = cipher.mint_open_view(store_ctx());
        const auto ctx = store_ctx();
        for (int i = 0; i < N; ++i) {
            record(cipher, ctx, view, ContentHash{kHashes[i]}, static_cast<std::uint64_t>(i));
        }
    }

    assert(read_file(dir + "/HEAD") == "fedcba9876543211\n");
    assert(log_timestamps(dir).size() == static_cast<std::size_t>(N));

    auto reopened = open_cipher(dir);
    auto view = reopened.mint_open_view(store_ctx());
    assert(reopened.head() == ContentHash{kHashes[N - 1]});
    for (int i = 0; i < N; ++i) {
        assert(reopened.hash_at_step(view, static_cast<std::uint64_t>(i)) == ContentHash{kHashes[i]});
    }

    std::printf("  multi_event_durability:                 PASSED\n");
}

// Every other use of the required row in this file is inside a
// function.  Naming it at namespace scope is what a caller outside the
// store does, and only that spelling proves it is publicly reachable.

namespace external_visibility {
using ExtRow = ::crucible::Cipher::record_event_required_row;
static_assert(eff::row_size_v<ExtRow> == 2u);
static_assert(eff::Subrow<ExtRow, eff::Row<eff::Effect::IO, eff::Effect::Block>>);
static_assert(eff::Subrow<ExtRow, eff::Row<eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block, eff::Effect::Bg>>);
static_assert(!eff::Subrow<ExtRow, eff::Row<>>);
static_assert(eff::CtxAdmits<::fixy::TestRunnerCtx, ExtRow>);
}  // namespace external_visibility

static void test_external_visibility() {
    using ExtRow = external_visibility::ExtRow;
    static_assert(eff::row_size_v<ExtRow> == 2u);
    std::printf("  external_visibility:                    PASSED\n");
}

// Each context that fits the commit is interchangeable at a call site,
// including a context narrowed to the two required atoms and the Test
// atom that admits the clock reader.

static void test_canonical_row_acceptance(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/canonical_row";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());

    record(cipher, store_ctx(), view, ContentHash{0xAAAA}, 1u);
    assert(cipher.head() == ContentHash{0xAAAA});

    const auto narrowed = store_ctx().in_row<eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>();
    record(cipher, narrowed, view, ContentHash{0xBBBB}, 2u);
    assert(cipher.head() == ContentHash{0xBBBB});

    // The same three atoms in another order, which containment by
    // membership accepts.
    const auto reordered = store_ctx().in_row<eff::Row<eff::Effect::Block, eff::Effect::IO, eff::Effect::Test>>();
    record(cipher, reordered, view, ContentHash{0xCCCC}, 3u);
    assert(cipher.head() == ContentHash{0xCCCC});

    std::printf("  canonical_row_acceptance:               PASSED\n");
}

// The context gate fires while the template is substituted and the
// precondition fires when the call runs, so satisfying the first says
// nothing about the second.  Violating the precondition would end the
// process, so what is checked here is that a caller who satisfies the
// gate still gets the ordered behaviour the precondition describes.

static void test_pre_clause_orthogonal(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/pre_clause";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());
    const auto ctx = store_ctx();

    record(cipher, ctx, view, ContentHash{1u}, 0u);
    record(cipher, ctx, view, ContentHash{2u}, 1u);
    record(cipher, ctx, view, ContentHash{3u}, 1u);  // a repeated step is allowed
    record(cipher, ctx, view, ContentHash{4u}, 5u);

    assert(cipher.head() == ContentHash{4u});
    assert(cipher.hash_at_step(view, 0u) == ContentHash{1u});
    assert(cipher.hash_at_step(view, 1u) == ContentHash{3u});  // the later of the two at step 1
    assert(cipher.hash_at_step(view, 5u) == ContentHash{4u});

    std::printf("  pre_clause_orthogonal:                  PASSED\n");
}

int main() {
    // Use a per-process temp dir so the test is hermetic.
    const auto tmpl =
        std::filesystem::temp_directory_path() / ("crucible_test_record_event_" + std::to_string(::getpid()));
    std::filesystem::create_directories(tmpl);
    const std::string base = tmpl.string();

    std::printf("test_cipher_record_event\n");
    test_t01_required_row_pinned();
    test_t02_subrow_accepted_shapes();
    test_t03_subrow_rejected_shapes();
    test_t04_record_event_is_durable(base.c_str());
    test_t05_round_trip(base.c_str());
    test_t06_bg_superset_row(base.c_str());
    test_t07_startup_superset_row(base.c_str());
    test_t08_monotonic_steps(base.c_str());
    test_t09_multiple_events_monotonic(base.c_str());
    test_t10_api_surface_pinned(base.c_str());
    test_t11_log_holds_monotonic_readings(base.c_str());

    test_required_row_header_fence();
    test_multi_event_durability(base.c_str());
    test_external_visibility();
    test_canonical_row_acceptance(base.c_str());
    test_pre_clause_orthogonal(base.c_str());

    std::error_code ec;
    std::filesystem::remove_all(tmpl, ec);

    std::printf("test_cipher_record_event: 16 groups, all passed\n");
    return 0;
}
