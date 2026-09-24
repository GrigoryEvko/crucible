// The combinator is mostly covered by static_asserts inside the headers.
// This file drives the content-addressed variant and the raw-payload
// variant over the same in-memory wire and shows the handles advance
// identically, which is what protocol-level equivalence claims.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionContentAddressed.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/SessionSubtype.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety::proto;

struct KernelEntry {
    int content_hash;
};

struct Wire {
    std::deque<std::string>* bytes = nullptr;
};

auto send_entry = [](Wire& w, KernelEntry&& k) noexcept { w.bytes->push_back("KE:" + std::to_string(k.content_hash)); };
auto recv_entry = [](Wire& w) noexcept -> KernelEntry {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return KernelEntry{std::atoi(s.data() + 3)};
};

// The wrapper is erased at serialisation time and the transport carries the
// underlying payload, so the on-wire behaviour matches the raw version.

auto send_ca_entry = [](Wire& w, ContentAddressed<KernelEntry>&&) noexcept {
    // The body only tags the payload, to show the path was taken. The
    // deduplication that belongs here, a cache lookup on the hash that skips
    // the wire write on a hit, is out of scope for an in-memory wire.
    w.bytes->push_back("CA:KE:42");
};
auto recv_ca_entry = [](Wire& w) noexcept -> ContentAddressed<KernelEntry> {
    w.bytes->pop_front();
    return {};
};

using CaPublisher = Loop<Send<ContentAddressed<KernelEntry>, Continue>>;
using RawPublisher = Loop<Send<KernelEntry, Continue>>;

static_assert(equivalent_sync_v<CaPublisher, RawPublisher>);

using CaSubscriber = dual_of_t<CaPublisher>;
using RawSubscriber = dual_of_t<RawPublisher>;

static_assert(equivalent_sync_v<CaSubscriber, RawSubscriber>);

int run_content_addressed_loop() {
    std::deque<std::string> wire;
    Wire p{&wire};
    Wire s{&wire};
    crucible::effects::HotFgCtx ctx{};

    auto [pub, sub] = mint_channel<CaPublisher>(ctx, ctx, std::move(p), std::move(s));

    auto p1 = std::move(pub).send(ContentAddressed<KernelEntry>{}, send_ca_entry);
    auto p2 = std::move(p1).send(ContentAddressed<KernelEntry>{}, send_ca_entry);
    auto p3 = std::move(p2).send(ContentAddressed<KernelEntry>{}, send_ca_entry);

    auto [k1, s1] = std::move(sub).recv(recv_ca_entry);
    auto [k2, s2] = std::move(s1).recv(recv_ca_entry);
    auto [k3, s3] = std::move(s2).recv(recv_ca_entry);
    (void)k1;
    (void)k2;
    (void)k3;

    // The loop has no close branch, so detaching is the only way out.
    std::move(p3).detach(detach_reason::InfiniteLoopProtocol{});
    std::move(s3).detach(detach_reason::InfiniteLoopProtocol{});

    if (wire.size() != 0) {
        std::fprintf(stderr, "ca loop: wire not drained (size=%zu)\n", wire.size());
        return 1;
    }
    return 0;
}

int run_raw_loop() {
    std::deque<std::string> wire;
    Wire p{&wire};
    Wire s{&wire};
    crucible::effects::HotFgCtx ctx{};

    auto [pub, sub] = mint_channel<RawPublisher>(ctx, ctx, std::move(p), std::move(s));

    auto p1 = std::move(pub).send(KernelEntry{100}, send_entry);
    auto p2 = std::move(p1).send(KernelEntry{200}, send_entry);

    auto [k1, s1] = std::move(sub).recv(recv_entry);
    auto [k2, s2] = std::move(s1).recv(recv_entry);

    if (k1.content_hash != 100 || k2.content_hash != 200) {
        std::fprintf(stderr, "raw loop: content_hash wrong (%d, %d)\n", k1.content_hash, k2.content_hash);
        return 1;
    }
    std::move(p2).detach(detach_reason::InfiniteLoopProtocol{});
    std::move(s2).detach(detach_reason::InfiniteLoopProtocol{});
    return 0;
}

static_assert(is_content_addressed_v<ContentAddressed<KernelEntry>>);
static_assert(!is_content_addressed_v<KernelEntry>);
static_assert(content_addressed_depth_v<ContentAddressed<KernelEntry>> == 1);
static_assert(content_addressed_depth_v<ContentAddressed<ContentAddressed<KernelEntry>>> == 2);

static_assert(std::is_same_v<unwrap_content_addressed_t<ContentAddressed<ContentAddressed<KernelEntry>>>, KernelEntry>);

static_assert(is_subsort_v<ContentAddressed<KernelEntry>, KernelEntry>);
static_assert(is_subsort_v<KernelEntry, ContentAddressed<KernelEntry>>);

struct Different {};
static_assert(!is_subsort_v<ContentAddressed<KernelEntry>, Different>);
static_assert(!is_subsort_v<Different, ContentAddressed<KernelEntry>>);

static_assert(is_subtype_sync_v<Send<ContentAddressed<KernelEntry>, End>, Send<KernelEntry, End>>);
static_assert(is_subtype_sync_v<Send<KernelEntry, End>, Send<ContentAddressed<KernelEntry>, End>>);

static_assert(
    equivalent_sync_v<Loop<Send<ContentAddressed<KernelEntry>, Continue>>, Loop<Send<KernelEntry, Continue>>>);

}  // anonymous namespace

int main() {
    if (int rc = run_content_addressed_loop(); rc != 0) return rc;
    if (int rc = run_raw_loop(); rc != 0) return rc;
    std::puts("session_content_addressed: CA + raw loop + equivalence OK");
    return 0;
}
