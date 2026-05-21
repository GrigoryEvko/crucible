#pragma once

// Git-like content-addressed object store for Merkle DAG nodes.
//
// Directory layout:
//   $root/objects/<first2hex>/<remaining14hex>  — one file per node
//   $root/HEAD                                  — hex string: current active hash + "\n"
//   $root/log                                   — append-only: "step_id,hash_hex,ts_ns\n"
//
// Key properties:
//   Idempotent writes: store() is a no-op if the file already exists.
//     Same content_hash = same bytes = no disk I/O.
//   No overwrites, no deletes: the log only grows (explicit GC is future work).
//   Time travel: hash_at_step(N) binary-searches the log (steps are monotonic).
//
// The Cipher is not thread-safe. It should be owned and accessed from
// a single thread (typically foreground, during persist() / load() calls).
//
// Lifecycle (compile-time via ScopedView, runtime via contract):
//
//   Closed (default-constructed or moved-from) → Open (returned by open())
//
// Open means `root_` is non-empty and the objects/ directory exists; all
// disk-touching methods (store, load, advance_head, hash_at_step) require
// an OpenView to prove the state.  head() and empty() work in either
// state because they read only in-memory fields that are well-defined
// for a Closed Cipher too (empty root, null head).

#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/Serialize.h>
#include <crucible/cipher/CipherTierPromotion.h>
#include <crucible/cipher/FederationProtocol.h>
#include <crucible/cipher/SessionPersistenceSurface.h>
#include <crucible/effects/EffectRow.h>           // FOUND-I09
// FIXY-U-092: safety wrappers reached through the fixy umbrella headers,
// not direct safety/ includes.  safety/Decide.h + Post.h + Pre.h are
// retained (CRUCIBLE_PRE/POST macros + crucible::decide predicate catalog)
// — fixy/Contract.h re-exports the macros but transitively includes this
// header, so it cannot be pulled in here without a circular include.
#include <crucible/fixy/Diag.h>             // row_hash_contribution_v
#include <crucible/fixy/Handle.h>           // open_read / FileHandle
#include <crucible/fixy/Is.h>               // is_opaque_lifetime_v
#include <crucible/fixy/SessContentAddr.h>  // ContentAddressed / is_content_addressed_v
#include <crucible/fixy/SessEventLog.h>     // SessionEvent / StepId / SessionTagId / KeyFn / Less
#include <crucible/fixy/Source.h>           // tags::source::Durable
#include <crucible/fixy/Wrap.h>             // Tagged / WriteOnce / OrderedAppendOnly / Positive / Refined / Wait / CipherTier(Tag_v) / cipher_tier / Lifetime_v / mint_view / ScopedView / no_scoped_view_field_check
#include <crucible/safety/Decide.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Pre.h>

#include <fcntl.h>     // FIXY-V-026: ::open(O_RDONLY|O_CLOEXEC) for fdatasync barrier
#include <unistd.h>    // FIXY-V-026: ::fdatasync, ::close

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible {

namespace cipher {

template <typename T>
class [[nodiscard]] ContentAddressedPayload {
 public:
    using value_type = T;
    using payload_type = crucible::fixy::sess::contentaddr::ContentAddressed<T>;

    constexpr explicit ContentAddressedPayload(const T* value) noexcept
        : value_(value) {}

    [[nodiscard]] constexpr const T* get() const noexcept { return value_; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return value_ != nullptr;
    }

 private:
    const T* value_ = nullptr;
};

template <typename T>
class [[nodiscard]] LoadedContentAddressedPayload {
 public:
    using value_type = T;
    using payload_type = crucible::fixy::sess::contentaddr::ContentAddressed<T>;

    constexpr LoadedContentAddressedPayload() noexcept = default;
    constexpr LoadedContentAddressedPayload(std::nullptr_t) noexcept {}

    constexpr LoadedContentAddressedPayload(T* value, bool cache_hit) noexcept
        : value_(value), cache_hit_(cache_hit) {}

    [[nodiscard]] constexpr T* get() const noexcept { return value_; }
    [[nodiscard]] constexpr bool cache_hit() const noexcept { return cache_hit_; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return value_ != nullptr;
    }
    [[nodiscard]] constexpr operator T*() const noexcept { return value_; }

 private:
    T* value_ = nullptr;
    bool cache_hit_ = false;
};

template <typename T>
[[nodiscard]] constexpr ContentAddressedPayload<T>
content_addressed_payload(const T* value) noexcept {
    return ContentAddressedPayload<T>{value};
}

}  // namespace cipher

// ── Cipher state tag ────────────────────────────────────────────────
// Open denotes: open() has run, objects/ exists, root_ is non-empty.
// Closed is implicit (no tag) — the negation of is_open().
//
// fixy-A2-014: cipher_state::Open is defined in
// <crucible/cipher/SessionPersistenceSurface.h> (included above) so
// the SessionPersistence bridge can name CipherOpenView without
// pulling the full Cipher.h transitive set.  The forward-friendly
// definition lives there; this comment is the breadcrumb.

class CRUCIBLE_OWNER Cipher {
 public:
    static constexpr std::size_t MAX_ROOT_PATH_BYTES = 4096;
    static constexpr std::size_t OBJECT_PATH_SUFFIX_BYTES =
        sizeof("/objects/") - 1 + 2 + 1 + 14;

    using ContentAddressedRegionPayload =
        cipher::ContentAddressedPayload<RegionNode>;
    using LoadedContentAddressedRegionPayload =
        cipher::LoadedContentAddressedPayload<RegionNode>;
    using SessionEvent = crucible::fixy::sess::eventlog::SessionEvent;

    // fixy-A2-014: alias the namespace-scope row alias from
    // SessionPersistenceSurface.h.  Lifting the typedef out lets
    // SessionPersistence.h reference the row without dragging the
    // full Cipher.h transitive set in.
    using persist_session_events_required_row =
        ::crucible::CipherSessionEventPersistenceRow;

    inline static constexpr RowHash SESSION_EVENT_FEDERATION_ROW_HASH{
        ::crucible::fixy::diag::row_hash_contribution_v<
            persist_session_events_required_row>};

    static_assert(static_cast<bool>(SESSION_EVENT_FEDERATION_ROW_HASH));
    static_assert(sizeof(SessionEvent) == 72,
        "Cipher session-event persistence is pinned to the SessionEvent "
        "cold-tier wire size.  Bumped from 56 to 72 by fixy-A2-005, which "
        "added two dedicated 64-bit epoch/generation threshold lanes to "
        "preserve EpochedDelegate/EpochedAccept reshard-guard NTTPs.");
    static_assert(std::is_trivially_copyable_v<SessionEvent>,
        "Cipher session-event persistence bulk-serializes SessionEvent "
        "bytes and therefore requires a trivially-copyable payload.");

    [[nodiscard]] static constexpr ContentAddressedRegionPayload
    content_addressed(const RegionNode* region) noexcept {
        return ContentAddressedRegionPayload{region};
    }

    // Factory: open (or create) a Cipher rooted at `root`.
    // Creates the objects/ subdirectory if absent, loads HEAD + log from disk.
    // gnu::cold: startup-only path, never on the op-dispatch hot path.
    [[gnu::cold]] static Cipher open(const std::string& root) {
        Cipher c;
        if (root.empty() || root.size() > MAX_ROOT_PATH_BYTES) {
            return c;
        }
        // root_ is WriteOnce<Tagged<std::string, source::Durable>>:
        // set exactly once here (WriteOnce), tagged at the type level as
        // loaded from on-disk state (Tagged).  A second attempt to set
        // (e.g. calling open on a Cipher that already has a root) would
        // contract-fire; reassignment via `=` is a compile error.
        [[maybe_unused]] const bool root_set = c.root_.try_set(
            crucible::fixy::wrap::Tagged<std::string,
                                     crucible::fixy::tags::source::Durable>{root});
        [[assume(root_set)]];
        std::filesystem::create_directories(root + "/objects");

        // Load the log first to populate in-memory state.
        c.load_log();

        // HEAD file overrides the log's last hash (HEAD is the authoritative pointer).
        // FileHandle's RAII closes the fd at scope exit; no iostream needed.
        // std::from_chars is exception-free (unlike std::stoull which throws
        // on malformed input — incompatible with -fno-exceptions), so a
        // corrupt HEAD file cleanly falls through to the log fallback.
        const std::string head_path = root + "/HEAD";
        // fixy-A1-013: open_read returns std::expected<FileHandle,
        // std::error_code>; a missing HEAD file (ENOENT) is the normal
        // boot case and falls through to the log-tail fallback below.
        auto hf_e = crucible::fixy::handle::open_read(head_path.c_str());
        bool head_from_file = false;
        if (hf_e && hf_e->is_open()) {
            auto& hf = *hf_e;
            char buf[32];
            const ssize_t n = ::read(hf.get(), buf, sizeof(buf));
            if (n > 0) {
                uint64_t raw = 0;
                const auto* begin = buf;
                const auto* end   = buf + n;
                auto [p, ec] = std::from_chars(begin, end, raw, /*base=*/16);
                if (ec == std::errc{} && p != begin) {
                    c.head_ = ContentHash{raw};
                    head_from_file = true;
                }
            }
        }
        if (!head_from_file) {
            c.head_ = c.latest_committed_head();
        }

        return c;
    }

    Cipher() = default;
    Cipher(const Cipher&)            = delete("Cipher holds mutable log state; move instead");
    Cipher& operator=(const Cipher&) = delete("Cipher holds mutable log state; move instead");
    Cipher(Cipher&&)                 = default;
    Cipher& operator=(Cipher&&)      = default;

    // ── Open-state query + view minting ─────────────────────────────
    [[nodiscard]] bool is_open() const noexcept { return root_.has_value(); }

    // fixy-A2-014: alias the namespace-scope ScopedView alias from
    // SessionPersistenceSurface.h for backwards-compatible call sites
    // (`auto view = cipher.mint_open_view();` resolves OpenView via
    // class-scope name lookup unchanged).
    using OpenView = ::crucible::CipherOpenView;

    [[nodiscard]] OpenView mint_open_view() const noexcept
        pre (is_open())
    {
        return crucible::fixy::wrap::mint_view<cipher_state::Open>(*this);
    }

    [[nodiscard]] friend constexpr bool view_ok(
        Cipher const& c, std::type_identity<cipher_state::Open>) noexcept {
        return c.is_open();
    }

    // ─── Store ──────────────────────────────────────────────────────

    // Typed: caller has proved Open.  Zero runtime state check.
    [[nodiscard]] ContentHash store(OpenView const&,
                                    ContentAddressedRegionPayload payload,
                                    const MetaLog* meta_log) {
        const RegionNode* region = payload.get();
        if (!region) return ContentHash{};
        const ContentHash hash = region->content_hash;
        if (!hash) return ContentHash{};

        const std::string path = obj_path(hash.raw());

        // Idempotent: if the file already exists, same bytes → skip write.
        if (std::filesystem::exists(path)) return hash;

        // Estimate serialization size conservatively.  #105:
        // estimate_serial_size returns Positive<std::size_t> — the
        // `.value()` unwrap is the explicit, grep-discoverable bridge
        // from the refinement domain back to std::vector's ctor.
        const size_t cap = estimate_serial_size(region).value();
        std::vector<uint8_t> buf(cap);
        const size_t n = serialize_region(region, meta_log, std::span<uint8_t>{buf});
        if (n == 0) return ContentHash{};

        // Ensure shard directory exists.
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path());

        std::ofstream f(path, std::ios::binary);
        if (!f) return ContentHash{};
        // §III-clean cast cascade: uint8_t* → void* → char*.  std::ofstream::
        // write demands char*; the byte buffer is plain trivially-copyable
        // storage so the cascade has no aliasing impact.
        f.write(static_cast<const char*>(static_cast<const void*>(buf.data())),
                static_cast<std::streamsize>(n));
        if (!f) return ContentHash{};

        // ── FIXY-V-026: durable-on-return contract ─────────────────
        //
        // Close the stream BEFORE the fdatasync barrier so the
        // userspace buffer is flushed to the kernel page cache.  A
        // crash between f.write() and the destructor's flush would
        // otherwise lose this object's bytes silently — and Cipher
        // is the crash-recovery foundation (L14): an object that
        // store() returned a hash for MUST be loadable after a
        // process / kernel / power crash.
        //
        // Re-open RDONLY for the fdatasync syscall because libstdc++
        // does not portably expose the std::ofstream's underlying
        // fd.  The double-open costs ~3 µs warm-cache; well under
        // any blocking-I/O budget the Cipher tier already commits
        // to (Wait::Block per store_pinned, ~100 µs floor).
        //
        // fdatasync (not fsync) is sufficient: Cipher objects are
        // immutable content-addressed payloads, so the inode never
        // changes after creation — only the data + the metadata
        // required to locate it (size, block map) need durable
        // ordering, exactly what fdatasync flushes.
        //
        // Failure modes flow into the existing empty-hash return:
        //   - close fails        → returns ContentHash{}, caller
        //                          treats as store failure
        //   - reopen fails       → ditto (race or ENOSPC during
        //                          fsync of an earlier inode-update)
        //   - fdatasync returns -1 (ENOSPC / EIO / EBADF) → ditto.
        // Every error path is durable-correct: either the data is
        // on disk AND the hash is returned, OR an empty hash is
        // returned and the caller treats as failure.  No silent
        // partial-durability state.
        f.close();
        if (!f) return ContentHash{};
        const int fsync_fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fsync_fd < 0) return ContentHash{};
        const int fsync_rc = ::fdatasync(fsync_fd);
        ::close(fsync_fd);
        if (fsync_rc != 0) return ContentHash{};

        remember_cached_bytes(hash, std::span<const uint8_t>{buf.data(), n});
        return hash;
    }

    // ═══════════════════════════════════════════════════════════════
    // FOUND-G27: Wait-pinned production surface
    // ═══════════════════════════════════════════════════════════════
    //
    // Cipher::store performs a blocking `f.write()` to the warm-tier
    // NVMe shard.  The wait strategy is Block — std::ofstream::write
    // may invoke the kernel's writeback path; on a busy system this
    // blocks tens of microseconds to milliseconds (well above any
    // hot-path budget).
    //
    // store_pinned() pins this classification at the type level so
    // that hot-path consumers declaring `requires Wait::satisfies<
    // SpinPause>` REJECT the value at compile time.  The Wait
    // lattice direction (Block(weakest) ⊑ Park ⊑ ... ⊑ SpinPause)
    // means a Block-tier value satisfies only Block-tier-and-weaker
    // (degenerate — Block is the bottom).
    //
    // Why additive: the existing store() callers are bg-thread
    // persistence paths that already accept blocking I/O.  The
    // additive overlay declares Block tier at the type level for
    // any new consumer that wants the rejection to fire when wired
    // into the wrong context.

    // Block-tier-pinned store — for new bg / IO-classified call sites.
    [[nodiscard]] crucible::fixy::wrap::Wait<crucible::fixy::wrap::WaitStrategy_v::Block, ContentHash>
    store_pinned(OpenView const& view,
                 ContentAddressedRegionPayload payload,
                 const MetaLog* meta_log) {
        return crucible::fixy::wrap::Wait<crucible::fixy::wrap::WaitStrategy_v::Block, ContentHash>{
            store(view, payload, meta_log)};
    }

    // ═══════════════════════════════════════════════════════════════
    // FOUND-G47: CipherTier-pinned publication surface
    // ═══════════════════════════════════════════════════════════════
    //
    // The Cipher persistence model defines three storage tiers
    // (CRUCIBLE.md §L14):
    //
    //   Hot  — peer-Relay RAM (RAID-like replication, single-node
    //          failure recovery; zero-cost reshard).
    //   Warm — local NVMe per Relay (1/N FSDP shard, reboot recovery
    //          in seconds).
    //   Cold — durable storage (S3/GCS, total cluster-failure recovery
    //          in minutes).
    //
    // Today only the Warm tier is implemented (NVMe via store());
    // publish_hot and publish_cold are Phase 5 deferred surfaces (per
    // CRUCIBLE.md "Phase 5: Keeper + Canopy + Cipher").  The TYPE-
    // level surfaces ship NOW so production call sites already speak
    // the tier vocabulary; the behavioral implementation lands in
    // Phase 5 without any downstream API churn.
    //
    //   publish_warm  → wraps store() — REAL implementation (NVMe write).
    //                   Returns CipherTier<Warm, ContentHash>.
    //   publish_hot   → Phase 5 stub.  Returns CipherTier<Hot,
    //                   ContentHash>{ContentHash{}} — typed at Hot,
    //                   carries no actual replication today.
    //   publish_cold  → Phase 5 stub.  Returns CipherTier<Cold,
    //                   ContentHash>{ContentHash{}} — typed at Cold,
    //                   no S3/GCS write today.
    //
    // SEMANTIC NOTE on the stubs: returning ContentHash{} (the
    // "none" sentinel — `static_cast<bool>(h) == false`) is a
    // deliberate non-lie.  The wrapper's TYPE claims "this value
    // is at tier T"; the VALUE bytes (ContentHash{}) carry no
    // false positive about durability — a downstream consumer
    // checking `static_cast<bool>(h)` distinguishes "no tier-T
    // store happened" from "tier-T store succeeded".  When Phase 5
    // ships the real Hot/Cold backends, the stubs become real
    // writes WITHOUT changing the type signature — this is
    // exactly the non-churn contract the additive-pinned pattern
    // promises.
    //
    // Why additive: existing store() callers are tier-agnostic bg-
    // thread persistence paths.  The CipherTier-pinned overlay
    // declares storage residency at the type level so:
    //   - A function `requires CipherTier::satisfies<Hot>` REJECTS
    //     values coming from publish_warm / publish_cold at compile
    //     time.  Captures the Keeper hot-tier reincarnation gate
    //     (zero-loss reshard requires RAM-replicated state, NEVER
    //     NVMe/S3).
    //   - runtime drift attribution can read the static `tier` to
    //     distinguish "S3 latency spike" from "Hot-tier issue"
    //     without a runtime tier-tagged enum field.
    //   - replay_engine accepts CipherTier<Cold> historical archives
    //     during recovery; production hot-path code declaring
    //     `requires CipherTier::satisfies<Hot>` rejects them.
    //
    // Lattice direction (CipherTierLattice.h):
    //     Cold(weakest) ⊑ Warm ⊑ Hot(strongest)
    //
    // satisfies<Required> = leq(Required, Self).  Hot satisfies Warm
    // and Cold (stronger subsumes weaker for use); Cold satisfies
    // only Cold (bottom of chain).

    // Warm-tier publish — REAL implementation (forwards to store()).
    [[nodiscard]] crucible::fixy::wrap::cipher_tier::Warm<ContentHash>
    publish_warm(OpenView const& view,
                 ContentAddressedRegionPayload payload,
                 const MetaLog* meta_log) {
        return crucible::fixy::wrap::cipher_tier::Warm<ContentHash>{
            store(view, payload, meta_log)};
    }

    // Hot-tier publish — Phase 5 STUB.
    //
    // Today: returns CipherTier<Hot, ContentHash{}> — the type
    // declares Hot residency, the value-bytes (none-hash) carry no
    // false positive.  When Phase 5 ships peer-RAM RAID replication,
    // the body grows to perform the actual replication; no caller
    // change required because the type signature is stable.
    [[nodiscard]] crucible::fixy::wrap::cipher_tier::Hot<ContentHash>
    publish_hot(OpenView const&,
                ContentAddressedRegionPayload /*payload*/,
                const MetaLog* /*meta_log*/) noexcept {
        // Phase 5: invoke peer-RAM RAID replication here, returning
        // the ContentHash of the replicated shard.  Until then, the
        // stub returns the none-hash — callers must check
        // `static_cast<bool>(h)` to detect "Hot tier not yet wired".
        return cipher::mint_promote<
            crucible::fixy::wrap::CipherTierTag_v::Cold,
            crucible::fixy::wrap::CipherTierTag_v::Hot>(
            crucible::fixy::wrap::cipher_tier::Cold<ContentHash>{ContentHash{}});
    }

    // Cold-tier publish — Phase 5 STUB.
    //
    // Today: returns CipherTier<Cold, ContentHash{}>.  When Phase 5
    // ships the S3/GCS backend, the body grows to PUT the serialized
    // region into durable storage; no caller change required.
    [[nodiscard]] crucible::fixy::wrap::cipher_tier::Cold<ContentHash>
    publish_cold(OpenView const&,
                 ContentAddressedRegionPayload /*payload*/,
                 const MetaLog* /*meta_log*/) noexcept {
        // Phase 5: durable-storage PUT here.
        return cipher::mint_demote<
            crucible::fixy::wrap::CipherTierTag_v::Hot,
            crucible::fixy::wrap::CipherTierTag_v::Cold>(
            crucible::fixy::wrap::cipher_tier::Hot<ContentHash>{ContentHash{}});
    }

    // ═══════════════════════════════════════════════════════════════
    // FOUND-G12: OpaqueLifetime-pinned commit surface
    // ═══════════════════════════════════════════════════════════════
    //
    // Cipher tier promotion has two ORTHOGONAL axes that the user
    // previously had to track manually:
    //
    //   (a) STORAGE RESIDENCY (Hot / Warm / Cold) — covered by
    //       publish_hot / publish_warm / publish_cold (FOUND-G47).
    //   (b) DATA LIFETIME SCOPE (PER_REQUEST / PER_PROGRAM /
    //       PER_FLEET) — the SOURCE-side promise: how long the
    //       caller meant the value to live.
    //
    // The two axes correlate but are not the same.  PER_REQUEST data
    // BELONGS in Hot tier (peer RAM, dies on session close) and is
    // a CROSS-REQUEST-LEAK BUG if it reaches Cold (S3, durable across
    // fleet).  This is precisely the inferlet-pattern bug class
    // documented in OpaqueLifetime.h docblock + 25_04_2026.md §16
    // SessionOpaqueState — a PdaState declared PER_REQUEST silently
    // committed to PER_FLEET cold storage leaks user grammar state
    // across requests.
    //
    // commit_per_{request,program,fleet}() encodes axis (b) at the
    // type level by accepting only OpaqueLifetime<MatchingScope, *>-
    // pinned values, and routes to the correct CipherTier on output:
    //
    //   commit_per_request → publish_hot   (PER_REQUEST → Hot,
    //                                       peer RAM, session-scoped)
    //   commit_per_program → publish_warm  (PER_PROGRAM → Warm,
    //                                       NVMe, program-scoped)
    //   commit_per_fleet   → publish_cold  (PER_FLEET → Cold,
    //                                       S3/GCS, fleet-durable)
    //
    // Lattice direction (LifetimeLattice.h):
    //     PER_REQUEST(narrowest) ⊑ PER_PROGRAM ⊑ PER_FLEET(widest)
    //
    // satisfies<Required> = leq(Required, Self).  Wider scope serves
    // narrower requirement (a fleet-scoped value is trivially safe
    // within a single request).  THE LOAD-BEARING DIRECTION:
    // a PER_REQUEST-pinned value DOES NOT satisfy<PER_FLEET>
    // (leq(PER_FLEET, PER_REQUEST) is false), so commit_per_fleet
    // REJECTS PER_REQUEST values at compile time — fencing the
    // cross-request leak at the persistence boundary.
    //
    // Why additive: existing publish_* callers stay unchanged.  The
    // commit_per_* layer is the lifetime-aware overlay; downstream
    // call sites that have already classified their data's lifetime
    // (inferlet user state, RegionNode owner annotations) graduate
    // from publish_* to commit_per_* and gain the leak fence.

    // PER_REQUEST commit — accepts only PER_REQUEST-pinned input.
    // Routes to publish_hot (peer-RAM RAID, dies on session close).
    // Most permissive — wider lifetimes (PER_PROGRAM, PER_FLEET)
    // also satisfy and are accepted (their availability subsumes
    // the request scope).
    template <typename W>
        requires (crucible::fixy::is::is_opaque_lifetime_v<W>
                  && W::template satisfies<crucible::fixy::wrap::Lifetime_v::PER_REQUEST>)
    [[nodiscard]] crucible::fixy::wrap::cipher_tier::Hot<ContentHash>
    commit_per_request(OpenView const& view,
                       W lifetime_pinned_region,
                       const MetaLog* meta_log) noexcept {
        // Consume the OpaqueLifetime wrapper to recover the bare
        // RegionNode*; route to the existing publish_hot path.
        const RegionNode* region =
            std::move(lifetime_pinned_region).consume();
        return publish_hot(view, content_addressed(region), meta_log);
    }

    // PER_PROGRAM commit — accepts PER_PROGRAM and PER_FLEET (wider).
    // REJECTS PER_REQUEST: a request-scoped value cannot promise
    // program-long persistence.  Routes to publish_warm (NVMe).
    template <typename W>
        requires (crucible::fixy::is::is_opaque_lifetime_v<W>
                  && W::template satisfies<crucible::fixy::wrap::Lifetime_v::PER_PROGRAM>)
    [[nodiscard]] crucible::fixy::wrap::cipher_tier::Warm<ContentHash>
    commit_per_program(OpenView const& view,
                       W lifetime_pinned_region,
                       const MetaLog* meta_log) {
        const RegionNode* region =
            std::move(lifetime_pinned_region).consume();
        return publish_warm(view, content_addressed(region), meta_log);
    }

    // PER_FLEET commit — accepts ONLY PER_FLEET-pinned input.
    // REJECTS PER_REQUEST and PER_PROGRAM: only fleet-scoped data
    // may be Raft-replicated to durable cold storage.  Routes to
    // publish_cold (S3/GCS).  THIS IS THE LOAD-BEARING REJECTION
    // SITE for the inferlet cross-request leak bug class.
    template <typename W>
        requires (crucible::fixy::is::is_opaque_lifetime_v<W>
                  && W::template satisfies<crucible::fixy::wrap::Lifetime_v::PER_FLEET>)
    [[nodiscard]] crucible::fixy::wrap::cipher_tier::Cold<ContentHash>
    commit_per_fleet(OpenView const& view,
                     W lifetime_pinned_region,
                     const MetaLog* meta_log) noexcept {
        const RegionNode* region =
            std::move(lifetime_pinned_region).consume();
        return publish_cold(view, content_addressed(region), meta_log);
    }

    // ─── Load ───────────────────────────────────────────────────────

    // Deserialize a RegionNode from objects/<content_hash>.
    // All structures are arena-allocated. Returns nullptr if not found.
    // Hard cap on a single object-file's on-disk size.  Real regions
    // sit at ≤ 1 MB; the 256 MB ceiling covers MoE and long-horizon
    // traces while rejecting corrupt or adversarial entries that would
    // OOM the loader with a SIZE_MAX allocation.
    static constexpr size_t MAX_OBJECT_BYTES = size_t{256} << 20;

    // Refined alias for byte-size inputs validated against the cap.
    // Internal helper; the `load` boundary constructs this when it
    // has proven the disk file size is within the ceiling, then
    // feeds the Refined value into the allocation path.  Downstream
    // code can treat the size as [[assume]]-bounded.
    using ValidatedObjectSize = crucible::fixy::wrap::Refined<
        crucible::fixy::wrap::bounded_above<MAX_OBJECT_BYTES>, size_t>;

    // Typed: caller has proved Open.  const because deserialize does
    // not mutate the durable Cipher state. The resident byte cache is
    // mutable process-local acceleration for the ContentAddressed
    // quotient: a cache hit materializes from bytes already observed
    // under this hash and performs no filesystem fetch.
    //
    // Source tag boundary: disk bytes are source::External until
    // deserialize_region validates their semantic structure. Cached
    // bytes were previously accepted by the same serializer or
    // deserializer path under the same ContentHash; deserialization
    // remains the Sanitized transition for the returned RegionNode*.
    [[nodiscard]] LoadedContentAddressedRegionPayload
    load_content_addressed(OpenView const&, effects::Alloc a,
                           ContentHash content_hash, Arena& arena) const {
        if (!content_hash) return nullptr;

        const std::span<const uint8_t> cached = cached_bytes(content_hash);
        if (!cached.empty()) {
            const LoadedRegionNode loaded_region =
                deserialize_region(a, cached, arena);
            RegionNode* region = loaded_region.value();
            if (region && region->content_hash == content_hash) {
                return LoadedContentAddressedRegionPayload{region, true};
            }
        }

        const std::string path = obj_path(content_hash.raw());
        if (!std::filesystem::exists(path)) return nullptr;

        std::ifstream f(path, std::ios::binary);
        if (!f) return nullptr;

        f.seekg(0, std::ios::end);
        const auto raw = f.tellg();
        if (raw < 0) return nullptr;  // tellg() failure
        const auto len = static_cast<size_t>(raw);
        // Guard the size against the ceiling AT the disk boundary.
        // Once validated, wrap in ValidatedObjectSize — the Refined
        // ctor's contract fires on len > MAX_OBJECT_BYTES, catching
        // a caller who skipped the if-return above (e.g. a future
        // refactor that splits this function and forgets the check).
        if (len == 0 || len > MAX_OBJECT_BYTES) return nullptr;
        const ValidatedObjectSize validated_len{len};
        f.seekg(0, std::ios::beg);

        // Buffer holds untrusted disk bytes until deserialize_region
        // validates their semantic structure.  The BYTE-COUNT is
        // already validated by construction of validated_len above.
        std::vector<uint8_t> buf(validated_len.value());
        // §III-clean cast cascade: uint8_t* → void* → char* for ifstream::read.
        f.read(static_cast<char*>(static_cast<void*>(buf.data())),
               static_cast<std::streamsize>(validated_len.value()));
        if (!f) return nullptr;

        const LoadedRegionNode loaded_region =
            deserialize_region(a, std::span<const uint8_t>{buf}, arena);
        RegionNode* region = loaded_region.value();
        if (!region) return nullptr;
        remember_cached_bytes(content_hash, std::span<const uint8_t>{buf});
        return LoadedContentAddressedRegionPayload{region, false};
    }

    // ─── HEAD management ────────────────────────────────────────────

    // Typed: caller has proved Open.
    // Contract: step_id must not go backward — steps are documented as
    // monotonic for binary search in hash_at_step() to be correct.
    //
    // Soundness gate (#884 WRAP-Cipher-1): content_hash MUST be non-zero.
    // Zero is the structural sentinel for "no content / empty Cipher" —
    // advancing HEAD to it would corrupt the log invariant (a step with
    // hash=0 is indistinguishable from "before first commit"), break
    // hash_at_step()'s binary search, and lose the federation
    // round-trip key for that step.  Every production caller derives
    // content_hash from a RegionNode (where 0 has already been guarded
    // against in store()'s `if (!hash) return ContentHash{};` early
    // return), so the contract is defense-in-depth: under
    // semantic=enforce a future caller that bypasses the early return
    // aborts here, under semantic=ignore the [[assume]] hint pins the
    // invariant for downstream optimizers (binary search + log append
    // can speculate on hash != 0 without re-checking).  Companion
    // typed witness: ValidCipherHead below.
    void advance_head(OpenView const&, ContentHash content_hash, uint64_t step_id)
        // CONTRACT-106: non-zero hash sentinel discharges through
        // decide::is_non_zero (CONTRACT-072 / -080 catalog).  ContentHash
        // is a strong hash type whose default-constructed value is 0
        // (CRUCIBLE_STRONG_HASH NSDMI), so is_non_zero(h) ⟺ h.raw() != 0.
        // The cite is grep-discoverable: future hardening to non-zero
        // sentinel handling propagates to every advance_head / publish /
        // lookup site through one predicate.
        pre (::crucible::decide::is_non_zero(content_hash))
        // CONTRACT-107 (step_id-vs-log invariant): the step_id-ordering
        // pre below moves from P2900 `pre()` to in-body CRUCIBLE_PRE
        // because P2900 `pre()` referencing a class member through
        // `this->` (`log_`) is silently bypassed at consteval in GCC
        // 16.1.1 (the same gotcha that forced CONTRACT-100 / -101 / -102
        // / -103 / -106-Cipher to migrate).  CRUCIBLE_PRE fires
        // symmetrically at consteval, runtime, and as `[[assume]]` for
        // the optimizer.
    {
        // CONTRACT-107: step_id non-regression discharges through
        // `decide::weakly_increasing` (CONTRACT-042 catalog) over a
        // two-element span [back.step_id, candidate].  The `<=` shape is
        // intentional — Cipher accepts duplicate step_ids at this gate
        // (the strict-consecutive-by-1 check happens deeper, at the log
        // append-only event-vector invariant near line 787) — so the
        // weakly-increasing cite matches the predicate semantic exactly.
        // Future hardening (PROD-WRAP-* lifting `step_id` to
        // Monotonic<uint64_t>) propagates uniformly through this cite.
        if (!log_.empty()) {
            uint64_t const ordering[2] = {
                log_.back().step_id.value, step_id};
            CRUCIBLE_PRE(::crucible::decide::weakly_increasing<std::uint64_t>(
                std::span<const std::uint64_t>{ordering, 2}));
        }
        head_ = content_hash;

        // Overwrite HEAD file (truncate to hex + newline).
        {
            std::ofstream hf(root_str() + "/HEAD");
            char hex[16];
            hex16_(content_hash.raw(), hex);
            hf.write(hex, sizeof(hex));
            hf.put('\n');
        }

        // Append log entry.
        const uint64_t ts = now_ns();
        log_.emplace(LogEntry::cipher_store_committed(
            crucible::fixy::sess::eventlog::StepId{step_id}, content_hash, ts));
        {
            std::ofstream lf(root_str() + "/log", std::ios::app);
            write_u64_dec_(lf, step_id);
            lf.put(',');
            char hex[16];
            hex16_(content_hash.raw(), hex);
            lf.write(hex, sizeof(hex));
            lf.put(',');
            write_u64_dec_(lf, ts);
            lf.put('\n');
        }

        // CONTRACT-107-POST: state-mutation contract — after advance_head
        // returns, three observable invariants must hold:
        //
        //   (1) head_ == content_hash      — the cached HEAD pointer
        //                                    reflects the freshly committed
        //                                    content_hash.  Downstream
        //                                    head() / empty() readers (and
        //                                    binary search in
        //                                    hash_at_step()) speculate on
        //                                    this without re-loading from
        //                                    disk.
        //
        //   (2) !log_.empty()              — log_ now has at least one
        //                                    entry (we just emplaced).
        //                                    latest_committed_head() and
        //                                    binary search in hash_at_step
        //                                    rely on this for the
        //                                    just-committed-step path.
        //
        //   (3) log_.back().step_id.value  — the back entry's step_id is
        //                                    the step we just committed
        //       == step_id                  (OrderedAppendOnly preserves
        //                                    insertion order; this catches
        //                                    a future refactor that
        //                                    accidentally appends a
        //                                    different step before
        //                                    advancing head).
        //
        // Routes through CRUCIBLE_POST (forwards to CRUCIBLE_PRE) because
        // P2900 `post (r: ...)` referencing class members through `this->`
        // is silently bypassed at consteval in GCC 16.1.1 (same gotcha
        // family as the in-body pre).  Void function: first arg of
        // CRUCIBLE_POST is the conventional sentinel `0`.  Under NDEBUG
        // each CRUCIBLE_POST collapses to `[[assume(cond)]]`, zero-cost
        // and propagates the invariant to every downstream optimizer.
        CRUCIBLE_POST(0, head_ == content_hash);
        CRUCIBLE_POST(0, !log_.empty());
        CRUCIBLE_POST(0, log_.back().step_id.value == step_id);
    }

    // ─── Row-typed event recording (FOUND-I09) ──────────────────────
    //
    // The 8th-axiom fence on the event-recording surface.  Wraps
    // advance_head with a compile-time effect-row constraint that
    // forces the caller to declare its row, and statically verifies
    // the row contains the capabilities record_event actually uses:
    //
    //   • Effect::IO    — record_event writes to HEAD file and
    //                     appends to the log file (file I/O).
    //   • Effect::Block — file writes may block on the kernel
    //                     (the std::ofstream destructor flushes;
    //                     write/fsync syscalls are blocking).
    //
    // Rationale (DetSafe — the 8th axiom):
    //   record_event is a deterministic state mutation: same
    //   (head_, content_hash, step_id) always produces the same
    //   on-disk bytes (HEAD overwrite + log append).  The row
    //   constraint is the type-level proof that callers are in a
    //   context that legitimately holds these capabilities — a
    //   foreground hot-path caller (Hot/DetSafe::Pure context, no
    //   IO or Block in row) MUST NOT invoke record_event because
    //   the file-I/O side effect would break replay determinism on
    //   the hot path.  The constraint catches this at compile time.
    //
    // Usage:
    //   cipher.record_event<effects::Row<effects::Effect::IO,
    //                                    effects::Effect::Block>>(
    //       open_view, content_hash, step_id);
    //
    // A caller in a `effects::Bg` context (which by construction
    // holds Alloc + IO + Block + Bg caps) satisfies the constraint
    // by passing any superset of {IO, Block} as CallerRow.  A caller
    // in a Hot/Pure context cannot satisfy the constraint and the
    // template substitution fails with the standard "constraints
    // not satisfied" diagnostic.
    //
    // The minimum-required-row constant is exposed below as
    // `record_event_required_row` so downstream Subrow checks (e.g.,
    // FOUND-I11-I15 migration batches) can refer to it by name.
    using record_event_required_row =
        ::crucible::effects::Row<
            ::crucible::effects::Effect::IO,
            ::crucible::effects::Effect::Block>;

    // ── Required-row content fence (FOUND-I09-AUDIT, Finding H) ────
    //
    // Pin the EXACT contents of record_event_required_row so a
    // future refactor that "improves" the typedef (drops Block,
    // adds Init, swaps to a different alias) fails loudly here
    // rather than silently widening the fence.  The static_asserts
    // are header-level: every TU that includes Cipher.h verifies
    // the contract, NOT just the cipher_record_event test TU.
    //
    // Rationale: a refactor like
    //   using record_event_required_row = effects::Row<IO>;
    // would compile and pass the neg_pure_row fixture (Row<> still
    // fails Subrow), but would now silently accept Row<IO> callers
    // — defeating the Block-fence.  The neg-compile fixtures for
    // IO-only / Block-only / Alloc-only / Bg-only callers (added
    // alongside this fence in the AUDIT) would also catch it, but
    // the static_asserts here fire FIRST during compilation, with
    // a clearer error message.
    static_assert(
        std::is_same_v<
            record_event_required_row,
            ::crucible::effects::Row<
                ::crucible::effects::Effect::IO,
                ::crucible::effects::Effect::Block>>,
        "Cipher::record_event_required_row MUST be exactly "
        "Row<IO, Block>.  Adding/removing atoms is a deliberate "
        "API tightening that breaks every FOUND-I11..I15 migration "
        "call site — bump the contract first, update the migration "
        "batches second.");
    static_assert(
        ::crucible::effects::row_size_v<record_event_required_row> == 2u,
        "record_event_required_row size MUST be exactly 2 atoms "
        "(IO + Block).");
    static_assert(
        ::crucible::effects::row_contains_v<  // ROW-CONTAINS-OK: concrete-row static_assert (record_event_required_row), not a Ctx capability check
            record_event_required_row,
            ::crucible::effects::Effect::IO>,
        "record_event_required_row MUST contain Effect::IO — the "
        "fence's reason for existence (HEAD + log file writes).");
    static_assert(
        ::crucible::effects::row_contains_v<  // ROW-CONTAINS-OK: concrete-row static_assert (record_event_required_row), not a Ctx capability check
            record_event_required_row,
            ::crucible::effects::Effect::Block>,
        "record_event_required_row MUST contain Effect::Block — "
        "the fence's reason for existence (file writes block on "
        "the kernel).");
    static_assert(std::is_same_v<
        persist_session_events_required_row,
        record_event_required_row>,
        "Session-event persistence uses the same IO+Block row fence "
        "as Cipher::record_event.");

    // Templated wrapper.  CallerRow is the row the caller declares
    // it holds; Subrow<required, CallerRow> checks
    // {IO, Block} ⊆ CallerRow at substitution time.
    //
    // The implementation is a single-line forwarder to advance_head
    // — the entire fence lives in the requires-clause.  No runtime
    // cost; no extra branch; no side effect beyond what advance_head
    // already does.
    template <typename CallerRow>
        requires ::crucible::effects::Subrow<
            record_event_required_row, CallerRow>
    void record_event(OpenView const& view,
                      ContentHash content_hash,
                      uint64_t step_id)
    {
        // CONTRACT-107: record_event forwards to advance_head, whose
        // in-body CRUCIBLE_PRE enforces the step_id-vs-log weakly-
        // increasing invariant via decide::weakly_increasing
        // (CONTRACT-042 catalog).  The forwarder declaration carries
        // no separate pre because the predicate would be a duplicate
        // (and consteval-bypass-vulnerable, per CONTRACT-100..103) —
        // the discharge happens once at the receiver.
        advance_head(view, content_hash, step_id);
    }

    template <typename CallerRow>
        requires ::crucible::effects::Subrow<
            persist_session_events_required_row, CallerRow>
    [[nodiscard]] ContentHash persist_session_events(
        OpenView const&,
        std::span<const SessionEvent> events)
    {
        if (events.empty()) return ContentHash{};

        if (events.size() > MAX_SESSION_EVENT_BATCH_EVENTS) {
            return ContentHash{};
        }

        const crucible::fixy::sess::eventlog::SessionTagId session = events.front().session;
        for (std::size_t i = 1; i < events.size(); ++i) {
            if (events[i].session != session) [[unlikely]] {
                return ContentHash{};
            }
            const uint64_t prev_step = events[i - 1].step_id.value;
            if (prev_step == std::numeric_limits<uint64_t>::max()
                || prev_step + 1 != events[i].step_id.value) [[unlikely]] {
                return ContentHash{};
            }
        }

        const std::size_t payload_bytes =
            events.size() * sizeof(SessionEvent);
        if (payload_bytes > MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES) {
            return ContentHash{};
        }

        // §III-clean cast cascade: SessionEvent* → void* → uint8_t*.  The
        // SessionEvent struct is trivially-copyable / standard-layout, so a
        // byte view is permitted under C++ object-model rules.  No actual
        // uint8_t-array lifetime is started — the span is read-only and the
        // hash function consumes bytes structurally.
        const auto payload = std::span<const std::uint8_t>{
            static_cast<const std::uint8_t*>(
                static_cast<const void*>(events.data())),
            payload_bytes};
        const ContentHash hash = session_event_payload_hash_(payload);
        const KernelCacheKey key{hash, SESSION_EVENT_FEDERATION_ROW_HASH};

        std::vector<std::uint8_t> encoded(
            cipher::federation::FEDERATION_HEADER_BYTES + payload.size());
        const auto written = cipher::federation::serialize_federation_entry(
            std::span<std::uint8_t>{encoded}, key, payload);
        if (!written) return ContentHash{};
        encoded.resize(*written);

        const std::string dir = session_event_dir(session);
        std::filesystem::create_directories(dir);
        const std::string path = session_event_batch_path(session, hash);
        if (std::filesystem::exists(path)) {
            if (!file_bytes_equal_(path, std::span<const std::uint8_t>{
                    encoded.data(), encoded.size()})) {
                return ContentHash{};
            }
        } else {
            std::ofstream out(path, std::ios::binary);
            if (!out) return ContentHash{};
            // §III-clean cascade: uint8_t* → void* → char* for ofstream::write.
            out.write(static_cast<const char*>(
                          static_cast<const void*>(encoded.data())),
                      static_cast<std::streamsize>(encoded.size()));
            if (!out) return ContentHash{};
        }

        std::ofstream index(dir + "/index", std::ios::app);
        if (!index) return ContentHash{};
        write_u64_dec_(index, events.front().step_id.value);
        index.put(',');
        write_u64_dec_(index, events.back().step_id.value);
        index.put(',');
        write_u64_dec_(index, events.size());
        index.put(',');
        char hex[16];
        hex16_(hash.raw(), hex);
        index.write(hex, sizeof(hex));
        index.put('\n');
        if (!index) return ContentHash{};

        return hash;
    }

    [[nodiscard]] std::vector<SessionEvent> load_session_events(
        OpenView const&,
        crucible::fixy::sess::eventlog::SessionTagId session,
        crucible::fixy::sess::eventlog::StepId from_step = {}) const
    {
        std::vector<SessionEvent> out;
        std::ifstream index(session_event_dir(session) + "/index");
        if (!index) return out;

        std::string line;
        uint64_t highest_loaded = from_step.value;
        bool have_loaded = false;
        while (std::getline(index, line)) {
            uint64_t first = 0;
            uint64_t last = 0;
            uint64_t count = 0;
            uint64_t raw_hash = 0;
            if (!parse_session_index_line_(line, first, last, count, raw_hash)) {
                continue;
            }
            if (last < from_step.value) continue;
            if (count == 0 || count > MAX_SESSION_EVENT_BATCH_EVENTS) continue;
            if ((last - first + 1u) != count) continue;

            const ContentHash hash{raw_hash};
            const std::string path = session_event_batch_path(session, hash);
            std::ifstream batch(path, std::ios::binary);
            if (!batch) continue;

            batch.seekg(0, std::ios::end);
            const auto raw_len = batch.tellg();
            if (raw_len < 0) continue;
            const auto len = static_cast<std::size_t>(raw_len);
            if (len < cipher::federation::FEDERATION_HEADER_BYTES
                || len > cipher::federation::FEDERATION_HEADER_BYTES
                        + MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES) {
                continue;
            }
            batch.seekg(0, std::ios::beg);

            std::vector<std::uint8_t> bytes(len);
            // §III-clean cascade: uint8_t* → void* → char* for ifstream::read.
            batch.read(static_cast<char*>(static_cast<void*>(bytes.data())),
                       static_cast<std::streamsize>(bytes.size()));
            if (!batch) continue;

            auto view = cipher::federation::deserialize_untrusted_federation_entry(
                std::span<const std::uint8_t>{bytes},
                static_cast<std::uint16_t>(
                    ::crucible::effects::OsUniverse::cardinality));
            if (!view) continue;
            if (view->header.content_hash != hash) continue;
            if (view->header.row_hash != SESSION_EVENT_FEDERATION_ROW_HASH) {
                continue;
            }
            if (session_event_payload_hash_(view->payload) != hash) continue;
            if ((view->payload.size() % sizeof(SessionEvent)) != 0u) continue;

            const std::size_t n =
                view->payload.size() / sizeof(SessionEvent);
            if (n != count) continue;

            std::vector<SessionEvent> decoded(n);
            std::memcpy(decoded.data(), view->payload.data(),
                        view->payload.size());
            if (decoded.front().step_id.value != first) continue;
            if (decoded.back().step_id.value != last) continue;
            bool valid_batch = true;
            for (std::size_t i = 0; i < decoded.size(); ++i) {
                if (decoded[i].session != session) {
                    valid_batch = false;
                    break;
                }
                if (i != 0) {
                    const uint64_t prev_step = decoded[i - 1].step_id.value;
                    if (prev_step == std::numeric_limits<uint64_t>::max()
                        || prev_step + 1u != decoded[i].step_id.value) {
                        valid_batch = false;
                        break;
                    }
                }
                if (have_loaded
                    && decoded[i].step_id.value <= highest_loaded) {
                    valid_batch = false;
                    break;
                }
            }
            if (!valid_batch) continue;

            out.reserve(out.size() + decoded.size());
            for (const SessionEvent& event : decoded) {
                if (event.step_id.value >= from_step.value) {
                    out.push_back(event);
                    highest_loaded = event.step_id.value;
                    have_loaded = true;
                }
            }
        }
        return out;
    }

    // ─── Time travel ────────────────────────────────────────────────

    // Typed: caller has proved Open.  const — reads only log_.
    [[nodiscard]] ContentHash hash_at_step(OpenView const&, uint64_t step_id) const {
        if (log_.empty()) return ContentHash{};

        // Find the first entry after step_id, then walk back to the
        // latest event that actually commits the Cipher head.  Today
        // Cipher writes only StoreCommitted events, but the unified
        // SessionEvent record can also carry non-head session events.
        std::size_t lo = 0;
        std::size_t hi = log_.size();

        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (log_[mid].step_id.value <= step_id) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        while (lo > 0) {
            --lo;
            const LogEntry& entry = log_[lo];
            if (entry.commits_cipher_head()) {
                return entry.cipher_content_hash();
            }
        }
        return ContentHash{};
    }

    // ─── Queries ────────────────────────────────────────────────────

    [[nodiscard]] ContentHash        head()  const { return head_; }
    [[nodiscard]] bool               empty() const { return !head_; }
    // root() unwraps WriteOnce + Tagged for callers that just want the
    // path string.  Internal code uses root_str() below; external callers
    // preserve the old signature.
    [[nodiscard]] const std::string& root()  const CRUCIBLE_LIFETIMEBOUND { return root_str(); }

 private:
    using LogEntry = crucible::fixy::sess::eventlog::SessionEvent;

    static_assert(std::is_same_v<LogEntry, crucible::fixy::sess::eventlog::SessionEvent>);
    static_assert(sizeof(LogEntry) == 72);
    static_assert(std::is_trivially_copyable_v<LogEntry>);

    struct CachedObjectBytes {
        ContentHash          hash;
        std::vector<uint8_t> bytes;
    };

    static constexpr size_t MAX_RESIDENT_CACHE_BYTES = size_t{8} << 20;
    static constexpr size_t MAX_RESIDENT_CACHE_ENTRIES = 64;
    static constexpr size_t MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES =
        size_t{64} << 20;
    static constexpr size_t MAX_SESSION_EVENT_BATCH_EVENTS =
        MAX_SESSION_EVENT_BATCH_PAYLOAD_BYTES / sizeof(SessionEvent);

    using LogEntryByStepId = crucible::fixy::sess::eventlog::StepIdKeyFn;
    using LogEntryStepLess = crucible::fixy::sess::eventlog::StepIdLess;

    // root_ is write-once (set by open, never reassigned) and carries
    // the provenance tag source::Durable at the type level.  Reassigning
    // via `=` is a compile error (WriteOnce); a second set() would
    // contract-fire.  Internal code unwraps via root_str().
    crucible::fixy::wrap::WriteOnce<
        crucible::fixy::wrap::Tagged<std::string, crucible::fixy::tags::source::Durable>>
                                             root_;
    ContentHash                              head_{};

    // Internal unwrap — single point that peels both layers.
    [[nodiscard]] const std::string& root_str() const noexcept {
        [[assume(root_.has_value())]];
        return root_.get_assuming_set().value();
    }
    // log_ grows append-only AND each entry's step_id must not go
    // backward.  OrderedAppendOnly<> fuses both invariants into one
    // type; .erase() / .clear() / out-of-order .append() all fail at
    // compile time (the first two) or contract-terminate (the third).
    crucible::fixy::wrap::OrderedAppendOnly<
        LogEntry, LogEntryByStepId, LogEntryStepLess> log_;
    mutable std::vector<CachedObjectBytes> resident_cache_;
    mutable size_t resident_cache_bytes_ = 0;

    [[nodiscard]] ContentHash latest_committed_head() const noexcept {
        std::size_t i = log_.size();
        while (i > 0) {
            --i;
            const LogEntry& entry = log_[i];
            if (entry.commits_cipher_head()) {
                return entry.cipher_content_hash();
            }
        }
        return ContentHash{};
    }

    // Path: root_/objects/<first2hex>/<remaining14hex>
    std::string obj_path(uint64_t hash) const {
        char hex[16];
        hex16_(hash, hex);
        const std::string& root = root_str();
        if (root.size() > MAX_ROOT_PATH_BYTES) {
            std::abort();
        }
        std::string path{root};
        path.reserve(root.size() + OBJECT_PATH_SUFFIX_BYTES);
        path.append("/objects/");
        path.append(hex, 2);
        path.push_back('/');
        path.append(hex + 2, 14);
        return path;
    }

    std::string session_event_dir(
        crucible::fixy::sess::eventlog::SessionTagId session) const {
        char hex[16];
        hex16_(session.value, hex);
        const std::string& root = root_str();
        std::string path{root};
        path.reserve(root.size() + sizeof("/session_events/") - 1 + 16);
        path.append("/session_events/");
        path.append(hex, 16);
        return path;
    }

    std::string session_event_batch_path(
        crucible::fixy::sess::eventlog::SessionTagId session,
        ContentHash hash) const {
        char hex[16];
        hex16_(hash.raw(), hex);
        std::string path = session_event_dir(session);
        path.push_back('/');
        path.append(hex, 16);
        path.append(".cfed");
        return path;
    }

    [[nodiscard]] std::span<const uint8_t>
    cached_bytes(ContentHash hash) const noexcept {
        for (std::size_t i = 0; i < resident_cache_.size(); ++i) {
            const CachedObjectBytes& entry = resident_cache_[i];
            if (entry.hash == hash) {
                if (i + 1 != resident_cache_.size()) {
                    CachedObjectBytes hit = std::move(resident_cache_[i]);
                    resident_cache_.erase(resident_cache_.begin()
                                          + static_cast<std::ptrdiff_t>(i));
                    resident_cache_.push_back(std::move(hit));
                }
                return std::span<const uint8_t>{resident_cache_.back().bytes};
            }
        }
        return {};
    }

    void remember_cached_bytes(ContentHash hash,
                               std::span<const uint8_t> bytes) const {
        if (!hash || bytes.empty() || bytes.size() > MAX_RESIDENT_CACHE_BYTES) {
            return;
        }
        for (const CachedObjectBytes& entry : resident_cache_) {
            if (entry.hash == hash) return;
        }

        if (resident_cache_.capacity() < MAX_RESIDENT_CACHE_ENTRIES) {
            resident_cache_.reserve(MAX_RESIDENT_CACHE_ENTRIES);
        }
        while (!resident_cache_.empty()
               && (resident_cache_.size() >= MAX_RESIDENT_CACHE_ENTRIES
                   || resident_cache_bytes_ + bytes.size()
                          > MAX_RESIDENT_CACHE_BYTES)) {
            resident_cache_bytes_ -= resident_cache_.front().bytes.size();
            resident_cache_.erase(resident_cache_.begin());
        }

        resident_cache_bytes_ += bytes.size();
        resident_cache_.push_back(CachedObjectBytes{
            .hash = hash,
            .bytes = std::vector<uint8_t>(bytes.begin(), bytes.end()),
        });
    }

    static void hex16_(uint64_t value, char (&out)[16]) noexcept {
        static constexpr char kHex[] = "0123456789abcdef";
        for (std::size_t i = 0; i < 16; ++i) {
            out[15 - i] = kHex[value & 0x0FULL];
            value >>= 4;
        }
    }

    static void write_u64_dec_(std::ofstream& out, uint64_t value) {
        char buf[20];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
        if (ec == std::errc{}) {
            out.write(buf, ptr - buf);
        }
    }

    [[nodiscard]] static bool file_bytes_equal_(
        const std::string& path,
        std::span<const std::uint8_t> expected)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        in.seekg(0, std::ios::end);
        const auto raw = in.tellg();
        if (raw < 0) return false;
        const auto len = static_cast<std::size_t>(raw);
        if (len != expected.size()) return false;
        in.seekg(0, std::ios::beg);

        std::array<std::uint8_t, 4096> actual{};
        std::size_t offset = 0;
        while (offset < expected.size()) {
            const std::size_t remaining = expected.size() - offset;
            const std::size_t n = remaining < actual.size()
                ? remaining
                : actual.size();
            // §III-clean cascade: uint8_t* → void* → char* for ifstream::read.
            in.read(static_cast<char*>(static_cast<void*>(actual.data())),
                    static_cast<std::streamsize>(n));
            if (in.gcount() != static_cast<std::streamsize>(n)) return false;
            if (std::memcmp(actual.data(), expected.data() + offset, n) != 0) {
                return false;
            }
            offset += n;
        }
        return true;
    }

    [[nodiscard]] static ContentHash session_event_payload_hash_(
        std::span<const std::uint8_t> bytes) noexcept {
        uint64_t h = 0xcbf29ce484222325ULL
                   ^ 0x53455353494f4e45ULL
                   ^ bytes.size();
        for (std::uint8_t byte : bytes) {
            h ^= static_cast<uint64_t>(byte);
            h *= 0x100000001b3ULL;
        }
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        if (h == 0u) h = 0x53455353494f4e45ULL;
        if (h == std::numeric_limits<uint64_t>::max()) --h;
        return ContentHash{h};
    }

    // Parse a single uint64_t field via std::from_chars — exception-free
    // (std::stoull throws on malformed input, which is UB under
    // -fno-exceptions).  Returns true iff the full [begin, end) range
    // parsed cleanly as a number in the given base.
    [[nodiscard]] static bool parse_u64(
        const char* begin, const char* end, int base, uint64_t& out) noexcept
    {
        if (begin >= end) return false;
        auto [p, ec] = std::from_chars(begin, end, out, base);
        return ec == std::errc{} && p == end;
    }

    [[nodiscard]] static bool parse_session_index_line_(
        std::string_view line,
        uint64_t& first,
        uint64_t& last,
        uint64_t& count,
        uint64_t& hash) noexcept
    {
        const std::size_t p1 = line.find(',');
        if (p1 == std::string_view::npos) return false;
        const std::size_t p2 = line.find(',', p1 + 1);
        if (p2 == std::string_view::npos) return false;
        const std::size_t p3 = line.find(',', p2 + 1);
        if (p3 == std::string_view::npos) return false;
        const char* begin = line.data();
        const char* end = begin + line.size();
        if (!parse_u64(begin, begin + p1, 10, first)) return false;
        if (!parse_u64(begin + p1 + 1, begin + p2, 10, last)) return false;
        if (!parse_u64(begin + p2 + 1, begin + p3, 10, count)) return false;
        if (!parse_u64(begin + p3 + 1, end, 16, hash)) return false;
        return first <= last;
    }

    // Load the log file into memory.  Malformed lines (missing commas,
    // non-numeric fields, out-of-range values) are SKIPPED — a corrupt
    // or adversarially-truncated log doesn't abort the process, it just
    // means the log's state may be a proper prefix of what was on disk.
    void load_log() {
        std::ifstream f(root_str() + "/log");
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            const size_t p1 = line.find(',');
            if (p1 == std::string::npos) continue;
            const size_t p2 = line.find(',', p1 + 1);
            if (p2 == std::string::npos) continue;

            const char* begin = line.data();
            uint64_t step_id = 0;
            uint64_t raw_hash = 0;
            uint64_t ts_ns = 0;
            if (!parse_u64(begin, begin + p1, 10, step_id))        continue;
            if (!parse_u64(begin + p1 + 1, begin + p2, 16, raw_hash)) continue;
            if (!parse_u64(begin + p2 + 1, begin + line.size(), 10, ts_ns)) continue;

            log_.emplace(LogEntry::cipher_store_committed(
                crucible::fixy::sess::eventlog::StepId{step_id}, ContentHash{raw_hash}, ts_ns));
        }
    }

    // Conservative upper-bound on serialized size for a RegionNode.
    //
    // Return type is `Positive<std::size_t>` (#105): the estimate is
    // always structurally ≥ 352 bytes (64 header + 32 fixed fields +
    // 256 tail headroom, all unconditional), so the `positive`
    // predicate is satisfied by construction.  The Refined wrapper
    // makes that non-zero invariant a TYPE-LEVEL claim — if a future
    // refactor zeros out the tail addition or conditionalizes the
    // headers, the Refined ctor's contract fires INSIDE this
    // function rather than the downstream `vector<uint8_t>(cap)`
    // silently allocating an empty buffer that `serialize_region`
    // then writes into as a zero-length span.
    //
    // Callers unwrap via `.value()` at the `std::vector<uint8_t>`
    // construction site — explicit, grep-discoverable, zero cost.
    static crucible::fixy::wrap::Positive<std::size_t>
    estimate_serial_size(const RegionNode* region) {
        size_t sz = 64; // header
        sz += 32;       // region fixed fields
        if (region->plan) {
            sz += 64 + region->plan->num_slots * sizeof(TensorSlot);
        }
        for (uint32_t i = 0; i < region->num_ops; i++) {
            const TraceEntry& te = region->ops[i];
            const size_t n_in  = te.num_inputs;
            const size_t n_out = te.num_outputs;
            const size_t n_sca = te.num_scalar_args;
            sz += 40; // fixed per-op header
            sz += (n_in + n_out) * sizeof(TensorMeta);
            sz += n_sca          * sizeof(int64_t);
            sz += n_in           * sizeof(uint32_t); // input_trace_indices
            sz += n_in           * sizeof(uint32_t); // input_slot_ids
            sz += n_out          * sizeof(uint32_t); // output_slot_ids
        }
        return crucible::fixy::wrap::Positive<std::size_t>{sz + 256}; // headroom
    }

    static uint64_t now_ns() {
        const auto tp = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                tp.time_since_epoch()).count());
    }
};

// ── Validated head witness (#884 WRAP-Cipher-1) ────────────────────
//
// The hash a caller commits to advance_head MUST be non-zero — zero is
// the structural sentinel for "no commit yet" and propagating it would
// silently corrupt hash_at_step()'s log binary search (a "before first
// commit" marker indistinguishable from a real step).  ValidCipherHead
// is the type-level witness that the caller has validated the hash at
// its source (typically from a RegionNode where compute_content_hash
// guarantees non-zero output for any non-empty region).  Construction
// is gated by Refined<>'s `pre(non_zero(v))` clause; under
// semantic=enforce the runtime path is contract violation ->
// handle_contract_violation (logged + abort), under semantic=ignore
// the optimizer treats `hash.raw() != 0` as `[[assume]]` and the
// downstream binary search / log append speculate without re-checking.
//
// `make_cipher_head` is a [[gnu::const]] factory that lifts the
// witness back to a bare ContentHash for the advance_head signature —
// preserving the existing 6 caller signatures + the 1313-byte Cipher
// layout while routing every external write through the
// ValidCipherHead ctor when callers opt in.
//
// Defense-in-depth: advance_head's `pre(content_hash.raw() != 0)`
// clause (see line 599) is retained as the runtime gate.
// ValidCipherHead catches the value at the call site; the pre clause
// catches it at the function boundary.  Both layers must reject for
// the type-level invariant and the runtime path to disagree.
//
// Cost: regime-1 EBO collapse — sizeof(ValidCipherHead) ==
// sizeof(ContentHash) == sizeof(uint64_t) == 8 B.
using ValidCipherHead = ::crucible::fixy::wrap::Refined<
    ::crucible::fixy::wrap::non_zero, ContentHash>;

[[nodiscard, gnu::const]] inline constexpr
ContentHash make_cipher_head(ValidCipherHead raw) noexcept {
    return raw.value();
}

// Tier 2 opt-in: nothing inside Cipher may be a ScopedView.
static_assert(crucible::fixy::wrap::no_scoped_view_field_check<Cipher>());
static_assert(sizeof(Cipher::ContentAddressedRegionPayload)
              == sizeof(const RegionNode*));
static_assert(crucible::fixy::sess::contentaddr::is_content_addressed_v<
    typename Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(crucible::fixy::sess::contentaddr::is_content_addressed_v<
    typename Cipher::LoadedContentAddressedRegionPayload::payload_type>);

} // namespace crucible
