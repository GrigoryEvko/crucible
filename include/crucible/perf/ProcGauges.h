#pragma once

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <foundation/os/PathText.h>

#include <cstddef>
#include <cstdint>

namespace crucible::perf {

namespace v2 {
enum class Gauge : uint32_t;
}  // namespace v2

// The kernel already aggregates these signals and publishes them under
// /proc and /sys.  A BPF tracepoint that counted the same events would
// pay per event for a number the kernel maintains anyway, so the files
// are read once per snapshot instead.
//
// Each read goes through the file door of foundation/os/File.h, which opens
// the file, reads it and closes it.  So the instance holds no descriptor.
class ProcGauges {
public:
    // A slot whose source file is unavailable reads this rather than
    // zero, so a missing signal stays distinguishable from a signal
    // that really is zero.
    static constexpr uint64_t UNAVAILABLE = ~uint64_t{0};

    // A host carries one block device per NVMe namespace plus a few
    // virtual loops, so 32 leaves headroom on a storage node.
    static constexpr std::size_t MAX_BLOCK_DEVS = 32;

    // A file that cannot be read at init is not fatal.  Its gauge slots
    // read UNAVAILABLE for the lifetime of the instance.
    [[nodiscard]] static ProcGauges init(::fixy::InitLoadCtx const& init) noexcept;

    // Writes each gauge into its slot of gauges.  A gauge whose slot is past
    // the end of gauges is not written.
    void populate(::fixy::View<uint64_t> gauges) noexcept;

    // The count of the sources that were readable at init.
    [[nodiscard]] std::size_t readable_count() const noexcept;

    ProcGauges(const ProcGauges&) = delete("ProcGauges owns its scratch buffer; a copy would copy 2 MiB");
    ProcGauges& operator=(const ProcGauges&) = delete("ProcGauges owns its scratch buffer; a copy would copy 2 MiB");
    ProcGauges(ProcGauges&&) noexcept = default;
    ProcGauges& operator=(ProcGauges&&) noexcept = default;
    ~ProcGauges() noexcept = default;

private:
    // One buffer serves every source, so it is sized for the largest
    // of them.  /proc/interrupts holds one column for each CPU on each
    // line, and it holds 800 KB on a host of 224 CPUs.
    static constexpr std::size_t SCRATCH_BYTES = 2 * 1024 * 1024;
    using ScratchText = ::fixy::FixedText<SCRATCH_BYTES>;

    // "/sys/block/" + the longest name of a directory entry + "/stat".
    static constexpr std::size_t BLOCK_STAT_PATH_BYTES = 11 + 255 + 5;
    using BlockStatPath = ::foundation::os::PathText<BLOCK_STAT_PATH_BYTES>;

    explicit ProcGauges(::fixy::Box<ScratchText> scratch) noexcept;

    // The scratch buffer lives on the heap so that no poll puts a buffer of
    // this size on the stack.
    ::fixy::Box<ScratchText> scratch_;

    bool is_slabinfo_readable_ = false;  // /proc/slabinfo
    bool is_interrupts_readable_ = false;  // /proc/interrupts
    bool is_softnet_stat_readable_ = false;  // /proc/net/softnet_stat
    bool is_snmp_readable_ = false;  // /proc/net/snmp
    bool is_proc_net_tcp_readable_ = false;  // /proc/net/tcp

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    bool is_vmstat_readable_ = false;  // /proc/vmstat
    bool is_loadavg_readable_ = false;  // /proc/loadavg
    // The three pressure files exist only on a kernel 4.20 or newer
    // built with CONFIG_PSI=y.
    bool is_pressure_cpu_readable_ = false;  // /proc/pressure/cpu
    bool is_pressure_memory_readable_ = false;  // /proc/pressure/memory
    bool is_pressure_io_readable_ = false;  // /proc/pressure/io
#endif

    // Every block device carries its own stat file.  The set is fixed at
    // init, and a device plugged in afterwards needs a fresh instance.
    ::fixy::FixedList<BlockStatPath, MAX_BLOCK_DEVS> block_stat_paths_{};

    // Reads the file at path into the scratch buffer.  The text is empty
    // when the read fails, and when the file fills the buffer, because the
    // file can then hold more than the buffer and a sum over part of it is
    // wrong.
    [[nodiscard]] ::fixy::TextView read_scratch_(::fixy::CText path) noexcept;

    // A reader that cannot read its file or cannot parse it returns
    // UNAVAILABLE.
    [[nodiscard]] uint64_t read_slab_total_bytes_() noexcept;
    [[nodiscard]] uint64_t read_hardirq_total_count_() noexcept;
    [[nodiscard]] uint64_t read_napi_poll_total_() noexcept;
    [[nodiscard]] uint64_t read_skb_drop_reason_total_() noexcept;
    [[nodiscard]] uint64_t read_tcp_recv_buffer_max_() noexcept;
    [[nodiscard]] uint64_t read_block_queue_depth_max_() const noexcept;
    [[nodiscard]] uint64_t read_printk_ring_bytes_free_() const noexcept;

    [[nodiscard]] uint64_t read_thp_split_total_() noexcept;

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    [[nodiscard]] uint64_t read_numa_hit_ratio_x100_() noexcept;
    [[nodiscard]] uint64_t read_tcp_established_current_() noexcept;

    // The load averages of /proc/loadavg, in hundredths.
    struct LoadAverages {
        uint64_t one_minute_x100 = UNAVAILABLE;
        uint64_t five_minutes_x100 = UNAVAILABLE;
        uint64_t fifteen_minutes_x100 = UNAVAILABLE;
    };
    [[nodiscard]] LoadAverages read_loadavg_x100_() const noexcept;
    [[nodiscard]] uint64_t read_pressure_avg10_x100_(bool is_source_readable, ::fixy::CText path,
                                                     ::fixy::TextView kind) const noexcept;
#endif
};

}  // namespace crucible::perf
