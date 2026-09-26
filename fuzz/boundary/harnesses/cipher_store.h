#pragma once

// A Cipher store on disk, written by someone other than this process.  The
// input fills the files a store holds: HEAD, the head log, one object, a
// session-event index, and one session-event batch.  The batch records come
// from the input.  The harness writes the batch the way an attacker does,
// with the store's own batch hash, so the hashes are right and the loader
// reaches the record bytes.  A content hash is not a signature: anyone who
// writes the store can compute it.  When every record decodes, the same
// events also go through the store's own writer.
//
// What the loader returns must be what the store's types claim.  A region
// loaded by its hash has that hash and passes every region claim, and each
// loaded session event is a record that the event decoder accepts.

#include "../harness.h"
#include "region.h"

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>
#include <fixy/session/EventLog.h>

#include <array>
#include <cinttypes>
#include <string>
#include <vector>

namespace crucible::fuzz::boundary {

namespace cipher_store_detail {

[[nodiscard]] inline std::span<const std::uint8_t> take_chunk(ByteCursor& cursor) noexcept {
    return cursor.take_bytes(cursor.take<std::uint16_t>());
}

[[nodiscard]] inline std::string object_path(const std::filesystem::path& root, std::uint64_t hash) {
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016" PRIx64, hash);
    return (root / "objects" / std::string(hex, 2) / (hex + 2)).string();
}

[[nodiscard]] inline std::string hex16(std::uint64_t value) {
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016" PRIx64, value);
    return std::string(hex, 16);
}

// Byte offsets of the step and the session in a 72-byte event record.
inline constexpr std::size_t kRecordStepOffset = 0;
inline constexpr std::size_t kRecordSessionOffset = 8;

}  // namespace cipher_store_detail

inline void run_cipher_store(std::span<const std::uint8_t> bytes) {
    using cipher_store_detail::hex16;
    using cipher_store_detail::take_chunk;
    using SessionEvent = Cipher::SessionEvent;

    empty_scratch_dir();
    const std::filesystem::path root = scratch_dir() / "store";
    std::filesystem::create_directories(root / "objects");

    ByteCursor cursor{bytes};
    write_file(root / "HEAD", take_chunk(cursor));
    write_file(root / "log", take_chunk(cursor));
    const auto object_hash = cursor.take<std::uint64_t>();
    write_file(cipher_store_detail::object_path(root, object_hash), take_chunk(cursor));

    // Each record is the input's bytes with this batch's session and its
    // own step written in, so the batch is one session whose steps run on
    // by one.
    const auto session_raw = cursor.take<std::uint64_t>();
    const auto first_step = cursor.take<std::uint32_t>();
    const std::size_t event_count = cursor.take<std::uint8_t>() % 9;
    std::vector<std::uint8_t> records(event_count * sizeof(SessionEvent));
    for (std::size_t i = 0; i < event_count; ++i) {
        std::uint8_t* record = records.data() + i * sizeof(SessionEvent);
        const auto input = cursor.take_bytes(sizeof(SessionEvent));
        if (!input.empty()) std::memcpy(record, input.data(), input.size());
        const std::uint64_t step = std::uint64_t{first_step} + i;
        std::memcpy(record + cipher_store_detail::kRecordStepOffset, &step, sizeof(step));
        std::memcpy(record + cipher_store_detail::kRecordSessionOffset, &session_raw, sizeof(session_raw));
    }
    const auto extra_index_lines = take_chunk(cursor);

    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto cipher = Cipher::open(ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(root));
    const auto view = cipher.mint_open_view(ctx);

    const std::filesystem::path session_dir = root / "session_events" / hex16(session_raw);
    std::filesystem::create_directories(session_dir);
    if (event_count > 0) {
        // The batch file and its index line, as an attacker writes them.
        const ContentHash batch_hash = Cipher::session_event_batch_hash(records);
        std::vector<std::uint8_t> entry(cipher::federation::FEDERATION_HEADER_BYTES + records.size());
        const auto written = cipher::federation::serialize_federation_entry(
            entry, KernelCacheKey{batch_hash, Cipher::SESSION_EVENT_FEDERATION_ROW_HASH}, records);
        if (written) {
            entry.resize(*written);
            write_file(session_dir / (hex16(batch_hash.raw()) + ".cfed"), entry);
            const std::string line = std::to_string(first_step) + "," + std::to_string(std::uint64_t{first_step} + event_count - 1)
                                   + "," + std::to_string(event_count) + "," + hex16(batch_hash.raw()) + "\n";
            std::ofstream index(session_dir / "index", std::ios::binary | std::ios::app);
            index.write(line.data(), static_cast<std::streamsize>(line.size()));
        }

        // The store's own writer takes only events, and only the decoder
        // turns bytes into an event.
        if (auto events = ::fixy::session::decode_session_log(as_bytes_view(records))) {
            (void)cipher.persist_session_events(ctx, view, std::span<const SessionEvent>{*events});
        }
    }
    // Lines an attacker appends to the index after the writer ran.
    {
        std::ofstream index(session_dir / "index", std::ios::binary | std::ios::app);
        index.write(as_text(extra_index_lines).data(), static_cast<std::streamsize>(extra_index_lines.size()));
    }

    Arena arena;
    if (object_hash != 0) {
        auto loaded = cipher.load_content_addressed(view, test_alloc(),ContentHash{object_hash}, arena);
        if (const RegionNode* region = loaded.get(); region != nullptr) {
            CRUCIBLE_FUZZ_CLAIM("cipher_store", region->content_hash == ContentHash{object_hash});
            claim_region_trusted("cipher_store", *region);
        }
    }

    const auto loaded_events = cipher.load_session_events(view, ::fixy::session::SessionTagId{session_raw});
    for (const SessionEvent& event : loaded_events) {
        CRUCIBLE_FUZZ_CLAIM("cipher_store", names_enumerator(event.op()));
        CRUCIBLE_FUZZ_CLAIM("cipher_store", event.session().value == session_raw);
        CRUCIBLE_FUZZ_CLAIM("cipher_store", ::fixy::session::decode_session_event(event.encode()).has_value());
    }
}

// A store with one region object, a head that names it, a one-line head
// log, and a batch of two session events.  The patch writes one byte of
// the first event record, so a regression seed can hold a record that no
// session step wrote.
[[nodiscard]] inline std::vector<std::uint8_t> cipher_store_seed(std::size_t patch_offset, std::uint8_t patch_byte,
                                                                std::uint64_t object_name_xor = 0,
                                                                std::string_view log_tail = {}) {
    Arena arena;
    const RegionNode* region = build_seed_region(arena, true);
    const auto image = region_image(region);
    const std::uint64_t hash = region->content_hash.raw();

    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016" PRIx64, hash);
    const std::string head = std::string(hex, 16) + "\n";
    const std::string log = "1," + std::string(hex, 16) + ",0\n" + std::string(log_tail);

    std::vector<std::uint8_t> seed;
    const auto chunk = [&seed](std::span<const std::uint8_t> bytes) {
        append_raw(seed, static_cast<std::uint16_t>(bytes.size()));
        append_bytes(seed, bytes);
    };
    chunk(text_bytes(head));
    chunk(text_bytes(log));
    append_raw(seed, hash ^ object_name_xor);
    chunk(image);
    append_raw(seed, std::uint64_t{0x5e55});
    append_raw(seed, std::uint32_t{1});
    append_raw(seed, std::uint8_t{2});
    const std::size_t first_record = seed.size();
    for (std::uint64_t role = 1; role <= 2; ++role) {
        const auto record = ::fixy::session::SessionEvent::close(::fixy::session::RoleTagId{role},
                                                                 ::fixy::session::RoleTagId{role + 1})
                                .encode();
        for (const std::byte b : record) seed.push_back(static_cast<std::uint8_t>(b));
    }
    seed[first_record + patch_offset] = patch_byte;
    chunk({});
    return seed;
}

// Byte offsets in a 72-byte event record.
inline constexpr std::size_t kRecordOpOffset = 64;
inline constexpr std::size_t kRecordLastPadOffset = 71;

[[nodiscard]] inline Seeds seeds_cipher_store() {
    constexpr auto kCloseOp = static_cast<std::uint8_t>(::fixy::session::SessionOp::Close);
    return {
        cipher_store_seed(kRecordOpOffset, kCloseOp),
        // Regressions: a stored batch whose record names no operation, and
        // one whose pad byte is not zero.  The loader returned both as
        // events.
        cipher_store_seed(kRecordOpOffset, 0x7F),
        cipher_store_seed(kRecordLastPadOffset, 0x01),
        // Regression: a sound region stored under the name of another
        // hash.  The loader returned it for a lookup of that other hash.
        cipher_store_seed(kRecordOpOffset, kCloseOp, 0x1),
        // Regression: a head log whose steps go back.  The log append
        // aborted on its order contract when the store opened.
        cipher_store_seed(kRecordOpOffset, kCloseOp, 0, "0,1,0\n"),
    };
}

}  // namespace crucible::fuzz::boundary
