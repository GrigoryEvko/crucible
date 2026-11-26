// Ops 7 to 14 of one compiled iteration of test_compute_vit: the
// feed-forward block and the classification head, computed through the
// pool.

#include "compute_vit.h"

#include "cpu_kernels.h"

#include <crucible/CrucibleContext.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;
using ::foundation::core::side::LeftFactor;
using ::foundation::core::side::RightFactor;
using cpu::make_dim;
using cpu::make_input;
using cpu::read_input;
using cpu::write_output;
namespace roles = cpu::side;

namespace test_compute_vit {

namespace {

// The op of a matrix product of [rows, inner] with [inner, columns].
void run_mm(CompiledScope& scope, MatchedOp const& op, cpu::Dim<roles::Rows> rows, cpu::Dim<roles::Columns> columns,
            cpu::Dim<roles::Inner> inner) {
    write_output(kTest, scope,
                 [left = read_input(scope, op, InputPosition{0}), right = read_input(scope, op, InputPosition{1}),
                  &rows, &columns, &inner](auto out) noexcept {
                     cpu::mm(kTest, make_input<LeftFactor>(left), make_input<RightFactor>(right), std::move(out), rows,
                             columns, inner);
                 });
}

}  // namespace

void run_ffn_and_head_ops(CompiledScope& scope) {
    const MatchedOp ln2 = cpu::advance_op(kTest, scope, H_LN2, S_LN2, ReplayStatus::MATCH);
    write_output(kTest, scope,
                 [input = read_input(scope, ln2, InputPosition{0}), scale = read_input(scope, ln2, InputPosition{1}),
                  shift = read_input(scope, ln2, InputPosition{2})](auto out) noexcept {
                     cpu::layer_norm(kTest, input, make_input<roles::Scale>(scale), make_input<roles::Shift>(shift),
                                     std::move(out), make_dim<roles::Rows>(B * S), make_dim<roles::Width>(D));
                 });

    const MatchedOp ff1 = cpu::advance_op(kTest, scope, H_MMFF1, S_MMFF1, ReplayStatus::MATCH);
    run_mm(scope, ff1, make_dim<roles::Rows>(B * S), make_dim<roles::Columns>(D_FF), make_dim<roles::Inner>(D));

    const MatchedOp relu = cpu::advance_op(kTest, scope, H_RELU, S_RELU, ReplayStatus::MATCH);
    write_output(kTest, scope, [input = read_input(scope, relu, InputPosition{0})](auto out) noexcept {
        cpu::relu(kTest, input, std::move(out), make_dim<roles::Count>(B * S * D_FF));
    });

    const MatchedOp ff2 = cpu::advance_op(kTest, scope, H_MMFF2, S_MMFF2, ReplayStatus::MATCH);
    run_mm(scope, ff2, make_dim<roles::Rows>(B * S), make_dim<roles::Columns>(D), make_dim<roles::Inner>(D_FF));

    const MatchedOp add2 = cpu::advance_op(kTest, scope, H_ADD2, S_ADD2, ReplayStatus::MATCH);
    write_output(kTest, scope,
                 [augend = read_input(scope, add2, InputPosition{0}),
                  addend = read_input(scope, add2, InputPosition{1})](auto out) noexcept {
                     cpu::add(kTest, make_input<roles::Augend>(augend), make_input<roles::Addend>(addend),
                              std::move(out), make_dim<roles::Count>(B * S * D));
                 });

    const MatchedOp select = cpu::advance_op(kTest, scope, H_IDXSEL, S_IDXSEL, ReplayStatus::MATCH);
    write_output(kTest, scope, [input = read_input(scope, select, InputPosition{0})](auto out) noexcept {
        cpu::index_select(kTest, input, std::move(out), make_dim<roles::Batch>(B), make_dim<roles::Sequence>(S),
                          make_dim<roles::Width>(D), make_dim<roles::Selected>(0));
    });

    const MatchedOp head = cpu::advance_op(kTest, scope, H_MMHEAD, S_MMHEAD, ReplayStatus::MATCH);
    run_mm(scope, head, make_dim<roles::Rows>(B), make_dim<roles::Columns>(N_CLS), make_dim<roles::Inner>(D));

    const MatchedOp softmax = cpu::advance_op(kTest, scope, H_SOFTMAX, S_SOFTMAX, ReplayStatus::COMPLETE);
    write_output(kTest, scope, [input = read_input(scope, softmax, InputPosition{0})](auto out) noexcept {
        cpu::softmax(kTest, input, std::move(out), make_dim<roles::Rows>(B), make_dim<roles::Width>(N_CLS));
    });
}

}  // namespace test_compute_vit
