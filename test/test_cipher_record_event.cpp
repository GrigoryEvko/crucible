// Recording an event writes a file and blocks on a syscall.  So the caller
// has to hand in a context whose row admits IO and Block.  A caller on the
// foreground path holds no such context, and the call does not compile for
// it.  The commit stamps the log line with the next place of a count that
// the store keeps, and it reads no clock.  Everything here is the accepting
// side of that fence, and the stamps of the log.  The negative-compile
// fixtures hold the rejecting side.

#include <crucible/Cipher.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Process.h>
#include <foundation/effects/Row.h>
#include <foundation/os/PathText.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

// Opening the store and its open view need a context whose row admits IO
// and Block.
[[nodiscard]] inline ::fixy::TestRunnerCtx store_ctx() {
    return ::fixy::TestRunnerCtx{::foundation::effects::testing::test()};
}

using crucible::Cipher;
using crucible::ContentHash;
namespace eff = ::foundation::effects;

// The text of the root copies the characters of the directory.
[[nodiscard]] static Cipher open_cipher(::fixy::TextView dir) {
    ::foundation::core::GrowText root = ::foundation::core::mint_grow_text(Cipher::MAX_ROOT_PATH_BYTES);
    for (const char character : dir) {
        root.append(store_ctx().cap().alloc, character).expect("the text holds each character of the directory");
    }
    ::foundation::core::HeapText const root_path = ::foundation::core::freeze(std::move(root));
    return Cipher::open(store_ctx(), store_ctx().cap().alloc,
                        ::fixy::mint_tagged<::fixy::tags::source::External>(root_path.view()))
        .expect("the test opens its store");
}

// The store at the path name under the scratch directory of the test.  The
// open makes the directory of the store when it is missing.
[[nodiscard]] static Cipher open_store(::fixy::fs::TemporaryDirectory const& scratch, ::fixy::TextView name) {
    ::foundation::os::PathText<Cipher::MAX_ROOT_PATH_BYTES> path{};
    path.append(scratch.path().view()).expect("the scratch directory fits the path of the store");
    path.append('/').expect("the path of the store holds its slash");
    path.append(name).expect("the name fits the path of the store");
    return open_cipher(path.c_text().view());
}

// Makes the directory name under the scratch directory.  A directory that is
// there already is no error.
static void make_store_directory(::fixy::fs::TemporaryDirectory const& scratch, ::fixy::CText name) {
    static_cast<void>(::fixy::fs::make_directory_at(store_ctx(), scratch.ref(), name).is_ok());
}

// Records one event and requires that the commit happened.
template <typename Ctx>
static void record(Cipher& cipher, Ctx const& ctx, Cipher::OpenView const& view, ContentHash hash, std::uint64_t step) {
    const auto recorded = cipher.record_event(ctx, view, hash, step);
    CRUCIBLE_TEST_ASSERT(kTest, recorded.is_ok() && "record_event must commit when the writes pass");
}

// The step of a commit that failed.
[[nodiscard]] static Cipher::CommitError failure_of(Cipher::CommitResult const& recorded) {
    CRUCIBLE_TEST_ASSERT(kTest, recorded.is_err() && "the commit must report the failed write");
    return recorded.err().expect("the commit failed");
}

// True when the whole content of the file path under the scratch directory is
// text.  A file that does not read holds no text.
[[nodiscard]] static bool holds_text(::fixy::fs::TemporaryDirectory const& scratch, ::fixy::CText path,
                                     ::fixy::TextView text) {
    ::fixy::GrowText read = ::fixy::mint_grow_text(Cipher::MAX_OBJECT_BYTES);
    const bool is_read =
        ::fixy::fs::read_whole_at(store_ctx(), store_ctx().cap().alloc, scratch.ref(), path, read).is_ok();
    return is_read && read.with_view([text](::fixy::TextView held) noexcept { return held == text; });
}

// The largest count of the lines of a log that a test reads.
constexpr std::size_t kLogLines = 64;

// The place of each commit of a log.
using LogOrders = ::fixy::FixedList<std::uint64_t, kLogLines>;

// The third field of one line of the log: the field after the second comma.
[[nodiscard]] static std::uint64_t order_of_line(::fixy::TextView line) noexcept {
    // The search for the second comma starts after the first comma, or at
    // the start of the line when the line holds no comma.
    const std::size_t after_first = ::fixy::find(line, ',').match([](std::size_t first) noexcept { return first + 1; },
                                                                  [] noexcept { return std::size_t{0}; });
    const ::fixy::TextView tail = line.window(after_first, line.size() - after_first).expect("the tail is in the line");
    const ::fixy::Option<std::size_t> second_comma = ::fixy::find(tail, ',');
    CRUCIBLE_TEST_ASSERT(kTest, second_comma.is_some());
    const std::size_t field_start = after_first + ::fixy::Option<std::size_t>{second_comma}.value_or(0) + 1;
    const ::fixy::TextView field =
        line.window(field_start, line.size() - field_start).expect("the field is inside the line");
    ::fixy::Result<std::uint64_t, ::fixy::ParseError> order = ::fixy::parse<std::uint64_t>(field);
    CRUCIBLE_TEST_ASSERT(kTest, order.is_ok());
    return std::move(order).value_or(0);
}

// The third field of each line of the log at the path under the scratch
// directory: the place of each commit, in the order of the commits.
// Complexity: linear in the size of the log.
[[nodiscard]] static LogOrders log_orders(::fixy::fs::TemporaryDirectory const& scratch, ::fixy::CText path) {
    ::fixy::GrowText log = ::fixy::mint_grow_text(Cipher::MAX_OBJECT_BYTES);
    static_cast<void>(
        ::fixy::fs::read_whole_at(store_ctx(), store_ctx().cap().alloc, scratch.ref(), path, log).is_ok());
    return log.with_view([](::fixy::TextView text) noexcept {
        LogOrders orders{};
        ::fixy::TextView rest = text;
        while (rest.size() > 0) {
            const std::size_t end = ::fixy::find(rest, '\n').value_or(rest.size());
            const ::fixy::TextView line = rest.window(0, end).expect("the line is inside the log");
            static_cast<void>(orders.append(order_of_line(line)).is_ok());
            const std::size_t next = end < rest.size() ? end + 1 : end;
            rest = rest.window(next, rest.size() - next).expect("the rest of the log is inside the log");
        }
        return orders;
    });
}

// The directory of the root of cipher.
[[nodiscard]] static ::fixy::fs::DirectoryHandle root_directory_of(Cipher const& cipher) {
    ::fixy::TextView const root = cipher.root().expect("the store of the test is open");
    return ::fixy::fs::open_directory(store_ctx(),
                                      ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(root))
                                          .expect("the sanitizer passes the root of the test"))
        .expect("the root of the test opens");
}

// The whole text of the log of cipher.
[[nodiscard]] static ::fixy::HeapText log_text_of(Cipher const& cipher) {
    ::fixy::GrowText text = ::fixy::mint_grow_text(Cipher::MAX_OBJECT_BYTES);
    const ::fixy::fs::DirectoryHandle root = root_directory_of(cipher);
    CRUCIBLE_TEST_ASSERT(
        kTest, ::fixy::fs::read_whole_at(store_ctx(), store_ctx().cap().alloc, root.ref(), "log", text).is_ok());
    return ::fixy::freeze(std::move(text));
}

// Writes text as the whole file name under the root of cipher, such as the
// log or HEAD.  A store that opens the root after the write reads the text.
static void write_file_of(Cipher const& cipher, ::fixy::CText name, ::fixy::TextView text) {
    using WriteTruncate = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteTruncate>;
    const ::fixy::fs::DirectoryHandle root = root_directory_of(cipher);
    const ::fixy::fs::FileOf<WriteTruncate> file =
        ::fixy::fs::mint_file_at<WriteTruncate>(store_ctx(), root.ref(), name)
            .expect("the test opens the file of the store to write it")
            .consume();
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fs::write_full(store_ctx(), file.ref(), text).is_ok());
}

// Writes text as the whole log of cipher.
static void write_log_of(Cipher const& cipher, ::fixy::TextView text) { write_file_of(cipher, "log", text); }

// The report of an open of the store at the path name under the scratch
// directory.  The open runs in a child, so the test reads the standard error
// of the open.
[[nodiscard]] static ::fixy::process::ChildReport open_report_of(::fixy::fs::TemporaryDirectory const& scratch,
                                                                 ::fixy::TextView name) {
    const ::fixy::TestRunnerCtx child_ctx = store_ctx();
    ::fixy::process::ChildReport report =
        ::fixy::process::wait(
            ::fixy::process::spawn_child(child_ctx,
                                         [&scratch, name] noexcept { static_cast<void>(open_store(scratch, name)); })
                .expect("the test starts a child"))
            .expect("the test waits for its child");
    CRUCIBLE_TEST_ASSERT(kTest, report.is_exit_with(0) && "the open of the store ends with no error");
    return report;
}

// The count of the places in text where needle starts.  Complexity: one
// search of the rest of the text for each place.
[[nodiscard]] static std::size_t count_in(::fixy::TextView text, ::fixy::TextView needle) noexcept {
    std::size_t count = 0;
    ::fixy::TextView rest = text;
    bool is_found = true;
    while (is_found) {
        is_found =
            ::fixy::find(rest, needle)
                .match(
                    [&](std::size_t at) noexcept {
                        ++count;
                        const std::size_t next = at + needle.size();
                        rest =
                            rest.window(next, rest.size() - next).expect("the place after the needle is in the text");
                        return true;
                    },
                    [] noexcept { return false; });
    }
    return count;
}

static void test_t01_required_row_pinned() {
    static_assert(std::is_same_v<Cipher::record_event_required_row, eff::Row<eff::Effect::IO, eff::Effect::Block>>,
                  "Cipher::record_event_required_row MUST be exactly "
                  "Row<IO, Block>.  Adding an atom here tightens the API and "
                  "breaks every existing call site, so change the contract "
                  "first.");

    static_assert(eff::row_contains(^^Cipher::record_event_required_row, eff::Effect::IO));
    static_assert(eff::row_contains(^^Cipher::record_event_required_row, eff::Effect::Block));
    static_assert(!eff::row_contains(^^Cipher::record_event_required_row, eff::Effect::Alloc));
    static_assert(!eff::row_contains(^^Cipher::record_event_required_row, eff::Effect::Bg));

    static_assert(eff::row_size(^^Cipher::record_event_required_row) == 2);

    ::fixy::check::pass(kTest, "  T01 required_row_pinned:                 PASSED\n");
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

    // The commit reads no clock, so a context narrowed to IO and Block fits
    // the commit too: its row owns none of Bg, Init and Test, and it holds
    // no Nondet.
    using StoreOnly = decltype(store_ctx().in_row<Required>());
    static_assert(crucible::CtxFitsCipherPersistence<StoreOnly>);
    static_assert(requires(Cipher& cipher, StoreOnly const& ctx, Cipher::OpenView const& view) {
        cipher.record_event(ctx, view, ContentHash{1u}, 1u);
    });

    ::fixy::check::pass(kTest, "  T02 subrow_accepted_shapes:              PASSED\n");
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

    ::fixy::check::pass(kTest, "  T03 subrow_rejected_shapes:              PASSED\n");
}

// The commit is durable: a Cipher opened again on the same directory
// reads the head and the step from the files that record_event wrote.

static void test_t04_record_event_is_durable(::fixy::fs::TemporaryDirectory const& scratch) {
    make_store_directory(scratch, "t04");

    constexpr ContentHash kHash{0xC0FFEEBA12345678ULL};
    constexpr std::uint64_t kStep = 42u;
    {
        auto cipher = open_store(scratch, "t04");
        auto view = cipher.mint_open_view(store_ctx());
        record(cipher, store_ctx(), view, kHash, kStep);
    }

    // The head file holds sixteen hex digits and a newline.
    CRUCIBLE_TEST_ASSERT(kTest, holds_text(scratch, "t04/HEAD", "c0ffeeba12345678\n"));

    auto reopened = open_store(scratch, "t04");
    auto view = reopened.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, reopened.head() == kHash);
    CRUCIBLE_TEST_ASSERT(kTest, reopened.hash_at_step(view, kStep) == kHash);

    ::fixy::check::pass(kTest, "  T04 record_event_is_durable:             PASSED\n");
}

static void test_t05_round_trip(::fixy::fs::TemporaryDirectory const& scratch) {
    make_store_directory(scratch, "t05");
    auto cipher = open_store(scratch, "t05");
    auto view = cipher.mint_open_view(store_ctx());

    constexpr ContentHash kHash{0xDEADBEEFCAFEBABEULL};
    constexpr std::uint64_t kStep = 7u;

    record(cipher, store_ctx(), view, kHash, kStep);

    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == kHash);
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, kStep) == kHash);

    // A query past the end answers with the last recorded step.
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, kStep + 100) == kHash);

    ::fixy::check::pass(kTest, "  T05 round_trip:                          PASSED\n");
}

// The context of a background thread carries more than the two required
// atoms, and is accepted because it carries them both.

static void test_t06_bg_superset_row(::fixy::fs::TemporaryDirectory const& scratch) {
    make_store_directory(scratch, "t06");
    auto cipher = open_store(scratch, "t06");
    auto view = cipher.mint_open_view(store_ctx());

    const ::fixy::BgLoadCtx bg_ctx{::foundation::effects::testing::bg()};
    static_assert(std::is_same_v<typename ::fixy::BgLoadCtx::row_type,
                                 eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block,
                                          eff::Effect::Nondet, eff::Effect::Div>>);

    constexpr ContentHash kHash{0x123456789ABCDEF0ULL};
    record(cipher, bg_ctx, view, kHash, 1u);

    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == kHash);
    ::fixy::check::pass(kTest, "  T06 bg_superset_row:                     PASSED\n");
}

// The startup context carries Init on top of the two required atoms.

static void test_t07_startup_superset_row(::fixy::fs::TemporaryDirectory const& scratch) {
    make_store_directory(scratch, "t07");
    auto cipher = open_store(scratch, "t07");
    auto view = cipher.mint_open_view(store_ctx());

    const ::fixy::InitLoadCtx init_ctx{::foundation::effects::testing::init()};

    constexpr ContentHash kHash{0xFEDCBA9876543210ULL};
    record(cipher, init_ctx, view, kHash, 1u);

    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == kHash);
    ::fixy::check::pass(kTest, "  T07 startup_superset_row:                PASSED\n");
}

// Recording an ordered sequence and querying it back shows that the
// precondition on step order holds on the recorded log.

static void test_t08_monotonic_steps(::fixy::fs::TemporaryDirectory const& scratch) {
    make_store_directory(scratch, "t08");
    auto cipher = open_store(scratch, "t08");
    auto view = cipher.mint_open_view(store_ctx());
    const auto ctx = store_ctx();

    record(cipher, ctx, view, ContentHash{0xAA}, 1u);
    record(cipher, ctx, view, ContentHash{0xBB}, 5u);
    record(cipher, ctx, view, ContentHash{0xCC}, 10u);

    // The query answers with the last entry at or before the step
    // asked for.
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 0u) == ContentHash{});  // before all
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 1u) == ContentHash{0xAA});
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 3u) == ContentHash{0xAA});  // gap
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 5u) == ContentHash{0xBB});
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 7u) == ContentHash{0xBB});  // gap
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 10u) == ContentHash{0xCC});
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 99u) == ContentHash{0xCC});  // future

    ::fixy::check::pass(kTest, "  T08 monotonic_steps:                     PASSED\n");
}

static void test_t09_multiple_events_monotonic(::fixy::fs::TemporaryDirectory const& scratch) {
    make_store_directory(scratch, "t09");
    auto cipher = open_store(scratch, "t09");
    auto view = cipher.mint_open_view(store_ctx());
    const auto ctx = store_ctx();

    constexpr int N = 32;
    for (int i = 0; i < N; ++i) {
        record(cipher, ctx, view, ContentHash{std::uint64_t{0x1000u} + static_cast<std::uint64_t>(i)},
               static_cast<std::uint64_t>(i));
    }

    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0x1000ULL + (N - 1)});

    for (int i = 0; i < N; ++i) {
        const auto h = cipher.hash_at_step(view, static_cast<std::uint64_t>(i));
        CRUCIBLE_TEST_ASSERT(kTest, h == ContentHash{std::uint64_t{0x1000u} + static_cast<std::uint64_t>(i)});
    }

    ::fixy::check::pass(kTest, "  T09 multiple_events_monotonic:           PASSED\n");
}

// The context travels as the first argument, and the result says whether
// the commit happened.  Dropping the context would keep every call site
// compiling for a caller who holds no evidence and lose the fence, so the
// argument list and the result type are pinned here.

static void test_t10_api_surface_pinned(::fixy::fs::TemporaryDirectory const& scratch) {
    make_store_directory(scratch, "t10");
    auto cipher = open_store(scratch, "t10");
    auto view = cipher.mint_open_view(store_ctx());

    using Ctx = ::fixy::TestRunnerCtx;
    using Result = decltype(cipher.record_event(std::declval<Ctx const&>(), view, ContentHash{1u}, std::uint64_t{1u}));
    static_assert(std::is_same_v<Result, ::fixy::Result<::fixy::Unit, Cipher::CommitError>>);

    // A context, the view and two runtime arguments.
    static_assert(std::is_invocable_r_v<::fixy::Result<::fixy::Unit, Cipher::CommitError>,
                                        decltype([](Cipher& c, Ctx const& ctx, Cipher::OpenView const& v, ContentHash h,
                                                    std::uint64_t s) { return c.record_event(ctx, v, h, s); }),
                                        Cipher&, Ctx const&, Cipher::OpenView const&, ContentHash, std::uint64_t>);

    record(cipher, store_ctx(), view, ContentHash{42u}, 0u);
    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{42u});

    ::fixy::check::pass(kTest, "  T10 api_surface_pinned:                  PASSED\n");
}

// Each log line holds the place of its commit: one for the first commit,
// and one more for each commit after it.

static void test_t11_log_holds_the_places(::fixy::fs::TemporaryDirectory const& scratch) {
    make_store_directory(scratch, "t11");
    auto cipher = open_store(scratch, "t11");
    auto view = cipher.mint_open_view(store_ctx());
    const auto ctx = store_ctx();

    constexpr int N = 8;
    for (int i = 0; i < N; ++i) {
        record(cipher, ctx, view, ContentHash{std::uint64_t{0x2000u} + static_cast<std::uint64_t>(i)},
               static_cast<std::uint64_t>(i));
    }

    const LogOrders orders = log_orders(scratch, "t11/log");
    CRUCIBLE_TEST_ASSERT(kTest, orders.size() == static_cast<std::size_t>(N));
    std::uint64_t place = 0;
    for (const std::uint64_t order : orders) {
        ++place;
        CRUCIBLE_TEST_ASSERT(kTest, order == place);
    }

    ::fixy::check::pass(kTest, "  T11 log_holds_the_places:                PASSED\n");
}

// Two runs of one computation write the same log, byte for byte.  On a log
// that holds a clock time, the two logs hold two times.

// Records the three commits of one computation: two of them at one step.
static void record_one_computation(Cipher& cipher) {
    const auto view = cipher.mint_open_view(store_ctx());
    record(cipher, store_ctx(), view, ContentHash{0x3001u}, 1u);
    record(cipher, store_ctx(), view, ContentHash{0x3002u}, 4u);
    record(cipher, store_ctx(), view, ContentHash{0x3003u}, 4u);
}

static void test_t12_two_runs_write_one_log(::fixy::fs::TemporaryDirectory const& scratch) {
    auto first = open_store(scratch, "t12_first");
    auto second = open_store(scratch, "t12_second");
    record_one_computation(first);
    record_one_computation(second);

    const ::fixy::HeapText first_log = log_text_of(first);
    const ::fixy::HeapText second_log = log_text_of(second);
    CRUCIBLE_TEST_ASSERT(kTest, first_log.view() == second_log.view());
    CRUCIBLE_TEST_ASSERT(kTest, first_log.view()
                                    == ::fixy::TextView{"1,0000000000003001,1\n"
                                                        "4,0000000000003002,2\n"
                                                        "4,0000000000003003,3\n"});

    ::fixy::check::pass(kTest, "  T12 two_runs_write_one_log:              PASSED\n");
}

// A store that opens again continues the count from the place of the last
// line of its log.

static void test_t13_reopen_continues_the_places(::fixy::fs::TemporaryDirectory const& scratch) {
    {
        auto cipher = open_store(scratch, "t13");
        const auto view = cipher.mint_open_view(store_ctx());
        record(cipher, store_ctx(), view, ContentHash{0x4001u}, 1u);
        record(cipher, store_ctx(), view, ContentHash{0x4002u}, 2u);
    }
    auto cipher = open_store(scratch, "t13");
    const auto view = cipher.mint_open_view(store_ctx());
    record(cipher, store_ctx(), view, ContentHash{0x4003u}, 3u);

    const ::fixy::HeapText log = log_text_of(cipher);
    CRUCIBLE_TEST_ASSERT(kTest, log.view()
                                    == ::fixy::TextView{"1,0000000000004001,1\n"
                                                        "2,0000000000004002,2\n"
                                                        "3,0000000000004003,3\n"});

    ::fixy::check::pass(kTest, "  T13 reopen_continues_the_places:         PASSED\n");
}

// A line whose place does not grow is a corrupt line, and the open skips
// it.  The count continues from the last line that the open reads.

static void test_t14_open_skips_a_place_that_does_not_grow(::fixy::fs::TemporaryDirectory const& scratch) {
    write_log_of(open_store(scratch, "t14"), ::fixy::TextView{"1,0000000000005001,5\n"
                                                              "2,0000000000005002,5\n"
                                                              "3,0000000000005003,4\n"
                                                              "4,0000000000005004,6\n"});
    auto cipher = open_store(scratch, "t14");
    const auto view = cipher.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0x5004u});
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 2u) == ContentHash{0x5001u});
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 3u) == ContentHash{0x5001u});

    record(cipher, store_ctx(), view, ContentHash{0x5005u}, 5u);
    const ::fixy::HeapText log = log_text_of(cipher);
    CRUCIBLE_TEST_ASSERT(kTest, log.view()
                                    == ::fixy::TextView{"1,0000000000005001,5\n"
                                                        "2,0000000000005002,5\n"
                                                        "3,0000000000005003,4\n"
                                                        "4,0000000000005004,6\n"
                                                        "5,0000000000005005,7\n"});

    ::fixy::check::pass(kTest, "  T14 open_skips_a_place_that_does_not_grow: PASSED\n");
}

// A log whose last order is one below MAX_COMMIT_ORDER leaves the next commit
// the last place.  The commit at that place loads again.  The commit after it
// has no place: it reports order_exhausted, and the head does not move.

static void test_t15_no_place_after_the_ceiling(::fixy::fs::TemporaryDirectory const& scratch) {
    static_assert(Cipher::MAX_COMMIT_ORDER == 4611686018427387903u);
    constexpr ::fixy::TextView below{"1,0000000000006001,4611686018427387902\n"};
    write_log_of(open_store(scratch, "t15"), below);
    {
        auto cipher = open_store(scratch, "t15");
        const auto view = cipher.mint_open_view(store_ctx());
        CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0x6001u});
        record(cipher, store_ctx(), view, ContentHash{0x6002u}, 2u);
    }

    auto cipher = open_store(scratch, "t15");
    const auto view = cipher.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0x6002u} && "the commit at the ceiling loads again");
    CRUCIBLE_TEST_ASSERT(kTest, failure_of(cipher.record_event(store_ctx(), view, ContentHash{0x6003u}, 3u))
                                    == Cipher::CommitError::order_exhausted);
    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0x6002u});
    const ::fixy::HeapText log = log_text_of(cipher);
    CRUCIBLE_TEST_ASSERT(kTest, log.view()
                                    == ::fixy::TextView{"1,0000000000006001,4611686018427387902\n"
                                                        "2,0000000000006002,4611686018427387903\n"});

    ::fixy::check::pass(kTest, "  T15 no_place_after_the_ceiling:          PASSED\n");
}

// A line whose order is above MAX_COMMIT_ORDER is corrupt, as a line whose
// step is above MAX_COMMIT_STEP is.  The open skips the line and reports it,
// and the lines after it load.  The next commit takes the order after the
// last line that loads.  The old store took the order 2^64 - 1, skipped each
// later line, and refused each later commit with order_exhausted.

static void test_t16_open_skips_an_order_above_the_ceiling(::fixy::fs::TemporaryDirectory const& scratch) {
    write_log_of(open_store(scratch, "t16"), ::fixy::TextView{"1,0000000000007001,1\n"
                                                              "2,0000000000007002,18446744073709551615\n"
                                                              "3,0000000000007003,3\n"});
    auto cipher = open_store(scratch, "t16");
    const auto view = cipher.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0x7003u});
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 2u) == ContentHash{0x7001u});
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 3u) == ContentHash{0x7003u});

    record(cipher, store_ctx(), view, ContentHash{0x7004u}, 4u);
    const ::fixy::HeapText log = log_text_of(cipher);
    CRUCIBLE_TEST_ASSERT(kTest, log.view()
                                    == ::fixy::TextView{"1,0000000000007001,1\n"
                                                        "2,0000000000007002,18446744073709551615\n"
                                                        "3,0000000000007003,3\n"
                                                        "4,0000000000007004,4\n"});

    const ::fixy::process::ChildReport report = open_report_of(scratch, "t16");
    CRUCIBLE_TEST_ASSERT(kTest, count_in(report.output(), "[crucible] the Cipher store skips the line ") == 1);
    constexpr ::fixy::TextView kSkip{
        "[crucible] the Cipher store skips the line 2 of its head log, with the step 2 and the order "
        "18446744073709551615: the order is above MAX_COMMIT_ORDER\n"};
    CRUCIBLE_TEST_ASSERT(kTest, count_in(report.output(), kSkip) == 1);

    ::fixy::check::pass(kTest, "  T16 open_skips_an_order_above_the_ceiling: PASSED\n");
}

// The open skips each kind of corrupt line, and reports each skipped line
// one time, with its number and its reason.  The lines between them load.

static void test_t17_open_reports_each_skipped_line(::fixy::fs::TemporaryDirectory const& scratch) {
    write_log_of(open_store(scratch, "t17"), ::fixy::TextView{"1,0000000000008001,1\n"
                                                              "a line with no field\n"
                                                              "4611686018427387904,0000000000008002,2\n"
                                                              "2,0000000000008003,4611686018427387904\n"
                                                              "0,0000000000008004,3\n"
                                                              "2,0000000000008005,1\n"
                                                              "2,0000000000008006,4\n"
                                                              "3,0000000000008007,5"});
    auto cipher = open_store(scratch, "t17");
    const auto view = cipher.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0x8006u});
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 1u) == ContentHash{0x8001u});
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 3u) == ContentHash{0x8006u});
    CRUCIBLE_TEST_ASSERT(
        kTest, cipher.last_committed_step().is_some_and([](::fixy::session::StepId step) { return step.raw() == 2u; }));

    const ::fixy::process::ChildReport report = open_report_of(scratch, "t17");
    const ::fixy::TextView output = report.output();
    CRUCIBLE_TEST_ASSERT(kTest, count_in(output, "[crucible] the Cipher store skips the line ") == 6);
    constexpr ::fixy::FixedArray<::fixy::TextView, 6> kReports{
        "the line 2 of its head log: the line is not a decimal step, a hexadecimal hash and a decimal order\n",
        "the line 3 of its head log, with the step 4611686018427387904 and the order 2: the step is above "
        "MAX_COMMIT_STEP\n",
        "the line 4 of its head log, with the step 2 and the order 4611686018427387904: the order is above "
        "MAX_COMMIT_ORDER\n",
        "the line 5 of its head log, with the step 0 and the order 3: the step is below the step of the last line "
        "that the load took\n",
        "the line 6 of its head log, with the step 2 and the order 1: the order is not above the order of the last "
        "line that the load took\n",
        "the line 8 of its head log: the last line has no newline, so its append did not end\n",
    };
    for (::fixy::Index<6> const i : kReports.indices()) {
        CRUCIBLE_TEST_ASSERT(kTest, count_in(output, kReports.at(i)) == 1);
    }

    ::fixy::check::pass(kTest, "  T17 open_reports_each_skipped_line:      PASSED\n");
}

// A line whose hash is zero is corrupt: zero is the sentinel of no content,
// and record_event refuses it.  The open skips the line and reports it, so
// the head is the hash of the last line that loads.  The old store took the
// line as a commit with no head.  It then took the head from a HEAD that
// names no commit of the log, gave no content at the step of the line, and
// refused a commit at the step of the last sound line.

static void test_t18_open_skips_a_zero_hash(::fixy::fs::TemporaryDirectory const& scratch) {
    {
        const Cipher made = open_store(scratch, "t18");
        write_file_of(made, "HEAD", ::fixy::TextView{"00000000000090ee\n"});
        write_log_of(made, ::fixy::TextView{"1,0000000000009001,1\n"
                                            "2,0000000000000000,2\n"});
    }
    auto cipher = open_store(scratch, "t18");
    const auto view = cipher.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0x9001u} && "the head is the last commit of the log");
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 2u) == ContentHash{0x9001u});
    CRUCIBLE_TEST_ASSERT(
        kTest, cipher.last_committed_step().is_some_and([](::fixy::session::StepId step) { return step.raw() == 1u; }));
    record(cipher, store_ctx(), view, ContentHash{0x9002u}, 1u);
    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0x9002u});

    const ::fixy::process::ChildReport report = open_report_of(scratch, "t18");
    constexpr ::fixy::TextView kSkip{
        "[crucible] the Cipher store skips the line 2 of its head log, with the step 2 and the order 2: the hash is "
        "zero, and no commit names the hash zero\n"};
    CRUCIBLE_TEST_ASSERT(kTest, count_in(report.output(), kSkip) == 1);

    ::fixy::check::pass(kTest, "  T18 open_skips_a_zero_hash:              PASSED\n");
}

// The header asserts the content of the required row where it
// declares it.  Restating those assertions here makes this binary's
// compilation depend on them as well, and puts them where someone
// reading the test can find them.

static void test_required_row_header_fence() {
    static_assert(std::is_same_v<Cipher::record_event_required_row, eff::Row<eff::Effect::IO, eff::Effect::Block>>);
    static_assert(eff::row_size(^^Cipher::record_event_required_row) == 2u);
    static_assert(eff::row_contains(^^Cipher::record_event_required_row, eff::Effect::IO));
    static_assert(eff::row_contains(^^Cipher::record_event_required_row, eff::Effect::Block));
    static_assert(!eff::row_contains(^^Cipher::record_event_required_row, eff::Effect::Alloc));
    static_assert(!eff::row_contains(^^Cipher::record_event_required_row, eff::Effect::Bg));
    static_assert(!eff::row_contains(^^Cipher::record_event_required_row, eff::Effect::Init));
    static_assert(!eff::row_contains(^^Cipher::record_event_required_row, eff::Effect::Test));

    ::fixy::check::pass(kTest, "  required_row_header_fence:              PASSED\n");
}

// One record is not enough to show that the files read back.  A defect
// that shows only after several records, or only on some hash values,
// needs a longer and more varied sequence than that.

static void test_multi_event_durability(::fixy::fs::TemporaryDirectory const& scratch) {
    make_store_directory(scratch, "multi_event");

    constexpr std::size_t N = 16;
    constexpr ::fixy::FixedArray<std::uint64_t, N> kHashes{
        0x0001000100010001ULL, 0xFFFE000200030004ULL, 0xC0FFEE00000000ULL,   0xDEADBEEFCAFEBABEULL,
        0x123456789ABCDEF0ULL, 0xFEDCBA9876543210ULL, 0x5555555555555555ULL, 0xAAAAAAAAAAAAAAAAULL,
        0x0F0F0F0F0F0F0F0FULL, 0xF0F0F0F0F0F0F0F0ULL, 0x1111222233334444ULL, 0x4444333322221111ULL,
        0x8000000000000001ULL, 0x7FFFFFFFFFFFFFFEULL, 0x0123456789ABCDEFULL, 0xFEDCBA9876543211ULL,
    };

    {
        auto cipher = open_store(scratch, "multi_event");
        auto view = cipher.mint_open_view(store_ctx());
        const auto ctx = store_ctx();
        for (::fixy::Index<N> const i : kHashes.indices()) {
            record(cipher, ctx, view, ContentHash{kHashes.at(i)}, std::uint64_t{i.raw()});
        }
    }

    CRUCIBLE_TEST_ASSERT(kTest, holds_text(scratch, "multi_event/HEAD", "fedcba9876543211\n"));
    CRUCIBLE_TEST_ASSERT(kTest, log_orders(scratch, "multi_event/log").size() == N);

    auto reopened = open_store(scratch, "multi_event");
    auto view = reopened.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, reopened.head() == ContentHash{kHashes.at(::fixy::index<N - 1>())});
    for (::fixy::Index<N> const i : kHashes.indices()) {
        CRUCIBLE_TEST_ASSERT(kTest, reopened.hash_at_step(view, std::uint64_t{i.raw()}) == ContentHash{kHashes.at(i)});
    }

    ::fixy::check::pass(kTest, "  multi_event_durability:                 PASSED\n");
}

// Every other use of the required row in this file is inside a
// function.  Naming it at namespace scope is what a caller outside the
// store does, and only that spelling proves it is publicly reachable.

namespace external_visibility {
using ExtRow = ::crucible::Cipher::record_event_required_row;
static_assert(eff::row_size(^^ExtRow) == 2u);
static_assert(eff::Subrow<ExtRow, eff::Row<eff::Effect::IO, eff::Effect::Block>>);
static_assert(eff::Subrow<ExtRow, eff::Row<eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block, eff::Effect::Bg>>);
static_assert(!eff::Subrow<ExtRow, eff::Row<>>);
static_assert(eff::CtxAdmits<::fixy::TestRunnerCtx, ExtRow>);
}  // namespace external_visibility

static void test_external_visibility() {
    using ExtRow = external_visibility::ExtRow;
    static_assert(eff::row_size(^^ExtRow) == 2u);
    ::fixy::check::pass(kTest, "  external_visibility:                    PASSED\n");
}

// Each context that fits the commit is interchangeable at a call site,
// including a context narrowed to the two required atoms.

static void test_canonical_row_acceptance(::fixy::fs::TemporaryDirectory const& scratch) {
    make_store_directory(scratch, "canonical_row");
    auto cipher = open_store(scratch, "canonical_row");
    auto view = cipher.mint_open_view(store_ctx());

    record(cipher, store_ctx(), view, ContentHash{0xAAAA}, 1u);
    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0xAAAA});

    // A commit reads no clock, so the narrowed row needs no Nondet.
    const auto narrowed = store_ctx().in_row<eff::Row<eff::Effect::IO, eff::Effect::Block>>();
    record(cipher, narrowed, view, ContentHash{0xBBBB}, 2u);
    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0xBBBB});

    // The same two atoms in another order, which containment by
    // membership accepts.
    const auto reordered = store_ctx().in_row<eff::Row<eff::Effect::Block, eff::Effect::IO>>();
    record(cipher, reordered, view, ContentHash{0xCCCC}, 3u);
    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0xCCCC});

    ::fixy::check::pass(kTest, "  canonical_row_acceptance:               PASSED\n");
}

// The context gate fires while the template is substituted and the
// precondition fires when the call runs, so satisfying the first says
// nothing about the second.  Violating the precondition would end the
// process, so what is checked here is that a caller who satisfies the
// gate still gets the ordered behaviour the precondition describes.

static void test_pre_clause_orthogonal(::fixy::fs::TemporaryDirectory const& scratch) {
    make_store_directory(scratch, "pre_clause");
    auto cipher = open_store(scratch, "pre_clause");
    auto view = cipher.mint_open_view(store_ctx());
    const auto ctx = store_ctx();

    record(cipher, ctx, view, ContentHash{1u}, 0u);
    record(cipher, ctx, view, ContentHash{2u}, 1u);
    record(cipher, ctx, view, ContentHash{3u}, 1u);  // a repeated step is allowed
    record(cipher, ctx, view, ContentHash{4u}, 5u);

    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{4u});
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 0u) == ContentHash{1u});
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 1u) == ContentHash{3u});  // the later of the two at step 1
    CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 5u) == ContentHash{4u});

    ::fixy::check::pass(kTest, "  pre_clause_orthogonal:                  PASSED\n");
}

// A store opened at a path makes a directory there.  A directory at the
// path of the log makes its append fail, and a directory at the path of
// HEAD makes the rename of the new HEAD fail, also for the root user.  Each
// writer of HEAD writes a temporary file of its own name, so no directory
// at a temporary name blocks the write.

// The log append is the commit.  When it fails, the commit reports the
// failure and the head does not move, in memory and in HEAD.
static void test_failed_log_append_moves_no_head(::fixy::fs::TemporaryDirectory const& scratch) {
    CRUCIBLE_TEST_ASSERT(kTest, open_store(scratch, "log_blocked/log").is_open());

    constexpr ContentHash kHash{0xA11CE5EEDULL};
    constexpr std::uint64_t kStep = 3u;
    {
        auto cipher = open_store(scratch, "log_blocked");
        auto view = cipher.mint_open_view(store_ctx());
        CRUCIBLE_TEST_ASSERT(kTest, failure_of(cipher.record_event(store_ctx(), view, kHash, kStep))
                                        == Cipher::CommitError::log_append);
        CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{});
        CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, kStep) == ContentHash{});
    }

    // The log holds no commit, so the open reads HEAD.  The failed commit
    // wrote no HEAD, so the reopen has no head.
    auto reopened = open_store(scratch, "log_blocked");
    auto view = reopened.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, reopened.head() == ContentHash{});
    CRUCIBLE_TEST_ASSERT(kTest, reopened.hash_at_step(view, kStep) == ContentHash{});

    ::fixy::check::pass(kTest, "  failed_log_append_moves_no_head:        PASSED\n");
}

// A commit whose log append fails after a part of its line cuts the log back
// to its whole lines.  The log holds its bytes of before the commit, the head
// does not move, and a reopen reads the last good commit.  The write fault of
// the test cuts the line after five bytes, as a full disk does.

static void test_failed_log_append_cuts_back(::fixy::fs::TemporaryDirectory const& scratch) {
    {
        auto cipher = open_store(scratch, "log_fault");
        auto view = cipher.mint_open_view(store_ctx());
        record(cipher, store_ctx(), view, ContentHash{0xA001u}, 1u);
        const ::fixy::HeapText before = log_text_of(cipher);
        {
            const ::fixy::fs::WriteFaultScope fault = ::fixy::fs::WriteFaultScope::arm(
                kTest, ::foundation::core::ByteCount{5}, ::fixy::fs::WriteFailure::no_space);
            CRUCIBLE_TEST_ASSERT(kTest, failure_of(cipher.record_event(store_ctx(), view, ContentHash{0xA002u}, 2u))
                                            == Cipher::CommitError::log_append);
        }
        const ::fixy::HeapText after = log_text_of(cipher);
        CRUCIBLE_TEST_ASSERT(kTest, after.view() == before.view() && "the failed append leaves no part of its line");
        CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0xA001u});
    }

    auto reopened = open_store(scratch, "log_fault");
    CRUCIBLE_TEST_ASSERT(kTest, reopened.head() == ContentHash{0xA001u});
    CRUCIBLE_TEST_ASSERT(kTest, reopened.last_committed_step().is_some_and(
                                    [](::fixy::session::StepId step) { return step.raw() == 1u; }));

    ::fixy::check::pass(kTest, "  failed_log_append_cuts_back:            PASSED\n");
}

// A commit whose write of the new HEAD fails after a part of its bytes keeps
// the old HEAD whole, and the durable replace removes its temporary file.  The
// log append comes first and passes, so the commit stands: the head moves, and
// a reopen takes it from the log.  The write fault of the test lets the log
// line pass whole and cuts the new HEAD after five bytes.

static void test_failed_head_write_keeps_the_old_head(::fixy::fs::TemporaryDirectory const& scratch) {
    constexpr ::fixy::TextView kSecondLine{"2,000000000000b002,2\n"};
    {
        auto cipher = open_store(scratch, "head_fault");
        auto view = cipher.mint_open_view(store_ctx());
        record(cipher, store_ctx(), view, ContentHash{0xB001u}, 1u);
        {
            const ::fixy::fs::WriteFaultScope fault = ::fixy::fs::WriteFaultScope::arm(
                kTest, ::foundation::core::ByteCount{kSecondLine.size() + 5}, ::fixy::fs::WriteFailure::no_space);
            CRUCIBLE_TEST_ASSERT(kTest, failure_of(cipher.record_event(store_ctx(), view, ContentHash{0xB002u}, 2u))
                                            == Cipher::CommitError::head_write);
        }
        CRUCIBLE_TEST_ASSERT(kTest, holds_text(scratch, "head_fault/HEAD", "000000000000b001\n")
                                        && "the failed write keeps the old HEAD whole");
        CRUCIBLE_TEST_ASSERT(
            kTest, !::fixy::fs::has_entry_at(store_ctx(), scratch.ref(), "head_fault/HEAD.tmp-00").value_or(true)
                       && "the failed write removes its temporary file");
        CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == ContentHash{0xB002u});
    }

    auto reopened = open_store(scratch, "head_fault");
    CRUCIBLE_TEST_ASSERT(kTest, reopened.head() == ContentHash{0xB002u});

    ::fixy::check::pass(kTest, "  failed_head_write_keeps_the_old_head:   PASSED\n");
}

// A failed HEAD write comes after a durable log append.  The commit
// reports the failure, and the commit stands: the head moves in memory,
// and a reopen takes the same head from the log, while HEAD names no
// commit.  The write leaves no temporary file of its own.
static void test_failed_head_write_keeps_the_logged_commit(::fixy::fs::TemporaryDirectory const& scratch) {
    constexpr ContentHash kFirst{0x0F1257ULL};
    constexpr ContentHash kSecond{0x5EC0DDULL};
    {
        auto cipher = open_store(scratch, "head_blocked");
        auto view = cipher.mint_open_view(store_ctx());
        record(cipher, store_ctx(), view, kFirst, 1u);

        const ::fixy::fs::DirectoryHandle root = root_directory_of(cipher);
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fs::unlink_at(store_ctx(), root.ref(), "HEAD").is_ok()
                                        && ::fixy::fs::make_directory_at(store_ctx(), root.ref(), "HEAD").is_ok());

        CRUCIBLE_TEST_ASSERT(kTest, failure_of(cipher.record_event(store_ctx(), view, kSecond, 2u))
                                        == Cipher::CommitError::head_write);
        CRUCIBLE_TEST_ASSERT(kTest, !::fixy::fs::has_entry_at(store_ctx(), root.ref(), "HEAD.tmp-00").value_or(true)
                                        && "a failed HEAD write removes its temporary file");
        CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == kSecond);
        CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(view, 2u) == kSecond);
    }

    auto reopened = open_store(scratch, "head_blocked");
    auto view = reopened.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, reopened.head() == kSecond);
    CRUCIBLE_TEST_ASSERT(kTest, reopened.hash_at_step(view, 1u) == kFirst);
    CRUCIBLE_TEST_ASSERT(kTest, reopened.hash_at_step(view, 2u) == kSecond);

    ::fixy::check::pass(kTest, "  failed_head_write_keeps_logged_commit:  PASSED\n");
}

// The open reads HEAD when the log holds no commit.  HEAD holds sixteen
// hexadecimal digits and a newline, and the open reads the digits before
// the newline.  The store then has a head and no logged step.
static void test_head_without_logged_commit(::fixy::fs::TemporaryDirectory const& scratch) {
    constexpr ContentHash kHash{0xABCDEF0123456789ULL};
    {
        auto cipher = open_store(scratch, "head_only");
        auto view = cipher.mint_open_view(store_ctx());
        record(cipher, store_ctx(), view, kHash, 1u);
    }

    static_cast<void>(::fixy::fs::unlink_at(store_ctx(), scratch.ref(), "head_only/log").is_ok());
    auto reopened = open_store(scratch, "head_only");
    auto view = reopened.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, reopened.head() == kHash);
    CRUCIBLE_TEST_ASSERT(kTest, reopened.hash_at_step(view, 1u) == ContentHash{});

    ::fixy::check::pass(kTest, "  head_without_logged_commit:             PASSED\n");
}

int main() {
    // A scratch directory of this process, so the test is hermetic.  The end
    // of its owner removes it with each store below it.
    const ::fixy::fs::TemporaryDirectory scratch =
        std::move(::fixy::fs::mint_temporary_directory(store_ctx()).expect("the test makes its scratch directory"))
            .consume();
    CRUCIBLE_TEST_ASSERT(kTest, scratch.is_open());

    ::fixy::report(::fixy::Sink::Out, "test_cipher_record_event\n");
    test_t01_required_row_pinned();
    test_t02_subrow_accepted_shapes();
    test_t03_subrow_rejected_shapes();
    test_t04_record_event_is_durable(scratch);
    test_t05_round_trip(scratch);
    test_t06_bg_superset_row(scratch);
    test_t07_startup_superset_row(scratch);
    test_t08_monotonic_steps(scratch);
    test_t09_multiple_events_monotonic(scratch);
    test_t10_api_surface_pinned(scratch);
    test_t11_log_holds_the_places(scratch);
    test_t12_two_runs_write_one_log(scratch);
    test_t13_reopen_continues_the_places(scratch);
    test_t14_open_skips_a_place_that_does_not_grow(scratch);
    test_t15_no_place_after_the_ceiling(scratch);
    test_t16_open_skips_an_order_above_the_ceiling(scratch);
    test_t17_open_reports_each_skipped_line(scratch);
    test_t18_open_skips_a_zero_hash(scratch);

    test_required_row_header_fence();
    test_multi_event_durability(scratch);
    test_external_visibility();
    test_canonical_row_acceptance(scratch);
    test_pre_clause_orthogonal(scratch);
    test_failed_log_append_moves_no_head(scratch);
    test_failed_log_append_cuts_back(scratch);
    test_failed_head_write_keeps_the_old_head(scratch);
    test_failed_head_write_keeps_the_logged_commit(scratch);
    test_head_without_logged_commit(scratch);

    ::fixy::check::pass(kTest, "test_cipher_record_event: 23 groups, all passed\n");
    return 0;
}
