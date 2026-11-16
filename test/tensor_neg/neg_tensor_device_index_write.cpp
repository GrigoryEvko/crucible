// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: a write gives the index 0 to a built device of the host.  The
// two members of TensorDevice are private, so only its constructor sets the
// index, and the constructor gives each index of the host as -1.  A second
// index of the host makes the equality and each hash of one device disagree.
//
// The companion fixture neg_tensor_device_designated_host_zero.cpp builds the
// device with a designated initializer.

#include <crucible/Types.h>

int main() {
    crucible::TensorDevice device{crucible::DeviceType::CPU, -1};
    device.index_ = 0;
    return device.index();
}
