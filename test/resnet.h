#pragma once

// The shared part of test_resnet: the network builder, the builder of each
// op packet, and the steps of the run that drive the compiled replay.  The
// run up to the memory plan, and main, are in test_resnet.cpp.

#include <crucible/Vigil.h>

#include <cstdint>
#include <vector>

namespace test_resnet {

inline constexpr uint16_t PARAM = 0x8000;

struct TRef {
    uint16_t ref = 0;
    uint8_t ndim = 0;
    int64_t d[4]{};
};

struct OpDef {
    crucible::SchemaHash schema;
    crucible::ShapeHash shape;
    uint8_t n_in = 0, n_out = 0;
    TRef t[6]{};
};

// Carries a tensor's shape through the builder so each layer can
// compute the next one.
struct Tens {
    uint16_t ref = 0;
    uint8_t ndim = 4;
    int ch = 0, h = 0, w = 0;
};

// The builder of the network (test_resnet_model.cpp).
struct ResNet50 {
    std::vector<OpDef> ops;
    uint16_t np = 0, na = 0;
    uint64_t params = 0;
    int N = 0;

    TRef tref(Tens t) const;
    TRef pr(int ndim, int64_t d0, int64_t d1 = 1, int64_t d2 = 1, int64_t d3 = 1);
    void emit(crucible::SchemaHash sch, int ni, int no, TRef t0 = {}, TRef t1 = {}, TRef t2 = {}, TRef t3 = {});
    Tens conv(Tens in, int co, int k, int s, int p);
    Tens bn(Tens in);
    Tens relu(Tens in);
    Tens maxpool(Tens in, int k, int s, int p);
    Tens add(Tens a, Tens b);
    Tens avgpool(Tens in);
    Tens linear(Tens in, int out_ch);
    Tens softmax(Tens in);

    // A bottleneck narrows, convolves, and widens again, with a skip
    // path around it.  The stride goes on the middle convolution,
    // which is the convention the reference implementation uses and
    // which decides where the spatial reduction happens.
    Tens bottleneck(Tens x, int mid, int out_ch, int stride);

    void build(int batch);
};

struct OpPacket {
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta metas[6]{};
    uint16_t n_metas = 0;
};

// The op packets (test_resnet_model.cpp).
OpPacket build_pkt(const OpDef& op, uint32_t iter);
void feed_iter(crucible::Vigil& v, const std::vector<OpDef>& ops, uint32_t iter);
void feed_trigger(crucible::Vigil& v, const std::vector<OpDef>& ops, uint32_t iter);

// The compiled replay (test_resnet_replay.cpp): the alignment, a thousand
// compiled iterations, and the check of the data flow from the first op
// to the second.
void align_and_complete_iteration(crucible::Vigil& vigil, const std::vector<OpDef>& ops);
void run_compiled_iterations(crucible::Vigil& vigil, const std::vector<OpDef>& ops);
void verify_data_flow(crucible::Vigil& vigil, const std::vector<OpDef>& ops);

}  // namespace test_resnet
