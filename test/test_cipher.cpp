#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>
#include <crucible/cipher/FederationProtocol.h>
#include <foundation/core/KeyedHash.h>

#include <memory>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Process.h>
#include <fixy/session/Subtype.h>
#include "test_assert.h"
#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

// Opening the store and its open view need a context whose row admits IO
// and Block.
[[nodiscard]] inline ::fixy::TestRunnerCtx store_ctx() {
    return ::fixy::TestRunnerCtx{::foundation::effects::testing::test()};
}

static auto g_test = ::foundation::effects::testing::test();

// The root path crosses a trust boundary, and every call site here has
// to say so.  The helper keeps that declaration from swamping the calls.
// The text of the root copies the characters of the directory.
[[nodiscard]] static crucible::Cipher open_cipher(const char* dir) {
    ::foundation::core::GrowText root = ::foundation::core::mint_grow_text(crucible::Cipher::MAX_ROOT_PATH_BYTES);
    for (std::size_t index = 0; dir[index] != '\0'; ++index) {
        root.append(g_test.alloc, dir[index]).expect("the text holds each character of the directory");
    }
    ::foundation::core::HeapText const root_path = ::foundation::core::freeze(std::move(root));
    return crucible::Cipher::open(store_ctx(), g_test.alloc,
                                  ::fixy::mint_tagged<::fixy::tags::source::External>(root_path.view()))
        .expect("the test opens its store");
}

// The salt goes into the schema hashes, so two salts give two regions with
// two content hashes.  The ops, their arrays and the region are in
// dag_arena.
static ::fixy::Ref<crucible::RegionNode> make_test_region(::fixy::BumpArena& dag_arena, std::uint64_t salt = 0) {
    constexpr uint32_t NUM_OPS = 2;
    const ::fixy::View<crucible::TraceEntry> ops =
        dag_arena.array<crucible::TraceEntry>(g_test.alloc, NUM_OPS, crucible::TraceEntry{});

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        crucible::TraceEntry& op = *ops.get(i).expect("i is below NUM_OPS");
        op.schema_hash = crucible::SchemaHash{0xCAFE0000 + i + (salt << 8)};

        crucible::TensorMeta meta{};
        meta.ndim = 1;
        meta.sizes[0] = ::crucible::tensor_dim(16);
        meta.strides[0] = ::crucible::tensor_dim(1);
        meta.dtype = crucible::ScalarType::Float;
        op.set_inputs(crucible::test::run_of(g_test.alloc, dag_arena, meta),
                      crucible::test::run_of(g_test.alloc, dag_arena, crucible::OpIndex{}),
                      crucible::test::run_of(g_test.alloc, dag_arena, crucible::SlotId{}));
        op.set_outputs(crucible::test::run_of(g_test.alloc, dag_arena, meta),
                       crucible::test::run_of(g_test.alloc, dag_arena, crucible::SlotId{i}));
    }

    return crucible::make_region(g_test.alloc, dag_arena, ops);
}

// An object lives at objects/<first two hex digits>/<remaining digits>,
// so a directory never accumulates every object at one level.
static std::string object_path(const char* dir, crucible::ContentHash hash) {
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016" PRIx64, hash.raw());
    return std::string(dir) + "/objects/" + std::string(hex, 2) + "/" + (hex + 2);
}

// Commits a head and requires that the commit happened.
static void commit_head(crucible::Cipher& cipher, crucible::Cipher::OpenView const& view, crucible::ContentHash hash,
                        std::uint64_t step) {
    const auto committed = cipher.record_event(store_ctx(), view, hash, step);
    assert(committed.is_ok() && "record_event must commit when the writes pass");
}

// The third field of the last line of the log, the place of the last
// commit.
static std::uint64_t last_log_order(const char* dir) {
    std::ifstream log(std::string(dir) + "/log");
    std::string line;
    std::string last;
    while (std::getline(log, line)) {
        if (!line.empty()) last = line;
    }
    const std::size_t second_comma = last.find(',', last.find(',') + 1);
    assert(second_comma != std::string::npos);
    std::uint64_t order = 0;
    const char* const end = last.data() + last.size();
    const auto [stop, error] = std::from_chars(last.data() + second_comma + 1, end, order);
    assert(error == std::errc{} && stop == end);
    return order;
}

// The events of one session, at steps first, first + 1, ..., first + count - 1.
static ::fixy::HeapArray<crucible::Cipher::SessionEvent> session_batch(std::uint64_t first, std::size_t count) {
    ::fixy::HeapArray<crucible::Cipher::SessionEvent> events = ::fixy::mint_heap_array(
        g_test.alloc, count,
        crucible::Cipher::SessionEvent::close(::fixy::session::RoleTagId{}, ::fixy::session::RoleTagId{}));
    std::uint64_t step = first;
    for (crucible::Cipher::SessionEvent& event : events.view()) {
        event = crucible::Cipher::SessionEvent::cipher_event(
            ::fixy::session::SessionOp::StoreCommitted, ::fixy::session::StepId{step},
            ::fixy::session::StateHash{0x5E550000u + step}, ::fixy::session::CipherEventOrder{step});
        ++step;
    }
    return events;
}

// The step of the event at position of a list of loaded events.
[[nodiscard]] static std::uint64_t step_at(crucible::Cipher::SessionEventLoad const& loaded, std::size_t position) {
    return loaded.events.with_element(
        position, [](crucible::Cipher::SessionEvent const& event) noexcept { return event.step_id().value; },
        [] noexcept -> std::uint64_t { ::fixy::fatal("the position is inside the list of loaded events"); });
}

// The hash of a batch that the store persists.  A refusal ends the test.
[[nodiscard]] static crucible::ContentHash persisted_hash(crucible::Cipher::PersistResult const& persisted) {
    return persisted.match([](crucible::ContentHash hash) { return hash; },
                           [](crucible::Cipher::PersistError const&) -> crucible::ContentHash {
                               ::fixy::fatal("the store refuses a batch that the test persists");
                           });
}

// The directory of the root of cipher.
[[nodiscard]] static ::fixy::fs::Dirfd root_dirfd_of(crucible::Cipher const& cipher) {
    return ::fixy::fs::open_dirfd(
               store_ctx(), ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(cipher.root()))
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
    const ::fixy::fs::OwnedFd file =
        ::fixy::fs::mint_file_at<WriteTruncate>(store_ctx(), root_dirfd_of(cipher), batch.c_text())
            .expect("the test opens the batch file to write it")
            .consume();
    assert(::fixy::fs::write_full(store_ctx(), file, bytes).is_ok());
}

// Appends line to the index of the events of session under the root of
// cipher.
static void append_index_line(crucible::Cipher const& cipher, ::fixy::session::SessionTagId session,
                              ::fixy::TextView line) {
    ::foundation::os::PathText<64> index = session_events_path(session);
    index.append(::fixy::TextView{"index"}).expect("the index path fits its text");
    using WriteAppend = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteAppend>;
    const ::fixy::fs::OwnedFd file =
        ::fixy::fs::mint_file_at<WriteAppend>(store_ctx(), root_dirfd_of(cipher), index.c_text())
            .expect("the test opens the index to append a line")
            .consume();
    assert(::fixy::fs::write_full(store_ctx(), file, line).is_ok());
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
    const ::fixy::fs::Dirfd root_dir =
        ::fixy::fs::open_dirfd(ctx, ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(root))
                                        .expect("the root of a store is a sanitized path"))
            .expect("the root of the store opens");
    ::foundation::os::PathText<64> object{};
    object.append(::fixy::TextView{"objects/"}).expect("the object path fits its text");
    object.append_hex<2>(hash.raw() >> 56U).expect("the object path fits its text");
    static_cast<void>(::fixy::fs::make_directory_at(ctx, root_dir, object.c_text()).is_ok());
    object.append('/').expect("the object path fits its text");
    object.append_hex<14>(hash.raw()).expect("the object path fits its text");
    using WriteTruncate = ::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteTruncate>;
    const bool is_written = ::fixy::fs::mint_file_at<WriteTruncate>(ctx, root_dir, object.c_text())
                                .match(
                                    [&](::fixy::Linear<::fixy::fs::OwnedFd> opened) {
                                        const ::fixy::fs::OwnedFd file = std::move(opened).consume();
                                        return bytes.size() == 0 || ::fixy::fs::write_full(ctx, file, bytes).is_ok();
                                    },
                                    [](::foundation::os::Errno) { return false; });
    assert(is_written && "the test writes the file of the object");
}

static_assert(
    ::fixy::session::is_content_addressed_v<typename crucible::Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(::fixy::session::is_content_addressed_v<
              typename crucible::Cipher::LoadedContentAddressedRegionPayload::payload_type>);
static_assert(::fixy::session::is_payload_subsort_v<
              crucible::RegionNode, typename crucible::Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(sizeof(crucible::Cipher::ContentAddressedRegionPayload) == sizeof(const crucible::RegionNode*));

// A load result that holds a region comes only from the store, because its
// cache flag is a claim about where the bytes came from.  A payload comes
// only from content_addressed_payload.
static_assert(!std::is_constructible_v<crucible::Cipher::LoadedContentAddressedRegionPayload,
                                       ::fixy::Ref<crucible::RegionNode>, bool>);
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

int main() {
    char tmpdir[] = "/tmp/crucible_cipher_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != nullptr && "mkdtemp failed");

    ::fixy::BumpArena dag_arena{1 << 16};

    {
        auto cipher = open_cipher(dir);
        assert(cipher.empty() && "freshly opened Cipher must be empty");
        // The store keeps each character of its root.
        std::size_t root_index = 0;
        for (const char character : cipher.root()) {
            assert(character == dir[root_index]);
            ++root_index;
        }
        assert(dir[root_index] == '\0');
        assert(std::filesystem::is_directory(std::string(dir) + "/objects"));
    }

    const crucible::RegionNode& region = *make_test_region(dag_arena);
    const crucible::ContentHash expected_hash = region.content_hash;
    assert(static_cast<bool>(expected_hash));

    {
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        const crucible::ContentHash stored_hash =
            cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(::fixy::to(region)));
        assert(stored_hash == expected_hash);

        const std::string expected_path = object_path(dir, expected_hash);
        assert(std::filesystem::exists(expected_path) && "serialized object file must exist after store()");

        // The content decides the name, so storing the same region
        // again names the same object.
        const crucible::ContentHash second_hash =
            cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(::fixy::to(region)));
        assert(second_hash == expected_hash);
    }

    {
        // A fresh arena receives the loaded region, so nothing it holds
        // can be a pointer back into the arena that produced it.
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        ::fixy::BumpArena arena2{1 << 16};
        auto loaded_ca = cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, expected_hash, arena2);
        assert(loaded_ca.is_ok() && "load() must succeed for a stored hash");
        assert(region_of(loaded_ca).content_hash == expected_hash);
        assert(region_of(loaded_ca).num_ops() == region.num_ops());
    }

    {
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        (void)cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(::fixy::to(region)));

        // The log line of the first commit of the store holds the first
        // place, one.
        commit_head(cipher, ov, expected_hash, 10);
        assert(cipher.head() == expected_hash);
        assert(last_log_order(dir) == 1u);

        // The HEAD file holds the hash as sixteen lowercase hex digits
        // on one line, and that spelling is what another reader parses.
        std::ifstream hf(std::string(dir) + "/HEAD");
        std::string head_str;
        std::getline(hf, head_str);
        char hex[17];
        std::snprintf(hex, sizeof(hex), "%016" PRIx64, expected_hash.raw());
        assert(head_str == hex && "HEAD file must contain the hex hash");

        // Nothing was ever stored under this hash.  Advancing to it
        // still succeeds, because the head names a commit rather than
        // an object that has to be resident.
        const crucible::ContentHash hash2{0xDEADBEEF12345678ULL};
        commit_head(cipher, ov, hash2, 50);
        assert(cipher.head() == hash2);
        assert(last_log_order(dir) == 2u && "the second commit takes the next place");
    }

    {
        // Opening again reads the log back from disk, so the queries
        // below run against the parsed file and not against state left
        // in memory by the block above.
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());

        const crucible::ContentHash hash2{0xDEADBEEF12345678ULL};

        // A step before the first commit has no answer.
        assert(!cipher.hash_at_step(ov, 0) && "hash_at_step before first commit must return default");

        // The log holds commits at steps 10 and 50.  A step between or
        // beyond them resolves to the last commit at or before it.
        assert(cipher.hash_at_step(ov, 10) == expected_hash);
        assert(cipher.hash_at_step(ov, 30) == expected_hash);
        assert(cipher.hash_at_step(ov, 50) == hash2);
        assert(cipher.hash_at_step(ov, 999) == hash2);
    }

    // Each refusal of a load names its reason.  The files below have hashes
    // that no store of the test stored, so the cache holds no bytes of
    // them, and each load reads its file.
    {
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        ::fixy::BumpArena arena3{1 << 16};
        ::fixy::BumpArena reason_dag_arena{1 << 16};
        const auto load = [&](std::uint64_t hash) {
            return cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, crucible::ContentHash{hash}, arena3);
        };
        using crucible::LoadError;
        assert(reason_of(load(0)) == LoadError::none_hash);
        assert(reason_of(load(0xBADBADBADBADBAD0ULL)) == LoadError::file_open_failed);

        const crucible::RegionNode& other_region = *make_test_region(reason_dag_arena, 0x5E);
        ::fixy::HeapArray<std::byte> image = ::fixy::mint_heap_array(g_test.alloc, 4096, std::byte{0});
        const std::size_t image_bytes =
            crucible::serialize_region(crucible::SerializedRegion{other_region}, image.view());
        assert(image_bytes > 0);
        const ::fixy::View<std::byte> sound = image.view().window(0, image_bytes).expect("the image holds its bytes");

        write_object_file(cipher.root(), crucible::ContentHash{0x1111111111111111ULL}, ::fixy::View<const std::byte>{});
        assert(reason_of(load(0x1111111111111111ULL)) == LoadError::file_size);

        // A sound region under the name of another hash.
        write_object_file(cipher.root(), crucible::ContentHash{0x2222222222222222ULL}, sound);
        assert(reason_of(load(0x2222222222222222ULL)) == LoadError::object_hash_mismatch);

        // A damaged image gives the reason of its first fault.
        write_object_file(cipher.root(), crucible::ContentHash{0x3333333333333333ULL},
                          sound.window(0, 16).expect("the image holds sixteen bytes"));
        assert(reason_of(load(0x3333333333333333ULL)) == LoadError::truncated);
        *sound.get(0).expect("the image holds its magic") = std::byte{0};
        write_object_file(cipher.root(), crucible::ContentHash{0x4444444444444444ULL}, sound);
        assert(reason_of(load(0x4444444444444444ULL)) == LoadError::bad_magic);
    }

    {
        char tmpl_ca[] = "/tmp/crucible_cipher_ca_XXXXXX";
        char* dir_ca = mkdtemp(tmpl_ca);
        assert(dir_ca != nullptr);

        ::fixy::BumpArena ca_dag_arena{1 << 16};
        const crucible::RegionNode& ca_region = *make_test_region(ca_dag_arena);
        const auto ca_payload = crucible::Cipher::content_addressed(::fixy::to(ca_region));

        auto cipher = open_cipher(dir_ca);
        auto ov = cipher.mint_open_view(store_ctx());
        const crucible::ContentHash hash = cipher.store(store_ctx(), ov, ca_payload);
        assert(hash == ca_region.content_hash);

        const std::string path = object_path(dir_ca, hash);
        assert(std::filesystem::exists(path));

        // Overwriting the object with something shorter makes the next
        // store detectable: if it rewrote the bytes, the file would
        // grow back to its original size.
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f << "hash-only";
        }
        const auto corrupted_size = std::filesystem::file_size(path);

        const crucible::ContentHash second = cipher.store(store_ctx(), ov, ca_payload);
        assert(second == hash);
        assert(std::filesystem::file_size(path) == corrupted_size
               && "duplicate ContentAddressed store must not rewrite bytes");

        // Deleting the object leaves the load no disk to fall back on,
        // so a successful read can only have come from memory.
        std::filesystem::remove(path);
        ::fixy::BumpArena read_arena{1 << 16};
        auto loaded = cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, hash, read_arena);
        assert(is_cache_hit(loaded) && "resident ContentAddressed bytes must avoid disk fetch");
        assert(loaded.is_ok());
        assert(region_of(loaded).content_hash == hash);

        std::filesystem::remove_all(dir_ca);
    }

    // Two independent stores of the same region, in two separate roots,
    // must agree on the name.  That agreement is what lets one side
    // send a hash instead of the bytes.
    {
        char sender_tmpl[] = "/tmp/crucible_cipher_sender_XXXXXX";
        char receiver_tmpl[] = "/tmp/crucible_cipher_receiver_XXXXXX";
        char* sender_dir = mkdtemp(sender_tmpl);
        char* receiver_dir = mkdtemp(receiver_tmpl);
        assert(sender_dir != nullptr);
        assert(receiver_dir != nullptr);

        ::fixy::BumpArena ca_dag_arena{1 << 16};
        const auto ca_payload = crucible::Cipher::content_addressed(make_test_region(ca_dag_arena));

        auto sender = open_cipher(sender_dir);
        auto receiver = open_cipher(receiver_dir);
        auto sender_ov = sender.mint_open_view(store_ctx());
        auto receiver_ov = receiver.mint_open_view(store_ctx());
        const crucible::ContentHash sender_hash = sender.store(store_ctx(), sender_ov, ca_payload);
        const crucible::ContentHash receiver_hash = receiver.store(store_ctx(), receiver_ov, ca_payload);
        assert(sender_hash == receiver_hash);

        std::filesystem::remove(object_path(receiver_dir, receiver_hash));
        ::fixy::BumpArena read_arena{1 << 16};
        auto loaded = receiver.load_content_addressed(store_ctx(), receiver_ov, g_test.alloc, sender_hash, read_arena);
        assert(is_cache_hit(loaded));
        assert(loaded.is_ok());
        assert(region_of(loaded).content_hash == sender_hash);

        std::filesystem::remove_all(sender_dir);
        std::filesystem::remove_all(receiver_dir);
    }

    {
        crucible::Cipher closed{::foundation::effects::testing::test().alloc};
        assert(!closed.is_open() && "a constructed Cipher must be Closed");
        // These two queries are answerable without a root, so they stay
        // callable in the closed state rather than becoming errors.
        assert(closed.empty());
        assert(!closed.head());
    }

    {
        auto cipher = open_cipher(dir);
        assert(cipher.is_open());
        // Minting the view is itself the check.  Every call below takes
        // it as proof and repeats no check of its own.
        auto ov = cipher.mint_open_view(store_ctx());
        const auto hash =
            cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(make_test_region(dag_arena)));
        assert(static_cast<bool>(hash));
        commit_head(cipher, ov, hash, 100);
        assert(cipher.head() == hash);
        assert(cipher.hash_at_step(ov, 100) == hash);
    }

    {
        // The empty payload stores nothing, and the store gives the none
        // hash.  Only content_addressed_payload makes the empty payload.
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        const crucible::ContentHash hash = cipher.store(
            store_ctx(), ov, crucible::cipher::content_addressed_payload<crucible::RegionNode>(::fixy::none));
        assert(!static_cast<bool>(hash));
    }

    {
        auto cipher = open_cipher(dir);
        assert(cipher.is_open());
        auto moved = std::move(cipher);
        assert(moved.is_open() && "moved-to must be Open");
        // Nothing is asserted about the source.  Its openness follows
        // from the state of a moved-from string, which the standard
        // leaves valid but unspecified.
    }

    // The log is parsed without exceptions, so a malformed line has to
    // be skipped rather than aborting the parse.  Each bad line below
    // encodes a different way for a line to be malformed.
    {
        char tmpl2[] = "/tmp/crucible_corrupt_XXXXXX";
        char* dir2 = mkdtemp(tmpl2);
        assert(dir2 != nullptr);
        std::filesystem::create_directories(std::string(dir2) + "/objects");

        {
            std::ofstream lf(std::string(dir2) + "/log");
            lf << "10,deadbeef00000001,1000\n";  // valid
            lf << "0x14,deadbeef00000002,2000\n";  // bad: step_id with a hex prefix
            lf << "20,GHI,2000\n";  // bad: invalid hex
            lf << "30,deadbeef00000003\n";  // bad: missing field
            lf << "40,deadbeef00000004,4000";  // valid, and the last line has no newline
        }

        auto cipher = open_cipher(dir2);
        auto ov = cipher.mint_open_view(store_ctx());
        assert(cipher.hash_at_step(ov, 10) == crucible::ContentHash{0xdeadbeef00000001ULL});
        assert(cipher.hash_at_step(ov, 40) == crucible::ContentHash{0xdeadbeef00000004ULL});
        // Step 30 falls where a skipped line claimed a commit.  It must
        // resolve back to step 10, which is what shows the bad line was
        // dropped rather than half-parsed.
        assert(cipher.hash_at_step(ov, 30) == crucible::ContentHash{0xdeadbeef00000001ULL});

        std::filesystem::remove_all(dir2);
    }

    // One batch of session events at steps 10 to 19, and one at steps 20
    // to 24.  A load from a step inside the first batch gives the rest of
    // that batch and all of the second, in step order.
    {
        char tmpl_events[] = "/tmp/crucible_cipher_events_XXXXXX";
        char* dir_events = mkdtemp(tmpl_events);
        assert(dir_events != nullptr);
        auto cipher = open_cipher(dir_events);
        auto ov = cipher.mint_open_view(store_ctx());
        const auto first_batch = session_batch(10, 10);
        const auto second_batch = session_batch(20, 5);
        const crucible::ContentHash first_hash =
            persisted_hash(cipher.persist_session_events(store_ctx(), ov, g_test.alloc, first_batch.view()));
        assert(static_cast<bool>(first_hash));
        const crucible::ContentHash second_hash =
            persisted_hash(cipher.persist_session_events(store_ctx(), ov, g_test.alloc, second_batch.view()));
        assert(static_cast<bool>(second_hash));
        // A batch whose file the store holds gives its hash again, after a
        // compare of the stored bytes with the bytes of the batch.
        assert(persisted_hash(cipher.persist_session_events(store_ctx(), ov, g_test.alloc, first_batch.view()))
               == first_hash);

        const ::fixy::session::SessionTagId session =
            first_batch.view().get(0).expect("the first batch holds ten events")->session();
        const auto from_middle =
            cipher.load_session_events(store_ctx(), ov, g_test.alloc, session, ::fixy::session::StepId{15});
        assert(from_middle.events.size() == 10 && from_middle.skipped_batches == 0);
        for (std::size_t i = 0; i < from_middle.events.size(); ++i) {
            assert(step_at(from_middle, i) == 15 + i);
        }
        // The index holds the line of the first batch two times, and the
        // second line is no skip.
        const auto every_event = cipher.load_session_events(store_ctx(), ov, g_test.alloc, session);
        assert(every_event.events.size() == 15 && every_event.skipped_batches == 0);
        assert(step_at(every_event, 0) == 10 && step_at(every_event, 14) == 24);
        const auto second_only =
            cipher.load_session_events(store_ctx(), ov, g_test.alloc, session, ::fixy::session::StepId{20});
        assert(second_only.events.size() == 5 && step_at(second_only, 0) == 20);

        // A batch at steps 35 to 39, and then a batch at steps 30 to 34.  The
        // load holds steps 35 to 39 when it reads the second line, so it skips
        // that batch, counts the skip and reports it.
        const auto latest_batch = session_batch(35, 5);
        const auto late_batch = session_batch(30, 5);
        assert(cipher.persist_session_events(store_ctx(), ov, g_test.alloc, latest_batch.view()).is_ok());
        assert(cipher.persist_session_events(store_ctx(), ov, g_test.alloc, late_batch.view()).is_ok());
        const auto out_of_order = cipher.load_session_events(store_ctx(), ov, g_test.alloc, session);
        assert(out_of_order.events.size() == 20 && step_at(out_of_order, 15) == 35);
        assert(out_of_order.skipped_batches == 1 && "a batch that overlaps the events of the load is a skip");

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
        assert(is_unreadable_reported && "a stored batch that does not read gives its LoadError");

        // A load skips the first batch at each of its two lines, and the
        // batch at steps 30 to 34.  It counts each skip and reports it.  The
        // load runs in a child, so the test reads the report.
        const auto without_first = cipher.load_session_events(store_ctx(), ov, g_test.alloc, session);
        assert(without_first.events.size() == 10 && step_at(without_first, 0) == 20);
        assert(step_at(without_first, 5) == 35 && without_first.skipped_batches == 3);
        const ::fixy::TestRunnerCtx child_ctx = store_ctx();
        const ::fixy::process::ChildReport report =
            ::fixy::process::wait(::fixy::process::spawn_child(child_ctx,
                                                               [&cipher, &ov, &child_ctx, session] noexcept {
                                                                   static_cast<void>(cipher.load_session_events(
                                                                       child_ctx, ov, g_test.alloc, session));
                                                               })
                                      .expect("the test starts a child"))
                .expect("the test waits for its child");
        assert(report.is_exit_with(0));
        assert(::fixy::find(report.output(),
                            ::fixy::TextView{"[crucible] the Cipher store skips the session-event batch "})
                   .is_some());
        assert(::fixy::find(report.output(), crucible::load_error_text(crucible::LoadError::file_size)).is_some());
        assert(::fixy::find(report.output(), crucible::load_error_text(crucible::LoadError::batch_steps_mismatch))
                   .is_some());

        // The batch file at steps 35 to 39 holds bytes that are no
        // federation entry, and a new line of the index counts 3 events for
        // the 5 steps 60 to 64.  The load skips the two, and the batch at
        // steps 30 to 34 then overlaps no loaded step.
        const crucible::ContentHash latest_hash =
            persisted_hash(cipher.persist_session_events(store_ctx(), ov, g_test.alloc, latest_batch.view()));
        ::fixy::FixedArray<std::byte, 24> garbage{};
        ::fixy::fill(garbage.view(), std::byte{0x5A});
        overwrite_batch_file(cipher, session, latest_hash, garbage.view());
        ::fixy::FixedText<16> latest_digits{};
        ::foundation::core::append_hex<16>(latest_digits, latest_hash.raw()).expect("sixteen digits fit the text");
        ::fixy::FixedText<96> counted_line{};
        append_index_line(cipher, session,
                          ::fixy::format(counted_line, "60,64,3,{}\n", latest_digits.view())
                              .expect("an index line fits 96 characters"));
        const auto damaged = cipher.load_session_events(store_ctx(), ov, g_test.alloc, session);
        assert(damaged.events.size() == 10 && step_at(damaged, 5) == 30);
        assert(damaged.skipped_batches == 5 && "two lines of each damaged batch, and the line of the wrong count");
        const ::fixy::process::ChildReport damaged_report =
            ::fixy::process::wait(::fixy::process::spawn_child(child_ctx,
                                                               [&cipher, &ov, &child_ctx, session] noexcept {
                                                                   static_cast<void>(cipher.load_session_events(
                                                                       child_ctx, ov, g_test.alloc, session));
                                                               })
                                      .expect("the test starts a child"))
                .expect("the test waits for its child");
        assert(damaged_report.is_exit_with(0));
        assert(
            ::fixy::find(damaged_report.output(), crucible::load_error_text(crucible::LoadError::batch_entry_malformed))
                .is_some());
        assert(
            ::fixy::find(damaged_report.output(), crucible::load_error_text(crucible::LoadError::batch_count_mismatch))
                .is_some());

        std::filesystem::remove_all(dir_events);
    }

    // The batch hash names a stored batch file, so its value for given bytes
    // must not change.  Each value is the hash of the release of this
    // format: the bytes 0 to 143, 72 bytes of 0xAB, and no byte.
    {
        ::foundation::core::FixedArray<std::byte, 144> counting{};
        std::size_t position = 0;
        for (std::byte& byte : counting) {
            byte = static_cast<std::byte>(position);
            ++position;
        }
        ::foundation::core::FixedArray<std::byte, 72> repeated{};
        ::fixy::fill(repeated.view(), std::byte{0xAB});
        assert(crucible::Cipher::session_event_batch_hash(counting.view())
               == crucible::ContentHash{0xeb48340c1ea14751ULL});
        assert(crucible::Cipher::session_event_batch_hash(repeated.view())
               == crucible::ContentHash{0x1a51e80700eac78fULL});
        assert(crucible::Cipher::session_event_batch_hash(::fixy::View<std::byte const>{})
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
        assert(crucible::Cipher::session_event_batch_key(counting.view()) == counting_key);
        assert(crucible::Cipher::session_event_batch_key(repeated.view()) == repeated_key);
        assert(crucible::Cipher::session_event_batch_key(::fixy::View<std::byte const>{}) == empty_key);
    }

    // The cache of object bytes holds at most 64 entries, and it removes the
    // entry used least recently.  A load of the first object before the last
    // store makes the first object the most recent entry.  So the 65th store
    // removes the second object, and the first and the last stay.  The files
    // are deleted first, so a load finds a region only in the cache.
    {
        char tmpl_cache[] = "/tmp/crucible_cipher_cache_XXXXXX";
        char* dir_cache = mkdtemp(tmpl_cache);
        assert(dir_cache != nullptr);
        ::fixy::BumpArena cache_dag_arena{1 << 20};
        auto cipher = open_cipher(dir_cache);
        auto ov = cipher.mint_open_view(store_ctx());
        constexpr std::size_t kStoreCount = 65;
        ::fixy::FixedArray<crucible::ContentHash, kStoreCount> hashes{};
        ::fixy::BumpArena read_arena{1 << 16};
        for (std::size_t i = 0; i < kStoreCount; ++i) {
            if (i + 1 == kStoreCount) {
                assert(is_cache_hit(cipher.load_content_addressed(store_ctx(), ov, g_test.alloc,
                                                                  hashes.at(::fixy::index<0>()), read_arena)));
            }
            crucible::ContentHash& stored = crucible::test::element_at(hashes, i);
            stored = cipher.store(store_ctx(), ov,
                                  crucible::Cipher::content_addressed(make_test_region(cache_dag_arena, i + 1)));
            assert(static_cast<bool>(stored));
        }
        for (const crucible::ContentHash hash : hashes) {
            std::filesystem::remove(object_path(dir_cache, hash));
        }
        // The cache removed the second object, and its file is gone.
        assert(reason_of(cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, hashes.at(::fixy::index<1>()),
                                                       read_arena))
               == crucible::LoadError::file_open_failed);
        assert(is_cache_hit(
            cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, hashes.at(::fixy::index<0>()), read_arena)));
        const auto newest = cipher.load_content_addressed(store_ctx(), ov, g_test.alloc,
                                                          hashes.at(::fixy::index<kStoreCount - 1>()), read_arena);
        assert(is_cache_hit(newest) && newest.is_ok());

        std::filesystem::remove_all(dir_cache);
    }

    std::filesystem::remove_all(dir);

    crucible::test::pass("test_cipher: all tests passed\n");
    return 0;
}
