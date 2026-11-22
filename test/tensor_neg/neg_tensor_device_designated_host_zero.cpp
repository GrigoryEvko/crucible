// Negative-compile test. This file must fail to compile.
//
// Violation: a designated initializer builds the device of the host with the
// index 0.  TensorDevice is no aggregate, so no initializer list skips its
// constructor, which gives each index of the host as -1.  A second index of
// the host makes the equality and each hash of one device disagree.
//
// The companion fixture neg_tensor_device_index_write.cpp writes the index of
// a built device.

#include <crucible/Types.h>

int main() {
    crucible::TensorDevice const device{.type = crucible::DeviceType::CPU, .index = 0};
    return device.index();
}
