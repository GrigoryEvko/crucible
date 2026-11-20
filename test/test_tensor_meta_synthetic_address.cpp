// Tests of mint_synthetic_data_ptr of crucible/TensorMeta.h: the data pointer
// that a test or a bench gives a descriptor in place of the data pointer of a
// front end.  The pointer is a cookie, so the test reads it only through the
// equality, the storage key and the address door of the descriptor.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

using ::crucible::SyntheticAddress;
using ::crucible::TensorMeta;
using ::fixy::check::pass;

// The descriptor of a Float tensor of 16 elements at the synthetic address.
[[nodiscard]] TensorMeta meta_at(SyntheticAddress address) noexcept {
    TensorMeta meta{};
    meta.dtype = ::crucible::ScalarType::Float;
    meta.ndim = 1;
    meta.sizes[0] = ::crucible::tensor_dim(16);
    meta.strides[0] = ::crucible::tensor_dim(1);
    meta.data_ptr = ::crucible::mint_synthetic_data_ptr(address);
    return meta;
}

// One address gives one descriptor and one storage key.  Two addresses give
// two descriptors and two storage keys.
void storage_keys(::fixy::TestRunnerCtx const& test) noexcept {
    TensorMeta const first = meta_at(SyntheticAddress{0x10000});
    TensorMeta const again = meta_at(SyntheticAddress{0x10000});
    TensorMeta const second = meta_at(SyntheticAddress{0x20000});
    CRUCIBLE_TEST_ASSERT(test, first == again);
    CRUCIBLE_TEST_ASSERT(test, ::crucible::storage_key(first) == ::crucible::storage_key(again));
    CRUCIBLE_TEST_ASSERT(test, !(first == second));
    CRUCIBLE_TEST_ASSERT(test, !(::crucible::storage_key(first) == ::crucible::storage_key(second)));
    pass(test, "  storage_keys: PASSED\n");
}

// A synthetic address is not zero, so its descriptor has a data address.  A
// descriptor with no data pointer has none.
void data_addresses(::fixy::TestRunnerCtx const& test) noexcept {
    CRUCIBLE_TEST_ASSERT(test, ::crucible::data_address(meta_at(SyntheticAddress{0x10000})).is_some());
    CRUCIBLE_TEST_ASSERT(test, ::crucible::data_address(TensorMeta{}).is_none());
    pass(test, "  data_addresses: PASSED\n");
}

// The zero address breaks the precondition of the mint at run time too, and
// the process ends.
void zero_address_ends_the_process(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            if (::crucible::data_address(meta_at(SyntheticAddress{0})).is_some()) {
                ::fixy::fatal("the zero address gave a data address");
            }
        },
        ::fixy::TextView{"foundation: contract violation: address != SyntheticAddress{}"});
    pass(test, "  zero_address_ends_the_process: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    storage_keys(test);
    data_addresses(test);
    zero_address_ends_the_process(test);
    pass(test, "test_tensor_meta_synthetic_address: all tests passed\n");
    return 0;
}
