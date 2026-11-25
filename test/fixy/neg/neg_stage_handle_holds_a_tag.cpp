// Negative compile test.  This file must fail to compile.
//
// A bare tag gives its effect to its holder.  The producer handle here keeps
// the tag of Nondet, and the context of the stage owns no Nondet, so the body
// could read the schedule.  So the stage refuses the handle.
//
// Expected diagnostic: the handles of the body reach an effect.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/Stage.h>
#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <utility>

namespace neg_stage_handle_holds_a_tag_types {

struct Wire {};

struct Consumer {
    using channel_type = Wire;
    static constexpr std::size_t channel_capacity = 1;

    [[nodiscard]] ::fixy::Option<int> try_pop() noexcept { return {}; }
};

class TagProducer {
public:
    using channel_type = Wire;
    static constexpr std::size_t channel_capacity = 1;

    explicit TagProducer(::foundation::effects::cap::Nondet nondet) noexcept : nondet_{nondet} {}
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] ::foundation::effects::cap::Nondet nondet() const noexcept { return nondet_; }

private:
    ::foundation::effects::cap::Nondet nondet_;
};

inline void body(Consumer&& /*in*/, TagProducer&& /*out*/) noexcept {}

}  // namespace neg_stage_handle_holds_a_tag_types

int main() {
    using namespace neg_stage_handle_holds_a_tag_types;
    namespace eff = ::foundation::effects;
    ::fixy::BgLoadCtx const wide{eff::testing::bg()};
    ::fixy::BgDrainCtx const ctx{eff::testing::bg()};
    [[maybe_unused]] auto refused = ::fixy::concurrent::mint_stage<&body>(
        ctx, Consumer{}, TagProducer{eff::extract_bare_from_ctx<eff::Effect::Nondet>(wide)});
    return 0;
}
