#pragma once

#include <crucible/algebra/lattices/_VendorLattice.h>

#include <atomic>
#include <cstdint>

namespace crucible::mimic {

using ::crucible::algebra::lattices::VendorBackend;

struct DeviceSemaphore {
    VendorBackend backend = VendorBackend::CPU;
    std::uint64_t native_handle = 0;
    std::atomic<std::uint64_t>* value = nullptr;
};

namespace detail {

inline void semaphore_signal_oracle(DeviceSemaphore sem, std::uint64_t value) noexcept {
    sem.value->store(value, std::memory_order_release);
}

[[nodiscard]] inline bool semaphore_poll_oracle(DeviceSemaphore sem, std::uint64_t expected) noexcept {
    return sem.value->load(std::memory_order_acquire) >= expected;
}

}  // namespace detail

namespace cpu {
inline void semaphore_signal(DeviceSemaphore sem, std::uint64_t value) noexcept {
    detail::semaphore_signal_oracle(sem, value);
}
[[nodiscard]] inline bool semaphore_wait(DeviceSemaphore sem, std::uint64_t expected) noexcept {
    return detail::semaphore_poll_oracle(sem, expected);
}
}  // namespace cpu

namespace nv {
inline void semaphore_signal(DeviceSemaphore sem, std::uint64_t value) noexcept {
    detail::semaphore_signal_oracle(sem, value);
}
[[nodiscard]] inline bool semaphore_wait(DeviceSemaphore sem, std::uint64_t expected) noexcept {
    return detail::semaphore_poll_oracle(sem, expected);
}
}  // namespace nv

namespace amd {
inline void semaphore_signal(DeviceSemaphore sem, std::uint64_t value) noexcept {
    detail::semaphore_signal_oracle(sem, value);
}
[[nodiscard]] inline bool semaphore_wait(DeviceSemaphore sem, std::uint64_t expected) noexcept {
    return detail::semaphore_poll_oracle(sem, expected);
}
}  // namespace amd

namespace tpu {
inline void semaphore_signal(DeviceSemaphore sem, std::uint64_t value) noexcept {
    detail::semaphore_signal_oracle(sem, value);
}
[[nodiscard]] inline bool semaphore_wait(DeviceSemaphore sem, std::uint64_t expected) noexcept {
    return detail::semaphore_poll_oracle(sem, expected);
}
}  // namespace tpu

namespace trn {
inline void semaphore_signal(DeviceSemaphore sem, std::uint64_t value) noexcept {
    detail::semaphore_signal_oracle(sem, value);
}
[[nodiscard]] inline bool semaphore_wait(DeviceSemaphore sem, std::uint64_t expected) noexcept {
    return detail::semaphore_poll_oracle(sem, expected);
}
}  // namespace trn

namespace detail {

template <VendorBackend Backend>
inline void semaphore_signal(DeviceSemaphore sem, std::uint64_t value) noexcept {
    if constexpr (Backend == VendorBackend::NV) {
        nv::semaphore_signal(sem, value);
    } else if constexpr (Backend == VendorBackend::AMD) {
        amd::semaphore_signal(sem, value);
    } else if constexpr (Backend == VendorBackend::TPU) {
        tpu::semaphore_signal(sem, value);
    } else if constexpr (Backend == VendorBackend::TRN) {
        trn::semaphore_signal(sem, value);
    } else {
        cpu::semaphore_signal(sem, value);
    }
}

template <VendorBackend Backend>
[[nodiscard]] inline bool semaphore_wait(DeviceSemaphore sem, std::uint64_t expected) noexcept {
    if constexpr (Backend == VendorBackend::NV) {
        return nv::semaphore_wait(sem, expected);
    } else if constexpr (Backend == VendorBackend::AMD) {
        return amd::semaphore_wait(sem, expected);
    } else if constexpr (Backend == VendorBackend::TPU) {
        return tpu::semaphore_wait(sem, expected);
    } else if constexpr (Backend == VendorBackend::TRN) {
        return trn::semaphore_wait(sem, expected);
    } else {
        return cpu::semaphore_wait(sem, expected);
    }
}

}  // namespace detail

}  // namespace crucible::mimic
