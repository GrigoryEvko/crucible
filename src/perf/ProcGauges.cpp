// crucible::perf::ProcGauges — userspace /proc + /sys gauge poller impl.
//
// Reads kernel-aggregated counters at SNAPSHOT time (not per-event).
// Companion to SenseHub v2's BPF-side counter array; supplies the
// gauge slots that the kernel already aggregates and exposes via
// /proc and /sys.
//
// Per-source readers: each `read_*_()` helper preads the corresponding
// fd into the heap-owned `scratch_` buffer at offset 0 (so we don't
// need to lseek between reads), parses the relevant fields, returns
// the aggregated value.  Failures (EAGAIN, parse errors, fd-not-valid)
// return UNAVAILABLE — ProcGauges never throws.

#include <crucible/perf/ProcGauges.h>
#include <crucible/perf/SenseHubV2.h>

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

namespace crucible::perf {

// ─── ScopedFd dtor + close_ ───────────────────────────────────────────

ScopedFd::~ScopedFd() noexcept { close_(); }

void ScopedFd::close_() noexcept {
    if (fd_ >= 0) {
        // Loop on EINTR — POSIX permits close to interrupt and on Linux
        // the fd is always reclaimed regardless, but loop defends
        // against systems where it isn't.
        int r;
        do { r = ::close(fd_); } while (r == -1 && errno == EINTR);
        fd_ = -1;
    }
}

// ─── Helpers ──────────────────────────────────────────────────────────

namespace {

// Open a /proc or /sys file read-only, with O_CLOEXEC.  Return -1 on
// failure (file missing, permission denied, kernel feature absent).
[[nodiscard]] int open_ro(const char* path) noexcept {
    int fd;
    do {
        fd = ::open(path, O_RDONLY | O_CLOEXEC);
    } while (fd == -1 && errno == EINTR);
    return fd;
}

// pread(fd, buf, n-1, 0) and NUL-terminate.  Returns bytes read on
// success, -1 on failure.  Loops on EINTR.
[[nodiscard]] ssize_t pread_full(int fd, char* buf, size_t n) noexcept {
    if (fd < 0 || n == 0)
        return -1;
    ssize_t r;
    do {
        r = ::pread(fd, buf, n - 1, 0);
    } while (r == -1 && errno == EINTR);
    if (r >= 0)
        buf[r] = '\0';
    return r;
}

// Skip whitespace
[[nodiscard]] const char* skip_ws(const char* p, const char* end) noexcept {
    while (p < end && (*p == ' ' || *p == '\t'))
        ++p;
    return p;
}

// Parse one decimal u64.  Advances *pp.  Returns 0 if no digits.
[[nodiscard]] uint64_t parse_u64(const char** pp, const char* end) noexcept {
    const char* p = skip_ws(*pp, end);
    uint64_t v = 0;
    bool any = false;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + (uint64_t)(*p - '0');
        ++p;
        any = true;
    }
    *pp = p;
    return any ? v : 0;
}

// Parse a fixed-point float "X.Y" → uint64_t (x * scale + y_truncated_to_scale_digits).
// Used for /proc/loadavg + /proc/pressure/* avg10 fields scaled ×100.
[[nodiscard]] uint64_t parse_fixed_x100(const char** pp, const char* end) noexcept {
    const char* p = skip_ws(*pp, end);
    uint64_t whole = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        whole = whole * 10 + (uint64_t)(*p - '0');
        ++p;
    }
    uint64_t frac = 0;
    if (p < end && *p == '.') {
        ++p;
        int digits = 0;
        while (p < end && *p >= '0' && *p <= '9' && digits < 2) {
            frac = frac * 10 + (uint64_t)(*p - '0');
            ++p;
            ++digits;
        }
        if (digits == 1) frac *= 10;        // "1.5" → 50, want 50
        // skip remaining frac digits
        while (p < end && *p >= '0' && *p <= '9') ++p;
    }
    *pp = p;
    return whole * 100 + frac;
}

// Locate first occurrence of `needle` (NUL-terminated) in [p, end).
// Returns pointer to start of needle, or end if not found.
[[nodiscard]] const char* find_str(const char* p, const char* end, const char* needle) noexcept {
    const size_t nlen = std::strlen(needle);
    if (nlen == 0 || (size_t)(end - p) < nlen)
        return end;
    for (const char* q = p; q + nlen <= end; ++q) {
        if (std::memcmp(q, needle, nlen) == 0)
            return q;
    }
    return end;
}

} // anonymous namespace

// ─── ProcGauges::init ─────────────────────────────────────────────────

std::optional<ProcGauges>
ProcGauges::init(::crucible::effects::Init) noexcept {
    ProcGauges p{};

    // Heap-allocated scratch buffer (avoid 128 KB stack alloc per
    // populate() call).  Fail load if alloc fails — caller gets
    // nullopt and can decide whether to proceed without proc gauges.
    p.scratch_ = std::unique_ptr<char[]>(new (std::nothrow) char[SCRATCH_BYTES]);
    if (!p.scratch_)
        return std::nullopt;

    // Open every always-available /proc + /sys file.  Failures per-file
    // are tolerated — populate() writes UNAVAILABLE for each unreadable
    // slot.
    p.fd_slabinfo_      = ScopedFd{open_ro("/proc/slabinfo")};
    p.fd_interrupts_    = ScopedFd{open_ro("/proc/interrupts")};
    p.fd_softnet_stat_  = ScopedFd{open_ro("/proc/net/softnet_stat")};
    p.fd_snmp_          = ScopedFd{open_ro("/proc/net/snmp")};
    p.fd_proc_net_tcp_  = ScopedFd{open_ro("/proc/net/tcp")};

    // /sys/block enumeration: open one fd per block device's `stat`
    // file at init() time.  Hot-plugged devices added later require
    // re-init.  Cap at MAX_BLOCK_DEVS = 32.
    DIR* sysblock = ::opendir("/sys/block");
    if (sysblock) {
        struct dirent* de;
        char path[256];
        while ((de = ::readdir(sysblock)) != nullptr &&
               p.num_block_devs_ < MAX_BLOCK_DEVS)
        {
            // Skip "." / ".."
            if (de->d_name[0] == '.' &&
                (de->d_name[1] == '\0' ||
                 (de->d_name[1] == '.' && de->d_name[2] == '\0'))) {
                continue;
            }
            // /sys/block/<name>/stat — at most ~250 bytes
            int n = std::snprintf(path, sizeof(path),
                                  "/sys/block/%s/stat", de->d_name);
            if (n <= 0 || (size_t)n >= sizeof(path))
                continue;
            int fd = open_ro(path);
            if (fd >= 0) {
                p.fd_block_stats_[p.num_block_devs_++] = ScopedFd{fd};
            }
        }
        ::closedir(sysblock);
    }

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    p.fd_vmstat_          = ScopedFd{open_ro("/proc/vmstat")};
    p.fd_loadavg_         = ScopedFd{open_ro("/proc/loadavg")};
    p.fd_pressure_cpu_    = ScopedFd{open_ro("/proc/pressure/cpu")};
    p.fd_pressure_memory_ = ScopedFd{open_ro("/proc/pressure/memory")};
    p.fd_pressure_io_     = ScopedFd{open_ro("/proc/pressure/io")};
#endif

    return p;
}

// ─── ProcGauges::open_count ───────────────────────────────────────────

std::size_t ProcGauges::open_count() const noexcept {
    std::size_t n = 0;
    if (fd_slabinfo_.valid())     ++n;
    if (fd_interrupts_.valid())   ++n;
    if (fd_softnet_stat_.valid()) ++n;
    if (fd_snmp_.valid())         ++n;
    if (fd_proc_net_tcp_.valid()) ++n;
    n += num_block_devs_;
#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    if (fd_vmstat_.valid())          ++n;
    if (fd_loadavg_.valid())         ++n;
    if (fd_pressure_cpu_.valid())    ++n;
    if (fd_pressure_memory_.valid()) ++n;
    if (fd_pressure_io_.valid())     ++n;
#endif
    return n;
}

// ─── Per-source readers ───────────────────────────────────────────────

uint64_t ProcGauges::read_slab_total_bytes_() const noexcept {
    if (!fd_slabinfo_.valid())
        return UNAVAILABLE;
    ssize_t n = pread_full(fd_slabinfo_.raw(), scratch_.get(), SCRATCH_BYTES);
    if (n <= 0)
        return UNAVAILABLE;
    // Format: "slabinfo - version: 2.1\n# name <active_objs> <num_objs> <objsize> ...\n"
    // Sum = Σ(num_objs × objsize).  Skip first 2 lines (header + comment).
    const char* p   = scratch_.get();
    const char* end = scratch_.get() + n;
    int header_skip = 2;
    uint64_t total  = 0;
    while (p < end) {
        // Find newline
        const char* nl = (const char*)std::memchr(p, '\n', (size_t)(end - p));
        if (!nl) break;
        if (header_skip > 0) {
            --header_skip;
            p = nl + 1;
            continue;
        }
        // Skip slab name (first whitespace-delimited token)
        const char* tok = skip_ws(p, nl);
        while (tok < nl && *tok != ' ' && *tok != '\t') ++tok;
        // Active objs (skip)
        (void)parse_u64(&tok, nl);
        // num_objs
        uint64_t num_objs = parse_u64(&tok, nl);
        // objsize
        uint64_t objsize  = parse_u64(&tok, nl);
        total += num_objs * objsize;
        p = nl + 1;
    }
    return total;
}

uint64_t ProcGauges::read_hardirq_total_count_() const noexcept {
    if (!fd_interrupts_.valid())
        return UNAVAILABLE;
    ssize_t n = pread_full(fd_interrupts_.raw(), scratch_.get(), SCRATCH_BYTES);
    if (n <= 0)
        return UNAVAILABLE;
    // Format: header line "            CPU0  CPU1 ..." then one line per
    // IRQ.  Each IRQ line: "  N:    <count_cpu0>   <count_cpu1>  ...
    // <handler_name>".  Sum all per-CPU counts across all rows.
    const char* p   = scratch_.get();
    const char* end = scratch_.get() + n;
    // Skip header line
    const char* nl = (const char*)std::memchr(p, '\n', (size_t)(end - p));
    if (!nl) return 0;
    p = nl + 1;
    uint64_t total = 0;
    while (p < end) {
        nl = (const char*)std::memchr(p, '\n', (size_t)(end - p));
        if (!nl) break;
        // Skip leading whitespace + "<irq>:" prefix
        const char* tok = skip_ws(p, nl);
        // Skip the IRQ number/name up to the colon
        while (tok < nl && *tok != ':') ++tok;
        if (tok < nl && *tok == ':') ++tok;
        // Sum per-CPU columns until we hit alpha (handler name)
        for (;;) {
            tok = skip_ws(tok, nl);
            if (tok >= nl || *tok < '0' || *tok > '9')
                break;
            total += parse_u64(&tok, nl);
        }
        p = nl + 1;
    }
    return total;
}

uint64_t ProcGauges::read_napi_poll_total_() const noexcept {
    if (!fd_softnet_stat_.valid())
        return UNAVAILABLE;
    ssize_t n = pread_full(fd_softnet_stat_.raw(), scratch_.get(), SCRATCH_BYTES);
    if (n <= 0)
        return UNAVAILABLE;
    // Format: one hex-formatted line per CPU.  Column 0 is
    // packets_processed (hex u32).  Sum across all CPUs.
    const char* p   = scratch_.get();
    const char* end = scratch_.get() + n;
    uint64_t total = 0;
    while (p < end) {
        const char* nl = (const char*)std::memchr(p, '\n', (size_t)(end - p));
        if (!nl) nl = end;
        // Parse first hex token
        const char* tok = skip_ws(p, nl);
        uint64_t v = 0;
        while (tok < nl &&
               ((*tok >= '0' && *tok <= '9') ||
                (*tok >= 'a' && *tok <= 'f') ||
                (*tok >= 'A' && *tok <= 'F'))) {
            uint64_t d = (*tok >= '0' && *tok <= '9') ? (uint64_t)(*tok - '0')
                       : (*tok >= 'a' && *tok <= 'f') ? (uint64_t)(*tok - 'a' + 10)
                                                      : (uint64_t)(*tok - 'A' + 10);
            v = (v << 4) | d;
            ++tok;
        }
        total += v;
        if (nl == end) break;
        p = nl + 1;
    }
    return total;
}

uint64_t ProcGauges::read_skb_drop_reason_total_() const noexcept {
    if (!fd_snmp_.valid())
        return UNAVAILABLE;
    ssize_t n = pread_full(fd_snmp_.raw(), scratch_.get(), SCRATCH_BYTES);
    if (n <= 0)
        return UNAVAILABLE;
    // /proc/net/snmp has paired header/value lines per protocol.  We
    // sum {Tcp.OutSegs, Udp.SndbufErrors, Icmp.OutDestUnreachs} as a
    // proxy for "drops at the IP layer".  Not perfect, but tracks the
    // skb drop pressure trend.
    const char* p   = scratch_.get();
    const char* end = scratch_.get() + n;
    // Find "Ip: "
    const char* ip_line = find_str(p, end, "Ip: ");
    if (ip_line == end) return 0;
    // Skip the header line (which starts with "Ip:" followed by names),
    // jump to the next "Ip: " (the value line).
    const char* nl = (const char*)std::memchr(ip_line, '\n', (size_t)(end - ip_line));
    if (!nl) return 0;
    const char* ip_values = find_str(nl + 1, end, "Ip: ");
    if (ip_values == end) return 0;
    // Sum per-line: "Ip: <fwd> <default_ttl> <inreceives> <inhdr_errors>
    //                 <inaddr_errors> <forwdatagrams> <inunknown_protos>
    //                 <indiscards> <indelivers> <outrequests> <outdiscards>
    //                 <outnoroutes> ..."
    // We want indiscards (col 7) + outdiscards (col 10).
    const char* tok = ip_values + 4;  // past "Ip: "
    uint64_t cols[16] = {};
    int idx = 0;
    const char* line_end = (const char*)std::memchr(tok, '\n', (size_t)(end - tok));
    if (!line_end) line_end = end;
    while (tok < line_end && idx < 16) {
        cols[idx++] = parse_u64(&tok, line_end);
    }
    return (idx > 7 ? cols[7] : 0) + (idx > 10 ? cols[10] : 0);
}

uint64_t ProcGauges::read_tcp_recv_buffer_max_() const noexcept {
    if (!fd_proc_net_tcp_.valid())
        return UNAVAILABLE;
    ssize_t n = pread_full(fd_proc_net_tcp_.raw(), scratch_.get(), SCRATCH_BYTES);
    if (n <= 0)
        return UNAVAILABLE;
    // Format: "  sl  local_address rem_address st tx_queue rx_queue tr ..."
    // tx_queue + rx_queue are hex "XXXXXXXX:XXXXXXXX".  We want max(rx_queue).
    const char* p   = scratch_.get();
    const char* end = scratch_.get() + n;
    // Skip header line
    const char* nl = (const char*)std::memchr(p, '\n', (size_t)(end - p));
    if (!nl) return 0;
    p = nl + 1;
    uint64_t max_rx = 0;
    while (p < end) {
        nl = (const char*)std::memchr(p, '\n', (size_t)(end - p));
        if (!nl) break;
        // Token 0: sl (decimal index with colon)
        const char* tok = skip_ws(p, nl);
        while (tok < nl && *tok != ' ' && *tok != '\t') ++tok;
        // Tokens 1, 2: local_address rem_address (hex:hex)
        for (int i = 0; i < 2; ++i) {
            tok = skip_ws(tok, nl);
            while (tok < nl && *tok != ' ' && *tok != '\t') ++tok;
        }
        // Token 3: st (hex state)
        tok = skip_ws(tok, nl);
        while (tok < nl && *tok != ' ' && *tok != '\t') ++tok;
        // Token 4: tx_queue:rx_queue
        tok = skip_ws(tok, nl);
        // Skip tx_queue (8 hex chars + ':')
        while (tok < nl && *tok != ':') ++tok;
        if (tok < nl && *tok == ':') ++tok;
        // Parse rx_queue (hex)
        uint64_t rx = 0;
        while (tok < nl &&
               ((*tok >= '0' && *tok <= '9') ||
                (*tok >= 'a' && *tok <= 'f') ||
                (*tok >= 'A' && *tok <= 'F'))) {
            uint64_t d = (*tok >= '0' && *tok <= '9') ? (uint64_t)(*tok - '0')
                       : (*tok >= 'a' && *tok <= 'f') ? (uint64_t)(*tok - 'a' + 10)
                                                      : (uint64_t)(*tok - 'A' + 10);
            rx = (rx << 4) | d;
            ++tok;
        }
        if (rx > max_rx) max_rx = rx;
        p = nl + 1;
    }
    return max_rx;
}

uint64_t ProcGauges::read_block_queue_depth_max_() const noexcept {
    if (num_block_devs_ == 0)
        return UNAVAILABLE;
    uint64_t max_depth = 0;
    bool any_ok = false;
    for (std::size_t i = 0; i < num_block_devs_; ++i) {
        int fd = fd_block_stats_[i].raw();
        if (fd < 0) continue;
        // Each /sys/block/<dev>/stat is small (~250 B) — share scratch.
        char buf[512];
        ssize_t n;
        do {
            n = ::pread(fd, buf, sizeof(buf) - 1, 0);
        } while (n == -1 && errno == EINTR);
        if (n <= 0) continue;
        buf[n] = '\0';
        // Format: "<read_ios> <read_merges> <read_sectors> <read_ticks>
        //          <write_ios> <write_merges> <write_sectors> <write_ticks>
        //          <in_flight> <io_ticks> <time_in_queue> ..."
        // We want column 8 (in_flight, 0-indexed).
        const char* p   = buf;
        const char* end = buf + n;
        for (int col = 0; col < 8; ++col) {
            (void)parse_u64(&p, end);
        }
        uint64_t in_flight = parse_u64(&p, end);
        if (in_flight > max_depth) max_depth = in_flight;
        any_ok = true;
    }
    return any_ok ? max_depth : UNAVAILABLE;
}

uint64_t ProcGauges::read_printk_ring_bytes_free_() const noexcept {
    // Reading /sys/kernel/debug/printk requires CAP_SYS_ADMIN and is
    // typically restricted.  Return UNAVAILABLE — a future PR can
    // probe /proc/kmsg or netlink kobject_uevent NETLINK_KOBJECT_UEVENT
    // for a non-debugfs proxy.
    return UNAVAILABLE;
}

uint64_t ProcGauges::read_thp_split_total_() const noexcept {
#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    if (!fd_vmstat_.valid())
        return UNAVAILABLE;
    ssize_t n = pread_full(fd_vmstat_.raw(), scratch_.get(), SCRATCH_BYTES);
    if (n <= 0)
        return UNAVAILABLE;
    // /proc/vmstat has lines "<name> <value>".  Sum thp_split_page +
    // thp_split_pmd + thp_split_pud (kernel 4.20+).
    const char* p   = scratch_.get();
    const char* end = scratch_.get() + n;
    uint64_t total = 0;
    static constexpr const char* k_keys[] = {
        "thp_split_page ", "thp_split_pmd ", "thp_split_pud "
    };
    for (const char* key : k_keys) {
        const char* hit = find_str(p, end, key);
        if (hit == end) continue;
        const char* tok = hit + std::strlen(key);
        const char* nl  = (const char*)std::memchr(tok, '\n', (size_t)(end - tok));
        if (!nl) nl = end;
        total += parse_u64(&tok, nl);
    }
    return total;
#else
    // Basic build: vmstat fd not opened; thp_split unavailable here.
    // Future basic-build addition could open /proc/vmstat unconditionally.
    return UNAVAILABLE;
#endif
}

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED

uint64_t ProcGauges::read_numa_hit_ratio_x100_() const noexcept {
    if (!fd_vmstat_.valid())
        return UNAVAILABLE;
    ssize_t n = pread_full(fd_vmstat_.raw(), scratch_.get(), SCRATCH_BYTES);
    if (n <= 0)
        return UNAVAILABLE;
    const char* p   = scratch_.get();
    const char* end = scratch_.get() + n;
    uint64_t hit = 0, miss = 0;
    const char* h = find_str(p, end, "numa_hit ");
    if (h != end) {
        const char* tok = h + 9;
        const char* nl  = (const char*)std::memchr(tok, '\n', (size_t)(end - tok));
        if (!nl) nl = end;
        hit = parse_u64(&tok, nl);
    }
    const char* m = find_str(p, end, "numa_miss ");
    if (m != end) {
        const char* tok = m + 10;
        const char* nl  = (const char*)std::memchr(tok, '\n', (size_t)(end - tok));
        if (!nl) nl = end;
        miss = parse_u64(&tok, nl);
    }
    uint64_t denom = hit + miss;
    return denom == 0 ? 0 : (hit * 10000) / denom;
}

uint64_t ProcGauges::read_tcp_established_current_() const noexcept {
    if (!fd_snmp_.valid())
        return UNAVAILABLE;
    ssize_t n = pread_full(fd_snmp_.raw(), scratch_.get(), SCRATCH_BYTES);
    if (n <= 0)
        return UNAVAILABLE;
    // "Tcp: " value line.  CurrEstab is column 8 (0-indexed).
    const char* p   = scratch_.get();
    const char* end = scratch_.get() + n;
    const char* tcp_hdr = find_str(p, end, "Tcp: ");
    if (tcp_hdr == end) return 0;
    const char* nl = (const char*)std::memchr(tcp_hdr, '\n', (size_t)(end - tcp_hdr));
    if (!nl) return 0;
    const char* tcp_val = find_str(nl + 1, end, "Tcp: ");
    if (tcp_val == end) return 0;
    const char* tok = tcp_val + 5;
    const char* line_end = (const char*)std::memchr(tok, '\n', (size_t)(end - tok));
    if (!line_end) line_end = end;
    uint64_t v = 0;
    for (int i = 0; i <= 8; ++i) {
        v = parse_u64(&tok, line_end);
    }
    return v;
}

void ProcGauges::read_loadavg_x100_(uint64_t& out_1m,
                                    uint64_t& out_5m,
                                    uint64_t& out_15m) const noexcept {
    out_1m = out_5m = out_15m = UNAVAILABLE;
    if (!fd_loadavg_.valid())
        return;
    char buf[256];
    ssize_t n;
    do {
        n = ::pread(fd_loadavg_.raw(), buf, sizeof(buf) - 1, 0);
    } while (n == -1 && errno == EINTR);
    if (n <= 0) return;
    buf[n] = '\0';
    const char* p   = buf;
    const char* end = buf + n;
    out_1m  = parse_fixed_x100(&p, end);
    out_5m  = parse_fixed_x100(&p, end);
    out_15m = parse_fixed_x100(&p, end);
}

uint64_t ProcGauges::read_pressure_avg10_x100_(int fd,
                                               const char* kind) const noexcept {
    if (fd < 0 || kind == nullptr)
        return UNAVAILABLE;
    char buf[512];
    ssize_t n;
    do {
        n = ::pread(fd, buf, sizeof(buf) - 1, 0);
    } while (n == -1 && errno == EINTR);
    if (n <= 0)
        return UNAVAILABLE;
    buf[n] = '\0';
    // PSI format:
    //   "some avg10=<f> avg60=<f> avg300=<f> total=<u64>"
    //   "full avg10=<f> avg60=<f> avg300=<f> total=<u64>"
    // We expose `some avg10` × 100 — captures any-task-blocked %.
    const char* p   = buf;
    const char* end = buf + n;
    const char* hit = find_str(p, end, kind);
    if (hit == end) return UNAVAILABLE;
    const char* tag = find_str(hit, end, "avg10=");
    if (tag == end) return UNAVAILABLE;
    const char* tok = tag + 6;
    return parse_fixed_x100(&tok, end);
}

#endif // CRUCIBLE_SENSE_HUB_EXTENDED

// ─── populate ─────────────────────────────────────────────────────────

void ProcGauges::populate(uint64_t* gauge_array,
                          std::size_t gauge_count) const noexcept {
    if (!gauge_array || gauge_count == 0)
        return;

    auto write = [&](Gauge g, uint64_t v) noexcept {
        const auto idx = static_cast<std::size_t>(g);
        if (idx < gauge_count)
            gauge_array[idx] = v;
    };

    // Basic build slots (16-22)
    write(Gauge::SLAB_TOTAL_BYTES,         read_slab_total_bytes_());
    write(Gauge::HARDIRQ_TOTAL_COUNT,      read_hardirq_total_count_());
    write(Gauge::NAPI_POLL_TOTAL,          read_napi_poll_total_());
    write(Gauge::SKB_DROP_REASON_TOTAL,    read_skb_drop_reason_total_());
    write(Gauge::TCP_RECV_BUFFER_MAX,      read_tcp_recv_buffer_max_());
    write(Gauge::BLOCK_QUEUE_DEPTH_MAX,    read_block_queue_depth_max_());
    write(Gauge::PRINTK_RING_BYTES_FREE,   read_printk_ring_bytes_free_());
    write(Gauge::THP_SPLIT,                read_thp_split_total_());

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    // Extended slots (49-56)
    write(Gauge::NUMA_HIT_RATIO_X100,      read_numa_hit_ratio_x100_());
    write(Gauge::TCP_ESTABLISHED_CURRENT,  read_tcp_established_current_());

    uint64_t l1, l5, l15;
    read_loadavg_x100_(l1, l5, l15);
    write(Gauge::LOAD_AVG_1M_X100,         l1);
    write(Gauge::LOAD_AVG_5M_X100,         l5);
    write(Gauge::LOAD_AVG_15M_X100,        l15);

    write(Gauge::PSI_CPU_SOME_AVG10_X100,
          read_pressure_avg10_x100_(fd_pressure_cpu_.raw(), "some"));
    write(Gauge::PSI_MEM_SOME_AVG10_X100,
          read_pressure_avg10_x100_(fd_pressure_memory_.raw(), "some"));
    write(Gauge::PSI_IO_SOME_AVG10_X100,
          read_pressure_avg10_x100_(fd_pressure_io_.raw(), "some"));
#endif
}

} // namespace crucible::perf
