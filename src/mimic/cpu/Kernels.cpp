// The cold path of the kernels of crucible/mimic/cpu/Kernels.h.

#include <crucible/mimic/cpu/Kernels.h>

#include <fixy/Core.h>

namespace crucible::mimic::cpu::detail {

void refuse_views_(::fixy::TextView kernel) noexcept {
    ::fixy::fatal("the views of the kernel {} do not have the sizes of its op, and a plan checks them before a run",
                  kernel);
}

}  // namespace crucible::mimic::cpu::detail
