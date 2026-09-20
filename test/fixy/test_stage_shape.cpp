// The pipeline-stage shape, driven at run time and read against a real
// channel's handles.
//
// fixy/concurrent/StageShape.h keeps its static_assert wall, which reads
// the shape predicates against fake handles declared beside them.  Two
// things live here instead.
//
// The run-time read: the predicates consulted through a volatile bound,
// so the reads are not folded away.  That body was an inline smoke test
// in the old header, called by nothing.
//
// And the cell the wall cannot state: a stage body whose parameters are
// a real channel's ConsumerHandle and ProducerHandle is recognized as a
// stage, and the values the shape recovers are the channel's element
// types.  The old header's fakes prove the predicate matches the shape
// it was written for; this proves it matches the shape the channels
// actually have.

#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/StageShape.h>

#include <cstdio>
#include <optional>
#include <type_traits>

namespace {

namespace c = ::fixy::concurrent;

int g_failures = 0;

#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

// ── stage bodies over a real channel's handles ──────────────────────

struct StageShapeTag {};
using IntSpsc = c::PermissionedSpscChannel<int, 8, StageShapeTag>;
using FloatSpsc = c::PermissionedSpscChannel<float, 8, StageShapeTag>;

// A body that drains one real channel into another is a stage.  Nothing
// about these two handle types was written with StageShape in view.
void real_pass_through(IntSpsc::ConsumerHandle&&, IntSpsc::ProducerHandle&&) noexcept {}
static_assert(c::PipelineStage<&real_pass_through>);
static_assert(c::StageArity<&real_pass_through>::input_count == 1);
static_assert(c::StageArity<&real_pass_through>::output_count == 1);
static_assert(std::is_same_v<c::pipeline_stage_input_value_t<&real_pass_through>, int>);
static_assert(std::is_same_v<c::pipeline_stage_output_value_t<&real_pass_through>, int>);
static_assert(c::pipeline_stage_is_value_preserving_v<&real_pass_through>);

// A body that changes the element type is a stage that is not value
// preserving, and the shape recovers both element types.
void real_int_to_float(IntSpsc::ConsumerHandle&&, FloatSpsc::ProducerHandle&&) noexcept {}
static_assert(c::PipelineStage<&real_int_to_float>);
static_assert(std::is_same_v<c::pipeline_stage_input_value_t<&real_int_to_float>, int>);
static_assert(std::is_same_v<c::pipeline_stage_output_value_t<&real_int_to_float>, float>);
static_assert(!c::pipeline_stage_is_value_preserving_v<&real_int_to_float>);

// Two real inputs into one real output is variadic but not the
// one-to-one shape.
void real_fan_in(IntSpsc::ConsumerHandle&&, IntSpsc::ConsumerHandle&&, IntSpsc::ProducerHandle&&) noexcept {}
static_assert(c::VariadicPipelineStage<&real_fan_in>);
static_assert(!c::PipelineStage<&real_fan_in>);
static_assert(c::StageArity<&real_fan_in>::input_count == 2);
static_assert(c::StageArity<&real_fan_in>::output_count == 1);

// The producer ahead of the consumer breaks the data-flow order, with
// real handles as with fakes.
void real_out_of_order(IntSpsc::ProducerHandle&&, IntSpsc::ConsumerHandle&&) noexcept {}
static_assert(!c::VariadicPipelineStage<&real_out_of_order>);
static_assert(!c::PipelineStage<&real_out_of_order>);

// A real handle taken by lvalue reference is not a stage: a stage
// consumes its handles.
void real_lvalue_in(IntSpsc::ConsumerHandle&, IntSpsc::ProducerHandle&&) noexcept {}
static_assert(!c::PipelineStage<&real_lvalue_in>);

// The channel itself is not a handle, so a body taking channels is not
// a stage.
void real_takes_channels(IntSpsc&&, IntSpsc&&) noexcept {}
static_assert(!c::PipelineStage<&real_takes_channels>);

// ── the run-time reads ──────────────────────────────────────────────

template <typename T>
struct fake_consumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct fake_producer {
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

void f_one_to_one(fake_consumer<int>&&, fake_producer<int>&&) noexcept {}
void f_nullary() noexcept {}
void f_one_int(int) noexcept {}
void f_two_ints(int, int) noexcept {}
void f_three_params(int, int, int) noexcept {}
int f_returns_int(fake_consumer<int>&&, fake_producer<int>&&) noexcept { return 0; }

void every_predicate_reads_at_run_time() {
    volatile std::size_t const cap = 4;

    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT(c::PipelineStage<&f_one_to_one>);
        EXPECT(c::VariadicPipelineStage<&f_one_to_one>);
        EXPECT(!c::PipelineStage<&f_nullary>);
        EXPECT(!c::PipelineStage<&f_one_int>);
        EXPECT(!c::PipelineStage<&f_two_ints>);
        EXPECT(!c::PipelineStage<&f_three_params>);
        EXPECT(!c::PipelineStage<&f_returns_int>);

        EXPECT(c::StageArity<&f_one_to_one>::input_count == 1);
        EXPECT(c::StageArity<&f_one_to_one>::output_count == 1);
        EXPECT(c::StageArity<&f_one_to_one>::ordered);

        // The real handles read the same way as the fakes.
        EXPECT(c::PipelineStage<&real_pass_through>);
        EXPECT(c::PipelineStage<&real_int_to_float>);
        EXPECT(!c::PipelineStage<&real_out_of_order>);
        EXPECT(!c::PipelineStage<&real_lvalue_in>);
        EXPECT(c::StageArity<&real_fan_in>::input_count == 2);
    }
}

// A body the shape admitted can be called with the handles the shape
// named.  Without this the predicates could be describing a signature
// nothing can satisfy.
void the_recognized_stage_body_runs() {
    fake_consumer<int> in{};
    fake_producer<int> out{};
    f_one_to_one(std::move(in), std::move(out));

    // The real-handle body is not called: constructing a live
    // ConsumerHandle needs the channel's split permissions, and this
    // file's claim is about the shape, not about the drain.  The handles
    // are exercised in test_handle_traits.cpp.
    EXPECT(c::PipelineStage<&real_pass_through>);
}

}  // namespace

int main() {
    every_predicate_reads_at_run_time();
    the_recognized_stage_body_runs();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_stage_shape: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_stage_shape: the shape reads the same on fake and real channel handles\n");
    return 0;
}
