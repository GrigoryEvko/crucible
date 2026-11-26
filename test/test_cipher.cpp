#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>
#include <crucible/cipher/FederationProtocol.h>
#include <foundation/core/KeyedHash.h>

#include <memory>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Process.h>
#include <fixy/session/Subtype.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>
#include <foundation/effects/Effect.h>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using Bytes = ::fixy::Quantity<::fixy::unit::Byte, size_t>;

}  // namespace

using crucible::DagArena;

// Opening the store and its open view need a context whose row admits IO
// and Block.
[[nodiscard]] inline ::fixy::TestRunnerCtx store_ctx() {
    return ::fixy::TestRunnerCtx{::foundation::effects::testing::test()};
}

static auto g_test = ::foundation::effects::testing::test();

// The root path crosses a trust boundary, and every call site here has
// to say so.  The helper keeps that declaration from swamping the calls.
// The text of the root copies the characters of the directory.
[[nodiscard]] static crucible::Cipher open_cipher(::fixy::TextView dir) {
    ::foundation::core::GrowText root = ::foundation::core::mint_grow_text(crucible::Cipher::MAX_ROOT_PATH_BYTES);
    for (const char character : dir) {
        root.append(g_test.alloc, character).expect("the text holds each character of the directory");
    }
    ::foundation::core::HeapText const root_path = ::foundation::core::freeze(std::move(root));
    return crucible::Cipher::open(store_ctx(), g_test.alloc,
                                  ::fixy::mint_tagged<::fixy::tags::source::External>(root_path.view()))
        .expect("the test opens its store");
}

// The salt goes into the schema hashes, so two salts give two regions with
// two content hashes.  The ops, their arrays and the region are in
// dag_arena, and one frame on it builds them.
static crucible::DagRef<crucible::RegionNode> make_test_region(::fixy::BumpArena<DagArena>& dag_arena,
                                                               std::uint64_t salt = 0) {
    return ::foundation::core::with_arena_frame(dag_arena, [salt](auto& frame) noexcept {
        constexpr uint32_t NUM_OPS = 2;
        auto ops_run = frame.template array<crucible::TraceEntry>(g_test.alloc, NUM_OPS, crucible::TraceEntry{});
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<crucible::TraceEntry> ops = ops_run.view();

        for (uint32_t i = 0; i < NUM_OPS; i++) {
            crucible::TraceEntry& op = *ops.get(i).expect("i is below NUM_OPS");
            op.schema_hash = crucible::SchemaHash{0xCAFE0000 + i + (salt << 8)};

            crucible::TensorMeta meta{};
            meta.ndim = 1;
            meta.sizes[0] = ::crucible::tensor_dim(16);
            meta.strides[0] = ::crucible::tensor_dim(1);
            meta.dtype = crucible::ScalarType::Float;
            op.set_inputs(::fixy::check::run_of(kTest, g_test.alloc, frame, meta).weaken(),
                          ::fixy::check::run_of(kTest, g_test.alloc, frame, crucible::OpIndex{}).weaken(),
                          ::fixy::check::run_of(kTest, g_test.alloc, frame, crucible::SlotId{}).weaken());
            op.set_outputs(::fixy::check::run_of(kTest, g_test.alloc, frame, meta).weaken(),
                           ::fixy::check::run_of(kTest, g_test.alloc, frame, crucible::SlotId{i}).weaken());
        }

        return crucible::make_region(g_test.alloc, frame, ops_run).weaken();
    });
}

// Commits a head and requires that the commit happened.
static void commit_head(crucible::Cipher& cipher, crucible::Cipher::OpenView const& view, crucible::ContentHash hash,
                        std::uint64_t step) {
    const auto committed = cipher.record_event(store_ctx(), view, hash, step);
    CRUCIBLE_TEST_ASSERT(kTest, committed.is_ok() && "record_event must commit when the writes pass");
}

// The events of one session, at steps first, first + 1, ..., first + count - 1.
static ::fixy::HeapArray<crucible::Cipher::SessionEvent> session_batch(std::uint64_t first, std::size_t count) {
    return ::fixy::mint_heap_array_with<crucible::Cipher::SessionEvent>(
        g_test.alloc, count, [first](std::size_t position) noexcept {
            const std::uint64_t step = first + position;
            return crucible::Cipher::SessionEvent::cipher_event(
                ::fixy::session::SessionOp::StoreCommitted, ::fixy::session::StepId{step},
                ::fixy::session::StateHash{0x5E550000u + step}, ::fixy::session::CipherEventOrder{step});
        });
}

// The step of the event at position of a list of loaded events.
[[nodiscard]] static std::uint64_t step_at(crucible::Cipher::SessionEventLoad const& loaded, std::size_t position) {
    return loaded.events.get(position).expect("the position is inside the list of loaded events")->step_id().raw();
}

// The hash of a batch that the store persists.  A refusal ends the test.
[[nodiscard]] static crucible::ContentHash persisted_hash(crucible::Cipher::PersistResult const& persisted) {
    return persisted.match([](crucible::ContentHash hash) { return hash; },
                           [](crucible::Cipher::PersistError const&) -> crucible::ContentHash {
                               ::fixy::fatal("the store refuses a batch that the test persists");
                           });
}

// The root of an open store of the tests.  A store that is not open ends the
// test.
[[nodiscard]] static ::fixy::TextView root_of(crucible::Cipher const& cipher CRUCIBLE_LIFETIMEBOUND) {
    return cipher.root().expect("the store of the test is open");
}
void root_of(crucible::Cipher const&&) = delete("a temporary store releases its root at the end of the statement");

// The directory of the root of cipher.
[[nodiscard]] static ::fixy::fs::DirectoryHandle root_directory_of(crucible::Cipher const& cipher) {
    return ::fixy::fs::open_directory(
               store_ctx(), ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(root_of(cipher)))
                                .expect("the sanitizer passes the root of the test"))
        .expect("the root of the test opens");
}

// The path of the directory of the events of session under the root:
// "session_events/<16 digits>/".
[[nodiscard]] static ::foundation::os::PathText<64> session_events_path(::fixy::session::SessionTagId session) {
    ::foundation::os::PathText<64> path{};
    path.append(::fixy::TextView{"session_events/"}).expect("the session path fits its text");
    path.append_hex<16>(session.value).expect("the session path fits its text");
    path.append('/').expect("the session path fits its text");
    return path;
}

// Writes bytes over the batch file of hash in the directory of session under
// the root of cipher.  No bytes empty the file, so that a read of the file
// gives LoadError::file_size.
static void overwrite_batch_file(crucible::Cipher const& cipher, ::fixy::session::SessionTagId session,
                                 crucible::ContentHash hash, ::fixy::View<std::byte const> bytes) {
    ::foundation::os::PathText<64> batch = session_events_path(session);
    batch.append_hex<16>(hash.raw()).expect("the batch path fits its text");
    batch.append(::fixy::TextView{".cfed"}).expect("the batch path fits its text");
    using WriteTruncate = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteTruncate>;
    const ::fixy::fs::DirectoryHandle root = root_directory_of(cipher);
    const ::fixy::fs::FileOf<WriteTruncate> file =
        ::fixy::fs::mint_file_at<WriteTruncate>(store_ctx(), root.ref(), batch.c_text())
            .expect("the test opens the batch file to write it")
            .consume();
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fs::write_full(store_ctx(), file.ref(), bytes).is_ok());
}

// Appends line to the index of the events of session under the root of
// cipher.
static void append_index_line(crucible::Cipher const& cipher, ::fixy::session::SessionTagId session,
                              ::fixy::TextView line) {
    ::foundation::os::PathText<64> index = session_events_path(session);
    index.append(::fixy::TextView{"index"}).expect("the index path fits its text");
    using WriteAppend = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteAppend>;
    const ::fixy::fs::DirectoryHandle root = root_directory_of(cipher);
    const ::fixy::fs::FileOf<WriteAppend> file =
        ::fixy::fs::mint_file_at<WriteAppend>(store_ctx(), root.ref(), index.c_text())
            .expect("the test opens the index to append a line")
            .consume();
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fs::write_full(store_ctx(), file.ref(), line).is_ok());
}

// The region of a load that gives one.
static const crucible::RegionNode& region_of(crucible::Cipher::LoadResult const& loaded) {
    return *crucible::Cipher::LoadResult{loaded}.expect("the load gives a region").get();
}

// True when the load gives a region from the resident cache.
static bool is_cache_hit(crucible::Cipher::LoadResult const& loaded) {
    return loaded.match(
        [](crucible::Cipher::LoadedContentAddressedRegionPayload const& payload) noexcept {
            return payload.cache_hit();
        },
        [](crucible::LoadError) noexcept { return false; });
}

// The reason of a load that refuses.  A load that gives a region ends the
// test.
static crucible::LoadError reason_of(crucible::Cipher::LoadResult const& loaded) {
    return loaded.err().expect("the load refuses");
}

// Writes bytes to the file of the object of hash under the root of the
// store, as a writer of the store can, and makes the directory of the object
// when it is missing.  The test gives a hash that no store of the test
// stored, so the cache holds no bytes of it, and a load reads this file.
static void write_object_file(::fixy::TextView root, crucible::ContentHash hash, ::fixy::View<const std::byte> bytes) {
    const ::fixy::TestRunnerCtx ctx = store_ctx();
    const ::fixy::fs::DirectoryHandle root_dir =
        ::fixy::fs::open_directory(ctx, ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(root))
                                            .expect("the root of a store is a sanitized path"))
            .expect("the root of the store opens");
    ::foundation::os::PathText<64> object{};
    object.append(::fixy::TextView{"objects/"}).expect("the object path fits its text");
    object.append_hex<2>(hash.raw() >> 56U).expect("the object path fits its text");
    static_cast<void>(::fixy::fs::make_directory_at(ctx, root_dir.ref(), object.c_text()).is_ok());
    object.append('/').expect("the object path fits its text");
    object.append_hex<14>(hash.raw()).expect("the object path fits its text");
    using WriteTruncate = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteTruncate>;
    const bool is_written =
        ::fixy::fs::mint_file_at<WriteTruncate>(ctx, root_dir.ref(), object.c_text())
            .match(
                [&](::fixy::Linear<::fixy::fs::FileOf<WriteTruncate>> opened) {
                    const ::fixy::fs::FileOf<WriteTruncate> file = std::move(opened).consume();
                    return bytes.size() == 0 || ::fixy::fs::write_full(ctx, file.ref(), bytes).is_ok();
                },
                [](::foundation::os::Errno) { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_written && "the test writes the file of the object");
}

// The store at the directory name under root.  The open makes the directory
// when it is missing, and the removal of root at the end of the test removes
// it.
[[nodiscard]] static crucible::Cipher open_cipher_under(::fixy::TextView root, ::fixy::TextView name) {
    ::foundation::os::PathText<crucible::Cipher::MAX_ROOT_PATH_BYTES> path{};
    path.append(root).expect("the root of the test fits its path");
    path.append('/').expect("the root of the test fits its path");
    path.append(name).expect("the root of the test fits its path");
    return crucible::Cipher::open(store_ctx(), g_test.alloc,
                                  ::fixy::mint_tagged<::fixy::tags::source::External>(path.c_text().view()))
        .expect("the test opens its store");
}

// Writes text over the file name under the root of a store, as a writer of the
// store can.
static void write_store_file(::fixy::TextView root, ::fixy::CText name, ::fixy::TextView text) {
    const ::fixy::TestRunnerCtx ctx = store_ctx();
    const ::fixy::fs::DirectoryHandle root_dir =
        ::fixy::fs::open_directory(ctx, ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(root))
                                            .expect("the root of a store is a sanitized path"))
            .expect("the root of the store opens");
    using WriteTruncate = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteTruncate>;
    const bool is_written = ::fixy::fs::mint_file_at<WriteTruncate>(ctx, root_dir.ref(), name)
                                .match(
                                    [&](::fixy::Linear<::fixy::fs::FileOf<WriteTruncate>> opened) {
                                        const ::fixy::fs::FileOf<WriteTruncate> file = std::move(opened).consume();
                                        return ::fixy::fs::write_full(ctx, file.ref(), text).is_ok();
                                    },
                                    [](::foundation::os::Errno) { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_written && "the test writes the file of the store");
}

// The path of the file beside the object of hash under the root of a store:
// "objects/<2 digits>/<14 digits>" and then suffix.
[[nodiscard]] static ::foundation::os::PathText<64> beside_object_path(crucible::ContentHash hash,
                                                                       ::fixy::TextView suffix) {
    ::foundation::os::PathText<64> path{};
    path.append(::fixy::TextView{"objects/"}).expect("the object path fits its text");
    path.append_hex<2>(hash.raw() >> 56U).expect("the object path fits its text");
    path.append('/').expect("the object path fits its text");
    path.append_hex<14>(hash.raw()).expect("the object path fits its text");
    path.append(suffix).expect("the object path fits its text");
    return path;
}

// Writes text to the file beside the object of hash, as another writer of the
// store can, and makes the directory of the object when it is missing.
static void write_beside_object(::fixy::TextView root, crucible::ContentHash hash, ::fixy::TextView suffix,
                                ::fixy::TextView text) {
    const ::fixy::TestRunnerCtx ctx = store_ctx();
    const ::fixy::fs::DirectoryHandle root_dir =
        ::fixy::fs::open_directory(ctx, ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(root))
                                            .expect("the root of a store is a sanitized path"))
            .expect("the root of the store opens");
    ::foundation::os::PathText<64> shard{};
    shard.append(::fixy::TextView{"objects/"}).expect("the shard path fits its text");
    shard.append_hex<2>(hash.raw() >> 56U).expect("the shard path fits its text");
    static_cast<void>(::fixy::fs::make_directory_at(ctx, root_dir.ref(), shard.c_text()).is_ok());
    const ::foundation::os::PathText<64> path = beside_object_path(hash, suffix);
    using WriteTruncate = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteTruncate>;
    const bool is_written = ::fixy::fs::mint_file_at<WriteTruncate>(ctx, root_dir.ref(), path.c_text())
                                .match(
                                    [&](::fixy::Linear<::fixy::fs::FileOf<WriteTruncate>> opened) {
                                        const ::fixy::fs::FileOf<WriteTruncate> file = std::move(opened).consume();
                                        return ::fixy::fs::write_full(ctx, file.ref(), text).is_ok();
                                    },
                                    [](::foundation::os::Errno) { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_written && "the test writes the file beside the object");
}

// True when the file beside the object of hash holds the characters of text.
[[nodiscard]] static bool holds_beside_object(::fixy::TextView root, crucible::ContentHash hash,
                                              ::fixy::TextView suffix, ::fixy::TextView text) {
    const ::fixy::TestRunnerCtx ctx = store_ctx();
    const ::fixy::fs::DirectoryHandle root_dir =
        ::fixy::fs::open_directory(ctx, ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(root))
                                            .expect("the root of a store is a sanitized path"))
            .expect("the root of the store opens");
    const ::foundation::os::PathText<64> path = beside_object_path(hash, suffix);
    ::fixy::GrowText read = ::fixy::mint_grow_text(4096);
    const bool is_read = ::fixy::fs::read_whole_at(ctx, g_test.alloc, root_dir.ref(), path.c_text(), read).is_ok();
    return is_read && read.with_view([text](::fixy::TextView held) noexcept { return held == text; });
}

// A new temporary directory of the test.  The end of the owner removes the
// directory with each entry below it.
[[nodiscard]] static ::fixy::fs::TemporaryDirectory make_temporary_directory() {
    return std::move(::fixy::fs::mint_temporary_directory(store_ctx()).expect("the test makes a temporary directory"))
        .consume();
}

// An object lives at objects/<first two hex digits>/<remaining digits>,
// so a directory never accumulates every object at one level.  True when
// the store of cipher holds the file of the object of hash.
[[nodiscard]] static bool holds_object_file(crucible::Cipher const& cipher, crucible::ContentHash hash) {
    const ::fixy::fs::DirectoryHandle root = root_directory_of(cipher);
    const ::foundation::os::PathText<64> path = beside_object_path(hash, ::fixy::TextView{});
    return ::fixy::fs::has_entry_at(store_ctx(), root.ref(), path.c_text()).value_or(false);
}

// Removes the file of the object of hash from the store of cipher.  A file
// that is gone already is no error.
static void remove_object_file(crucible::Cipher const& cipher, crucible::ContentHash hash) {
    const ::fixy::fs::DirectoryHandle root = root_directory_of(cipher);
    const ::foundation::os::PathText<64> path = beside_object_path(hash, ::fixy::TextView{});
    static_cast<void>(::fixy::fs::unlink_at(store_ctx(), root.ref(), path.c_text()).is_ok());
}

// The size in bytes of the file of the object of hash in the store of cipher.
[[nodiscard]] static std::int64_t object_file_size(crucible::Cipher const& cipher, crucible::ContentHash hash) {
    const ::fixy::fs::DirectoryHandle root = root_directory_of(cipher);
    const ::foundation::os::PathText<64> path = beside_object_path(hash, ::fixy::TextView{});
    using ReadOnly = ::fixy::atom::fs::mode<::fixy::fs::open_mode::ReadOnly>;
    const ::fixy::fs::FileOf<ReadOnly> file = ::fixy::fs::mint_file_at<ReadOnly>(store_ctx(), root.ref(), path.c_text())
                                                  .expect("the test opens the file of the object")
                                                  .consume();
    return ::fixy::fs::file_size(store_ctx(), file.ref()).expect("the size of the file of the object reads");
}

// True when the first line of the file name under the root of cipher holds
// the characters of text, with no newline.
[[nodiscard]] static bool has_first_line(crucible::Cipher const& cipher, ::fixy::CText name, ::fixy::TextView text) {
    const ::fixy::fs::DirectoryHandle root = root_directory_of(cipher);
    ::fixy::GrowText read = ::fixy::mint_grow_text(4096);
    const bool is_read = ::fixy::fs::read_whole_at(store_ctx(), g_test.alloc, root.ref(), name, read).is_ok();
    return is_read && read.with_view([text](::fixy::TextView held) noexcept {
        const std::size_t end = ::fixy::find(held, '\n').value_or(held.size());
        return held.window(0, end).expect("the first line is inside the file") == text;
    });
}

// The third field of the last line of the log, the place of the last
// commit.  Complexity: linear in the size of the log.
static std::uint64_t last_log_order(crucible::Cipher const& cipher) {
    const ::fixy::fs::DirectoryHandle root = root_directory_of(cipher);
    ::fixy::GrowText log = ::fixy::mint_grow_text(1U << 20U);
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fs::read_whole_at(store_ctx(), g_test.alloc, root.ref(), "log", log).is_ok());
    return log.with_view([](::fixy::TextView text) noexcept {
        ::fixy::TextView last{};
        ::fixy::TextView rest = text;
        while (rest.size() > 0) {
            const std::size_t end = ::fixy::find(rest, '\n').value_or(rest.size());
            const ::fixy::TextView line = rest.window(0, end).expect("the line is inside the log");
            if (line.size() > 0) last = line;
            const std::size_t next = end < rest.size() ? end + 1 : end;
            rest = rest.window(next, rest.size() - next).expect("the rest of the log is inside the log");
        }
        // The search for the second comma starts after the first comma, or
        // at the start of the line when the line holds no comma.
        const std::size_t after_first = ::fixy::find(last, ',').match(
            [](std::size_t first) noexcept { return first + 1; }, [] noexcept { return std::size_t{0}; });
        const ::fixy::TextView tail =
            last.window(after_first, last.size() - after_first).expect("the tail is inside the line");
        const ::fixy::Option<std::size_t> second_comma = ::fixy::find(tail, ',');
        CRUCIBLE_TEST_ASSERT(kTest, second_comma.is_some());
        const std::size_t field_start = after_first + ::fixy::Option<std::size_t>{second_comma}.value_or(0) + 1;
        const ::fixy::TextView field =
            last.window(field_start, last.size() - field_start).expect("the field is inside the line");
        ::fixy::Result<std::uint64_t, ::fixy::ParseError> order = ::fixy::parse<std::uint64_t>(field);
        CRUCIBLE_TEST_ASSERT(kTest, order.is_ok());
        return std::move(order).value_or(0);
    });
}

// True when a commit gives the error error.
[[nodiscard]] static bool is_commit_error(crucible::Cipher::CommitResult const& committed,
                                          crucible::Cipher::CommitError error) {
    return committed.err().is_some_and(
        [error](crucible::Cipher::CommitError given) noexcept { return given == error; });
}

static_assert(
    ::fixy::session::ContentAddressedType<typename crucible::Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(::fixy::session::ContentAddressedType<
              typename crucible::Cipher::LoadedContentAddressedRegionPayload::payload_type>);
static_assert(::fixy::session::is_payload_subsort_v<
              crucible::RegionNode, typename crucible::Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(sizeof(crucible::Cipher::ContentAddressedRegionPayload) == sizeof(const crucible::RegionNode*));

// A load result that holds a region comes only from the store, because its
// cache flag is a claim about where the bytes came from.  A payload comes
// only from content_addressed_payload.
static_assert(!std::is_constructible_v<crucible::Cipher::LoadedContentAddressedRegionPayload,
                                       crucible::DagRef<crucible::RegionNode>, bool>);
static_assert(!std::is_constructible_v<crucible::Cipher::LoadedContentAddressedRegionPayload, ::fixy::NoValue>,
              "a loaded value holds a region: a refused load gives the LoadError of its Result");
static_assert(!std::is_constructible_v<crucible::Cipher::LoadedContentAddressedRegionPayload, std::nullptr_t>,
              "no pointer converts to a loaded value");
static_assert(!std::is_constructible_v<crucible::Cipher::ContentAddressedRegionPayload,
                                       crucible::Cipher::ContentAddressedRegionPayload::Value>);
static_assert(!std::is_constructible_v<crucible::Cipher::ContentAddressedRegionPayload, const crucible::RegionNode*>);
static_assert(!std::is_default_constructible_v<crucible::Cipher::ContentAddressedRegionPayload>);
static_assert(sizeof(crucible::Cipher::LoadedContentAddressedRegionPayload::Value) == sizeof(crucible::RegionNode*),
              "the value of a load is a Ref, which holds one pointer");

// A store whose log holds the commits of the steps 1 to 10, and a second open
// of the store.  A commit at a step below the last commit is an error of the
// commit, and the process goes on.  A commit at the last step and after it
// passes.  Each commit runs in a child, so a commit that ends its process
// shows as a child that does not exit with 0.  Each store of this test and of
// the tests below is a directory under root.
static void test_commit_below_the_last_step(::fixy::TextView root) {
    {
        auto first_run = open_cipher_under(root, ::fixy::TextView{"steps"});
        auto first_ov = first_run.mint_open_view(store_ctx());
        for (std::uint64_t step = 1; step <= 10; ++step) {
            commit_head(first_run, first_ov, crucible::ContentHash{0x5700000000000000ULL + step}, step);
        }
    }
    auto second_run = open_cipher_under(root, ::fixy::TextView{"steps"});
    auto second_ov = second_run.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, second_run.last_committed_step().is_some_and([](::fixy::session::StepId step) {
        return step.raw() == 10;
    }) && "the open gives the step of the last commit of the log");
    const ::fixy::TestRunnerCtx child_ctx = store_ctx();
    const auto commit_in_child = [&](std::uint64_t step, bool is_refusal_expected) {
        return ::fixy::process::wait(::fixy::process::spawn_child(
                                         child_ctx,
                                         [&second_run, &second_ov, &child_ctx, step, is_refusal_expected] noexcept {
                                             const bool is_refused =
                                                 second_run
                                                     .record_event(child_ctx, second_ov,
                                                                   crucible::ContentHash{0x5800000000000000ULL}, step)
                                                     .is_err();
                                             if (is_refused != is_refusal_expected) {
                                                 ::fixy::fatal("the commit gave another result than the test expects");
                                             }
                                         })
                                         .expect("the test starts a child"))
            .expect("the test waits for its child");
    };
    CRUCIBLE_TEST_ASSERT(kTest, commit_in_child(1, true).is_exit_with(0)
                                    && "a commit below the last step is an error, not the end of the process");
    CRUCIBLE_TEST_ASSERT(kTest, commit_in_child(9, true).is_exit_with(0));
    CRUCIBLE_TEST_ASSERT(kTest, commit_in_child(10, false).is_exit_with(0));
    CRUCIBLE_TEST_ASSERT(kTest, commit_in_child(11, false).is_exit_with(0));

    // The refusals in this process give their errors, and they take no place.
    // Each child above wrote its line with the place 11 of its own count, so
    // the commit here also takes the place 11 when no refusal took a place.  A
    // reopen skips each line whose place does not grow, so its head is the
    // commit of the first child.
    using crucible::Cipher;
    CRUCIBLE_TEST_ASSERT(kTest,
                         is_commit_error(second_run.record_event(store_ctx(), second_ov,
                                                                 crucible::ContentHash{0x5800000000000001ULL}, 1),
                                         Cipher::CommitError::step_order));
    CRUCIBLE_TEST_ASSERT(kTest, is_commit_error(second_run.record_event(store_ctx(), second_ov,
                                                                        crucible::ContentHash{0x5800000000000002ULL},
                                                                        Cipher::MAX_COMMIT_STEP + 1),
                                                Cipher::CommitError::step_above_limit));
    commit_head(second_run, second_ov, crucible::ContentHash{0x5800000000000003ULL}, 10);
    CRUCIBLE_TEST_ASSERT(kTest, second_run.head() == crucible::ContentHash{0x5800000000000003ULL});
    CRUCIBLE_TEST_ASSERT(kTest, second_run.last_committed_step().is_some_and(
                                    [](::fixy::session::StepId step) { return step.raw() == 10; }));
    const crucible::Cipher third_run = open_cipher_under(root, ::fixy::TextView{"steps"});
    CRUCIBLE_TEST_ASSERT(kTest, third_run.head() == crucible::ContentHash{0x5800000000000000ULL}
                                    && "a refused commit takes no place of the count");
}

// A log whose last line holds the step 2^64 - 1.  The line is corrupt, so the
// open skips it, and a commit after the step of the line before it passes.
// The old store took the line, and each later commit ended its process.  A
// line at MAX_COMMIT_STEP is a sound commit, and a commit below it gives
// step_order.
static void test_step_above_the_ceiling_is_corrupt(::fixy::TextView root) {
    const crucible::Cipher made = open_cipher_under(root, ::fixy::TextView{"high_step"});
    write_store_file(root_of(made), "log",
                     ::fixy::TextView{"5,5900000000000001,1\n18446744073709551615,5900000000000002,2\n"});
    auto cipher = open_cipher_under(root, ::fixy::TextView{"high_step"});
    auto ov = cipher.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(
        kTest, cipher.last_committed_step().is_some_and([](::fixy::session::StepId step) { return step.raw() == 5; }));
    CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == crucible::ContentHash{0x5900000000000001ULL}
                                    && "the line of the step 2^64 - 1 is no commit");
    const ::fixy::TestRunnerCtx child_ctx = store_ctx();
    const ::fixy::process::ChildReport report =
        ::fixy::process::wait(
            ::fixy::process::spawn_child(
                child_ctx,
                [&cipher, &ov, &child_ctx] noexcept {
                    if (cipher.record_event(child_ctx, ov, crucible::ContentHash{0x5900000000000003ULL}, 6).is_err()) {
                        ::fixy::fatal("a commit after the last sound step fails");
                    }
                })
                .expect("the test starts a child"))
            .expect("the test waits for its child");
    CRUCIBLE_TEST_ASSERT(kTest, report.is_exit_with(0) && "a corrupt step of the log ends no later commit");

    const crucible::Cipher at_ceiling_made = open_cipher_under(root, ::fixy::TextView{"ceiling_step"});
    write_store_file(root_of(at_ceiling_made), "log", ::fixy::TextView{"4611686018427387903,5900000000000004,1\n"});
    auto at_ceiling = open_cipher_under(root, ::fixy::TextView{"ceiling_step"});
    auto at_ceiling_ov = at_ceiling.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, at_ceiling.last_committed_step().is_some_and([](::fixy::session::StepId step) {
        return step.raw() == crucible::Cipher::MAX_COMMIT_STEP;
    }) && "a line at the ceiling is a commit");
    CRUCIBLE_TEST_ASSERT(kTest,
                         is_commit_error(at_ceiling.record_event(store_ctx(), at_ceiling_ov,
                                                                 crucible::ContentHash{0x5900000000000005ULL}, 1),
                                         crucible::Cipher::CommitError::step_order));
    commit_head(at_ceiling, at_ceiling_ov, crucible::ContentHash{0x5900000000000006ULL},
                crucible::Cipher::MAX_COMMIT_STEP);
}

// A crash in the middle of an append leaves a torn last record with no
// newline.  The next commit cuts the torn record and appends after the last
// whole line, so a reopen finds the commit.  The old store wrote the commit
// after the torn bytes, and the two joined into one line.  A torn record of
// other characters made a line that does not parse, and the commit was lost.
// A torn record of digits only made the commit at step 3 a line of the step
// 13.
static void test_commit_after_a_torn_record(::fixy::TextView root) {
    const crucible::Cipher torn_made = open_cipher_under(root, ::fixy::TextView{"torn_log"});
    write_store_file(root_of(torn_made), "log", ::fixy::TextView{"1,5b00000000000001,1\n0,01234"});
    {
        auto cipher = open_cipher_under(root, ::fixy::TextView{"torn_log"});
        auto ov = cipher.mint_open_view(store_ctx());
        CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == crucible::ContentHash{0x5b00000000000001ULL});
        commit_head(cipher, ov, crucible::ContentHash{0x5b00000000000002ULL}, 2);
    }
    const crucible::Cipher torn_reopened = open_cipher_under(root, ::fixy::TextView{"torn_log"});
    CRUCIBLE_TEST_ASSERT(kTest, torn_reopened.head() == crucible::ContentHash{0x5b00000000000002ULL}
                                    && "the commit after a torn record loads at the reopen");

    const crucible::Cipher digit_made = open_cipher_under(root, ::fixy::TextView{"torn_digit"});
    write_store_file(root_of(digit_made), "log", ::fixy::TextView{"1,5b00000000000003,1\n1"});
    {
        auto cipher = open_cipher_under(root, ::fixy::TextView{"torn_digit"});
        auto ov = cipher.mint_open_view(store_ctx());
        commit_head(cipher, ov, crucible::ContentHash{0x5b00000000000004ULL}, 3);
    }
    auto digit_reopened = open_cipher_under(root, ::fixy::TextView{"torn_digit"});
    auto digit_ov = digit_reopened.mint_open_view(store_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, digit_reopened.hash_at_step(digit_ov, 5) == crucible::ContentHash{0x5b00000000000004ULL}
                                    && "the commit after a torn digit keeps its step 3");
}

// Another writer holds the temporary file <leaf>.tmp of an object.  A store of
// the object writes a temporary file of its own, and it leaves the file of the
// other writer as it is.  The old store opened <leaf>.tmp with O_TRUNC, cut
// the bytes of the other writer and renamed that file onto the object.  A
// sound object of a hash with other bytes, here the image of another variant,
// stays as it is, and the store reports it durable.
static void test_store_leaves_the_file_of_another_writer(::fixy::TextView root) {
    auto cipher = open_cipher_under(root, ::fixy::TextView{"writers"});
    auto ov = cipher.mint_open_view(store_ctx());
    ::fixy::BumpArena<DagArena> writers_dag = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
    const crucible::RegionNode& writer_region = *make_test_region(writers_dag, 0x61);
    const crucible::ContentHash writer_hash = writer_region.content_hash;
    write_beside_object(root_of(cipher), writer_hash, ::fixy::TextView{".tmp"}, ::fixy::TextView{"another writer"});
    CRUCIBLE_TEST_ASSERT(kTest,
                         cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(::fixy::to(writer_region)))
                             == writer_hash);
    CRUCIBLE_TEST_ASSERT(kTest, holds_beside_object(root_of(cipher), writer_hash, ::fixy::TextView{".tmp"},
                                                    ::fixy::TextView{"another writer"})
                                    && "the store leaves the temporary file of another writer");
    {
        const crucible::Cipher fresh = open_cipher_under(root, ::fixy::TextView{"writers"});
        const auto fresh_ov = fresh.mint_open_view(store_ctx());
        ::fixy::BumpArena<DagArena> fresh_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        const auto loaded = fresh.load_content_addressed(store_ctx(), fresh_ov, g_test.alloc, writer_hash, fresh_arena);
        CRUCIBLE_TEST_ASSERT(kTest, loaded.is_ok() && !is_cache_hit(loaded) && "the stored object loads from the disk");
    }

    crucible::RegionNode& variant = *make_test_region(writers_dag, 0x62);
    variant.variant_id.advance(7);
    auto const& variant_read = variant;
    auto [variant_bytes, variant_image] =
        ::fixy::with_mut(::fixy::mint_heap_array(g_test.alloc, 4096, std::byte{0}), [&variant_read](auto run) noexcept {
            return crucible::serialize_region(::fixy::to(variant_read), std::move(run));
        });
    CRUCIBLE_TEST_ASSERT(kTest, variant_bytes > 0);
    write_object_file(root_of(cipher), variant.content_hash,
                      variant_image.view().window(0, variant_bytes).expect("the writer writes inside its image"));
    const crucible::RegionNode& plain = *make_test_region(writers_dag, 0x62);
    CRUCIBLE_TEST_ASSERT(kTest, plain.content_hash == variant.content_hash);
    CRUCIBLE_TEST_ASSERT(kTest, cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(::fixy::to(plain)))
                                    == plain.content_hash);
    const crucible::Cipher fresh = open_cipher_under(root, ::fixy::TextView{"writers"});
    const auto fresh_ov = fresh.mint_open_view(store_ctx());
    ::fixy::BumpArena<DagArena> fresh_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
    const auto loaded =
        fresh.load_content_addressed(store_ctx(), fresh_ov, g_test.alloc, plain.content_hash, fresh_arena);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.is_ok() && region_of(loaded).variant_id.get() == 7
                                    && "a store keeps a sound object of its hash");
}

// Two child processes store one region at the same time, again and again, and
// each removes the object before each store.  So the two write one entry at
// the same time.  Each store gives the hash, and the object loads after the
// two end.  This holds for each order of the steps of the two.  With one
// shared temporary name, a writer could cut the file of the other or rename it
// away, and a store gave the none hash.
static void test_two_writers_of_one_object(::fixy::TextView root) {
    auto cipher = open_cipher_under(root, ::fixy::TextView{"two_writers"});
    auto ov = cipher.mint_open_view(store_ctx());
    ::fixy::BumpArena<DagArena> two_dag = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
    const crucible::RegionNode& two_region = *make_test_region(two_dag, 0x63);
    const ::fixy::fs::DirectoryHandle two_root = root_directory_of(cipher);
    const ::foundation::os::PathText<64> object = beside_object_path(two_region.content_hash, ::fixy::TextView{});
    const ::fixy::TestRunnerCtx child_ctx = store_ctx();
    const auto store_again_and_again = [&cipher, &ov, &child_ctx, &two_region, &two_root, &object] noexcept {
        for (std::size_t round = 0; round < 40; ++round) {
            static_cast<void>(::fixy::fs::unlink_at(child_ctx, two_root.ref(), object.c_text()).is_ok());
            const crucible::ContentHash stored =
                cipher.store(child_ctx, ov, crucible::Cipher::content_addressed(::fixy::to(two_region)));
            if (stored != two_region.content_hash) ::fixy::fatal("a store of one of two writers gave no hash");
        }
    };
    ::fixy::process::Child first =
        ::fixy::process::spawn_child(child_ctx, store_again_and_again).expect("the test starts a child");
    ::fixy::process::Child second =
        ::fixy::process::spawn_child(child_ctx, store_again_and_again).expect("the test starts a child");
    CRUCIBLE_TEST_ASSERT(
        kTest, ::fixy::process::wait(std::move(first)).expect("the test waits for its child").is_exit_with(0));
    CRUCIBLE_TEST_ASSERT(
        kTest, ::fixy::process::wait(std::move(second)).expect("the test waits for its child").is_exit_with(0));
    const crucible::Cipher fresh = open_cipher_under(root, ::fixy::TextView{"two_writers"});
    const auto fresh_ov = fresh.mint_open_view(store_ctx());
    ::fixy::BumpArena<DagArena> fresh_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
    CRUCIBLE_TEST_ASSERT(
        kTest,
        fresh.load_content_addressed(store_ctx(), fresh_ov, g_test.alloc, two_region.content_hash, fresh_arena).is_ok()
            && "the object of two writers loads");
}

// A torn last line of a session-event index, and a persist of a second batch
// after it.  The persist cuts the torn line, so the load gives the events of
// the two batches.  The old index joined the line of the second batch to the
// torn line, and the load skipped the second batch.
static void test_batch_after_a_torn_index_line(::fixy::TextView root) {
    auto cipher = open_cipher_under(root, ::fixy::TextView{"torn_index"});
    auto ov = cipher.mint_open_view(store_ctx());
    const auto first_batch = session_batch(10, 5);
    const auto second_batch = session_batch(15, 5);
    CRUCIBLE_TEST_ASSERT(kTest,
                         cipher.persist_session_events(store_ctx(), ov, g_test.alloc, first_batch.view()).is_ok());
    const ::fixy::session::SessionTagId session =
        first_batch.view().get(0).expect("the first batch holds five events")->session();
    append_index_line(cipher, session, ::fixy::TextView{"15,1"});
    CRUCIBLE_TEST_ASSERT(kTest,
                         cipher.persist_session_events(store_ctx(), ov, g_test.alloc, second_batch.view()).is_ok());
    const auto loaded = cipher.load_session_events(store_ctx(), ov, g_test.alloc, session);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.events.size() == 10 && loaded.skipped_batches == 0
                                    && "the batch after a torn index line loads");
    CRUCIBLE_TEST_ASSERT(kTest, step_at(loaded, 0) == 10 && step_at(loaded, 9) == 19);
}

// True when the first temporary file of a write of the object of hash is in
// the store of cipher.
[[nodiscard]] static bool holds_object_temporary(crucible::Cipher const& cipher, crucible::ContentHash hash) {
    const ::fixy::fs::DirectoryHandle root = root_directory_of(cipher);
    const ::foundation::os::PathText<64> path = beside_object_path(hash, ::fixy::TextView{".tmp-00"});
    return ::fixy::fs::has_entry_at(store_ctx(), root.ref(), path.c_text()).value_or(true);
}

// A store of a new object keeps an object at its name with the keep rename.
// A file system with no RENAME_NOREPLACE gives einval, and the store then
// replaces the entry: the object is written and loads from the disk, and no
// temporary file stays.  The rename fault of the test gives the einval.  A
// device error of the keep rename is no such case: the store reports it, no
// temporary file stays, and the object of the store before it still loads.
static void test_keep_rename_faults_of_a_new_object(::fixy::TextView root) {
    auto cipher = open_cipher_under(root, ::fixy::TextView{"rename_fault"});
    auto ov = cipher.mint_open_view(store_ctx());
    ::fixy::BumpArena<DagArena> fault_dag = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
    const crucible::RegionNode& written_region = *make_test_region(fault_dag, 0x71);
    const crucible::RegionNode& failed_region = *make_test_region(fault_dag, 0x72);
    {
        const ::fixy::fs::RenameFaultScope fault =
            ::fixy::fs::RenameFaultScope::arm(kTest, ::fixy::fs::RenameFailure::not_supported);
        CRUCIBLE_TEST_ASSERT(
            kTest, cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(::fixy::to(written_region)))
                           == written_region.content_hash
                       && "a store replaces the entry after einval of the keep rename");
    }
    CRUCIBLE_TEST_ASSERT(kTest, !holds_object_temporary(cipher, written_region.content_hash));
    {
        const ::fixy::fs::RenameFaultScope fault =
            ::fixy::fs::RenameFaultScope::arm(kTest, ::fixy::fs::RenameFailure::device_error);
        CRUCIBLE_TEST_ASSERT(
            kTest, cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(::fixy::to(failed_region)))
                           == crucible::ContentHash{}
                       && "a store reports a device error of the keep rename");
    }
    CRUCIBLE_TEST_ASSERT(kTest, !holds_object_file(cipher, failed_region.content_hash));
    CRUCIBLE_TEST_ASSERT(kTest, !holds_object_temporary(cipher, failed_region.content_hash));

    const crucible::Cipher fresh = open_cipher_under(root, ::fixy::TextView{"rename_fault"});
    const auto fresh_ov = fresh.mint_open_view(store_ctx());
    ::fixy::BumpArena<DagArena> fresh_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
    const auto loaded =
        fresh.load_content_addressed(store_ctx(), fresh_ov, g_test.alloc, written_region.content_hash, fresh_arena);
    CRUCIBLE_TEST_ASSERT(kTest,
                         loaded.is_ok() && !is_cache_hit(loaded) && "the object after einval loads from the disk");
}

// The whole text of the index of the events of session under the root of
// cipher.
[[nodiscard]] static ::fixy::HeapText index_text_of(crucible::Cipher const& cipher,
                                                    ::fixy::session::SessionTagId session) {
    ::foundation::os::PathText<64> index = session_events_path(session);
    index.append(::fixy::TextView{"index"}).expect("the index path fits its text");
    ::fixy::GrowText text = ::fixy::mint_grow_text(crucible::Cipher::MAX_OBJECT_BYTES);
    const ::fixy::fs::DirectoryHandle root = root_directory_of(cipher);
    CRUCIBLE_TEST_ASSERT(
        kTest, ::fixy::fs::read_whole_at(store_ctx(), g_test.alloc, root.ref(), index.c_text(), text).is_ok());
    return ::fixy::freeze(std::move(text));
}

// A persist whose index append fails after a part of its line cuts the index
// back to its whole lines.  The index holds its bytes of before the persist,
// and a reopen loads the events of the batch before it.  The write fault of
// the test lets the batch file pass whole, and it cuts the index line after
// five bytes, as a full disk does.
static void test_failed_index_append_cuts_back(::fixy::TextView root) {
    auto cipher = open_cipher_under(root, ::fixy::TextView{"index_fault"});
    auto ov = cipher.mint_open_view(store_ctx());
    const auto first_batch = session_batch(10, 5);
    const auto second_batch = session_batch(15, 5);
    CRUCIBLE_TEST_ASSERT(kTest,
                         cipher.persist_session_events(store_ctx(), ov, g_test.alloc, first_batch.view()).is_ok());
    const ::fixy::session::SessionTagId session =
        first_batch.view().get(0).expect("the first batch holds five events")->session();
    const ::fixy::HeapText before = index_text_of(cipher, session);
    {
        const ::foundation::core::ByteCount budget{crucible::cipher::federation::FEDERATION_HEADER_BYTES
                                                   + second_batch.size() * sizeof(crucible::Cipher::SessionEvent) + 5};
        const ::fixy::fs::WriteFaultScope fault =
            ::fixy::fs::WriteFaultScope::arm(kTest, budget, ::fixy::fs::WriteFailure::no_space);
        const crucible::Cipher::PersistResult persisted =
            cipher.persist_session_events(store_ctx(), ov, g_test.alloc, second_batch.view());
        CRUCIBLE_TEST_ASSERT(kTest, persisted.is_err_and([](crucible::Cipher::PersistError const& error) {
            return error.match(
                [](crucible::Cipher::PersistRefusal refusal) {
                    return refusal == crucible::Cipher::PersistRefusal::index_append_failed;
                },
                [](crucible::LoadError) { return false; });
        }));
    }
    const ::fixy::HeapText after = index_text_of(cipher, session);
    CRUCIBLE_TEST_ASSERT(kTest, after.view() == before.view() && "the failed append leaves no part of its line");

    const crucible::Cipher reopened = open_cipher_under(root, ::fixy::TextView{"index_fault"});
    const auto reopened_ov = reopened.mint_open_view(store_ctx());
    const auto loaded = reopened.load_session_events(store_ctx(), reopened_ov, g_test.alloc, session);
    CRUCIBLE_TEST_ASSERT(kTest, loaded.events.size() == 5 && loaded.skipped_batches == 0);
    CRUCIBLE_TEST_ASSERT(kTest, step_at(loaded, 0) == 10 && step_at(loaded, 4) == 14);
}

int main() {
    // The end of the owner removes the root of the test with each store below
    // it.
    const ::fixy::fs::TemporaryDirectory dir_owner = make_temporary_directory();
    CRUCIBLE_TEST_ASSERT(kTest, dir_owner.is_open() && "the test makes its temporary directory");
    const ::fixy::TextView dir = dir_owner.path().view();

    ::fixy::BumpArena<DagArena> dag_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});

    {
        auto cipher = open_cipher(dir);
        CRUCIBLE_TEST_ASSERT(kTest, cipher.empty() && "freshly opened Cipher must be empty");
        // The store keeps each character of its root.
        CRUCIBLE_TEST_ASSERT(kTest, root_of(cipher).size() == dir.size());
        CRUCIBLE_TEST_ASSERT(kTest, root_of(cipher) == dir);
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fs::open_directory_at(store_ctx(), dir_owner.ref(), "objects").is_ok());
    }

    const crucible::RegionNode& region = *make_test_region(dag_arena);
    const crucible::ContentHash expected_hash = region.content_hash;
    CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(expected_hash));

    {
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        const crucible::ContentHash stored_hash =
            cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(::fixy::to(region)));
        CRUCIBLE_TEST_ASSERT(kTest, stored_hash == expected_hash);

        CRUCIBLE_TEST_ASSERT(kTest, holds_object_file(cipher, expected_hash)
                                        && "serialized object file must exist after store()");

        // The content decides the name, so storing the same region
        // again names the same object.
        const crucible::ContentHash second_hash =
            cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(::fixy::to(region)));
        CRUCIBLE_TEST_ASSERT(kTest, second_hash == expected_hash);
    }

    {
        // A fresh arena receives the loaded region, so nothing it holds
        // can be a pointer back into the arena that produced it.
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        ::fixy::BumpArena<DagArena> arena2 = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        auto loaded_ca = cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, expected_hash, arena2);
        CRUCIBLE_TEST_ASSERT(kTest, loaded_ca.is_ok() && "load() must succeed for a stored hash");
        CRUCIBLE_TEST_ASSERT(kTest, region_of(loaded_ca).content_hash == expected_hash);
        CRUCIBLE_TEST_ASSERT(kTest, region_of(loaded_ca).num_ops() == region.num_ops());
    }

    {
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        (void)cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(::fixy::to(region)));

        // The log line of the first commit of the store holds the first
        // place, one.
        commit_head(cipher, ov, expected_hash, 10);
        CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == expected_hash);
        CRUCIBLE_TEST_ASSERT(kTest, last_log_order(cipher) == 1u);

        // The HEAD file holds the hash as sixteen lowercase hex digits
        // on one line, and that spelling is what another reader parses.
        ::fixy::FixedText<16> hex{};
        ::fixy::append_hex<16>(hex, expected_hash.raw()).expect("sixteen digits fit the text");
        CRUCIBLE_TEST_ASSERT(kTest,
                             has_first_line(cipher, "HEAD", hex.view()) && "HEAD file must contain the hex hash");

        // Nothing was ever stored under this hash.  Advancing to it
        // still succeeds, because the head names a commit rather than
        // an object that has to be resident.
        const crucible::ContentHash hash2{0xDEADBEEF12345678ULL};
        commit_head(cipher, ov, hash2, 50);
        CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == hash2);
        CRUCIBLE_TEST_ASSERT(kTest, last_log_order(cipher) == 2u && "the second commit takes the next place");
    }

    {
        // Opening again reads the log back from disk, so the queries
        // below run against the parsed file and not against state left
        // in memory by the block above.
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());

        const crucible::ContentHash hash2{0xDEADBEEF12345678ULL};

        // A step before the first commit has no answer.
        CRUCIBLE_TEST_ASSERT(kTest,
                             !cipher.hash_at_step(ov, 0) && "hash_at_step before first commit must return default");

        // The log holds commits at steps 10 and 50.  A step between or
        // beyond them resolves to the last commit at or before it.
        CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(ov, 10) == expected_hash);
        CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(ov, 30) == expected_hash);
        CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(ov, 50) == hash2);
        CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(ov, 999) == hash2);
    }

    // Each refusal of a load names its reason.  The files below have hashes
    // that no store of the test stored, so the cache holds no bytes of
    // them, and each load reads its file.
    {
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        ::fixy::BumpArena<DagArena> arena3 = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        ::fixy::BumpArena<DagArena> reason_dag_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        const auto load = [&](std::uint64_t hash) {
            return cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, crucible::ContentHash{hash}, arena3);
        };
        using crucible::LoadError;
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(load(0)) == LoadError::none_hash);
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(load(0xBADBADBADBADBAD0ULL)) == LoadError::file_open_failed);

        const crucible::RegionNode& other_region = *make_test_region(reason_dag_arena, 0x5E);
        auto [image_bytes, image] = ::fixy::with_mut(
            ::fixy::mint_heap_array(g_test.alloc, 4096, std::byte{0}), [&other_region](auto run) noexcept {
                return crucible::serialize_region(::fixy::to(other_region), std::move(run));
            });
        CRUCIBLE_TEST_ASSERT(kTest, image_bytes > 0);
        const ::fixy::View<const std::byte> sound =
            image.view().window(0, image_bytes).expect("the image holds its bytes");

        write_object_file(root_of(cipher), crucible::ContentHash{0x1111111111111111ULL},
                          ::fixy::View<const std::byte>{});
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(load(0x1111111111111111ULL)) == LoadError::file_size);

        // A sound region under the name of another hash.
        write_object_file(root_of(cipher), crucible::ContentHash{0x2222222222222222ULL}, sound);
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(load(0x2222222222222222ULL)) == LoadError::object_hash_mismatch);

        // A damaged image gives the reason of its first fault.
        write_object_file(root_of(cipher), crucible::ContentHash{0x3333333333333333ULL},
                          sound.window(0, 16).expect("the image holds sixteen bytes"));
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(load(0x3333333333333333ULL)) == LoadError::truncated);
        image.with_element(
            0, [](std::byte& element) noexcept { element = std::byte{0}; },
            [] noexcept { ::fixy::fatal("the image holds its magic"); });
        write_object_file(root_of(cipher), crucible::ContentHash{0x4444444444444444ULL},
                          image.view().window(0, image_bytes).expect("the image holds its bytes"));
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(load(0x4444444444444444ULL)) == LoadError::bad_magic);
    }

    {
        // The end of the owner removes the directory and the store in it.
        const ::fixy::fs::TemporaryDirectory dir_ca = make_temporary_directory();
        CRUCIBLE_TEST_ASSERT(kTest, dir_ca.is_open());

        ::fixy::BumpArena<DagArena> ca_dag_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        const crucible::RegionNode& ca_region = *make_test_region(ca_dag_arena);
        const auto ca_payload = crucible::Cipher::content_addressed(::fixy::to(ca_region));

        auto cipher = open_cipher(dir_ca.path().view());
        auto ov = cipher.mint_open_view(store_ctx());
        const crucible::ContentHash hash = cipher.store(store_ctx(), ov, ca_payload);
        CRUCIBLE_TEST_ASSERT(kTest, hash == ca_region.content_hash);

        CRUCIBLE_TEST_ASSERT(kTest, holds_object_file(cipher, hash));

        // A short object is a damaged object, such as one that a crash left
        // before its bytes reached the disk.  The store does not count it as
        // durable: it writes the object again, and the file grows back to its
        // full size.  A sound object stays as it is (the block of the
        // temporary file of another writer below).
        write_beside_object(root_of(cipher), hash, ::fixy::TextView{}, ::fixy::TextView{"hash-only"});
        const std::int64_t corrupted_size = object_file_size(cipher, hash);

        const crucible::ContentHash second = cipher.store(store_ctx(), ov, ca_payload);
        CRUCIBLE_TEST_ASSERT(kTest, second == hash);
        CRUCIBLE_TEST_ASSERT(kTest, object_file_size(cipher, hash) > corrupted_size
                                        && "a store writes a damaged object of its hash again");

        // Deleting the object leaves the load no disk to fall back on,
        // so a successful read can only have come from memory.
        remove_object_file(cipher, hash);
        ::fixy::BumpArena<DagArena> read_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        auto loaded = cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, hash, read_arena);
        CRUCIBLE_TEST_ASSERT(kTest, is_cache_hit(loaded) && "resident ContentAddressed bytes must avoid disk fetch");
        CRUCIBLE_TEST_ASSERT(kTest, loaded.is_ok());
        CRUCIBLE_TEST_ASSERT(kTest, region_of(loaded).content_hash == hash);
    }

    // Two independent stores of the same region, in two separate roots,
    // must agree on the name.  That agreement is what lets one side
    // send a hash instead of the bytes.
    {
        const ::fixy::fs::TemporaryDirectory sender_dir = make_temporary_directory();
        const ::fixy::fs::TemporaryDirectory receiver_dir = make_temporary_directory();
        CRUCIBLE_TEST_ASSERT(kTest, sender_dir.is_open());
        CRUCIBLE_TEST_ASSERT(kTest, receiver_dir.is_open());

        ::fixy::BumpArena<DagArena> ca_dag_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        const auto ca_payload = crucible::Cipher::content_addressed(make_test_region(ca_dag_arena));

        auto sender = open_cipher(sender_dir.path().view());
        auto receiver = open_cipher(receiver_dir.path().view());
        auto sender_ov = sender.mint_open_view(store_ctx());
        auto receiver_ov = receiver.mint_open_view(store_ctx());
        const crucible::ContentHash sender_hash = sender.store(store_ctx(), sender_ov, ca_payload);
        const crucible::ContentHash receiver_hash = receiver.store(store_ctx(), receiver_ov, ca_payload);
        CRUCIBLE_TEST_ASSERT(kTest, sender_hash == receiver_hash);

        remove_object_file(receiver, receiver_hash);
        ::fixy::BumpArena<DagArena> read_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        auto loaded = receiver.load_content_addressed(store_ctx(), receiver_ov, g_test.alloc, sender_hash, read_arena);
        CRUCIBLE_TEST_ASSERT(kTest, is_cache_hit(loaded));
        CRUCIBLE_TEST_ASSERT(kTest, loaded.is_ok());
        CRUCIBLE_TEST_ASSERT(kTest, region_of(loaded).content_hash == sender_hash);
    }

    {
        crucible::Cipher closed{::foundation::effects::testing::test().alloc};
        CRUCIBLE_TEST_ASSERT(kTest, !closed.is_open() && "a constructed Cipher must be Closed");
        // A store that is not open has no root to read.
        CRUCIBLE_TEST_ASSERT(kTest, closed.root().is_none());
        // These two queries are answerable without a root, so they stay
        // callable in the closed state rather than becoming errors.
        CRUCIBLE_TEST_ASSERT(kTest, closed.empty());
        CRUCIBLE_TEST_ASSERT(kTest, !closed.head());
    }

    {
        auto cipher = open_cipher(dir);
        CRUCIBLE_TEST_ASSERT(kTest, cipher.is_open());
        // Minting the view is itself the check.  Every call below takes
        // it as proof and repeats no check of its own.
        auto ov = cipher.mint_open_view(store_ctx());
        const auto hash =
            cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(make_test_region(dag_arena)));
        CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(hash));
        commit_head(cipher, ov, hash, 100);
        CRUCIBLE_TEST_ASSERT(kTest, cipher.head() == hash);
        CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(ov, 100) == hash);
    }

    {
        // The empty payload stores nothing, and the store gives the none
        // hash.  Only content_addressed_payload makes the empty payload.
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        const crucible::ContentHash hash = cipher.store(
            store_ctx(), ov,
            crucible::cipher::content_addressed_payload<crucible::RegionNode, crucible::DagArena>(::fixy::none));
        CRUCIBLE_TEST_ASSERT(kTest, !static_cast<bool>(hash));
    }

    {
        auto cipher = open_cipher(dir);
        CRUCIBLE_TEST_ASSERT(kTest, cipher.is_open());
        auto moved = std::move(cipher);
        CRUCIBLE_TEST_ASSERT(kTest, moved.is_open() && "moved-to must be Open");
        // Nothing is asserted about the source.  Its openness follows
        // from the state of a moved-from string, which the standard
        // leaves valid but unspecified.
    }

    // The log is parsed without exceptions, so a malformed line has to
    // be skipped rather than aborting the parse.  Each bad line below
    // encodes a different way for a line to be malformed.
    {
        const ::fixy::fs::TemporaryDirectory dir2 = make_temporary_directory();
        CRUCIBLE_TEST_ASSERT(kTest, dir2.is_open());
        static_cast<void>(::fixy::fs::make_directory_at(store_ctx(), dir2.ref(), "objects").is_ok());

        write_store_file(dir2.path().view(), "log",
                         ::fixy::TextView{"10,deadbeef00000001,1000\n"  // valid
                                          "0x14,deadbeef00000002,2000\n"  // bad: step_id with a hex prefix
                                          "20,GHI,2000\n"  // bad: invalid hex
                                          "30,deadbeef00000003\n"  // bad: missing field
                                          // valid, and then a sound line with no newline, which is a torn record
                                          "40,deadbeef00000004,4000\n50,deadbeef00000005,5000"});

        auto cipher = open_cipher(dir2.path().view());
        auto ov = cipher.mint_open_view(store_ctx());
        CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(ov, 10) == crucible::ContentHash{0xdeadbeef00000001ULL});
        CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(ov, 40) == crucible::ContentHash{0xdeadbeef00000004ULL});
        // A writer ends each record with a newline in the same write, so the
        // last line is a record whose append did not end, and it is no commit.
        CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(ov, 50) == crucible::ContentHash{0xdeadbeef00000004ULL}
                                        && "a last line with no newline is a torn record");
        // Step 30 falls where a skipped line claimed a commit.  It must
        // resolve back to step 10, which is what shows the bad line was
        // dropped rather than half-parsed.
        CRUCIBLE_TEST_ASSERT(kTest, cipher.hash_at_step(ov, 30) == crucible::ContentHash{0xdeadbeef00000001ULL});
    }

    // One batch of session events at steps 10 to 19, and one at steps 20
    // to 24.  A load from a step inside the first batch gives the rest of
    // that batch and all of the second, in step order.
    {
        const ::fixy::fs::TemporaryDirectory dir_events = make_temporary_directory();
        CRUCIBLE_TEST_ASSERT(kTest, dir_events.is_open());
        auto cipher = open_cipher(dir_events.path().view());
        auto ov = cipher.mint_open_view(store_ctx());
        const auto first_batch = session_batch(10, 10);
        const auto second_batch = session_batch(20, 5);
        const crucible::ContentHash first_hash =
            persisted_hash(cipher.persist_session_events(store_ctx(), ov, g_test.alloc, first_batch.view()));
        CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(first_hash));
        const crucible::ContentHash second_hash =
            persisted_hash(cipher.persist_session_events(store_ctx(), ov, g_test.alloc, second_batch.view()));
        CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(second_hash));
        // A batch whose file the store holds gives its hash again, after a
        // compare of the stored bytes with the bytes of the batch.
        CRUCIBLE_TEST_ASSERT(
            kTest, persisted_hash(cipher.persist_session_events(store_ctx(), ov, g_test.alloc, first_batch.view()))
                       == first_hash);

        const ::fixy::session::SessionTagId session =
            first_batch.view().get(0).expect("the first batch holds ten events")->session();
        const auto from_middle =
            cipher.load_session_events(store_ctx(), ov, g_test.alloc, session, ::fixy::session::StepId{15});
        CRUCIBLE_TEST_ASSERT(kTest, from_middle.events.size() == 10 && from_middle.skipped_batches == 0);
        for (std::size_t i = 0; i < from_middle.events.size(); ++i) {
            CRUCIBLE_TEST_ASSERT(kTest, step_at(from_middle, i) == 15 + i);
        }
        // The index holds the line of the first batch two times, and the
        // second line is no skip.
        const auto every_event = cipher.load_session_events(store_ctx(), ov, g_test.alloc, session);
        CRUCIBLE_TEST_ASSERT(kTest, every_event.events.size() == 15 && every_event.skipped_batches == 0);
        CRUCIBLE_TEST_ASSERT(kTest, step_at(every_event, 0) == 10 && step_at(every_event, 14) == 24);
        const auto second_only =
            cipher.load_session_events(store_ctx(), ov, g_test.alloc, session, ::fixy::session::StepId{20});
        CRUCIBLE_TEST_ASSERT(kTest, second_only.events.size() == 5 && step_at(second_only, 0) == 20);

        // A batch at steps 35 to 39, and then a batch at steps 30 to 34.  The
        // load holds steps 35 to 39 when it reads the second line, so it skips
        // that batch, counts the skip and reports it.
        const auto latest_batch = session_batch(35, 5);
        const auto late_batch = session_batch(30, 5);
        CRUCIBLE_TEST_ASSERT(kTest,
                             cipher.persist_session_events(store_ctx(), ov, g_test.alloc, latest_batch.view()).is_ok());
        CRUCIBLE_TEST_ASSERT(kTest,
                             cipher.persist_session_events(store_ctx(), ov, g_test.alloc, late_batch.view()).is_ok());
        const auto out_of_order = cipher.load_session_events(store_ctx(), ov, g_test.alloc, session);
        CRUCIBLE_TEST_ASSERT(kTest, out_of_order.events.size() == 20 && step_at(out_of_order, 15) == 35);
        CRUCIBLE_TEST_ASSERT(kTest, out_of_order.skipped_batches == 1
                                        && "a batch that overlaps the events of the load is a skip");

        // The batch file of the first batch does not read.  A persist of
        // the first batch gives the LoadError of the read, and no hash.
        overwrite_batch_file(cipher, session, first_hash, ::fixy::View<std::byte const>{});
        const bool is_unreadable_reported =
            cipher.persist_session_events(store_ctx(), ov, g_test.alloc, first_batch.view())
                .match([](crucible::ContentHash) { return false; },
                       [](crucible::Cipher::PersistError error) {
                           return error.match(
                               [](crucible::Cipher::PersistRefusal) { return false; },
                               [](crucible::LoadError reason) { return reason == crucible::LoadError::file_size; });
                       });
        CRUCIBLE_TEST_ASSERT(kTest, is_unreadable_reported && "a stored batch that does not read gives its LoadError");

        // A load skips the first batch at each of its two lines, and the
        // batch at steps 30 to 34.  It counts each skip and reports it.  The
        // load runs in a child, so the test reads the report.
        const auto without_first = cipher.load_session_events(store_ctx(), ov, g_test.alloc, session);
        CRUCIBLE_TEST_ASSERT(kTest, without_first.events.size() == 10 && step_at(without_first, 0) == 20);
        CRUCIBLE_TEST_ASSERT(kTest, step_at(without_first, 5) == 35 && without_first.skipped_batches == 3);
        const ::fixy::TestRunnerCtx child_ctx = store_ctx();
        const ::fixy::process::ChildReport report =
            ::fixy::process::wait(::fixy::process::spawn_child(child_ctx,
                                                               [&cipher, &ov, &child_ctx, session] noexcept {
                                                                   static_cast<void>(cipher.load_session_events(
                                                                       child_ctx, ov, g_test.alloc, session));
                                                               })
                                      .expect("the test starts a child"))
                .expect("the test waits for its child");
        CRUCIBLE_TEST_ASSERT(kTest, report.is_exit_with(0));
        CRUCIBLE_TEST_ASSERT(
            kTest, ::fixy::find(report.output(),
                                ::fixy::TextView{"[crucible] the Cipher store skips the session-event batch "})
                       .is_some());
        CRUCIBLE_TEST_ASSERT(
            kTest, ::fixy::find(report.output(), crucible::load_error_text(crucible::LoadError::file_size)).is_some());
        CRUCIBLE_TEST_ASSERT(
            kTest, ::fixy::find(report.output(), crucible::load_error_text(crucible::LoadError::batch_steps_mismatch))
                       .is_some());

        // The batch file at steps 35 to 39 holds bytes that are no
        // federation entry, and a new line of the index counts 3 events for
        // the 5 steps 60 to 64.  The load skips the two, and the batch at
        // steps 30 to 34 then overlaps no loaded step.
        const crucible::ContentHash latest_hash =
            persisted_hash(cipher.persist_session_events(store_ctx(), ov, g_test.alloc, latest_batch.view()));
        ::fixy::FixedArray<std::byte, 24> garbage{};
        garbage.fill(std::byte{0x5A});
        overwrite_batch_file(cipher, session, latest_hash, garbage.view());
        ::fixy::FixedText<16> latest_digits{};
        ::fixy::append_hex<16>(latest_digits, latest_hash.raw()).expect("sixteen digits fit the text");
        ::fixy::FixedText<96> counted_line{};
        append_index_line(cipher, session,
                          ::fixy::format(counted_line, "60,64,3,{}\n", latest_digits.view())
                              .expect("an index line fits 96 characters"));
        const auto damaged = cipher.load_session_events(store_ctx(), ov, g_test.alloc, session);
        CRUCIBLE_TEST_ASSERT(kTest, damaged.events.size() == 10 && step_at(damaged, 5) == 30);
        CRUCIBLE_TEST_ASSERT(kTest, damaged.skipped_batches == 5
                                        && "two lines of each damaged batch, and the line of the wrong count");
        const ::fixy::process::ChildReport damaged_report =
            ::fixy::process::wait(::fixy::process::spawn_child(child_ctx,
                                                               [&cipher, &ov, &child_ctx, session] noexcept {
                                                                   static_cast<void>(cipher.load_session_events(
                                                                       child_ctx, ov, g_test.alloc, session));
                                                               })
                                      .expect("the test starts a child"))
                .expect("the test waits for its child");
        CRUCIBLE_TEST_ASSERT(kTest, damaged_report.is_exit_with(0));
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::find(damaged_report.output(),
                                                 crucible::load_error_text(crucible::LoadError::batch_entry_malformed))
                                        .is_some());
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::find(damaged_report.output(),
                                                 crucible::load_error_text(crucible::LoadError::batch_count_mismatch))
                                        .is_some());
    }

    // The batch hash names a stored batch file, so its value for given bytes
    // must not change.  Each value is the hash of the release of this
    // format: the bytes 0 to 143, 72 bytes of 0xAB, and no byte.
    {
        ::foundation::core::FixedArray<std::byte, 144> const counting =
            ::foundation::core::build_fixed_array<std::byte, 144>([](auto cells) noexcept {
                std::size_t position = 0;
                for (std::byte& byte : cells) {
                    byte = static_cast<std::byte>(position);
                    ++position;
                }
            });
        ::foundation::core::FixedArray<std::byte, 72> repeated{};
        repeated.fill(std::byte{0xAB});
        CRUCIBLE_TEST_ASSERT(kTest, crucible::Cipher::session_event_batch_hash(counting.view())
                                        == crucible::ContentHash{0xeb48340c1ea14751ULL});
        CRUCIBLE_TEST_ASSERT(kTest, crucible::Cipher::session_event_batch_hash(repeated.view())
                                        == crucible::ContentHash{0x1a51e80700eac78fULL});
        CRUCIBLE_TEST_ASSERT(kTest, crucible::Cipher::session_event_batch_hash(::fixy::View<std::byte const>{})
                                        == crucible::ContentHash{0x35c3cb3b6b06b0e2ULL});

        // The federation key in the header of a batch is the keyed BLAKE3 of
        // the payload under the key "crucible cipher session events 2".  Each
        // value is the output of the official BLAKE3 C at the commit
        // 657284b1, as four little-endian words.
        using ::crucible::cipher::federation::FederationKey;
        using ::foundation::core::Hash256;
        const FederationKey counting_key{
            Hash256{0x78b117643bdd5d63ULL, 0x637360a4b51fa722ULL, 0xe7fd70a65a9f030dULL, 0xd95da61236d8f887ULL}};
        const FederationKey repeated_key{
            Hash256{0x87b6a87e3ca535d0ULL, 0x482bd9e3290213c7ULL, 0x8f9efe6225b472a0ULL, 0x9e74f9b92b0cf626ULL}};
        const FederationKey empty_key{
            Hash256{0x8017e8ac3fb85a68ULL, 0xe1668b978596f805ULL, 0x0379df74210974b3ULL, 0x9960f40a5d09d12dULL}};
        CRUCIBLE_TEST_ASSERT(kTest, crucible::Cipher::session_event_batch_key(counting.view()) == counting_key);
        CRUCIBLE_TEST_ASSERT(kTest, crucible::Cipher::session_event_batch_key(repeated.view()) == repeated_key);
        CRUCIBLE_TEST_ASSERT(kTest,
                             crucible::Cipher::session_event_batch_key(::fixy::View<std::byte const>{}) == empty_key);
    }

    // The cache of object bytes holds at most 64 entries, and it removes the
    // entry used least recently.  A load of the first object before the last
    // store makes the first object the most recent entry.  So the 65th store
    // removes the second object, and the first and the last stay.  The files
    // are deleted first, so a load finds a region only in the cache.
    {
        const ::fixy::fs::TemporaryDirectory dir_cache = make_temporary_directory();
        CRUCIBLE_TEST_ASSERT(kTest, dir_cache.is_open());
        ::fixy::BumpArena<DagArena> cache_dag_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 20});
        auto cipher = open_cipher(dir_cache.path().view());
        auto ov = cipher.mint_open_view(store_ctx());
        constexpr std::size_t kStoreCount = 65;
        ::fixy::FixedArray<crucible::ContentHash, kStoreCount> hashes{};
        ::fixy::BumpArena<DagArena> read_arena = ::fixy::mint_bump_arena<DagArena>(Bytes{size_t{1} << 16});
        for (std::size_t i = 0; i < kStoreCount; ++i) {
            if (i + 1 == kStoreCount) {
                CRUCIBLE_TEST_ASSERT(
                    kTest, is_cache_hit(cipher.load_content_addressed(store_ctx(), ov, g_test.alloc,
                                                                      hashes.at(::fixy::index<0>()), read_arena)));
            }
            crucible::ContentHash const stored = cipher.store(
                store_ctx(), ov, crucible::Cipher::content_addressed(make_test_region(cache_dag_arena, i + 1)));
            hashes.set(::fixy::Index<kStoreCount>::check(i).expect(
                           "a test reads an element at or past the count of its array"),
                       stored);
            CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(stored));
        }
        for (const crucible::ContentHash hash : hashes) {
            remove_object_file(cipher, hash);
        }
        // The cache removed the second object, and its file is gone.
        CRUCIBLE_TEST_ASSERT(kTest, reason_of(cipher.load_content_addressed(store_ctx(), ov, g_test.alloc,
                                                                            hashes.at(::fixy::index<1>()), read_arena))
                                        == crucible::LoadError::file_open_failed);
        CRUCIBLE_TEST_ASSERT(kTest, is_cache_hit(cipher.load_content_addressed(
                                        store_ctx(), ov, g_test.alloc, hashes.at(::fixy::index<0>()), read_arena)));
        const auto newest = cipher.load_content_addressed(store_ctx(), ov, g_test.alloc,
                                                          hashes.at(::fixy::index<kStoreCount - 1>()), read_arena);
        CRUCIBLE_TEST_ASSERT(kTest, is_cache_hit(newest) && newest.is_ok());
    }

    // The tests of the order of the steps, of a torn record, of the writers
    // of one object and of a torn index line.  Each store is a directory under
    // the root of the test.
    {
        const crucible::Cipher anchor = open_cipher(dir);
        test_commit_below_the_last_step(root_of(anchor));
        test_step_above_the_ceiling_is_corrupt(root_of(anchor));
        test_commit_after_a_torn_record(root_of(anchor));
        test_store_leaves_the_file_of_another_writer(root_of(anchor));
        test_two_writers_of_one_object(root_of(anchor));
        test_batch_after_a_torn_index_line(root_of(anchor));
        test_failed_index_append_cuts_back(root_of(anchor));
        test_keep_rename_faults_of_a_new_object(root_of(anchor));
    }

    ::fixy::check::pass(kTest, "test_cipher: all tests passed\n");
    return 0;
}
