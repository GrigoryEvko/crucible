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

#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/Serialize.h>

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace crucible {

class Cipher {
 public:
    // Factory: open (or create) a Cipher rooted at `root`.
    // Creates the objects/ subdirectory if absent, loads HEAD + log from disk.
    static Cipher open(const std::string& root) {
        Cipher c;
        c.root_ = root;
        std::filesystem::create_directories(root + "/objects");

        // Load the log first to populate in-memory state.
        c.load_log();

        // HEAD file overrides the log's last hash (HEAD is the authoritative pointer).
        std::ifstream hf(root + "/HEAD");
        if (hf) {
            std::string hex;
            std::getline(hf, hex);
            if (!hex.empty()) {
                c.head_ = std::stoull(hex, nullptr, 16);
            }
        } else if (!c.log_.empty()) {
            c.head_ = c.log_.back().hash;
        }

        return c;
    }

    Cipher() = default;

    // ─── Store ──────────────────────────────────────────────────────

    // Serialize `region` and write to objects/<hash>. Idempotent.
    // Returns content_hash on success, 0 on error.
    uint64_t store(const RegionNode* region, const MetaLog* meta_log) {
        if (!region) return 0;
        const uint64_t hash = region->content_hash;
        if (hash == 0) return 0;

        const std::string path = obj_path(hash);

        // Idempotent: if the file already exists, same bytes → skip write.
        if (std::filesystem::exists(path)) return hash;

        // Estimate serialization size conservatively.
        const size_t cap = estimate_serial_size(region);
        std::vector<uint8_t> buf(cap);
        const size_t n = serialize_region(region, meta_log, buf.data(), cap);
        if (n == 0) return 0;

        // Ensure shard directory exists.
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path());

        std::ofstream f(path, std::ios::binary);
        if (!f) return 0;
        f.write(reinterpret_cast<const char*>(buf.data()),
                static_cast<std::streamsize>(n));
        return f ? hash : 0;
    }

    // ─── Load ───────────────────────────────────────────────────────

    // Deserialize a RegionNode from objects/<content_hash>.
    // All structures are arena-allocated. Returns nullptr if not found.
    RegionNode* load(uint64_t content_hash, Arena& arena) const {
        if (content_hash == 0) return nullptr;
        const std::string path = obj_path(content_hash);
        if (!std::filesystem::exists(path)) return nullptr;

        std::ifstream f(path, std::ios::binary);
        if (!f) return nullptr;

        f.seekg(0, std::ios::end);
        const size_t len = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);

        std::vector<uint8_t> buf(len);
        f.read(reinterpret_cast<char*>(buf.data()),
               static_cast<std::streamsize>(len));
        if (!f) return nullptr;

        return deserialize_region(buf.data(), len, arena);
    }

    // ─── HEAD management ────────────────────────────────────────────

    // Advance HEAD to `content_hash` and append a log entry for `step_id`.
    void advance_head(uint64_t content_hash, uint64_t step_id) {
        head_ = content_hash;

        // Overwrite HEAD file (truncate to hex + newline).
        {
            std::ofstream hf(root_ + "/HEAD");
            char hex[17];
            std::snprintf(hex, sizeof(hex), "%016" PRIx64, content_hash);
            hf << hex << "\n";
        }

        // Append log entry.
        const uint64_t ts = now_ns();
        log_.push_back({step_id, content_hash, ts});
        {
            std::ofstream lf(root_ + "/log", std::ios::app);
            char hex[17];
            std::snprintf(hex, sizeof(hex), "%016" PRIx64, content_hash);
            lf << step_id << "," << hex << "," << ts << "\n";
        }
    }

    // ─── Time travel ────────────────────────────────────────────────

    // Returns the content_hash committed at or before `step_id`.
    // Binary-searches the in-memory log (steps are monotonically increasing).
    // Returns 0 if no commit at or before that step exists.
    uint64_t hash_at_step(uint64_t step_id) const {
        if (log_.empty()) return 0;

        // Find last entry with log_[mid].step_id <= step_id.
        uint32_t lo = 0;
        auto     hi = static_cast<uint32_t>(log_.size());
        uint64_t result = 0;

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

    // ─── Queries ────────────────────────────────────────────────────

    uint64_t           head()  const { return head_; }
    bool               empty() const { return head_ == 0; }
    const std::string& root()  const { return root_; }

 private:
    struct LogEntry {
        uint64_t step_id;
        uint64_t hash;
        uint64_t ts_ns;
    };

    std::string            root_;
    uint64_t               head_{0};
    std::vector<LogEntry>  log_;

    // Path: root_/objects/<first2hex>/<remaining14hex>
    std::string obj_path(uint64_t hash) const {
        char hex[17];
        std::snprintf(hex, sizeof(hex), "%016" PRIx64, hash);
        return root_ + "/objects/" + std::string(hex, 2) + "/" + (hex + 2);
    }

    // Load the log file into memory.
    void load_log() {
        std::ifstream f(root_ + "/log");
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            const size_t p1 = line.find(',');
            const size_t p2 = line.find(',', p1 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos) continue;
            const uint64_t step_id = std::stoull(line.substr(0, p1));
            const uint64_t hash    = std::stoull(line.substr(p1 + 1, p2 - p1 - 1),
                                                  nullptr, 16);
            const uint64_t ts_ns   = std::stoull(line.substr(p2 + 1));
            log_.push_back({step_id, hash, ts_ns});
        }
    }

    // Conservative upper-bound on serialized size for a RegionNode.
    static size_t estimate_serial_size(const RegionNode* region) {
        size_t sz = 64; // header
        sz += 32;       // region fixed fields
        if (region->plan) {
            sz += 64 + region->plan->num_slots * sizeof(TensorSlot);
        }
        for (uint32_t i = 0; i < region->num_ops; i++) {
            const TraceEntry& te = region->ops[i];
            sz += 40; // fixed per-op header
            sz += (te.num_inputs + te.num_outputs) * sizeof(TensorMeta);
            sz += te.num_scalar_args * sizeof(int64_t);
            sz += te.num_inputs  * sizeof(uint32_t);
            sz += te.num_outputs * sizeof(uint32_t);
        }
        return sz + 256; // headroom
    }

    static uint64_t now_ns() {
        const auto tp = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                tp.time_since_epoch()).count());
    }
};

} // namespace crucible
