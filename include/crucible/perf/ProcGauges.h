#pragma once

#include <crucible/effects/Capabilities.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/Linear.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace crucible::perf {

enum class Gauge : uint32_t;

// A descriptor holder local to this header rather than the shared
// linear-ownership wrapper.  The invariant here is one descriptor
// closed once, and stating it inline reads more cheaply than the
// cross-header indirection.
class ScopedFd {
public:
    ScopedFd() noexcept = default;
    explicit ScopedFd(int fd) noexcept : fd_{fd} {}
    ~ScopedFd() noexcept;
    ScopedFd(const ScopedFd&) = delete("ScopedFd owns a Linux file descriptor; copying would double-close on destruct");
    ScopedFd& operator=(const ScopedFd&) =
        delete("ScopedFd owns a Linux file descriptor; copying would double-close on destruct");
    ScopedFd(ScopedFd&& other) noexcept : fd_{other.fd_} { other.fd_ = -1; }
    ScopedFd& operator=(ScopedFd&& other) noexcept {
        if (this != &other) {
            close_();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int raw() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

private:
    int fd_ = -1;
    void close_() noexcept;
};

// The kernel already aggregates these signals and publishes them under
// /proc and /sys.  A BPF tracepoint that counted the same events would
// pay per event for a number the kernel maintains anyway, so the files
// are read once per snapshot instead.
class ProcGauges {
public:
    // A slot whose source file is unavailable reads this rather than
    // zero, so a missing signal stays distinguishable from a signal
    // that really is zero.
    static constexpr uint64_t UNAVAILABLE = static_cast<uint64_t>(-1);

    // A host carries one block device per NVMe namespace plus a few
    // virtual loops, so 32 leaves headroom on a storage node.
    static constexpr std::size_t MAX_BLOCK_DEVS = 32;

    // A file that fails to open is not fatal.  Its gauge slots read
    // UNAVAILABLE for the lifetime of the instance.
    [[nodiscard]] static std::optional<ProcGauges> init(::crucible::effects::Init) noexcept;

    void populate(uint64_t* gauge_array, std::size_t gauge_count) const noexcept;

    [[nodiscard]] std::size_t open_count() const noexcept;

    ProcGauges(const ProcGauges&) =
        delete("ProcGauges owns multiple ScopedFds + scratch buffer; copying would double-close all FDs");
    ProcGauges& operator=(const ProcGauges&) =
        delete("ProcGauges owns multiple ScopedFds + scratch buffer; copying would double-close all FDs");
    ProcGauges(ProcGauges&&) noexcept = default;
    ProcGauges& operator=(ProcGauges&&) noexcept = default;
    ~ProcGauges() noexcept = default;

private:
    ScopedFd fd_slabinfo_;  // /proc/slabinfo
    ScopedFd fd_interrupts_;  // /proc/interrupts
    ScopedFd fd_softnet_stat_;  // /proc/net/softnet_stat
    ScopedFd fd_snmp_;  // /proc/net/snmp
    ScopedFd fd_proc_net_tcp_;  // /proc/net/tcp

    // Every block device carries its own stat file, so one descriptor
    // per device is the only way to read them.  The set is fixed at
    // init, and a device plugged in afterwards needs a fresh instance.
    std::array<ScopedFd, MAX_BLOCK_DEVS> fd_block_stats_;
    std::size_t num_block_devs_ = 0;

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    ScopedFd fd_vmstat_;  // /proc/vmstat
    ScopedFd fd_loadavg_;  // /proc/loadavg
    // The three pressure files exist only on a kernel 4.20 or newer
    // built with CONFIG_PSI=y.
    ScopedFd fd_pressure_cpu_;  // /proc/pressure/cpu
    ScopedFd fd_pressure_memory_;  // /proc/pressure/memory
    ScopedFd fd_pressure_io_;  // /proc/pressure/io
#endif

    // One buffer serves every source, so it is sized for the largest
    // of them.  /proc/interrupts grows with core count and IRQ count
    // and reaches tens of kilobytes on a large host.
    static constexpr std::size_t SCRATCH_BYTES = 128 * 1024;
    std::unique_ptr<char[]> scratch_;

    ProcGauges() noexcept = default;

    // A reader that cannot open its file, cannot read it, or cannot
    // parse it returns UNAVAILABLE.
    [[nodiscard]] uint64_t read_slab_total_bytes_() const noexcept;
    [[nodiscard]] uint64_t read_hardirq_total_count_() const noexcept;
    [[nodiscard]] uint64_t read_napi_poll_total_() const noexcept;
    [[nodiscard]] uint64_t read_skb_drop_reason_total_() const noexcept;
    [[nodiscard]] uint64_t read_tcp_recv_buffer_max_() const noexcept;
    [[nodiscard]] uint64_t read_block_queue_depth_max_() const noexcept;
    [[nodiscard]] uint64_t read_printk_ring_bytes_free_() const noexcept;

    [[nodiscard]] uint64_t read_thp_split_total_() const noexcept;

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    [[nodiscard]] uint64_t read_numa_hit_ratio_x100_() const noexcept;
    [[nodiscard]] uint64_t read_tcp_established_current_() const noexcept;
    void read_loadavg_x100_(uint64_t& out_1m, uint64_t& out_5m, uint64_t& out_15m) const noexcept;
    // `kind` must be NUL-terminated.  The parser measures its length,
    // so a view over part of a longer buffer walks off the end.
    [[nodiscard]] uint64_t read_pressure_avg10_x100_(int fd, const char* kind) const noexcept;
#endif
};

}  // namespace crucible::perf
