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
#include <crucible/handles/FileHandle.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/ScopedView.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/Wait.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

namespace crucible {

// ── Cipher state tag ────────────────────────────────────────────────
// Open denotes: open() has run, objects/ exists, root_ is non-empty.
// Closed is implicit (no tag) — the negation of is_open().
namespace cipher_state {
    struct Open {};
}

class CRUCIBLE_OWNER Cipher {
 public:
    // Factory: open (or create) a Cipher rooted at `root`.
    // Creates the objects/ subdirectory if absent, loads HEAD + log from disk.
    // gnu::cold: startup-only path, never on the op-dispatch hot path.
    [[gnu::cold]] static Cipher open(const std::string& root) {
        Cipher c;
        // root_ is WriteOnce<Tagged<std::string, source::Durable>>:
        // set exactly once here (WriteOnce), tagged at the type level as
        // loaded from on-disk state (Tagged).  A second attempt to set
        // (e.g. calling open on a Cipher that already has a root) would
        // contract-fire; reassignment via `=` is a compile error.
        c.root_.set(crucible::safety::Tagged<std::string,
                                             crucible::safety::source::Durable>{root});
        std::filesystem::create_directories(root + "/objects");

        // Load the log first to populate in-memory state.
        c.load_log();

        // HEAD file overrides the log's last hash (HEAD is the authoritative pointer).
        // FileHandle's RAII closes the fd at scope exit; no iostream needed.
        // std::from_chars is exception-free (unlike std::stoull which throws
        // on malformed input — incompatible with -fno-exceptions), so a
        // corrupt HEAD file cleanly falls through to the log fallback.
        const std::string head_path = root + "/HEAD";
        auto hf = crucible::safety::open_read(head_path.c_str());
        bool head_from_file = false;
        if (hf.is_open()) {
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
        if (!head_from_file && !c.log_.empty()) {
            c.head_ = c.log_.back().hash;
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

    using OpenView = crucible::safety::ScopedView<Cipher, cipher_state::Open>;

    [[nodiscard]] OpenView mint_open_view() const noexcept
        pre (is_open())
    {
        return crucible::safety::mint_view<cipher_state::Open>(*this);
    }

    [[nodiscard]] friend constexpr bool view_ok(
        Cipher const& c, std::type_identity<cipher_state::Open>) noexcept {
        return c.is_open();
    }

    // ─── Store ──────────────────────────────────────────────────────

    // Typed: caller has proved Open.  Zero runtime state check.
    [[nodiscard]] ContentHash store(OpenView const&,
                                    const RegionNode* region,
                                    const MetaLog* meta_log) {
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
        f.write(reinterpret_cast<const char*>(buf.data()),
                static_cast<std::streamsize>(n));
        return f ? hash : ContentHash{};
    }

    // Legacy: mints OpenView locally; fires mint_open_view()'s pre()
    // contract if the Cipher is Closed.
    [[nodiscard]] ContentHash store(const RegionNode* region, const MetaLog* meta_log) {
        return store(mint_open_view(), region, meta_log);
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
    [[nodiscard]] safety::Wait<safety::WaitStrategy_v::Block, ContentHash>
    store_pinned(OpenView const& view,
                 const RegionNode* region,
                 const MetaLog* meta_log) {
        return safety::Wait<safety::WaitStrategy_v::Block, ContentHash>{
            store(view, region, meta_log)};
    }

    // Legacy-shape pinned variant — mints view locally.
    [[nodiscard]] safety::Wait<safety::WaitStrategy_v::Block, ContentHash>
    store_pinned(const RegionNode* region, const MetaLog* meta_log) {
        return safety::Wait<safety::WaitStrategy_v::Block, ContentHash>{
            store(region, meta_log)};
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
    using ValidatedObjectSize = crucible::safety::Refined<
        crucible::safety::bounded_above<MAX_OBJECT_BYTES>, size_t>;

    // Typed: caller has proved Open.  const because deserialize does
    // not mutate the Cipher (it only reads paths under root_).
    //
    // Source tag boundary: buf holds source::External bytes (disk, not
    // yet validated).  deserialize_region IS the External→Sanitized
    // transition — its bounds-checked Reader rejects truncated input,
    // and Serialize's pre-flight size checks reject adversarial num_slots
    // claims.  On success, the returned RegionNode* carries Sanitized
    // contents (ndim ≤ 8 per-meta, num_ops ≤ CDAG_MAX_OPS, etc.).  On
    // any failure along the way: nullptr, no state change, no partial
    // arena growth (see Serialize.h pre-flight).
    [[nodiscard]] RegionNode* load(OpenView const&, effects::Alloc a,
                                   ContentHash content_hash, Arena& arena) const {
        if (!content_hash) return nullptr;
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
        f.read(reinterpret_cast<char*>(buf.data()),
               static_cast<std::streamsize>(validated_len.value()));
        if (!f) return nullptr;

        return deserialize_region(a, std::span<const uint8_t>{buf}, arena);
    }

    // Legacy: mints OpenView locally.  const because mint_open_view()
    // is now const-callable (ScopedView stores Carrier const*, so
    // view minting works through a const reference).
    [[nodiscard]] RegionNode* load(effects::Alloc a, ContentHash content_hash, Arena& arena) const {
        return load(mint_open_view(), a, content_hash, arena);
    }

    // ─── HEAD management ────────────────────────────────────────────

    // Typed: caller has proved Open.
    // Contract: step_id must not go backward — steps are documented as
    // monotonic for binary search in hash_at_step() to be correct.
    void advance_head(OpenView const&, ContentHash content_hash, uint64_t step_id)
        pre (log_.empty() || step_id >= log_.back().step_id)
    {
        head_ = content_hash;

        // Overwrite HEAD file (truncate to hex + newline).
        {
            std::ofstream hf(root_str() + "/HEAD");
            hf << std::format("{:016x}", content_hash.raw()) << "\n";
        }

        // Append log entry.
        const uint64_t ts = now_ns();
        log_.emplace(LogEntry{.step_id = step_id, .hash = content_hash, .ts_ns = ts});
        {
            std::ofstream lf(root_str() + "/log", std::ios::app);
            lf << std::format("{},{:016x},{}", step_id, content_hash.raw(), ts) << "\n";
        }
    }

    // Legacy: mints OpenView locally.
    void advance_head(ContentHash content_hash, uint64_t step_id) {
        advance_head(mint_open_view(), content_hash, step_id);
    }

    // ─── Time travel ────────────────────────────────────────────────

    // Typed: caller has proved Open.  const — reads only log_.
    [[nodiscard]] ContentHash hash_at_step(OpenView const&, uint64_t step_id) const {
        if (log_.empty()) return ContentHash{};

        // Find last entry with log_[mid].step_id <= step_id.
        uint32_t lo = 0;
        auto     hi = static_cast<uint32_t>(log_.size());
        ContentHash result{};

        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2;
            if (log_[mid].step_id <= step_id) {
                result = log_[mid].hash;
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return result;
    }

    // Legacy: mints OpenView locally.  const because mint_open_view()
    // is const-callable — same rationale as load() above.
    [[nodiscard]] ContentHash hash_at_step(uint64_t step_id) const {
        return hash_at_step(mint_open_view(), step_id);
    }

    // ─── Queries ────────────────────────────────────────────────────

    [[nodiscard]] ContentHash        head()  const { return head_; }
    [[nodiscard]] bool               empty() const { return !head_; }
    // root() unwraps WriteOnce + Tagged for callers that just want the
    // path string.  Internal code uses root_str() below; external callers
    // preserve the old signature.
    [[nodiscard]] const std::string& root()  const CRUCIBLE_LIFETIMEBOUND { return root_str(); }

 private:
    struct LogEntry {
        uint64_t    step_id;
        ContentHash hash;
        uint64_t    ts_ns;
    };

    // Stateless projection used by OrderedAppendOnly below.  hash_at_step()
    // binary-searches the log on step_id; that search requires monotonic
    // non-decreasing step_id across the entire log.  Projecting step_id
    // through OrderedAppendOnly makes the ordering invariant structural
    // (contract-fires at append time) rather than a doc comment.
    struct LogEntryByStepId {
        [[nodiscard]] constexpr uint64_t operator()(const LogEntry& e) const noexcept {
            return e.step_id;
        }
    };

    // root_ is write-once (set by open, never reassigned) and carries
    // the provenance tag source::Durable at the type level.  Reassigning
    // via `=` is a compile error (WriteOnce); a second set() would
    // contract-fire.  Internal code unwraps via root_str().
    crucible::safety::WriteOnce<
        crucible::safety::Tagged<std::string, crucible::safety::source::Durable>>
                                             root_;
    ContentHash                              head_{};

    // Internal unwrap — single point that peels both layers.
    [[nodiscard]] const std::string& root_str() const noexcept
        pre (root_.has_value())
    {
        return root_.get().value();
    }
    // log_ grows append-only AND each entry's step_id must not go
    // backward.  OrderedAppendOnly<> fuses both invariants into one
    // type; .erase() / .clear() / out-of-order .append() all fail at
    // compile time (the first two) or contract-terminate (the third).
    crucible::safety::OrderedAppendOnly<LogEntry, LogEntryByStepId> log_;

    // Path: root_/objects/<first2hex>/<remaining14hex>
    std::string obj_path(uint64_t hash) const {
        auto hex = std::format("{:016x}", hash);
        return root_str() + "/objects/" + hex.substr(0, 2) + "/" + hex.substr(2);
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

            log_.emplace(LogEntry{.step_id = step_id,
                                   .hash    = ContentHash{raw_hash},
                                   .ts_ns   = ts_ns});
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
    static crucible::safety::Positive<std::size_t>
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
        return crucible::safety::Positive<std::size_t>{sz + 256}; // headroom
    }

    static uint64_t now_ns() {
        const auto tp = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                tp.time_since_epoch()).count());
    }
};

// Tier 2 opt-in: nothing inside Cipher may be a ScopedView.
static_assert(crucible::safety::no_scoped_view_field_check<Cipher>());

} // namespace crucible
