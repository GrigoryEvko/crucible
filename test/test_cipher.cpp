#include <crucible/Cipher.h>

#include <memory>
#include <crucible/effects/_Capabilities.h>
#include "test_assert.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>

static auto g_test = crucible::effects::testing::test();

// The root path crosses a trust boundary, and every call site here has
// to say so.  The alias keeps that declaration from swamping the calls.
using CipherRoot = crucible::fixy::wrap::Path<crucible::fixy::tags::source::External>;

static crucible::RegionNode* make_test_region(crucible::Arena& arena) {
    constexpr uint32_t NUM_OPS = 2;
    auto* ops = arena.alloc_array<crucible::TraceEntry>(g_test.alloc, NUM_OPS);
    std::uninitialized_value_construct_n(ops, NUM_OPS);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        ops[i].schema_hash = crucible::SchemaHash{0xCAFE0000 + i};
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

static_assert(crucible::safety::proto::is_content_addressed_v<
              typename crucible::Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(crucible::safety::proto::is_content_addressed_v<
              typename crucible::Cipher::LoadedContentAddressedRegionPayload::payload_type>);
static_assert(crucible::safety::proto::is_subsort_v<
              crucible::RegionNode, typename crucible::Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(sizeof(crucible::Cipher::ContentAddressedRegionPayload) == sizeof(const crucible::RegionNode*));

int main() {
    char tmpdir[] = "/tmp/crucible_cipher_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != nullptr && "mkdtemp failed");

    crucible::Arena arena(1 << 16);

    {
        auto cipher = crucible::Cipher::open(CipherRoot{dir});
        assert(cipher.empty() && "freshly opened Cipher must be empty");
        assert(cipher.root() == dir);
        assert(std::filesystem::is_directory(std::string(dir) + "/objects"));
    }

    auto* region = make_test_region(arena);
    const crucible::ContentHash expected_hash = region->content_hash;
    assert(static_cast<bool>(expected_hash));

    {
        auto cipher = crucible::Cipher::open(CipherRoot{dir});
        auto ov = cipher.mint_open_view();
        const crucible::ContentHash stored_hash =
            cipher.store(ov, crucible::Cipher::content_addressed(region), nullptr);
        assert(stored_hash == expected_hash);

        const std::string expected_path = object_path(dir, expected_hash);
        assert(std::filesystem::exists(expected_path) && "serialized object file must exist after store()");

        // The content decides the name, so storing the same region
        // again names the same object.
        const crucible::ContentHash second_hash =
            cipher.store(ov, crucible::Cipher::content_addressed(region), nullptr);
        assert(second_hash == expected_hash);
    }

    {
        // A fresh arena receives the loaded region, so nothing it holds
        // can be a pointer back into the arena that produced it.
        auto cipher = crucible::Cipher::open(CipherRoot{dir});
        auto ov = cipher.mint_open_view();
        crucible::Arena arena2(1 << 16);
        auto loaded_ca = cipher.load_content_addressed(ov, g_test.alloc, expected_hash, arena2);
        auto* loaded = loaded_ca.get();
        assert(loaded != nullptr && "load() must succeed for a stored hash");
        assert(loaded->content_hash == expected_hash);
        assert(loaded->num_ops == region->num_ops);
    }

    {
        auto cipher = crucible::Cipher::open(CipherRoot{dir});
        auto ov = cipher.mint_open_view();
        (void)cipher.store(ov, crucible::Cipher::content_addressed(region), nullptr);

        cipher.advance_head(ov, expected_hash, 10);
        assert(cipher.head() == expected_hash);

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
        cipher.advance_head(ov, hash2, 50);
        assert(cipher.head() == hash2);
    }

    {
        // Opening again reads the log back from disk, so the queries
        // below run against the parsed file and not against state left
        // in memory by the block above.
        auto cipher = crucible::Cipher::open(CipherRoot{dir});
        auto ov = cipher.mint_open_view();

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
        auto cipher = crucible::Cipher::open(CipherRoot{dir});
        auto ov = cipher.mint_open_view();
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

        auto cipher = crucible::Cipher::open(CipherRoot{dir_ca});
        auto ov = cipher.mint_open_view();
        const crucible::ContentHash hash = cipher.store(ov, ca_payload, nullptr);
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

        const crucible::ContentHash second = cipher.store(ov, ca_payload, nullptr);
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

        auto sender = crucible::Cipher::open(CipherRoot{sender_dir});
        auto receiver = crucible::Cipher::open(CipherRoot{receiver_dir});
        auto sender_ov = sender.mint_open_view();
        auto receiver_ov = receiver.mint_open_view();
        const crucible::ContentHash sender_hash = sender.store(sender_ov, ca_payload, nullptr);
        const crucible::ContentHash receiver_hash = receiver.store(receiver_ov, ca_payload, nullptr);
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
        auto cipher = crucible::Cipher::open(CipherRoot{dir});
        assert(cipher.is_open());
        // Minting the view is itself the check.  Every call below takes
        // it as proof and repeats no check of its own.
        auto ov = cipher.mint_open_view();
        auto* region2 = make_test_region(arena);
        const auto hash = cipher.store(ov, crucible::Cipher::content_addressed(region2), nullptr);
        assert(static_cast<bool>(hash));
        cipher.advance_head(ov, hash, 100);
        assert(cipher.head() == hash);
        assert(cipher.hash_at_step(ov, 100) == hash);
    }

    {
        auto cipher = crucible::Cipher::open(CipherRoot{dir});
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

        auto cipher = crucible::Cipher::open(CipherRoot{dir2});
        auto ov = cipher.mint_open_view();
        assert(cipher.hash_at_step(ov, 10) == crucible::ContentHash{0xdeadbeef00000001ULL});
        assert(cipher.hash_at_step(ov, 40) == crucible::ContentHash{0xdeadbeef00000004ULL});
        // Step 30 falls where a skipped line claimed a commit.  It must
        // resolve back to step 10, which is what shows the bad line was
        // dropped rather than half-parsed.
        assert(cipher.hash_at_step(ov, 30) == crucible::ContentHash{0xdeadbeef00000001ULL});

        std::filesystem::remove_all(dir2);
    }

    std::filesystem::remove_all(dir);

    std::printf("test_cipher: all tests passed\n");
    return 0;
}
