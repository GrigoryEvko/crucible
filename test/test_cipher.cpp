#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>

#include <memory>
#include <fixy/Ctx.h>
#include <fixy/FixedArray.h>
#include <fixy/os/Time.h>
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
    return crucible::Cipher::open(store_ctx(), g_test.alloc,
                                  ::fixy::mint_tagged<::fixy::tags::source::External>(root.view()));
}

// The salt goes into the schema hashes, so two salts give two regions with
// two content hashes.  The ops, their arrays and the region are in
// dag_arena.
static crucible::RegionNode* make_test_region(::fixy::BumpArena& dag_arena, std::uint64_t salt = 0) {
    constexpr uint32_t NUM_OPS = 2;
    const ::fixy::View<crucible::TraceEntry> ops =
        dag_arena.array<crucible::TraceEntry>(g_test.alloc, NUM_OPS, crucible::TraceEntry{});

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        crucible::TraceEntry& op = *ops.get(i).expect("i is below NUM_OPS");
        op.schema_hash = crucible::SchemaHash{0xCAFE0000 + i + (salt << 8)};
        op.set_num_inputs(1);
        op.set_num_outputs(1);

        crucible::TensorMeta meta{};
        meta.ndim = 1;
        meta.sizes[0] = ::crucible::tensor_dim(16);
        meta.strides[0] = ::crucible::tensor_dim(1);
        meta.dtype = crucible::ScalarType::Float;
        op.set_input_metas(crucible::test::run_of(g_test.alloc, dag_arena, meta));
        op.set_output_metas(crucible::test::run_of(g_test.alloc, dag_arena, meta));
        op.set_input_trace_indices(crucible::test::run_of(g_test.alloc, dag_arena, crucible::OpIndex{}));
        op.set_input_slot_ids(crucible::test::run_of(g_test.alloc, dag_arena, crucible::SlotId{}));
        op.set_output_slot_ids(crucible::test::run_of(g_test.alloc, dag_arena, crucible::SlotId{i}));
    }

    auto* region = crucible::make_region(g_test.alloc, dag_arena, ops);
    assert(region != nullptr);
    return region;
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
    assert(committed.is_ok() && "record_event must commit when the clock read succeeds");
}

// A reading of CLOCK_MONOTONIC in nanoseconds, from the reader that the
// test context mints.
static std::uint64_t monotonic_now_ns() {
    const auto clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(store_ctx());
    return clock.read().expect("the monotonic clock gives a reading").peek();
}

// The third field of the last line of the log, the time of the last
// commit.
static std::uint64_t last_log_timestamp(const char* dir) {
    std::ifstream log(std::string(dir) + "/log");
    std::string line;
    std::string last;
    while (std::getline(log, line)) {
        if (!line.empty()) last = line;
    }
    const std::size_t second_comma = last.find(',', last.find(',') + 1);
    assert(second_comma != std::string::npos);
    std::uint64_t timestamp = 0;
    const char* const end = last.data() + last.size();
    const auto [stop, error] = std::from_chars(last.data() + second_comma + 1, end, timestamp);
    assert(error == std::errc{} && stop == end);
    return timestamp;
}

// The events of one session, at steps first, first + 1, ..., first + count - 1.
static std::vector<crucible::Cipher::SessionEvent> session_batch(std::uint64_t first, std::size_t count) {
    std::vector<crucible::Cipher::SessionEvent> events(
        count, crucible::Cipher::SessionEvent::close(::fixy::session::RoleTagId{}, ::fixy::session::RoleTagId{}));
    for (std::size_t i = 0; i < count; ++i) {
        events[i] = crucible::Cipher::SessionEvent::cipher_event(
            ::fixy::session::SessionOp::StoreCommitted, ::fixy::session::StepId{first + i},
            ::fixy::session::StateHash{0x5E550000u + first + i}, first + i);
    }
    return events;
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
static_assert(
    !std::is_constructible_v<crucible::Cipher::LoadedContentAddressedRegionPayload, crucible::RegionNode*, bool>);
static_assert(std::is_constructible_v<crucible::Cipher::LoadedContentAddressedRegionPayload, std::nullptr_t>);
static_assert(!std::is_constructible_v<crucible::Cipher::ContentAddressedRegionPayload, const crucible::RegionNode*>);
static_assert(!std::is_default_constructible_v<crucible::Cipher::ContentAddressedRegionPayload>);

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

    auto* region = make_test_region(dag_arena);
    const crucible::ContentHash expected_hash = region->content_hash;
    assert(static_cast<bool>(expected_hash));

    {
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        const crucible::ContentHash stored_hash =
            cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(region));
        assert(stored_hash == expected_hash);

        const std::string expected_path = object_path(dir, expected_hash);
        assert(std::filesystem::exists(expected_path) && "serialized object file must exist after store()");

        // The content decides the name, so storing the same region
        // again names the same object.
        const crucible::ContentHash second_hash =
            cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(region));
        assert(second_hash == expected_hash);
    }

    {
        // A fresh arena receives the loaded region, so nothing it holds
        // can be a pointer back into the arena that produced it.
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        ::fixy::BumpArena arena2{1 << 16};
        auto loaded_ca = cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, expected_hash, arena2);
        assert(loaded_ca.get() != nullptr && "load() must succeed for a stored hash");
        assert(loaded_ca.get()->content_hash == expected_hash);
        assert(loaded_ca.get()->num_ops() == region->num_ops());
    }

    {
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        (void)cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(region));

        // The log line of the commit holds a reading of CLOCK_MONOTONIC,
        // so it lies between two readings taken around the commit.
        const std::uint64_t before_commit_ns = monotonic_now_ns();
        commit_head(cipher, ov, expected_hash, 10);
        const std::uint64_t after_commit_ns = monotonic_now_ns();
        assert(cipher.head() == expected_hash);
        const std::uint64_t committed_at_ns = last_log_timestamp(dir);
        assert(before_commit_ns <= committed_at_ns && committed_at_ns <= after_commit_ns);

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
        assert(last_log_timestamp(dir) >= committed_at_ns && "the second commit must not read an earlier time");
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

    {
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        ::fixy::BumpArena arena3{1 << 16};
        assert(cipher
                   .load_content_addressed(store_ctx(), ov, g_test.alloc, crucible::ContentHash{0xBADBADBADBADBAD0ULL},
                                           arena3)
                   .get()
               == nullptr);
    }

    {
        char tmpl_ca[] = "/tmp/crucible_cipher_ca_XXXXXX";
        char* dir_ca = mkdtemp(tmpl_ca);
        assert(dir_ca != nullptr);

        ::fixy::BumpArena ca_dag_arena{1 << 16};
        auto* ca_region = make_test_region(ca_dag_arena);
        const auto ca_payload = crucible::Cipher::content_addressed(ca_region);

        auto cipher = open_cipher(dir_ca);
        auto ov = cipher.mint_open_view(store_ctx());
        const crucible::ContentHash hash = cipher.store(store_ctx(), ov, ca_payload);
        assert(hash == ca_region->content_hash);

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
        assert(loaded.cache_hit() && "resident ContentAddressed bytes must avoid disk fetch");
        assert(loaded.get() != nullptr);
        assert(loaded.get()->content_hash == hash);

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
        auto* ca_region = make_test_region(ca_dag_arena);
        const auto ca_payload = crucible::Cipher::content_addressed(ca_region);

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
        assert(loaded.cache_hit());
        assert(loaded.get() != nullptr);
        assert(loaded.get()->content_hash == sender_hash);

        std::filesystem::remove_all(sender_dir);
        std::filesystem::remove_all(receiver_dir);
    }

    {
        crucible::Cipher closed;
        assert(!closed.is_open() && "default Cipher must be Closed");
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
            cipher.persist_session_events(store_ctx(), ov, g_test.alloc, first_batch);
        assert(static_cast<bool>(first_hash));
        assert(static_cast<bool>(cipher.persist_session_events(store_ctx(), ov, g_test.alloc, second_batch)));
        // A batch whose file the store holds gives its hash again, after a
        // compare of the stored bytes with the bytes of the batch.
        assert(cipher.persist_session_events(store_ctx(), ov, g_test.alloc, first_batch) == first_hash);

        const ::fixy::session::SessionTagId session = first_batch.front().session();
        const auto from_middle =
            cipher.load_session_events(store_ctx(), ov, g_test.alloc, session, ::fixy::session::StepId{15});
        assert(from_middle.size() == 10);
        for (std::size_t i = 0; i < from_middle.size(); ++i) {
            assert(from_middle[i].step_id().value == 15 + i);
        }
        const auto every_event = cipher.load_session_events(store_ctx(), ov, g_test.alloc, session);
        assert(every_event.size() == 15);
        assert(every_event.front().step_id().value == 10 && every_event.back().step_id().value == 24);
        const auto second_only =
            cipher.load_session_events(store_ctx(), ov, g_test.alloc, session, ::fixy::session::StepId{20});
        assert(second_only.size() == 5 && second_only.front().step_id().value == 20);

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
                assert(cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, hashes.front(), read_arena)
                           .cache_hit());
            }
            const auto* distinct_region = make_test_region(cache_dag_arena, i + 1);
            hashes[i] = cipher.store(store_ctx(), ov, crucible::Cipher::content_addressed(distinct_region));
            assert(static_cast<bool>(hashes[i]));
        }
        for (const crucible::ContentHash hash : hashes) {
            std::filesystem::remove(object_path(dir_cache, hash));
        }
        assert(cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, hashes[1], read_arena).get() == nullptr);
        assert(cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, hashes.front(), read_arena).cache_hit());
        const auto newest = cipher.load_content_addressed(store_ctx(), ov, g_test.alloc, hashes.back(), read_arena);
        assert(newest.cache_hit() && newest.get() != nullptr);

        std::filesystem::remove_all(dir_cache);
    }

    std::filesystem::remove_all(dir);

    crucible::test::pass("test_cipher: all tests passed\n");
    return 0;
}
