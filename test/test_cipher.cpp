#include <crucible/Cipher.h>

#include <memory>
#include <crucible/effects/Capabilities.h>
#include "test_assert.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>

static crucible::effects::Test g_test;

// Build a minimal RegionNode suitable for Cipher round-trip tests.
static crucible::RegionNode* make_test_region(crucible::Arena& arena) {
    constexpr uint32_t NUM_OPS = 2;
    auto* ops = arena.alloc_array<crucible::TraceEntry>(g_test.alloc, NUM_OPS);
    std::uninitialized_value_construct_n(ops, NUM_OPS);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        ops[i].schema_hash  = crucible::SchemaHash{0xCAFE0000 + i};
        ops[i].num_inputs   = 1;
        ops[i].num_outputs  = 1;

        ops[i].input_metas  = arena.alloc_array<crucible::TensorMeta>(g_test.alloc, 1);
        ops[i].input_metas[0] = {};
        ops[i].input_metas[0].ndim    = 1;
        ops[i].input_metas[0].sizes[0]   = 16;
        ops[i].input_metas[0].strides[0] = 1;
        ops[i].input_metas[0].dtype = crucible::ScalarType::Float;

        ops[i].output_metas  = arena.alloc_array<crucible::TensorMeta>(g_test.alloc, 1);
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

static std::string object_path(const char* dir, crucible::ContentHash hash) {
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016" PRIx64, hash.raw());
    return std::string(dir) + "/objects/" + std::string(hex, 2) + "/" + (hex + 2);
}

static_assert(crucible::safety::proto::is_content_addressed_v<
    typename crucible::Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(crucible::safety::proto::is_content_addressed_v<
    typename crucible::Cipher::LoadedContentAddressedRegionPayload::payload_type>);
static_assert(crucible::safety::proto::is_subsort_v<
    crucible::RegionNode,
    typename crucible::Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(sizeof(crucible::Cipher::ContentAddressedRegionPayload)
              == sizeof(const crucible::RegionNode*));

int main() {
    // Create a temporary directory for this test.
    char tmpdir[] = "/tmp/crucible_cipher_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != nullptr && "mkdtemp failed");

    crucible::Arena arena(1 << 16);

    // ── open() creates the objects/ subdir ──────────────────────────
    {
        auto cipher = crucible::Cipher::open(dir);
        assert(cipher.empty() && "freshly opened Cipher must be empty");
        assert(cipher.root() == dir);
        assert(std::filesystem::is_directory(std::string(dir) + "/objects"));
    }

    // ── store() + verify file on disk ───────────────────────────────
    auto* region = make_test_region(arena);
    const crucible::ContentHash expected_hash = region->content_hash;
    assert(static_cast<bool>(expected_hash));

    {
        auto cipher = crucible::Cipher::open(dir);
        const crucible::ContentHash stored_hash = cipher.store(region, nullptr);
        assert(stored_hash == expected_hash);

        // Object file must exist at the expected shard path.
        const std::string expected_path = object_path(dir, expected_hash);
        assert(std::filesystem::exists(expected_path)
               && "serialized object file must exist after store()");

        // Idempotent: second store() must be a no-op (same hash).
        const crucible::ContentHash second_hash = cipher.store(region, nullptr);
        assert(second_hash == expected_hash);
    }

    // ── load() round-trip ────────────────────────────────────────────
    {
        auto cipher = crucible::Cipher::open(dir);
        crucible::Arena arena2(1 << 16);
        auto* loaded = cipher.load(g_test.alloc, expected_hash, arena2);
        assert(loaded != nullptr && "load() must succeed for a stored hash");
        assert(loaded->content_hash == expected_hash);
        assert(loaded->num_ops == region->num_ops);
    }

    // ── advance_head() × 2, verify HEAD file ─────────────────────────
    {
        auto cipher = crucible::Cipher::open(dir);
        (void)cipher.store(region, nullptr);  // ensure object exists

        cipher.advance_head(expected_hash, 10);
        assert(cipher.head() == expected_hash);

        // HEAD file must contain the hex string.
        std::ifstream hf(std::string(dir) + "/HEAD");
        std::string head_str;
        std::getline(hf, head_str);
        char hex[17];
        std::snprintf(hex, sizeof(hex), "%016" PRIx64, expected_hash.raw());
        assert(head_str == hex && "HEAD file must contain the hex hash");

        // Advance to a different (fake) hash.
        const crucible::ContentHash hash2{0xDEADBEEF12345678ULL};
        cipher.advance_head(hash2, 50);
        assert(cipher.head() == hash2);
    }

    // ── hash_at_step() binary search ─────────────────────────────────
    {
        // Reopen to load the log from disk.
        auto cipher = crucible::Cipher::open(dir);

        // The log has entries at step 10 (expected_hash) and 50 (hash2).
        const crucible::ContentHash hash2{0xDEADBEEF12345678ULL};

        // Step 0 is before any commit → default (0).
        assert(!cipher.hash_at_step(0)
               && "hash_at_step before first commit must return default");

        // Step 10 → expected_hash.
        assert(cipher.hash_at_step(10) == expected_hash);

        // Step 30 (between 10 and 50) → expected_hash (last at-or-before 30).
        assert(cipher.hash_at_step(30) == expected_hash);

        // Step 50 → hash2.
        assert(cipher.hash_at_step(50) == hash2);

        // Step 999 (beyond last) → hash2.
        assert(cipher.hash_at_step(999) == hash2);
    }

    // ── load() on missing hash returns nullptr ────────────────────────
    {
        auto cipher = crucible::Cipher::open(dir);
        crucible::Arena arena3(1 << 16);
        assert(cipher.load(g_test.alloc, crucible::ContentHash{0xBADBADBADBADBAD0ULL}, arena3) == nullptr);
    }

    // ── ContentAddressed store/load: duplicate write and cache-hit read ─
    {
        char tmpl_ca[] = "/tmp/crucible_cipher_ca_XXXXXX";
        char* dir_ca = mkdtemp(tmpl_ca);
        assert(dir_ca != nullptr);

        crucible::Arena ca_arena(1 << 16);
        auto* ca_region = make_test_region(ca_arena);
        const auto ca_payload = crucible::Cipher::content_addressed(ca_region);

        auto cipher = crucible::Cipher::open(dir_ca);
        const crucible::ContentHash hash = cipher.store(ca_payload, nullptr);
        assert(hash == ca_region->content_hash);

        const std::string path = object_path(dir_ca, hash);
        assert(std::filesystem::exists(path));

        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f << "hash-only";
        }
        const auto corrupted_size = std::filesystem::file_size(path);

        const crucible::ContentHash second = cipher.store(ca_payload, nullptr);
        assert(second == hash);
        assert(std::filesystem::file_size(path) == corrupted_size
               && "duplicate ContentAddressed store must not rewrite bytes");

        std::filesystem::remove(path);
        crucible::Arena read_arena(1 << 16);
        auto loaded = cipher.load_content_addressed(
            g_test.alloc, hash, read_arena);
        assert(loaded.cache_hit()
               && "resident ContentAddressed bytes must avoid disk fetch");
        assert(loaded.get() != nullptr);
        assert(loaded.get()->content_hash == hash);

        std::filesystem::remove_all(dir_ca);
    }

    // ── Two Cipher instances: receiver cache admits hash-only transfer ──
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

        auto sender = crucible::Cipher::open(sender_dir);
        auto receiver = crucible::Cipher::open(receiver_dir);
        const crucible::ContentHash sender_hash = sender.store(ca_payload, nullptr);
        const crucible::ContentHash receiver_hash = receiver.store(ca_payload, nullptr);
        assert(sender_hash == receiver_hash);

        std::filesystem::remove(object_path(receiver_dir, receiver_hash));
        crucible::Arena read_arena(1 << 16);
        auto loaded = receiver.load_content_addressed(
            g_test.alloc, sender_hash, read_arena);
        assert(loaded.cache_hit());
        assert(loaded.get() != nullptr);
        assert(loaded.get()->content_hash == sender_hash);

        std::filesystem::remove_all(sender_dir);
        std::filesystem::remove_all(receiver_dir);
    }

    // ── Closed→Open state machine ────────────────────────────────────
    {
        // Default-constructed Cipher is Closed.
        crucible::Cipher closed;
        assert(!closed.is_open() && "default Cipher must be Closed");
        // head() and empty() are safe in Closed state.
        assert(closed.empty());
        assert(!closed.head());
    }

    // ── open() transitions to Open, mint_open_view succeeds ──────────
    {
        auto cipher = crucible::Cipher::open(dir);
        assert(cipher.is_open());
        auto ov = cipher.mint_open_view();    // no contract violation
        // Typed overloads compile and work via the minted view.
        auto* region2 = make_test_region(arena);
        const auto hash = cipher.store(ov, region2, nullptr);
        assert(static_cast<bool>(hash));
        cipher.advance_head(ov, hash, 100);
        assert(cipher.head() == hash);
        // Typed hash_at_step via the same view.
        assert(cipher.hash_at_step(ov, 100) == hash);
    }

    // ── Moved-from Cipher is Closed (root_ moves out) ────────────────
    {
        auto cipher = crucible::Cipher::open(dir);
        assert(cipher.is_open());
        auto moved = std::move(cipher);
        assert(moved.is_open() && "moved-to must be Open");
        // NB: a moved-from std::string is "valid but unspecified";
        // libstdc++ leaves it empty, which makes the source Closed.
        // Don't assert on the source (implementation-defined) — just
        // verify the invariant on the destination.
    }

    // ── load_log skips malformed lines (no exception, no abort) ─────
    // Pre-COMPOSE-3: std::stoull threw on malformed input, which is UB
    // under -fno-exceptions.  Now from_chars-based; bad lines are
    // skipped and the parse continues at the next newline.
    {
        // Build a corrupt log file in a fresh temp dir.
        char tmpl2[] = "/tmp/crucible_corrupt_XXXXXX";
        char* dir2 = mkdtemp(tmpl2);
        assert(dir2 != nullptr);
        std::filesystem::create_directories(std::string(dir2) + "/objects");

        // Write a log with mixed valid/garbage content.
        {
            std::ofstream lf(std::string(dir2) + "/log");
            lf << "10,deadbeef00000001,1000\n";   // valid
            lf << "garbage,not,numbers\n";        // bad: non-numeric step_id
            lf << "20,GHI,2000\n";                // bad: invalid hex
            lf << "30,deadbeef00000003\n";        // bad: missing field
            lf << "40,deadbeef00000004,4000\n";   // valid
        }

        auto cipher = crucible::Cipher::open(dir2);
        // Two valid entries should have parsed; corrupt lines skipped.
        assert(cipher.hash_at_step(10) == crucible::ContentHash{0xdeadbeef00000001ULL});
        assert(cipher.hash_at_step(40) == crucible::ContentHash{0xdeadbeef00000004ULL});
        // Step 30 is between the two valid entries — should resolve to step 10
        // (last entry with step_id <= 30).
        assert(cipher.hash_at_step(30) == crucible::ContentHash{0xdeadbeef00000001ULL});

        std::filesystem::remove_all(dir2);
    }

    // Cleanup temp dir.
    std::filesystem::remove_all(dir);

    std::printf("test_cipher: all tests passed\n");
    return 0;
}
