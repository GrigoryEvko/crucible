// The door that delegation uses to move a live endpoint into a message.
//
// mint_delegated_session moves the Resource and the watch record out of a
// live handle through endpoint_transfer.  The handle is then consumed, so
// its destructor reports no dropped protocol.  The record stays live,
// because the session continues at the receiver.  The test accepts the
// delegated endpoint, walks the handle to End, and checks that End
// releases the record.  A consumed handle aborts at the door, as at every
// operation, and a child process proves it.  No scope other than the door
// of the mint reaches endpoint_transfer or the handle factory.

#include <fixy/session/Delegate.h>

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace {

namespace s = ::fixy::session;
namespace sd = ::fixy::session::detail;

struct Ping {
    int value = 0;
};

struct Wire {
    int sent = 0;
};

using Proto = s::Send<Ping, s::End>;
using Handle = decltype(s::mint_session_handle<Proto, Wire>(Wire{}));

[[nodiscard]] int fail(const char* what) {
    std::fprintf(stderr, "test_session_endpoint_transfer: %s\n", what);
    return 1;
}

// The door returns a value that moves only, and only the door builds it.
static_assert(!std::is_copy_constructible_v<sd::transferred_endpoint<Wire>>);
static_assert(std::is_move_constructible_v<sd::transferred_endpoint<Wire>>);
static_assert(!std::is_constructible_v<sd::transferred_endpoint<Wire>, Wire, s::watch::session_ref>);

// The door and the factory are private to their friends.
template <typename H>
concept TakesAnEndpoint = requires(H&& handle) { sd::endpoint_transfer::take(std::move(handle)); };
template <typename R>
concept BuildsAHandle = requires(R&& resource) {
    s::HandleFactory::make_<Proto, R, void, s::DefaultAbandonmentPolicy>(std::move(resource));
};
template <typename R>
concept OpensASession = requires(R&& resource) {
    s::SessionMintDoor::open_<Proto, R, s::DefaultAbandonmentPolicy, ::foundation::permissions::EmptyPermSet>(
        std::move(resource), std::source_location{});
};
static_assert(!TakesAnEndpoint<Handle>);
static_assert(!BuildsAHandle<Wire>);
static_assert(!OpensASession<Wire>);
static_assert(!std::is_default_constructible_v<s::HandleFactory> && !std::is_trivially_copyable_v<s::HandleFactory>);
static_assert(!std::is_default_constructible_v<s::SessionMintDoor> && std::is_final_v<s::SessionMintDoor>);
static_assert(!std::is_default_constructible_v<s::DelegationDoor> && std::is_final_v<s::DelegationDoor>);
static_assert(!std::is_default_constructible_v<sd::endpoint_transfer> && std::is_final_v<sd::endpoint_transfer>);

int transfer_keeps_the_record_live() {
    const std::uint32_t live_before = s::watch::live_count();
    auto handle = s::mint_session_handle<Proto, Wire>(Wire{7});
    auto carried = s::mint_delegated_session(std::move(handle));
    if (handle.is_live()) return fail("the handle stayed live after its endpoint left it");
    if (!carried.holds_endpoint()) return fail("the payload does not hold the endpoint");
    if (s::watch::live_count() != live_before + 1) return fail("the transfer ended the session's record");

    // The receiver accepts the endpoint, and End releases the record.
    auto received = std::move(carried).accept();
    if (s::watch::live_count() != live_before + 1) return fail("the accept changed the number of live records");
    auto at_end = std::move(received).send(Ping{1}, [](Wire& wire, Ping& ping) noexcept {
        if (wire.sent != 7) return false;
        wire.sent = ping.value;
        return true;
    });
    if (s::watch::live_count() != live_before) return fail("a record stayed live after End");
    const Wire back = std::move(at_end).close();
    if (back.sent != 1) return fail("the receiver's handle did not step the transferred Resource");
    return 0;
}

// A handle that already left cannot leave again.
[[noreturn]] void take_from_a_consumed_handle() {
    auto handle = s::mint_session_handle<Proto, Wire>(Wire{});
    auto first = s::mint_delegated_session(std::move(handle));
    static_cast<void>(s::mint_delegated_session(std::move(handle)));
    static_cast<void>(first);
    std::_Exit(0);
}

int second_take_aborts() {
    std::fflush(stderr);
    // SPAWN-PROCESS-OK: the abort of a consumed handle ends the process,
    // so the attempt runs in a child that the parent observes.
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: see above
    if (pid < 0) return fail("fork failed");
    if (pid == 0) take_from_a_consumed_handle();
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) return fail("waitpid failed");  // SPAWN-PROCESS-OK: see above
    if (WIFSIGNALED(status) == 0) return fail("a second transfer from one handle did not abort");
    return 0;
}

}  // namespace

int main() {
    std::fprintf(stderr, "[expected] one child process prints a use-of-a-consumed-handle diagnostic\n");
    if (const int rc = transfer_keeps_the_record_live(); rc != 0) return rc;
    if (const int rc = second_take_aborts(); rc != 0) return rc;
    return 0;
}
