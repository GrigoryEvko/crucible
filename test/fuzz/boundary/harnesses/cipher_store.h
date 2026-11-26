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
//
// The open loads HEAD and the head log.  After the loads, the harness commits
// at the last step of the log.  The load takes no step and no order above its
// ceiling, so the commit gives no step error, and the order ends only at a
// line whose order is MAX_COMMIT_ORDER.  A commit that passes is the head of a
// reopen, because the store loads each line that it writes.
//
// The index lines go through the append door of the store itself,
// mint_durable_append_file of fixy/os/Fs.h.

#include "../harness.h"
#include "region.h"

#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <crucible/cipher/FederationProtocol.h>
#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <fixy/session/EventLog.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::fuzz::boundary {

namespace cipher_store_detail {

// A path under the scratch directory, or an absolute path.
using StorePath = ::foundation::os::PathText<::fixy::kPathBytes>;

// The sixteen hexadecimal digits of a hash, as the store names it.
using HexName = ::foundation::os::PathText<16>;

[[nodiscard]] inline HexName hex16(std::uint64_t value) noexcept {
    HexName hex{};
    hex.append_hex<16>(value).expect("sixteen digits fit the name");
    return hex;
}

// The path of the object of a hash under the store: objects/, the first two
// digits, a slash, and the other fourteen digits.
[[nodiscard]] inline StorePath object_path(std::uint64_t hash) noexcept {
    HexName const hex = hex16(hash);
    ::fixy::TextView const digits = hex.c_text().view();
    StorePath path{};
    path.append(::fixy::TextView{"store/objects/"}).expect("the object path fits");
    path.append(digits.window(0, 2).expect("a name holds two digits")).expect("the object path fits");
    path.append('/').expect("the object path fits");
    path.append(digits.window(2, 14).expect("a name holds sixteen digits")).expect("the object path fits");
    return path;
}

// Appends text to the file at path under the scratch directory through the
// append door of the store, which makes the file when it is missing.  A failed
// append leaves the file as the failure left it, as a failed write of an
// attacker does.
template <class Bytes>
inline void append_store_file(::fixy::CText path, Bytes const& bytes) noexcept {
    StorePath absolute{};
    if (absolute.append(scratch_dir().path()).is_err() || absolute.append('/').is_err()
        || absolute.append(path).is_err())
        return;
    ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(absolute.c_text().view()))
        .match(
            [&bytes](::fixy::Path<::fixy::tags::source::Sanitized> sanitized) noexcept {
                ::fixy::fs::mint_durable_append_file(kHarnessCtx, sanitized)
                    .match(
                        [&bytes](auto opened) noexcept {
                            auto const file = std::move(opened).consume();
                            static_cast<void>(::fixy::fs::write_full(kHarnessCtx, file.ref(), bytes));
                        },
                        [](::foundation::os::Errno) noexcept {});
            },
            [](::fixy::PathTraversalError) noexcept {});
}

// Byte offsets of the step and the session in a 72-byte event record.
inline constexpr std::size_t kRecordStepOffset = 0;
inline constexpr std::size_t kRecordSessionOffset = 8;

}  // namespace cipher_store_detail

inline void run_cipher_store(::fixy::View<std::byte const> bytes) noexcept {
    namespace detail = cipher_store_detail;
    using SessionEvent = Cipher::SessionEvent;

    empty_scratch_dir();

    InputCursor cursor{bytes};
    // The next chunk of the input: a count of two bytes, then that many bytes.
    auto const take_chunk = [&cursor]() noexcept {
        std::uint16_t const count = cursor.take<std::uint16_t>();
        return cursor.take_bytes(count);
    };
    static_cast<void>(write_scratch_file("store/HEAD", take_chunk()));
    const ::fixy::View<std::byte const> log_bytes = take_chunk();
    static_cast<void>(write_scratch_file("store/log", log_bytes));
    const auto object_hash = cursor.take<std::uint64_t>();
    detail::StorePath const object = detail::object_path(object_hash);
    static_cast<void>(write_scratch_file(object.c_text(), take_chunk()));

    // Each record is the input's bytes with this batch's session and its
    // own step written in, so the batch is one session whose steps run on
    // by one.
    const auto session_raw = cursor.take<std::uint64_t>();
    const auto first_step = cursor.take<std::uint32_t>();
    const std::size_t event_count = cursor.take<std::uint8_t>() % 9;
    ::fixy::HeapArray<std::byte> records =
        ::fixy::mint_heap_array(test_alloc(), event_count * sizeof(SessionEvent), std::byte{0});
    const ::fixy::View<std::byte const> event_bytes = cursor.take_bytes(event_count * sizeof(SessionEvent));
    records = ::fixy::with_mut(std::move(records), [event_bytes, event_count, first_step,
                                                    session_raw](auto run) noexcept {
        InputCursor events{event_bytes};
        for (std::size_t i = 0; i < event_count; ++i) {
            const ::fixy::View<std::byte const> input = events.take_bytes(sizeof(SessionEvent));
            const std::uint64_t step = std::uint64_t{first_step} + i;
            run = ::fixy::reborrow(std::move(run), [input, step, session_raw, i](auto lent) noexcept {
                auto record = ::fixy::take_window<sizeof(SessionEvent)>(std::move(lent), i * sizeof(SessionEvent))
                                  .expect("the batch holds each record");
                record = ::fixy::reborrow(std::move(record), [input](auto part) noexcept {
                    ::fixy::copy(
                        ::fixy::take_window(std::move(part), 0, input.size()).expect("an input holds at most a record"),
                        input)
                        .expect("the window holds the count of the input");
                });
                record = ::fixy::reborrow(std::move(record), [step](auto part) noexcept {
                    ::fixy::copy(::fixy::take_window<sizeof(step)>(std::move(part), detail::kRecordStepOffset)
                                     .expect("the record holds a step"),
                                 ::fixy::bytes(step));
                });
                ::fixy::copy(::fixy::take_window<sizeof(session_raw)>(std::move(record), detail::kRecordSessionOffset)
                                 .expect("the record holds a session"),
                             ::fixy::bytes(session_raw));
            });
        }
    });
    const ::fixy::View<std::byte const> extra_index_lines = take_chunk();

    // The root of the store is the absolute path of the store directory.
    detail::StorePath root{};
    if (root.append(scratch_dir().path()).is_err() || root.append(::fixy::TextView{"/store"}).is_err()) return;
    auto cipher = Cipher::open(kHarnessCtx, test_alloc(),
                               ::fixy::mint_tagged<::fixy::tags::source::External>(root.c_text().view()))
                      .expect("the harness opens its store");
    const auto view = cipher.mint_open_view(kHarnessCtx);
    // The log is the record of the commits, so a log with a commit gives the
    // head: the hash of its last commit, which is not zero.
    cipher.last_committed_step().match(
        [&cipher, &view](::fixy::session::StepId last) noexcept {
            CRUCIBLE_FUZZ_CLAIM("cipher_store",
                                !cipher.empty() && cipher.head() == cipher.hash_at_step(view, last.raw()));
        },
        [] noexcept {});

    detail::StorePath session_dir{};
    const detail::HexName session_name = detail::hex16(session_raw);
    session_dir.append(::fixy::TextView{"store/session_events/"}).expect("the session path fits");
    session_dir.append(session_name.c_text()).expect("the session path fits");
    make_scratch_directory(session_dir.c_text());
    detail::StorePath index = session_dir;
    index.append(::fixy::TextView{"/index"}).expect("the index path fits");
    if (event_count > 0) {
        // The batch file and its index line, as an attacker writes them.
        const ContentHash batch_hash = Cipher::session_event_batch_hash(records.view());
        ::fixy::HeapArray<std::byte> blank = ::fixy::mint_heap_array(
            test_alloc(), cipher::federation::FEDERATION_HEADER_BYTES + records.size(), std::byte{0});
        auto [written, entry] = ::fixy::with_mut(std::move(blank), [&records](auto run) noexcept {
            return cipher::federation::serialize_federation_entry(
                std::move(run), Cipher::session_event_batch_key(records.view()), records.view());
        });
        if (written.is_ok()) {
            const detail::HexName batch_name = detail::hex16(batch_hash.raw());
            detail::StorePath batch = session_dir;
            batch.append('/').expect("the batch path fits");
            batch.append(batch_name.c_text()).expect("the batch path fits");
            batch.append(::fixy::TextView{".cfed"}).expect("the batch path fits");
            static_cast<void>(write_scratch_file(batch.c_text(), ::fixy::View<std::byte const>{entry.view()}));
            // The line holds no zero byte, so a path text holds it.
            ::foundation::os::PathText<128> line{};
            bool const is_line = line.append_decimal(first_step).is_ok() && line.append(',').is_ok()
                              && line.append_decimal(std::uint64_t{first_step} + event_count - 1).is_ok()
                              && line.append(',').is_ok() && line.append_decimal(event_count).is_ok()
                              && line.append(',').is_ok() && line.append(batch_name.c_text()).is_ok()
                              && line.append('\n').is_ok();
            CRUCIBLE_FATAL_INVARIANT(is_line);
            detail::append_store_file(index.c_text(), line.c_text().view());
        }

        // The store's own writer takes only events, and only the decoder
        // turns bytes into an event.
        auto decoded = ::fixy::session::decode_session_log(test_alloc(), records.view());
        if (decoded.is_ok()) {
            const ::fixy::HeapArray<SessionEvent> events = std::move(decoded).expect("the batch decoded");
            (void)cipher.persist_session_events(kHarnessCtx, view, test_alloc(), events.view());
        }
    }
    // Lines an attacker appends to the index after the writer ran.
    detail::append_store_file(index.c_text(), extra_index_lines);

    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(kRegionArenaBlock);
    if (object_hash != 0) {
        const auto loaded =
            cipher.load_content_addressed(kHarnessCtx, view, test_alloc(), ContentHash{object_hash}, arena);
        loaded.match(
            [object_hash](auto const& payload) {
                const DagRef<RegionNode> region = payload.get();
                CRUCIBLE_FUZZ_CLAIM("cipher_store", region->content_hash == ContentHash{object_hash});
                claim_region_trusted(as_harness("cipher_store"), *region);
            },
            [](LoadError) {});
    }

    const auto loaded_events =
        cipher.load_session_events(kHarnessCtx, view, test_alloc(), ::fixy::session::SessionTagId{session_raw});
    for (const SessionEvent& event : loaded_events.events) {
        CRUCIBLE_FUZZ_CLAIM("cipher_store", names_enumerator(event.op()));
        CRUCIBLE_FUZZ_CLAIM("cipher_store", event.session().value == session_raw);
        auto const record = event.encode();
        CRUCIBLE_FUZZ_CLAIM("cipher_store", ::fixy::session::decode_session_log(test_alloc(), record.view()).is_ok());
    }

    // A commit at the last step of the log, or at step zero when the log
    // holds no commit.
    const std::uint64_t step = cipher.last_committed_step().match(
        [](::fixy::session::StepId last) noexcept { return last.raw(); }, [] noexcept { return std::uint64_t{0}; });
    constexpr ContentHash kCommitted{0x5eed5eed5eed5eedULL};
    const Cipher::CommitResult committed = cipher.record_event(kHarnessCtx, view, kCommitted, step);
    const auto is_error = [&committed](Cipher::CommitError error) noexcept {
        return committed.is_err_and([error](Cipher::CommitError const& given) noexcept { return given == error; });
    };
    CRUCIBLE_FUZZ_CLAIM("cipher_store", !is_error(Cipher::CommitError::step_order));
    CRUCIBLE_FUZZ_CLAIM("cipher_store", !is_error(Cipher::CommitError::step_above_limit));
    // A line whose order is the ceiling holds its decimal digits, so a log
    // with no such digits leaves the commit a place.
    ::fixy::FixedText<20> ceiling_digits{};
    const ::fixy::TextView ceiling = ::fixy::format(ceiling_digits, "{}", Cipher::MAX_COMMIT_ORDER)
                                         .expect("twenty digits hold the ceiling of the order");
    const ::fixy::HeapText log_text = text_of(log_bytes);
    CRUCIBLE_FUZZ_CLAIM("cipher_store", !is_error(Cipher::CommitError::order_exhausted)
                                            || ::fixy::find(log_text.view(), ceiling).is_some());
    if (committed.is_ok()) {
        const Cipher reopened = Cipher::open(kHarnessCtx, test_alloc(),
                                             ::fixy::mint_tagged<::fixy::tags::source::External>(root.c_text().view()))
                                    .expect("the harness opens its store again");
        CRUCIBLE_FUZZ_CLAIM("cipher_store", reopened.head() == kCommitted);
        CRUCIBLE_FUZZ_CLAIM("cipher_store", reopened.last_committed_step().is_some_and(
                                                [step](::fixy::session::StepId last) { return last.raw() == step; }));
    }
}

// The seeds of the store.  Each seed holds a store with one region object, a
// head that names it, a one-line head log, and a batch of two session events.
// A regression seed changes one part of it.
enum class CipherSeed : std::uint8_t {
    // The sound store.
    sound,
    // A stored batch whose record names no operation.
    unnamed_op,
    // A stored batch whose last pad byte is not zero.
    nonzero_pad,
    // A sound region stored under the name of another hash.
    foreign_name,
    // A head log whose steps go back.
    backward_log,
    // A head log with a line whose order is above MAX_COMMIT_ORDER, and a
    // sound line after it.
    order_above_ceiling,
    // A head log with no line, so the open reads the head from HEAD.
    head_only,
    // A head log whose last line has the hash zero.
    zero_hash,
};

// Byte offsets in a 72-byte event record.
inline constexpr std::size_t kRecordOpOffset = 64;
inline constexpr std::size_t kRecordLastPadOffset = 71;

// The seed of one kind.  A patch writes one byte of the first event record, so
// a regression seed can hold a record that no session step wrote.
[[nodiscard]] inline SeedBytes cipher_store_seed(CipherSeed kind) noexcept {
    namespace detail = cipher_store_detail;
    constexpr auto kCloseOp = static_cast<std::uint8_t>(::fixy::session::SessionOp::Close);
    std::size_t patch_offset = kRecordOpOffset;
    std::byte patch_byte{kCloseOp};
    std::uint64_t object_name_xor = 0;
    ::fixy::TextView log_tail{};
    bool has_log_line = true;
    switch (kind) {
        case CipherSeed::sound:
            break;
        case CipherSeed::unnamed_op:
            patch_byte = std::byte{0x7F};
            break;
        case CipherSeed::nonzero_pad:
            patch_offset = kRecordLastPadOffset;
            patch_byte = std::byte{0x01};
            break;
        case CipherSeed::foreign_name:
            object_name_xor = 0x1;
            break;
        case CipherSeed::backward_log:
            log_tail = ::fixy::TextView{"0,1,2\n"};
            break;
        case CipherSeed::order_above_ceiling:
            log_tail = ::fixy::TextView{"2,00000000000000bb,18446744073709551615\n3,00000000000000cc,3\n"};
            break;
        case CipherSeed::head_only:
            has_log_line = false;
            break;
        case CipherSeed::zero_hash:
            log_tail = ::fixy::TextView{"2,0000000000000000,2\n"};
            break;
        default:
            ::fixy::unreachable();
    }

    ::fixy::BumpArena<DagArena> arena = ::fixy::mint_bump_arena<DagArena>(kRegionArenaBlock);
    ::fixy::BumpArena<DagArena> dag_arena = ::fixy::mint_bump_arena<DagArena>(kRegionArenaBlock);
    const DagRef<RegionNode> region = build_seed_region(arena, dag_arena, SeedPlan::two_slots);
    const SeedBytes image = region_image(*region);
    const std::uint64_t hash = region->content_hash.raw();
    const detail::HexName hex = detail::hex16(hash);

    SeedWriter seed{};
    const auto chunk = [&seed](::fixy::View<std::byte const> part) noexcept {
        seed.append_raw(static_cast<std::uint16_t>(part.size()));
        seed.append_bytes(part);
    };
    SeedWriter head{};
    head.append_text(hex.c_text().view());
    head.append_text("\n");
    SeedWriter log{};
    if (has_log_line) {
        log.append_text("1,");
        log.append_text(hex.c_text().view());
        log.append_text(",1\n");
    }
    log.append_text(log_tail);
    const SeedBytes head_bytes = head.finish();
    const SeedBytes log_bytes = log.finish();
    chunk(::fixy::View<std::byte const>{head_bytes.view()});
    chunk(::fixy::View<std::byte const>{log_bytes.view()});
    seed.append_raw(hash ^ object_name_xor);
    chunk(::fixy::View<std::byte const>{image.view()});
    seed.append_raw(std::uint64_t{0x5e55});
    seed.append_raw(std::uint32_t{1});
    seed.append_raw(std::uint8_t{2});
    const std::size_t first_record = seed.size();
    for (std::uint64_t role = 1; role <= 2; ++role) {
        const auto record =
            ::fixy::session::SessionEvent::close(::fixy::session::RoleTagId{role}, ::fixy::session::RoleTagId{role + 1})
                .encode();
        seed.append_bytes(::fixy::bytes(record));
    }
    chunk(::fixy::View<std::byte const>{});
    SeedBytes bytes = seed.finish();
    bytes.with_element(
        first_record + patch_offset, [patch_byte](std::byte& patched) noexcept { patched = patch_byte; },
        [] noexcept { ::fixy::fatal("the patch is inside the first record"); });
    return bytes;
}

[[nodiscard]] inline SeedList seeds_cipher_store() noexcept {
    SeedList seeds = empty_seed_list();
    add_seed(seeds, cipher_store_seed(CipherSeed::sound));
    // Regressions: a stored batch whose record names no operation, and
    // one whose pad byte is not zero.  The loader returned both as
    // events.
    add_seed(seeds, cipher_store_seed(CipherSeed::unnamed_op));
    add_seed(seeds, cipher_store_seed(CipherSeed::nonzero_pad));
    // Regression: a sound region stored under the name of another
    // hash.  The loader returned it for a lookup of that other hash.
    add_seed(seeds, cipher_store_seed(CipherSeed::foreign_name));
    // Regression: a head log whose steps go back.  The log append
    // aborted on its order contract when the store opened.
    add_seed(seeds, cipher_store_seed(CipherSeed::backward_log));
    // Regression: a head log line whose order is 2^64 - 1.  The load took
    // it, skipped each later line, and each later commit found no place.
    add_seed(seeds, cipher_store_seed(CipherSeed::order_above_ceiling));
    // A store whose head log holds no line, so the open parses HEAD.
    add_seed(seeds, cipher_store_seed(CipherSeed::head_only));
    // Regression: a head log whose last line has the hash zero.  The load
    // took it as a commit with no head, and the open took the head from
    // HEAD.
    add_seed(seeds, cipher_store_seed(CipherSeed::zero_hash));
    return seeds;
}

}  // namespace crucible::fuzz::boundary
