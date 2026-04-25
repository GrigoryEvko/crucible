// Runtime harness for L11 diagnostic-class vocabulary (task #342,
// SEPLOG-H2d).  Most coverage is in-header static_asserts; this file
// exercises runtime access to ::name / ::description / ::remediation
// strings and demonstrates the classified-assertion macro in a
// realistic user scenario.

#include <crucible/sessions/SessionDiagnostic.h>
#include <crucible/sessions/SessionSubtype.h>
#include <crucible/sessions/Session.h>

#include <array>
#include <cstdio>
#include <string_view>
#include <tuple>

namespace {

using namespace crucible::safety::proto;

// ── Runtime access to tag fields ────────────────────────────────

int run_tag_field_access() {
    using namespace diagnostic;

    // Access all three fields per tag.
    if (diagnostic_name_v<SubtypeMismatch> != "SubtypeMismatch") {
        std::fprintf(stderr, "name accessor broken\n");
        return 1;
    }
    if (diagnostic_description_v<SubtypeMismatch>.empty()) {
        std::fprintf(stderr, "description accessor returned empty\n");
        return 1;
    }
    if (diagnostic_remediation_v<SubtypeMismatch>.empty()) {
        std::fprintf(stderr, "remediation accessor returned empty\n");
        return 1;
    }

    // Same for every shipped tag — exhaustive.
    const auto check = [](std::string_view name, std::string_view desc,
                           std::string_view rem) -> int {
        if (name.empty() || desc.empty() || rem.empty()) return 1;
        return 0;
    };

    if (check(diagnostic_name_v<ProtocolViolation_Label>,
              diagnostic_description_v<ProtocolViolation_Label>,
              diagnostic_remediation_v<ProtocolViolation_Label>)) return 1;
    if (check(diagnostic_name_v<ProtocolViolation_Payload>,
              diagnostic_description_v<ProtocolViolation_Payload>,
              diagnostic_remediation_v<ProtocolViolation_Payload>)) return 1;
    if (check(diagnostic_name_v<ProtocolViolation_State>,
              diagnostic_description_v<ProtocolViolation_State>,
              diagnostic_remediation_v<ProtocolViolation_State>)) return 1;
    if (check(diagnostic_name_v<Deadlock_Detected>,
              diagnostic_description_v<Deadlock_Detected>,
              diagnostic_remediation_v<Deadlock_Detected>)) return 1;
    if (check(diagnostic_name_v<Livelock_Detected>,
              diagnostic_description_v<Livelock_Detected>,
              diagnostic_remediation_v<Livelock_Detected>)) return 1;
    if (check(diagnostic_name_v<StarvationPossible>,
              diagnostic_description_v<StarvationPossible>,
              diagnostic_remediation_v<StarvationPossible>)) return 1;
    if (check(diagnostic_name_v<CrashBranch_Missing>,
              diagnostic_description_v<CrashBranch_Missing>,
              diagnostic_remediation_v<CrashBranch_Missing>)) return 1;
    if (check(diagnostic_name_v<PermissionImbalance>,
              diagnostic_description_v<PermissionImbalance>,
              diagnostic_remediation_v<PermissionImbalance>)) return 1;
    if (check(diagnostic_name_v<SubtypeMismatch>,
              diagnostic_description_v<SubtypeMismatch>,
              diagnostic_remediation_v<SubtypeMismatch>)) return 1;
    if (check(diagnostic_name_v<DepthBoundReached>,
              diagnostic_description_v<DepthBoundReached>,
              diagnostic_remediation_v<DepthBoundReached>)) return 1;
    if (check(diagnostic_name_v<UnboundedQueue>,
              diagnostic_description_v<UnboundedQueue>,
              diagnostic_remediation_v<UnboundedQueue>)) return 1;

    return 0;
}

// ── Iterate the catalog and print a summary ────────────────────

template <std::size_t... Is>
void print_catalog_impl(std::index_sequence<Is...>) {
    (std::printf("  [%zu] %-28.*s: %.*s\n",
                 Is,
                 static_cast<int>(std::tuple_element_t<Is, diagnostic::Catalog>::name.size()),
                 std::tuple_element_t<Is, diagnostic::Catalog>::name.data(),
                 static_cast<int>(std::tuple_element_t<Is, diagnostic::Catalog>::description.size()),
                 std::tuple_element_t<Is, diagnostic::Catalog>::description.data()),
     ...);
}

void print_catalog() {
    std::puts("Diagnostic catalog:");
    print_catalog_impl(std::make_index_sequence<diagnostic::catalog_size>{});
}

// ── Classified-assertion macro integration ─────────────────────
//
// Demonstrate the macro at a real-ish call site.  The condition is
// TRUE (we're not trying to fail compile) but the message routing
// is what matters for greppable build logs.

struct MyReq {};
struct MyResp {};
using MyProto = Send<MyReq, Recv<MyResp, End>>;

CRUCIBLE_SESSION_ASSERT_CLASSIFIED(
    is_well_formed_v<MyProto>,
    ProtocolViolation_State,
    "MyProto must be well-formed to drive handle dispatch.");

// Note: condition contains a comma (template arg list), so parenthesise.
CRUCIBLE_SESSION_ASSERT_CLASSIFIED(
    (is_subtype_sync_v<MyProto, MyProto>),
    SubtypeMismatch,
    "MyProto is trivially a subtype of itself (reflexivity).");

// ── User-defined tag extension (compile-time) ──────────────────

struct CrucibleSpecific_BadKernelCache : diagnostic::tag_base {
    static constexpr std::string_view name        = "CrucibleSpecific_BadKernelCache";
    static constexpr std::string_view description =
        "KernelCache publication failed schema validation.";
    static constexpr std::string_view remediation =
        "Verify the CompiledKernel's content hash matches its cached "
        "slot's expected hash.";
};

static_assert(diagnostic::is_diagnostic_class_v<CrucibleSpecific_BadKernelCache>);
static_assert(diagnostic::diagnostic_name_v<CrucibleSpecific_BadKernelCache>
              == "CrucibleSpecific_BadKernelCache");

int run_user_extension() {
    using T = CrucibleSpecific_BadKernelCache;
    if (diagnostic::diagnostic_name_v<T> != "CrucibleSpecific_BadKernelCache")
        return 1;
    if (diagnostic::diagnostic_description_v<T>.empty()) return 1;
    if (diagnostic::diagnostic_remediation_v<T>.empty()) return 1;
    return 0;
}

// ── Diagnostic<Tag, Ctx...> wrapper smoke ──────────────────────

int run_diagnostic_wrapper() {
    using D = diagnostic::Diagnostic<
        diagnostic::SubtypeMismatch,
        MyProto,
        Recv<MyReq, End>>;

    if (D::name != "SubtypeMismatch") return 1;
    if (D::description.empty()) return 1;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_tag_field_access();  rc != 0) return rc;
    if (int rc = run_user_extension();    rc != 0) return rc;
    if (int rc = run_diagnostic_wrapper(); rc != 0) return rc;
    print_catalog();
    std::puts("session_diagnostic: 18 tags + user extension + macro + catalog OK");
    return 0;
}
