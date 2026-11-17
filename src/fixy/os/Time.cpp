// The PTP door of fixy (fixy/os/Time.h): the query of the capabilities of a
// PTP hardware clock.  It is the one includer of <linux/ptp_clock.h>, and it
// sends the ioctl through the descriptor door of fixy/os/Fd.h.  It includes no
// header of the C library, because some declarations of the two kinds of
// header conflict.

#include <fixy/os/Time.h>

#include <fixy/os/Fd.h>
#include <foundation/core/Choice.h>
#include <foundation/os/Errno.h>

#include <linux/ptp_clock.h>

#include <cstdint>

namespace fixy::time::detail {

::foundation::core::Result<PtpClockCaps, ::foundation::os::Errno> read_ptp_caps_(int descriptor) noexcept {
    using Caps = ::foundation::core::Result<PtpClockCaps, ::foundation::os::Errno>;
    ::ptp_clock_caps kernel_caps{};
    return ::fixy::fs::detail::query_descriptor_(descriptor, PTP_CLOCK_GETCAPS2, &kernel_caps)
        .match(
            [&kernel_caps](::foundation::core::Unit) noexcept -> Caps {
                return PtpClockCaps{
                    .max_adjustment_ppb = kernel_caps.max_adj,
                    .alarms = kernel_caps.n_alarm,
                    .external_timestamp_channels = kernel_caps.n_ext_ts,
                    .periodic_outputs = kernel_caps.n_per_out,
                    .pins = kernel_caps.n_pins,
                    .max_phase_adjustment_ns = kernel_caps.max_phase_adj,
                    .pps = kernel_caps.pps != 0,
                    .cross_timestamping = kernel_caps.cross_timestamping != 0,
                    .adjust_phase = kernel_caps.adjust_phase != 0,
                };
            },
            [](::foundation::os::Errno error) noexcept -> Caps { return ::foundation::core::err(error); });
}

}  // namespace fixy::time::detail
