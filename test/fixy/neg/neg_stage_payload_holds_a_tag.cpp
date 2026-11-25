// Negative compile test.  This file must fail to compile.
//
// The payload of the consumer handle holds the tag of Block, so the body takes
// the tag out of each item.  The payload carries the row of the tag, and the
// context of the stage owns no Block, so the stage refuses the payload.
//
// Expected diagnostic: the row of the input is not admitted.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/Stage.h>
#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <utility>

namespace neg_stage_payload_holds_a_tag_types {

struct Wire {};

class TagPayload {
public:
    explicit TagPayload(::foundation::effects::cap::Block block) noexcept : block_{block} {}
    [[nodiscard]] ::foundation::effects::cap::Block block() const noexcept { return block_; }

private:
    ::foundation::effects::cap::Block block_;
};

struct TagConsumer {
    using channel_type = Wire;
    static constexpr std::size_t channel_capacity = 1;

    [[nodiscard]] ::fixy::Option<TagPayload> try_pop() noexcept { return {}; }
};

struct Producer {
    using channel_type = Wire;
    static constexpr std::size_t channel_capacity = 1;

    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

inline void body(TagConsumer&& /*in*/, Producer&& /*out*/) noexcept {}

}  // namespace neg_stage_payload_holds_a_tag_types

int main() {
    using namespace neg_stage_payload_holds_a_tag_types;
    ::fixy::BgDrainCtx const ctx{::foundation::effects::testing::bg()};
    [[maybe_unused]] auto refused = ::fixy::concurrent::mint_stage<&body>(ctx, TagConsumer{}, Producer{});
    return 0;
}
