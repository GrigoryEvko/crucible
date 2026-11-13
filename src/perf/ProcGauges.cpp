#include <crucible/perf/ProcGauges.h>
#include <crucible/perf/SenseHubV2.h>

#include <foundation/os/Directory.h>
#include <foundation/os/File.h>

#include <utility>

namespace crucible::perf {

namespace {

constexpr ::fixy::CText kSlabinfoPath{"/proc/slabinfo"};
constexpr ::fixy::CText kInterruptsPath{"/proc/interrupts"};
constexpr ::fixy::CText kSoftnetStatPath{"/proc/net/softnet_stat"};
constexpr ::fixy::CText kSnmpPath{"/proc/net/snmp"};
constexpr ::fixy::CText kProcNetTcpPath{"/proc/net/tcp"};
constexpr ::fixy::CText kSysBlockPath{"/sys/block"};

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
constexpr ::fixy::CText kVmstatPath{"/proc/vmstat"};
constexpr ::fixy::CText kLoadavgPath{"/proc/loadavg"};
constexpr ::fixy::CText kPressureCpuPath{"/proc/pressure/cpu"};
constexpr ::fixy::CText kPressureMemoryPath{"/proc/pressure/memory"};
constexpr ::fixy::CText kPressureIoPath{"/proc/pressure/io"};
#endif

// A block statistics file and a pressure file hold one or two short lines.
using SmallText = ::fixy::FixedText<512>;

[[nodiscard]] constexpr bool is_blank(char character) noexcept { return character == ' ' || character == '\t'; }

[[nodiscard]] constexpr bool is_not_blank(char character) noexcept { return !is_blank(character); }

[[nodiscard]] constexpr bool is_decimal_digit(char character) noexcept { return character >= '0' && character <= '9'; }

[[nodiscard]] constexpr uint64_t decimal_value(char character) noexcept {
    return static_cast<uint64_t>(character - '0');
}

[[nodiscard]] constexpr bool is_hex_digit(char character) noexcept {
    return is_decimal_digit(character) || (character >= 'a' && character <= 'f')
        || (character >= 'A' && character <= 'F');
}

[[nodiscard]] constexpr uint64_t hex_value(char character) noexcept {
    if (is_decimal_digit(character)) return decimal_value(character);
    if (character >= 'a' && character <= 'f') return static_cast<uint64_t>(character - 'a' + 10);
    return static_cast<uint64_t>(character - 'A' + 10);
}

// The first count characters of text.  The caller keeps count at most the
// size of the text.
[[nodiscard]] ::fixy::TextView prefix(::fixy::TextView text, std::size_t count) noexcept {
    return text.window(0, count).value_or(::fixy::TextView{});
}

// The characters of text after its first count characters, or no character
// when the text holds fewer.
[[nodiscard]] ::fixy::TextView suffix(::fixy::TextView text, std::size_t count) noexcept {
    std::size_t const start = ::fixy::min(count, text.size());
    return text.window(start, text.size() - start).value_or(::fixy::TextView{});
}

// The count of the leading characters of text that satisfy predicate.
// Complexity: linear in that count.
template <class Predicate>
[[nodiscard]] std::size_t leading_count(::fixy::TextView text, Predicate predicate) noexcept {
    std::size_t count = 0;
    for (char const character : text) {
        if (!predicate(character)) break;
        ++count;
    }
    return count;
}

// The position of the first character in text, or the size of the text when
// it holds none.
[[nodiscard]] std::size_t position_of(::fixy::TextView text, char character) noexcept {
    return ::fixy::find(text, character).value_or(text.size());
}

// The position of the first needle in text, or the size of the text when it
// holds none.  The needle is not empty.
[[nodiscard]] std::size_t position_of(::fixy::TextView text, ::fixy::TextView needle) noexcept {
    return ::fixy::find(text, needle).value_or(text.size());
}

// Drops the first count lines of text, with their newlines.  A text with
// fewer lines becomes empty.
[[nodiscard]] ::fixy::TextView after_lines(::fixy::TextView text, std::size_t count) noexcept {
    for (std::size_t line = 0; line < count; ++line) {
        text = suffix(text, position_of(text, '\n') + 1);
    }
    return text;
}

// Calls visit with each line of text that a newline ends, in order, without
// its newline.  A last line with no newline is not visited.
template <class Visit>
void for_each_line(::fixy::TextView text, Visit visit) noexcept {
    for (std::size_t newline = position_of(text, '\n'); newline < text.size(); newline = position_of(text, '\n')) {
        visit(prefix(text, newline));
        text = suffix(text, newline + 1);
    }
}

// The line of text from its start up to the first newline, or the whole text
// when it holds no newline.
[[nodiscard]] ::fixy::TextView line_of(::fixy::TextView text) noexcept { return prefix(text, position_of(text, '\n')); }

void skip_blanks(::fixy::TextView& text) noexcept { text = suffix(text, leading_count(text, is_blank)); }

// Moves past the run of characters before the next blank.
void skip_word(::fixy::TextView& text) noexcept { text = suffix(text, leading_count(text, is_not_blank)); }

// Moves past the first stop, or to the end when the text holds none.
void skip_past(::fixy::TextView& text, char stop) noexcept { text = suffix(text, position_of(text, stop) + 1); }

// The decimal number after the blanks at the start of text, or zero when no
// digit follows them.  The text then starts after the last digit.
[[nodiscard]] uint64_t take_decimal(::fixy::TextView& text) noexcept {
    skip_blanks(text);
    std::size_t const count = leading_count(text, is_decimal_digit);
    uint64_t value = 0;
    for (char const character : prefix(text, count)) {
        value = value * 10 + decimal_value(character);
    }
    text = suffix(text, count);
    return value;
}

// The hex digits at the start of text, the form in which
// /proc/net/softnet_stat and /proc/net/tcp write their counters.  The text
// then starts after the last digit.
[[nodiscard]] uint64_t take_hex(::fixy::TextView& text) noexcept {
    std::size_t const count = leading_count(text, is_hex_digit);
    uint64_t value = 0;
    for (char const character : prefix(text, count)) {
        value = (value << 4) | hex_value(character);
    }
    text = suffix(text, count);
    return value;
}

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
// Reads a decimal with up to two fractional digits, such as a load average
// or a pressure figure, as hundredths.
[[nodiscard]] uint64_t take_fixed_x100(::fixy::TextView& text) noexcept {
    uint64_t const whole = take_decimal(text);
    uint64_t fraction = 0;
    if (::fixy::starts_with(text, ".")) {
        text = suffix(text, 1);
        std::size_t const digits = ::fixy::min(leading_count(text, is_decimal_digit), std::size_t{2});
        for (char const character : prefix(text, digits)) {
            fraction = fraction * 10 + decimal_value(character);
        }
        // A single fractional digit counts tenths, so scale it to hundredths.
        if (digits == 1) fraction *= 10;
        text = suffix(text, leading_count(text, is_decimal_digit));
    }
    return whole * 100 + fraction;
}

// The number that follows key on its line, or zero when the text holds no
// such key.
[[nodiscard]] uint64_t value_after_key(::fixy::TextView text, ::fixy::TextView key) noexcept {
    std::size_t const hit = position_of(text, key);
    if (hit == text.size()) return 0;
    ::fixy::TextView value = line_of(suffix(text, hit + key.size()));
    return take_decimal(value);
}
#endif

// True when the file at path can be read now.
[[nodiscard]] bool is_readable(::fixy::CText path) noexcept {
    SmallText probe{};
    return ::foundation::os::read_file(path, probe).is_ok();
}

}  // anonymous namespace

ProcGauges::ProcGauges(::fixy::Box<ScratchText> scratch) noexcept : scratch_{std::move(scratch)} {}

ProcGauges ProcGauges::init(::fixy::InitLoadCtx const& init) noexcept {
    ProcGauges gauges{::fixy::mint_box<ScratchText>(init.cap().alloc)};

    gauges.is_slabinfo_readable_ = is_readable(kSlabinfoPath);
    gauges.is_interrupts_readable_ = is_readable(kInterruptsPath);
    gauges.is_softnet_stat_readable_ = is_readable(kSoftnetStatPath);
    gauges.is_snmp_readable_ = is_readable(kSnmpPath);
    gauges.is_proc_net_tcp_readable_ = is_readable(kProcNetTcpPath);

    // Every block device present now gets one path.  A device hot-plugged
    // afterwards is picked up only by a fresh init.  A host with no
    // /sys/block has no block device gauge.
    static_cast<void>(::foundation::os::for_each_entry(
        kSysBlockPath, [&gauges](::fixy::CText name) noexcept -> ::foundation::os::Visit {
            if (gauges.block_stat_paths_.size() == MAX_BLOCK_DEVS) return ::foundation::os::Visit::stop;
            BlockStatPath path{};
            if (path.append(kSysBlockPath).is_err() || path.append('/').is_err() || path.append(name).is_err()
                || path.append(::fixy::TextView{"/stat"}).is_err()) {
                return ::foundation::os::Visit::proceed;
            }
            if (!is_readable(path.c_text())) return ::foundation::os::Visit::proceed;
            static_cast<void>(gauges.block_stat_paths_.push(path).expect("the list has room for one more path"));
            return ::foundation::os::Visit::proceed;
        }));

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    gauges.is_vmstat_readable_ = is_readable(kVmstatPath);
    gauges.is_loadavg_readable_ = is_readable(kLoadavgPath);
    gauges.is_pressure_cpu_readable_ = is_readable(kPressureCpuPath);
    gauges.is_pressure_memory_readable_ = is_readable(kPressureMemoryPath);
    gauges.is_pressure_io_readable_ = is_readable(kPressureIoPath);
#endif

    return gauges;
}

std::size_t ProcGauges::readable_count() const noexcept {
    std::size_t count = 0;
    if (is_slabinfo_readable_) ++count;
    if (is_interrupts_readable_) ++count;
    if (is_softnet_stat_readable_) ++count;
    if (is_snmp_readable_) ++count;
    if (is_proc_net_tcp_readable_) ++count;
    count += block_stat_paths_.size();
#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    if (is_vmstat_readable_) ++count;
    if (is_loadavg_readable_) ++count;
    if (is_pressure_cpu_readable_) ++count;
    if (is_pressure_memory_readable_) ++count;
    if (is_pressure_io_readable_) ++count;
#endif
    return count;
}

::fixy::TextView ProcGauges::read_scratch_(::fixy::CText path) noexcept {
    ScratchText& text = *scratch_;
    if (::foundation::os::read_file(path, text).is_err() || text.size() == SCRATCH_BYTES) return ::fixy::TextView{};
    return text.view();
}

uint64_t ProcGauges::read_slab_total_bytes_() noexcept {
    if (!is_slabinfo_readable_) return UNAVAILABLE;
    ::fixy::TextView const text = read_scratch_(kSlabinfoPath);
    if (text.size() == 0) return UNAVAILABLE;
    // The file opens with a version line and a column-name comment line, then
    // carries one line per cache:
    //   "<name> <active_objs> <num_objs> <objsize> ..."
    uint64_t total = 0;
    for_each_line(after_lines(text, 2), [&total](::fixy::TextView line) noexcept {
        skip_blanks(line);
        skip_word(line);
        static_cast<void>(take_decimal(line));
        uint64_t const num_objs = take_decimal(line);
        uint64_t const objsize = take_decimal(line);
        total += num_objs * objsize;
    });
    return total;
}

uint64_t ProcGauges::read_hardirq_total_count_() noexcept {
    if (!is_interrupts_readable_) return UNAVAILABLE;
    ::fixy::TextView const text = read_scratch_(kInterruptsPath);
    if (text.size() == 0) return UNAVAILABLE;
    // A header line of CPU names comes first, then one line per interrupt:
    //   "  <N>:  <count_cpu0>  <count_cpu1>  ...  <handler_name>"
    uint64_t total = 0;
    for_each_line(after_lines(text, 1), [&total](::fixy::TextView line) noexcept {
        skip_blanks(line);
        skip_past(line, ':');
        // The handler name follows the last count, so the first non-digit
        // ends the per-CPU columns.
        for (;;) {
            skip_blanks(line);
            if (leading_count(line, is_decimal_digit) == 0) break;
            total += take_decimal(line);
        }
    });
    return total;
}

uint64_t ProcGauges::read_napi_poll_total_() noexcept {
    if (!is_softnet_stat_readable_) return UNAVAILABLE;
    ::fixy::TextView rest = read_scratch_(kSoftnetStatPath);
    if (rest.size() == 0) return UNAVAILABLE;
    // One hex-formatted line per CPU.  Column 0 is packets_processed.  The
    // last line counts also when no newline ends it.
    uint64_t total = 0;
    while (rest.size() > 0) {
        std::size_t const newline = position_of(rest, '\n');
        ::fixy::TextView line = prefix(rest, newline);
        skip_blanks(line);
        total += take_hex(line);
        rest = suffix(rest, newline + 1);
    }
    return total;
}

uint64_t ProcGauges::read_skb_drop_reason_total_() noexcept {
    if (!is_snmp_readable_) return UNAVAILABLE;
    ::fixy::TextView const text = read_scratch_(kSnmpPath);
    if (text.size() == 0) return UNAVAILABLE;
    // The file pairs a header line of column names with a value line, per
    // protocol, and both lines open with the same protocol prefix.  The
    // second "Ip: " is therefore the value line.
    std::size_t const header = position_of(text, ::fixy::TextView{"Ip: "});
    if (header == text.size()) return 0;
    ::fixy::TextView const after_header = suffix(text, header);
    std::size_t const header_end = position_of(after_header, '\n');
    if (header_end == after_header.size()) return 0;
    ::fixy::TextView const rest = suffix(after_header, header_end + 1);
    std::size_t const values = position_of(rest, ::fixy::TextView{"Ip: "});
    if (values == rest.size()) return 0;
    // The Ip columns run: <fwd> <default_ttl> <inreceives> <inhdr_errors>
    // <inaddr_errors> <forwdatagrams> <inunknown_protos> <indiscards>
    // <indelivers> <outrequests> <outdiscards> <outnoroutes> ...
    // The two discard counters are columns 7 and 10.  The walk reads at
    // most 16 columns.
    ::fixy::TextView line = line_of(suffix(rest, values + 4));
    uint64_t in_discards = 0;
    uint64_t out_discards = 0;
    for (std::size_t column = 0; line.size() > 0 && column < 16; ++column) {
        uint64_t const value = take_decimal(line);
        if (column == 7) in_discards = value;
        if (column == 10) out_discards = value;
    }
    return in_discards + out_discards;
}

uint64_t ProcGauges::read_tcp_recv_buffer_max_() noexcept {
    if (!is_proc_net_tcp_readable_) return UNAVAILABLE;
    ::fixy::TextView const text = read_scratch_(kProcNetTcpPath);
    if (text.size() == 0) return UNAVAILABLE;
    // A header line comes first, then one line per socket:
    //   "  sl  local_address rem_address st tx_queue rx_queue tr ..."
    // The queue pair is one hex field of the form "XXXXXXXX:XXXXXXXX".
    uint64_t max_rx = 0;
    for_each_line(after_lines(text, 1), [&max_rx](::fixy::TextView line) noexcept {
        // sl, local_address, rem_address and st
        for (int field = 0; field < 4; ++field) {
            skip_blanks(line);
            skip_word(line);
        }
        // tx_queue, up to the colon
        skip_blanks(line);
        skip_past(line, ':');
        // rx_queue
        max_rx = ::fixy::max(max_rx, take_hex(line));
    });
    return max_rx;
}

uint64_t ProcGauges::read_block_queue_depth_max_() const noexcept {
    if (block_stat_paths_.size() == 0) return UNAVAILABLE;
    uint64_t max_depth = 0;
    bool is_any_read = false;
    for (BlockStatPath const& path : block_stat_paths_) {
        SmallText stat{};
        if (::foundation::os::read_file(path.c_text(), stat).is_err() || stat.size() == 0) continue;
        // The columns run: <read_ios> <read_merges> <read_sectors>
        // <read_ticks> <write_ios> <write_merges> <write_sectors>
        // <write_ticks> <in_flight> <io_ticks> <time_in_queue> ...
        // in_flight is column 8, counting from zero.
        ::fixy::TextView columns = stat.view();
        for (int column = 0; column < 8; ++column) {
            static_cast<void>(take_decimal(columns));
        }
        max_depth = ::fixy::max(max_depth, take_decimal(columns));
        is_any_read = true;
    }
    return is_any_read ? max_depth : UNAVAILABLE;
}

uint64_t ProcGauges::read_printk_ring_bytes_free_() const noexcept {
    // Reading the debugfs printk file demands CAP_SYS_ADMIN and is restricted
    // on a normal system, so there is no reading to report.
    return UNAVAILABLE;
}

uint64_t ProcGauges::read_thp_split_total_() noexcept {
#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    if (!is_vmstat_readable_) return UNAVAILABLE;
    ::fixy::TextView const text = read_scratch_(kVmstatPath);
    if (text.size() == 0) return UNAVAILABLE;
    // Every line of the file has the form "<name> <value>".
    return value_after_key(text, ::fixy::TextView{"thp_split_page "})
         + value_after_key(text, ::fixy::TextView{"thp_split_pmd "})
         + value_after_key(text, ::fixy::TextView{"thp_split_pud "});
#else
    // The vmstat file is read only in the extended build.
    return UNAVAILABLE;
#endif
}

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED

uint64_t ProcGauges::read_numa_hit_ratio_x100_() noexcept {
    if (!is_vmstat_readable_) return UNAVAILABLE;
    ::fixy::TextView const text = read_scratch_(kVmstatPath);
    if (text.size() == 0) return UNAVAILABLE;
    uint64_t const hit = value_after_key(text, ::fixy::TextView{"numa_hit "});
    uint64_t const miss = value_after_key(text, ::fixy::TextView{"numa_miss "});
    uint64_t const denom = hit + miss;
    return denom == 0 ? 0 : (hit * 10000) / denom;
}

uint64_t ProcGauges::read_tcp_established_current_() noexcept {
    if (!is_snmp_readable_) return UNAVAILABLE;
    ::fixy::TextView const text = read_scratch_(kSnmpPath);
    if (text.size() == 0) return UNAVAILABLE;
    // The second "Tcp: " line carries the values.  CurrEstab is column 8,
    // counting from zero.
    std::size_t const header = position_of(text, ::fixy::TextView{"Tcp: "});
    if (header == text.size()) return 0;
    ::fixy::TextView const after_header = suffix(text, header);
    std::size_t const header_end = position_of(after_header, '\n');
    if (header_end == after_header.size()) return 0;
    ::fixy::TextView const rest = suffix(after_header, header_end + 1);
    std::size_t const values = position_of(rest, ::fixy::TextView{"Tcp: "});
    if (values == rest.size()) return 0;
    ::fixy::TextView line = line_of(suffix(rest, values + 5));
    uint64_t value = 0;
    for (int column = 0; column <= 8; ++column) {
        value = take_decimal(line);
    }
    return value;
}

ProcGauges::LoadAverages ProcGauges::read_loadavg_x100_() const noexcept {
    if (!is_loadavg_readable_) return LoadAverages{};
    ::fixy::FixedText<256> text{};
    if (::foundation::os::read_file(kLoadavgPath, text).is_err() || text.size() == 0) return LoadAverages{};
    ::fixy::TextView rest = text.view();
    LoadAverages averages{};
    averages.one_minute_x100 = take_fixed_x100(rest);
    averages.five_minutes_x100 = take_fixed_x100(rest);
    averages.fifteen_minutes_x100 = take_fixed_x100(rest);
    return averages;
}

uint64_t ProcGauges::read_pressure_avg10_x100_(bool is_source_readable, ::fixy::CText path,
                                               ::fixy::TextView kind) const noexcept {
    if (!is_source_readable) return UNAVAILABLE;
    SmallText text{};
    if (::foundation::os::read_file(path, text).is_err() || text.size() == 0) return UNAVAILABLE;
    // A pressure file carries two lines:
    //   "some avg10=<f> avg60=<f> avg300=<f> total=<u64>"
    //   "full avg10=<f> avg60=<f> avg300=<f> total=<u64>"
    // The "some" line covers any task blocked, where "full" covers only the
    // case of every task blocked.
    ::fixy::TextView const view = text.view();
    std::size_t const hit = position_of(view, kind);
    if (hit == view.size()) return UNAVAILABLE;
    ::fixy::TextView const from_kind = suffix(view, hit);
    std::size_t const tag = position_of(from_kind, ::fixy::TextView{"avg10="});
    if (tag == from_kind.size()) return UNAVAILABLE;
    ::fixy::TextView value = suffix(from_kind, tag + 6);
    return take_fixed_x100(value);
}

#endif  // CRUCIBLE_SENSE_HUB_EXTENDED

void ProcGauges::populate(::fixy::View<uint64_t> gauges) noexcept {
    using v2::Gauge;
    auto const write = [&gauges](Gauge gauge, uint64_t value) noexcept {
        for (auto const& slot : gauges.get(static_cast<std::size_t>(gauge))) {
            *slot = value;
        }
    };

    write(Gauge::SLAB_TOTAL_BYTES, read_slab_total_bytes_());
    write(Gauge::HARDIRQ_TOTAL_COUNT, read_hardirq_total_count_());
    write(Gauge::NAPI_POLL_TOTAL, read_napi_poll_total_());
    write(Gauge::SKB_DROP_REASON_TOTAL, read_skb_drop_reason_total_());
    write(Gauge::TCP_RECV_BUFFER_MAX, read_tcp_recv_buffer_max_());
    write(Gauge::BLOCK_QUEUE_DEPTH_MAX, read_block_queue_depth_max_());
    write(Gauge::PRINTK_RING_BYTES_FREE, read_printk_ring_bytes_free_());
    write(Gauge::THP_SPLIT, read_thp_split_total_());

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    write(Gauge::NUMA_HIT_RATIO_X100, read_numa_hit_ratio_x100_());
    write(Gauge::TCP_ESTABLISHED_CURRENT, read_tcp_established_current_());

    LoadAverages const averages = read_loadavg_x100_();
    write(Gauge::LOAD_AVG_1M_X100, averages.one_minute_x100);
    write(Gauge::LOAD_AVG_5M_X100, averages.five_minutes_x100);
    write(Gauge::LOAD_AVG_15M_X100, averages.fifteen_minutes_x100);

    write(Gauge::PSI_CPU_SOME_AVG10_X100,
          read_pressure_avg10_x100_(is_pressure_cpu_readable_, kPressureCpuPath, ::fixy::TextView{"some"}));
    write(Gauge::PSI_MEM_SOME_AVG10_X100,
          read_pressure_avg10_x100_(is_pressure_memory_readable_, kPressureMemoryPath, ::fixy::TextView{"some"}));
    write(Gauge::PSI_IO_SOME_AVG10_X100,
          read_pressure_avg10_x100_(is_pressure_io_readable_, kPressureIoPath, ::fixy::TextView{"some"}));
#endif
}

}  // namespace crucible::perf
