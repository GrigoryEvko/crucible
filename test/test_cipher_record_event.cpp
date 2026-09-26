// Recording an event writes a file and blocks on a syscall, so the
// caller has to hand in a context whose row says as much.  A caller on
// the foreground path holds no such context, and the call does not
// compile for it.  Everything here is the accepting side of that fence.
// The negative-compile fixtures hold the rejecting side.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include "test_assert.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <type_traits>
#include <utility>

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

    std::printf("  T03 subrow_rejected_shapes:              PASSED\n");
}

// The fenced call must write what the unfenced one writes.  Two
// ciphers on separate directories take the same arguments by the two
// routes, and their files are then compared.

static void test_t04_record_event_matches_advance_head(const char* base_dir) {
    const std::string dir_a = std::string(base_dir) + "/t04_a";
    const std::string dir_b = std::string(base_dir) + "/t04_b";
    std::filesystem::create_directories(dir_a);
    std::filesystem::create_directories(dir_b);

    auto cipher_a = open_cipher(dir_a);
    auto cipher_b = open_cipher(dir_b);
    auto view_a = cipher_a.mint_open_view(store_ctx());
    auto view_b = cipher_b.mint_open_view(store_ctx());

    constexpr ContentHash kHash{0xC0FFEEBA12345678ULL};
    constexpr std::uint64_t kStep = 42u;

    cipher_a.advance_head(view_a, kHash, kStep);
    cipher_b.record_event(store_ctx(), view_b, kHash, kStep);

    // The head file holds sixteen hex digits and a newline.
    auto read_head = [](const std::string& dir) {
        std::ifstream f(dir + "/HEAD");
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return s;
    };
    assert(read_head(dir_a) == read_head(dir_b));

    assert(cipher_a.head() == cipher_b.head());
    assert(cipher_a.head() == kHash);

    std::printf("  T04 record_event_matches_advance_head:   PASSED\n");
}

static void test_t05_round_trip(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t05";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());

    constexpr ContentHash kHash{0xDEADBEEFCAFEBABEULL};
    constexpr std::uint64_t kStep = 7u;

    cipher.record_event(store_ctx(), view, kHash, kStep);

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
    cipher.record_event(bg_ctx, view, kHash, 1u);

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
    cipher.record_event(init_ctx, view, kHash, 1u);

    assert(cipher.head() == kHash);
    std::printf("  T07 startup_superset_row:                PASSED\n");
}

// The precondition on step order comes from the unfenced call and is
// restated by the fenced one.  Recording an ordered sequence and
// querying it back is what shows the restatement is intact.

static void test_t08_monotonic_steps(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t08";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());
    const auto ctx = store_ctx();

    cipher.record_event(ctx, view, ContentHash{0xAA}, 1u);
    cipher.record_event(ctx, view, ContentHash{0xBB}, 5u);
    cipher.record_event(ctx, view, ContentHash{0xCC}, 10u);

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
        cipher.record_event(ctx, view, ContentHash{std::uint64_t{0x1000u} + static_cast<std::uint64_t>(i)},
                            static_cast<std::uint64_t>(i));
    }

    assert(cipher.head() == ContentHash{0x1000ULL + (N - 1)});

    for (int i = 0; i < N; ++i) {
        const auto h = cipher.hash_at_step(view, static_cast<std::uint64_t>(i));
        assert(h == ContentHash{std::uint64_t{0x1000u} + static_cast<std::uint64_t>(i)});
    }

    std::printf("  T09 multiple_events_monotonic:           PASSED\n");
}

// The context travels as the first argument.  Dropping it would keep
// every call site compiling for a caller who holds no evidence and lose
// the fence, so the argument list is pinned here.

static void test_t10_api_surface_pinned(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t10";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());

    using Ctx = ::fixy::TestRunnerCtx;
    using Result = decltype(cipher.record_event(std::declval<Ctx const&>(), view, ContentHash{1u}, std::uint64_t{1u}));
    static_assert(std::is_same_v<Result, void>);

    // A context, the view and two runtime arguments.
    static_assert(std::is_invocable_r_v<void,
                                        decltype([](Cipher& c, Ctx const& ctx, Cipher::OpenView const& v, ContentHash h,
                                                    std::uint64_t s) { c.record_event(ctx, v, h, s); }),
                                        Cipher&, Ctx const&, Cipher::OpenView const&, ContentHash, std::uint64_t>);

    cipher.record_event(store_ctx(), view, ContentHash{42u}, 0u);
    assert(cipher.head() == ContentHash{42u});

    std::printf("  T10 api_surface_pinned:                  PASSED\n");
}

// The header asserts the content of the required row where it
// declares it.  Restating those assertions here makes this binary's
// compilation depend on them as well, and puts them where someone
// reading the test can find them.

static void test_audit_a_required_row_header_fence() {
    static_assert(std::is_same_v<Cipher::record_event_required_row, eff::Row<eff::Effect::IO, eff::Effect::Block>>);
    static_assert(eff::row_size_v<Cipher::record_event_required_row> == 2u);
    static_assert(eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::IO>);
    static_assert(eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Block>);
    static_assert(!eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Alloc>);
    static_assert(!eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Bg>);
    static_assert(!eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Init>);
    static_assert(!eff::row_contains_v<Cipher::record_event_required_row, eff::Effect::Test>);

    std::printf("  [AUDIT-A] required_row_header_fence:    PASSED\n");
}

// The earlier comparison recorded one event.  A drift between the two
// routes that only shows after several, or only on certain hash
// values, needs a longer and more varied sequence than that.

static void test_audit_b_multi_event_byte_equivalence(const char* base_dir) {
    const std::string dir_a = std::string(base_dir) + "/aud_b_a";
    const std::string dir_b = std::string(base_dir) + "/aud_b_b";
    std::filesystem::create_directories(dir_a);
    std::filesystem::create_directories(dir_b);

    auto cipher_a = open_cipher(dir_a);
    auto cipher_b = open_cipher(dir_b);
    auto view_a = cipher_a.mint_open_view(store_ctx());
    auto view_b = cipher_b.mint_open_view(store_ctx());
    const auto ctx = store_ctx();

    constexpr int N = 16;
    constexpr std::uint64_t kHashes[N] = {
        0x0001000100010001ULL, 0xFFFE000200030004ULL, 0xC0FFEE00000000ULL,   0xDEADBEEFCAFEBABEULL,
        0x123456789ABCDEF0ULL, 0xFEDCBA9876543210ULL, 0x5555555555555555ULL, 0xAAAAAAAAAAAAAAAAULL,
        0x0F0F0F0F0F0F0F0FULL, 0xF0F0F0F0F0F0F0F0ULL, 0x1111222233334444ULL, 0x4444333322221111ULL,
        0x8000000000000001ULL, 0x7FFFFFFFFFFFFFFEULL, 0x0123456789ABCDEFULL, 0xFEDCBA9876543211ULL,
    };

    for (int i = 0; i < N; ++i) {
        cipher_a.advance_head(view_a, ContentHash{kHashes[i]}, static_cast<std::uint64_t>(i));
        cipher_b.record_event(ctx, view_b, ContentHash{kHashes[i]}, static_cast<std::uint64_t>(i));
    }

    auto read_file = [](const std::string& path) {
        std::ifstream f(path);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    assert(read_file(dir_a + "/HEAD") == read_file(dir_b + "/HEAD"));

    // A log line is a step, a hash and a timestamp, comma separated.
    // The timestamps differ between the two runs by construction, so
    // the comparison stops at the second comma.
    const auto log_a = read_file(dir_a + "/log");
    const auto log_b = read_file(dir_b + "/log");
    auto strip_ts = [](const std::string& s) {
        std::string out;
        std::size_t pos = 0;
        while (pos < s.size()) {
            std::size_t eol = s.find('\n', pos);
            if (eol == std::string::npos) eol = s.size();
            const auto line = s.substr(pos, eol - pos);
            std::size_t c1 = line.find(',');
            std::size_t c2 = line.find(',', c1 + 1);
            if (c1 != std::string::npos && c2 != std::string::npos) {
                out.append(line, 0, c2);
                out.push_back('\n');
            }
            pos = eol + 1;
        }
        return out;
    };
    assert(strip_ts(log_a) == strip_ts(log_b));

    for (int i = 0; i < N; ++i) {
        assert(cipher_a.hash_at_step(view_a, static_cast<std::uint64_t>(i))
               == cipher_b.hash_at_step(view_b, static_cast<std::uint64_t>(i)));
    }

    std::printf("  [AUDIT-B] multi_event_byte_equivalence: PASSED\n");
}

// Every other use of the required row in this file is inside a
// function.  Naming it at namespace scope is what a caller outside the
// store does, and only that spelling proves it is publicly reachable.

namespace audit_c_external_visibility {
using ExtRow = ::crucible::Cipher::record_event_required_row;
static_assert(eff::row_size_v<ExtRow> == 2u);
static_assert(eff::Subrow<ExtRow, eff::Row<eff::Effect::IO, eff::Effect::Block>>);
static_assert(eff::Subrow<ExtRow, eff::Row<eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block, eff::Effect::Bg>>);
static_assert(!eff::Subrow<ExtRow, eff::Row<>>);
static_assert(eff::CtxAdmits<::fixy::TestRunnerCtx, ExtRow>);
}  // namespace audit_c_external_visibility

static void test_audit_c_external_visibility() {
    using ExtRow = audit_c_external_visibility::ExtRow;
    static_assert(eff::row_size_v<ExtRow> == 2u);
    std::printf("  [AUDIT-C] external_visibility:          PASSED\n");
}

// Each context that admits the row is interchangeable at a call site,
// including a context narrowed to exactly the two required atoms.

static void test_audit_d_canonical_row_acceptance(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/aud_d";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());

    cipher.record_event(store_ctx(), view, ContentHash{0xAAAA}, 1u);
    assert(cipher.head() == ContentHash{0xAAAA});

    const auto narrowed = store_ctx().in_row<Cipher::record_event_required_row>();
    cipher.record_event(narrowed, view, ContentHash{0xBBBB}, 2u);
    assert(cipher.head() == ContentHash{0xBBBB});

    // The same two atoms the other way round, which containment by
    // membership accepts.
    const auto reordered = store_ctx().in_row<eff::Row<eff::Effect::Block, eff::Effect::IO>>();
    cipher.record_event(reordered, view, ContentHash{0xCCCC}, 3u);
    assert(cipher.head() == ContentHash{0xCCCC});

    std::printf("  [AUDIT-D] canonical_row_acceptance:     PASSED\n");
}

// The context gate fires while the template is substituted and the
// precondition fires when the call runs, so satisfying the first says
// nothing about the second.  Violating the precondition would end the
// process, so what is checked here is that a caller who satisfies the
// gate still gets the ordered behaviour the precondition describes.

static void test_audit_e_pre_clause_orthogonal(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/aud_e";
    std::filesystem::create_directories(dir);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());
    const auto ctx = store_ctx();

    cipher.record_event(ctx, view, ContentHash{1u}, 0u);
    cipher.record_event(ctx, view, ContentHash{2u}, 1u);
    cipher.record_event(ctx, view, ContentHash{3u}, 1u);  // a repeated step is allowed
    cipher.record_event(ctx, view, ContentHash{4u}, 5u);

    assert(cipher.head() == ContentHash{4u});
    assert(cipher.hash_at_step(view, 0u) == ContentHash{1u});
    assert(cipher.hash_at_step(view, 1u) == ContentHash{3u});  // the later of the two at step 1
    assert(cipher.hash_at_step(view, 5u) == ContentHash{4u});

    std::printf("  [AUDIT-E] pre_clause_orthogonal:        PASSED\n");
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
    test_t04_record_event_matches_advance_head(base.c_str());
    test_t05_round_trip(base.c_str());
    test_t06_bg_superset_row(base.c_str());
    test_t07_startup_superset_row(base.c_str());
    test_t08_monotonic_steps(base.c_str());
    test_t09_multiple_events_monotonic(base.c_str());
    test_t10_api_surface_pinned(base.c_str());

    std::printf("--- audit groups ---\n");
    test_audit_a_required_row_header_fence();
    test_audit_b_multi_event_byte_equivalence(base.c_str());
    test_audit_c_external_visibility();
    test_audit_d_canonical_row_acceptance(base.c_str());
    test_audit_e_pre_clause_orthogonal(base.c_str());

    std::error_code ec;
    std::filesystem::remove_all(tmpl, ec);

    std::printf("test_cipher_record_event: 10 + 5 audit groups, "
                "all passed\n");
    return 0;
}
