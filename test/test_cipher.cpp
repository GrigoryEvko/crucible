#include <crucible/Cipher.h>
#include <crucible/Effects.h>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>

static crucible::fx::Test g_test;

// Build a minimal RegionNode suitable for Cipher round-trip tests.
static crucible::RegionNode* make_test_region(crucible::Arena& arena) {
    constexpr uint32_t NUM_OPS = 2;
    auto* ops = arena.alloc_array<crucible::TraceEntry>(g_test.alloc, NUM_OPS);
    std::memset(ops, 0, NUM_OPS * sizeof(crucible::TraceEntry));

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
        char hex[17];
        std::snprintf(hex, sizeof(hex), "%016" PRIx64, expected_hash.raw());
        const std::string expected_path =
            std::string(dir) + "/objects/" +
            std::string(hex, 2) + "/" + (hex + 2);
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

    // Cleanup temp dir.
    std::filesystem::remove_all(dir);

    std::printf("test_cipher: all tests passed\n");
    return 0;
}
