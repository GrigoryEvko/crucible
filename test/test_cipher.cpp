#include <crucible/Arena.h>
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
[[nodiscard]] static crucible::Cipher open_cipher(const std::string& dir) {
    return crucible::Cipher::open(store_ctx(),
                                  ::fixy::mint_tagged<::fixy::tags::source::External>(std::filesystem::path{dir}));
}

// The salt goes into the schema hashes, so two salts give two regions with
// two content hashes.
static crucible::RegionNode* make_test_region(crucible::Arena& arena, std::uint64_t salt = 0) {
    constexpr uint32_t NUM_OPS = 2;
    auto* ops = arena.alloc_array<crucible::TraceEntry>(g_test.alloc, NUM_OPS);
    std::uninitialized_value_construct_n(ops, NUM_OPS);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        ops[i].schema_hash = crucible::SchemaHash{0xCAFE0000 + i + (salt << 8)};
        ops[i].num_inputs = 1;
        ops[i].num_outputs = 1;

        ops[i].input_metas = arena.alloc_array<crucible::TensorMeta>(g_test.alloc, 1);
        ops[i].input_metas[0] = {};
        ops[i].input_metas[0].ndim = 1;
        ops[i].input_metas[0].sizes[0] = ::crucible::tensor_dim(16);
        ops[i].input_metas[0].strides[0] = ::crucible::tensor_dim(1);
        ops[i].input_metas[0].dtype = crucible::ScalarType::Float;

        ops[i].output_metas = arena.alloc_array<crucible::TensorMeta>(g_test.alloc, 1);
        ops[i].output_metas[0] = ops[i].input_metas[0];

        ops[i].input_trace_indices = arena.alloc_array<crucible::OpIndex>(g_test.alloc, 1);
        ops[i].input_trace_indices[0] = crucible::OpIndex{};

        ops[i].input_slot_ids = arena.alloc_array<crucible::SlotId>(g_test.alloc, 1);
        ops[i].input_slot_ids[0] = crucible::SlotId{};

        ops[i].output_slot_ids = arena.alloc_array<crucible::SlotId>(g_test.alloc, 1);
        ops[i].output_slot_ids[0] = crucible::SlotId{i};
    }

    auto* region = crucible::make_region(g_test.alloc, arena, ops, NUM_OPS);
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
    assert(committed.has_value() && "record_event must commit when the clock read succeeds");
}

// A reading of CLOCK_MONOTONIC in nanoseconds, from the reader that the
// test context mints.
static std::uint64_t monotonic_now_ns() {
    const auto clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(store_ctx());
    const auto reading = clock.read();
    assert(reading.has_value());
    return reading->peek();
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

    crucible::Arena arena(1 << 16);

    {
        auto cipher = open_cipher(dir);
        assert(cipher.empty() && "freshly opened Cipher must be empty");
        assert(cipher.root() == dir);
        assert(std::filesystem::is_directory(std::string(dir) + "/objects"));
    }

    auto* region = make_test_region(arena);
    const crucible::ContentHash expected_hash = region->content_hash;
    assert(static_cast<bool>(expected_hash));

    {
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        const crucible::ContentHash stored_hash = cipher.store(ov, crucible::Cipher::content_addressed(region));
        assert(stored_hash == expected_hash);

        const std::string expected_path = object_path(dir, expected_hash);
        assert(std::filesystem::exists(expected_path) && "serialized object file must exist after store()");

        // The content decides the name, so storing the same region
        // again names the same object.
        const crucible::ContentHash second_hash = cipher.store(ov, crucible::Cipher::content_addressed(region));
        assert(second_hash == expected_hash);
    }

    {
        // A fresh arena receives the loaded region, so nothing it holds
        // can be a pointer back into the arena that produced it.
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        crucible::Arena arena2(1 << 16);
        auto loaded_ca = cipher.load_content_addressed(ov, g_test.alloc, expected_hash, arena2);
        auto* loaded = loaded_ca.get();
        assert(loaded != nullptr && "load() must succeed for a stored hash");
        assert(loaded->content_hash == expected_hash);
        assert(loaded->num_ops == region->num_ops);
    }

    {
        auto cipher = open_cipher(dir);
        auto ov = cipher.mint_open_view(store_ctx());
        (void)cipher.store(ov, crucible::Cipher::content_addressed(region));

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
        crucible::Arena arena3(1 << 16);
        assert(
            cipher.load_content_addressed(ov, g_test.alloc, crucible::ContentHash{0xBADBADBADBADBAD0ULL}, arena3).get()
            == nullptr);
    }

    {
        char tmpl_ca[] = "/tmp/crucible_cipher_ca_XXXXXX";
        char* dir_ca = mkdtemp(tmpl_ca);
        assert(dir_ca != nullptr);

        crucible::Arena ca_arena(1 << 16);
        auto* ca_region = make_test_region(ca_arena);
        const auto ca_payload = crucible::Cipher::content_addressed(ca_region);

        auto cipher = open_cipher(dir_ca);
        auto ov = cipher.mint_open_view(store_ctx());
        const crucible::ContentHash hash = cipher.store(ov, ca_payload);
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

        const crucible::ContentHash second = cipher.store(ov, ca_payload);
        assert(second == hash);
        assert(std::filesystem::file_size(path) == corrupted_size
               && "duplicate ContentAddressed store must not rewrite bytes");

        // Deleting the object leaves the load no disk to fall back on,
        // so a successful read can only have come from memory.
        std::filesystem::remove(path);
        crucible::Arena read_arena(1 << 16);
        auto loaded = cipher.load_content_addressed(ov, g_test.alloc, hash, read_arena);
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

        crucible::Arena ca_arena(1 << 16);
        auto* ca_region = make_test_region(ca_arena);
        const auto ca_payload = crucible::Cipher::content_addressed(ca_region);

        auto sender = open_cipher(sender_dir);
        auto receiver = open_cipher(receiver_dir);
        auto sender_ov = sender.mint_open_view(store_ctx());
        auto receiver_ov = receiver.mint_open_view(store_ctx());
        const crucible::ContentHash sender_hash = sender.store(sender_ov, ca_payload);
        const crucible::ContentHash receiver_hash = receiver.store(receiver_ov, ca_payload);
        assert(sender_hash == receiver_hash);

        std::filesystem::remove(object_path(receiver_dir, receiver_hash));
        crucible::Arena read_arena(1 << 16);
        auto loaded = receiver.load_content_addressed(receiver_ov, g_test.alloc, sender_hash, read_arena);
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
        auto* region2 = make_test_region(arena);
        const auto hash = cipher.store(ov, crucible::Cipher::content_addressed(region2));
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
            lf << "garbage,not,numbers\n";  // bad: non-numeric step_id
            lf << "20,GHI,2000\n";  // bad: invalid hex
            lf << "30,deadbeef00000003\n";  // bad: missing field
            lf << "40,deadbeef00000004,4000\n";  // valid
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
        assert(static_cast<bool>(cipher.persist_session_events(store_ctx(), ov, first_batch)));
        assert(static_cast<bool>(cipher.persist_session_events(store_ctx(), ov, second_batch)));

        const ::fixy::session::SessionTagId session = first_batch.front().session();
        const auto from_middle = cipher.load_session_events(ov, session, ::fixy::session::StepId{15});
        assert(from_middle.size() == 10);
        for (std::size_t i = 0; i < from_middle.size(); ++i) {
            assert(from_middle[i].step_id().value == 15 + i);
        }
        const auto every_event = cipher.load_session_events(ov, session);
        assert(every_event.size() == 15);
        assert(every_event.front().step_id().value == 10 && every_event.back().step_id().value == 24);
        const auto second_only = cipher.load_session_events(ov, session, ::fixy::session::StepId{20});
        assert(second_only.size() == 5 && second_only.front().step_id().value == 20);

        std::filesystem::remove_all(dir_events);
    }

    // The cache of object bytes holds at most 64 entries, and it removes the
    // entry used least recently.  After 65 distinct stores the first object
    // has left the cache, and the last one is still in it.  The files are
    // deleted first, so a load finds a region only in the cache.
    {
        char tmpl_cache[] = "/tmp/crucible_cipher_cache_XXXXXX";
        char* dir_cache = mkdtemp(tmpl_cache);
        assert(dir_cache != nullptr);
        crucible::Arena cache_arena(1 << 20);
        auto cipher = open_cipher(dir_cache);
        auto ov = cipher.mint_open_view(store_ctx());
        constexpr std::size_t kStoreCount = 65;
        ::fixy::FixedArray<crucible::ContentHash, kStoreCount> hashes{};
        for (std::size_t i = 0; i < kStoreCount; ++i) {
            const auto* distinct_region = make_test_region(cache_arena, i + 1);
            hashes[i] = cipher.store(ov, crucible::Cipher::content_addressed(distinct_region));
            assert(static_cast<bool>(hashes[i]));
        }
        for (const crucible::ContentHash hash : hashes) {
            std::filesystem::remove(object_path(dir_cache, hash));
        }
        crucible::Arena read_arena(1 << 16);
        assert(cipher.load_content_addressed(ov, g_test.alloc, hashes.front(), read_arena).get() == nullptr);
        const auto newest = cipher.load_content_addressed(ov, g_test.alloc, hashes.back(), read_arena);
        assert(newest.cache_hit() && newest.get() != nullptr);

        std::filesystem::remove_all(dir_cache);
    }

    std::filesystem::remove_all(dir);

    crucible::test::pass("test_cipher: all tests passed\n");
    return 0;
}
