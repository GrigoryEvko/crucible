// Three wrapper classes can wrap the same protocol and all inherit the same
// session base, so an abandonment abort naming only the protocol cannot say
// which of them aborted. wrapper_name() spells the derived class's bare
// template name, and this file pins what each one renders.

#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/sessions/Session.h>

#include <cstdio>
#include <string_view>

namespace {

using namespace crucible::safety::proto;

struct FakeRes {};
struct ServerPeer {};

using HEnd = SessionHandle<End, FakeRes>;
static_assert(HEnd::wrapper_name() == "SessionHandle",
              "wrapper_name() must spell the bare template name of the derived "
              "class.");

using HSend = SessionHandle<Send<int, End>, FakeRes>;
static_assert(HSend::wrapper_name() == "SessionHandle");

using HRecv = SessionHandle<Recv<int, End>, FakeRes>;
static_assert(HRecv::wrapper_name() == "SessionHandle");

using HSelect = SessionHandle<Select<Send<int, End>, Send<float, End>>, FakeRes>;
static_assert(HSelect::wrapper_name() == "SessionHandle");

using HOffer = SessionHandle<Offer<Recv<int, End>, Recv<float, End>>, FakeRes>;
static_assert(HOffer::wrapper_name() == "SessionHandle");

using CWEnd = CrashWatchedHandle<End, FakeRes, ServerPeer>;
static_assert(CWEnd::wrapper_name() == "CrashWatchedHandle",
              "a crash-watched handle must name itself, not the session base it "
              "shares with the plain handle.");

using CWSend = CrashWatchedHandle<Send<int, End>, FakeRes, ServerPeer>;
static_assert(CWSend::wrapper_name() == "CrashWatchedHandle");

using CWRecv = CrashWatchedHandle<Recv<int, End>, FakeRes, ServerPeer>;
static_assert(CWRecv::wrapper_name() == "CrashWatchedHandle");

using CWSelect = CrashWatchedHandle<Select<Send<int, End>, Send<float, End>>, FakeRes, ServerPeer>;
static_assert(CWSelect::wrapper_name() == "CrashWatchedHandle");

using CWOffer = CrashWatchedHandle<Offer<Recv<int, End>, Recv<float, End>>, FakeRes, ServerPeer>;
static_assert(CWOffer::wrapper_name() == "CrashWatchedHandle");

using REnd = RecordingSessionHandle<End, FakeRes>;
static_assert(REnd::wrapper_name() == "RecordingSessionHandle",
              "a recording handle must name itself, distinctly from the other two "
              "wrappers on the same protocol.");

using RSend = RecordingSessionHandle<Send<int, End>, FakeRes>;
static_assert(RSend::wrapper_name() == "RecordingSessionHandle");

using RRecv = RecordingSessionHandle<Recv<int, End>, FakeRes>;
static_assert(RRecv::wrapper_name() == "RecordingSessionHandle");

using RSelect = RecordingSessionHandle<Select<Send<int, End>, Send<float, End>>, FakeRes>;
static_assert(RSelect::wrapper_name() == "RecordingSessionHandle");

using ROffer = RecordingSessionHandle<Offer<Recv<int, End>, Recv<float, End>>, FakeRes>;
static_assert(ROffer::wrapper_name() == "RecordingSessionHandle");

// One protocol, three wrappers, three different names.

static_assert(SessionHandle<Recv<int, End>, FakeRes>::wrapper_name()
                  != CrashWatchedHandle<Recv<int, End>, FakeRes, ServerPeer>::wrapper_name(),
              "the same protocol under a different wrapper class must yield a "
              "different wrapper_name().");

static_assert(SessionHandle<Recv<int, End>, FakeRes>::wrapper_name()
                  != RecordingSessionHandle<Recv<int, End>, FakeRes>::wrapper_name(),
              "the same protocol under a different wrapper class must yield a "
              "different wrapper_name().");

static_assert(CrashWatchedHandle<Recv<int, End>, FakeRes, ServerPeer>::wrapper_name()
                  != RecordingSessionHandle<Recv<int, End>, FakeRes>::wrapper_name(),
              "the same protocol under a different wrapper class must yield a "
              "different wrapper_name().");

// The two accessors answer different questions and are independent:
// wrapper_name() spells the wrapper class, protocol_name() the protocol.

static_assert(HEnd::protocol_name().find("End") != std::string_view::npos,
              "protocol_name() must still render the Proto's shape.");

static_assert(CWRecv::protocol_name().find("Recv") != std::string_view::npos,
              "protocol_name() works identically across wrapper classes.");

// Each non-terminal protocol head names the consumer method that would have
// advanced it, which is what the abort message suggests to the reader.

static_assert(HSend::next_method_hint().find("send") != std::string_view::npos,
              "Send<T, K>::next_method_hint() must mention 'send'.");

static_assert(HRecv::next_method_hint().find("recv") != std::string_view::npos,
              "Recv<T, K>::next_method_hint() must mention 'recv'.");

static_assert(HSelect::next_method_hint().find("pick") != std::string_view::npos,
              "Select<Bs...>::next_method_hint() must mention 'pick'.");

static_assert(HOffer::next_method_hint().find("branch") != std::string_view::npos
                  || HOffer::next_method_hint().find("pick") != std::string_view::npos,
              "Offer<Bs...>::next_method_hint() must mention 'branch' or 'pick'.");

static_assert(CWRecv::next_method_hint() == HRecv::next_method_hint(),
              "Hints depend on Proto only, not on the wrapper class.");

// The full spelling carries the resource, peer tag and loop context too,
// which is the only thing that separates two crash-watched handles differing
// solely in their peer.

static_assert(CWRecv::full_handle_type_name().find("CrashWatchedHandle") != std::string_view::npos,
              "Full type spelling must contain the wrapper template name.");

static_assert(CWRecv::full_handle_type_name().find("ServerPeer") != std::string_view::npos,
              "the full type spelling must carry the peer tag, which is what "
              "separates two crash-watched handles differing only in their peer.");

static_assert(REnd::full_handle_type_name().find("RecordingSessionHandle") != std::string_view::npos,
              "Full type spelling must distinguish RecordingSessionHandle from "
              "SessionHandle, even on the same Proto.");

}  // anonymous namespace

int main() {
    // The output is laid out the way the destructor's abort message is, so a
    // reader of the harness log sees the strings that would be printed on
    // abandonment.

    auto dump = [](std::string_view label, std::string_view wrapper, std::string_view full, std::string_view proto,
                   std::string_view hint) {
        std::printf("session_wrapper_name: %.*s\n", static_cast<int>(label.size()), label.data());
        std::printf("    Wrapper class:    %.*s\n", static_cast<int>(wrapper.size()), wrapper.data());
        std::printf("    Full handle type: %.*s\n", static_cast<int>(full.size()), full.data());
        std::printf("    Protocol head:    %.*s\n", static_cast<int>(proto.size()), proto.data());
        std::printf("    Expected action:  call .%.*s\n", static_cast<int>(hint.size()), hint.data());
    };

    dump("SessionHandle<End>", HEnd::wrapper_name(), HEnd::full_handle_type_name(), HEnd::protocol_name(),
         HEnd::next_method_hint());

    dump("CrashWatchedHandle<Recv<int, End>, MyChannel, ServerPeer>", CWRecv::wrapper_name(),
         CWRecv::full_handle_type_name(), CWRecv::protocol_name(), CWRecv::next_method_hint());

    dump("RecordingSessionHandle<Send<int, End>>", RSend::wrapper_name(), RSend::full_handle_type_name(),
         RSend::protocol_name(), RSend::next_method_hint());

    std::puts("session_wrapper_name: wrapper name, next-method hint and "
              "full type all rendered OK");
    return 0;
}
