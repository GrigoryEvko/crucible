// A class outside the base states that no door builds its empty value.  Its
// public constructor takes a raw word and a voucher, and the voucher is a
// public class that does not copy, which each caller can build.  So the raw
// word of the empty value comes in.  A voucher is a key of the base with no
// public constructor, or a payload whose statement passes the check (THE
// CHECK OF THE STATEMENT of foundation/core/Choice.h).  So the walk of the
// constructors refuses the class, and the Option of the class does not
// compile.

#include <foundation/core/Choice.h>

#include <cstdint>

struct AnyVoucher {
    constexpr AnyVoucher() noexcept = default;
    AnyVoucher(AnyVoucher const&) = delete("a voucher does not copy");
};

class VoucherSlot {
    std::uint32_t raw_ = 0;
    struct EmptyTag {};
    constexpr explicit VoucherSlot(EmptyTag /*tag*/) noexcept {}

public:
    constexpr VoucherSlot(std::uint32_t raw, AnyVoucher const& /*voucher*/) noexcept : raw_{raw} {}

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr VoucherSlot niche_empty(::foundation::core::niche_key<VoucherSlot> const&) noexcept {
        return VoucherSlot{EmptyTag{}};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<VoucherSlot> const&) const noexcept {
        return raw_ == 0;
    }
};

int main() { return ::foundation::core::Option<VoucherSlot>::some(VoucherSlot{0u, AnyVoucher{}}).is_some() ? 0 : 1; }
