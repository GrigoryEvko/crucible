// Each process-wide object of the runtime, written in one shared library and
// read in another.
//
// crucible_native.py loads libcrucible_vessel.so and libcrucible_dispatch.so
// with ctypes, which calls dlopen with RTLD_LOCAL, and neither library names
// the other.  The project compiles with -fvisibility=hidden.  An object that
// the two libraries both define, as every inline variable and every static of
// an inline function is defined, then has one copy in each library, and a
// write in one library is not seen in the other.  This test loads two real
// libraries in the same way and checks that each process-wide object has one
// address, that each object for a thread is one object for that thread, and
// that each write reaches the other library.  Two objects with no marker are
// the control: each has a copy in each library.  The first case shows that the
// report of the fallback rows of the kernel floor is one for each process.
// The last case is the run-time witness of the rule static-tls.

#include "process_wide_libraries.h"

#include <crucible/CKernel.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Args.h>
#include <fixy/os/Process.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/KernelFallbacks.h>
#include <foundation/os/KernelFloor.h>

#include <dlfcn.h>

#include <bit>
#include <cstdio>
#include <thread>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace pwl = process_wide_libraries;

// One library, loaded as the vessel libraries are loaded.  The handle is
// never closed: a library that defines a process-wide object stays loaded.
// A load that is not required can fail, and its handle then stays null.
struct Library {
    void* handle = nullptr;

    explicit Library(const char* path, bool is_required = true) noexcept : handle{dlopen(path, RTLD_NOW | RTLD_LOCAL)} {
        if (handle == nullptr) std::fprintf(stderr, "dlopen of %s failed: %s\n", path, dlerror());
        CRUCIBLE_TEST_ASSERT(kTest, handle != nullptr || !is_required);
    }

    template <class Function>
    [[nodiscard]] Function* symbol(const char* name) const noexcept {
        void* const address = dlsym(handle, name);
        if (address == nullptr) std::fprintf(stderr, "dlsym of %s failed: %s\n", name, dlerror());
        CRUCIBLE_TEST_ASSERT(kTest, address != nullptr);
        return std::bit_cast<Function*>(address);
    }
};

// The assert of this tree ends the process with no message, so the claim is
// printed first.
void require(bool is_holding, const char* claim) {
    if (!is_holding) std::fprintf(stderr, "  FAILED: %s\n", claim);
    CRUCIBLE_TEST_ASSERT(kTest, is_holding);
}

template <class Body>
[[nodiscard]] auto on_other_thread(Body body) {
    decltype(body()) result{};
    std::jthread{[&] { result = body(); }}.join();
    return result;
}

// The report of a child that loads the writer and the reader, as the vessel
// loads its two libraries, and then gives the two to body.  The child makes
// the loads itself, so its standard error also holds each write of a load.
template <class Body>
[[nodiscard]] ::fixy::process::ChildReport run_with_libraries(::fixy::TestRunnerCtx const& test, Body body) {
    ::fixy::process::Child child = ::fixy::process::spawn_child(test, [&body] noexcept {
                                       const Library writer{PROCESS_WIDE_WRITER_PATH};
                                       const Library reader{PROCESS_WIDE_READER_PATH};
                                       body(writer, reader);
                                   }).expect("the test starts a child");
    return ::fixy::process::wait(std::move(child)).expect("the test waits for its child");
}

void report_floor_fallbacks_in(const Library& library) noexcept {
    library.symbol<decltype(process_wide_report_floor_fallbacks)>("process_wide_report_floor_fallbacks")();
}

void open_init_door_in(const Library& library) noexcept {
    library.symbol<decltype(process_wide_open_init_door)>("process_wide_open_init_door")();
}

// The report of the fallback rows of the kernel floor is one for each
// process, and the load of a library writes none of it.  A process spends the
// cell of the report one time, so each part runs in a child of its own, and
// the child loads the two libraries after the fork.  The release of the floor
// lacks each fallback row above it, so its text is the witness of a report.
// The release of this host can lack no row, and its text is then empty.
void test_one_fallback_report_for_the_process(::fixy::TestRunnerCtx const& test) {
    namespace os = ::foundation::os;
    ::fixy::FixedText<os::kernel_fallback_text_capacity> floor_text{};
    static_cast<void>(
        os::format_kernel_fallbacks(os::minimum_kernel(), floor_text).expect("the rows of the floor fit"));
    require(floor_text.size() != 0, "the release of the floor lacks a fallback row");
    ::fixy::FixedText<os::kernel_fallback_text_capacity> running_text{};
    static_cast<void>(
        os::format_kernel_fallbacks(os::running_kernel(), running_text).expect("the rows of this host fit"));

    ::fixy::process::ChildReport const loaded =
        run_with_libraries(test, [](const Library&, const Library&) noexcept {});
    require(loaded.is_exit_with(0) && loaded.output().size() == 0,
            "the load of the two libraries writes nothing before the init door");

    ::fixy::process::ChildReport const two_units =
        run_with_libraries(test, [](const Library& writer, const Library& reader) noexcept {
            report_floor_fallbacks_in(writer);
            report_floor_fallbacks_in(reader);
            open_init_door_in(reader);
            open_init_door_in(writer);
        });
    require(two_units.is_exit_with(0) && two_units.output() == floor_text.view(),
            "of four reports in two libraries, only the first writes");

    ::fixy::process::ChildReport const door_first =
        run_with_libraries(test, [](const Library& writer, const Library& reader) noexcept {
            open_init_door_in(writer);
            report_floor_fallbacks_in(reader);
            open_init_door_in(reader);
        });
    require(door_first.is_exit_with(0) && door_first.output() == running_text.view(),
            "the init door of one library writes the report of the running kernel and spends the report of the other");
    ::fixy::check::pass(test, "  test_one_fallback_report_for_the_process: PASSED\n");
}

void test_each_object_has_one_address(const Library& writer, const Library& reader) {
    auto* const writer_address = writer.symbol<decltype(process_wide_object_address)>("process_wide_object_address");
    auto* const reader_address = reader.symbol<decltype(process_wide_object_address)>("process_wide_object_address");
    unsigned index = 0;
    while (writer_address(index) != nullptr) {
        if (writer_address(index) != reader_address(index))
            std::fprintf(stderr, "  object %u: the writer sees %p, the reader sees %p\n", index, writer_address(index),
                         reader_address(index));
        CRUCIBLE_TEST_ASSERT(kTest, writer_address(index) == reader_address(index));
        ++index;
    }
    CRUCIBLE_TEST_ASSERT(kTest, reader_address(index) == nullptr);
    CRUCIBLE_TEST_ASSERT(kTest, index > 0);

    auto* const writer_control = writer.symbol<decltype(process_wide_control_address)>("process_wide_control_address");
    auto* const reader_control = reader.symbol<decltype(process_wide_control_address)>("process_wide_control_address");
    for (unsigned control = 0; writer_control(control) != nullptr; ++control)
        require(writer_control(control) != reader_control(control),
                "an object with no marker has a copy in each library, so the two libraries are loaded apart");
    ::fixy::check::pass(kTest, "  test_each_object_has_one_address: PASSED ({} objects)\n", index);
}

// A marked thread_local object is one object for each thread, whatever
// library reads it: the two libraries see one address on one thread, a write
// in the writer is read in the reader on that thread, and a second thread has
// an object of its own.
void test_each_thread_object_is_one_for_the_thread(const Library& writer, const Library& reader) {
    auto* const writer_address =
        writer.symbol<decltype(process_wide_thread_object_address)>("process_wide_thread_object_address");
    auto* const reader_address =
        reader.symbol<decltype(process_wide_thread_object_address)>("process_wide_thread_object_address");
    auto* const writer_set =
        writer.symbol<decltype(process_wide_set_thread_objects)>("process_wide_set_thread_objects");
    auto* const reader_value =
        reader.symbol<decltype(process_wide_thread_object_value)>("process_wide_thread_object_value");

    writer_set(7);
    unsigned count = 0;
    while (writer_address(count) != nullptr) {
        require(writer_address(count) == reader_address(count),
                "the two libraries see one address for a thread object on the main thread");
        ++count;
    }
    require(count == 3, "three thread objects are listed");
    require(reader_value(0) == 7 && reader_value(1) == 7 && reader_value(2) == 1,
            "the reader reads the values that the writer wrote on the main thread");
    require(reader_value(3) == 7,
            "the reader library reads the ThreadLocalRef cell that the writer library wrote on this thread");

    const void* const main_address = writer_address(0);
    const bool is_other_thread_right = on_other_thread([&] {
        bool is_right = writer_address(0) != main_address;
        for (unsigned index = 0; index < count; ++index) {
            is_right = is_right && writer_address(index) == reader_address(index);
            is_right = is_right && reader_value(index) == 0;
        }
        is_right = is_right && reader_value(3) == 0;
        writer_set(9);
        return is_right && reader_value(0) == 9 && reader_value(1) == 9 && reader_value(2) == 1 && reader_value(3) == 9;
    });
    require(is_other_thread_right,
            "a second thread has objects of its own, one address in the two libraries, and sees its own writes");
    require(reader_value(0) == 7 && reader_value(1) == 7,
            "the write of the second thread did not reach the main thread");
    ::fixy::check::pass(kTest, "  test_each_thread_object_is_one_for_the_thread: PASSED ({} objects)\n", count);
}

void test_the_reader_sees_each_write(const Library& writer, const Library& reader) {
    writer.symbol<decltype(process_wide_write)>("process_wide_write")();
    auto* const classify = reader.symbol<decltype(process_wide_classify)>("process_wide_classify");
    auto* const schema_name = reader.symbol<decltype(process_wide_schema_name)>("process_wide_schema_name");

    require(classify(pwl::kPublishedHash) == std::to_underlying(crucible::CKernelId::GEMM_MM),
            "the reader library sees the static kernel table that the writer published");
    require(classify(pwl::kRegisteredHash) == std::to_underlying(crucible::CKernelId::EWISE_ADD),
            "the reader library sees the kernel that the writer registered");
    require(schema_name(pwl::kNamedHash) == ::fixy::TextView{pwl::kSchemaName},
            "the reader library sees the schema name that the writer registered");
    ::fixy::check::pass(kTest, "  test_the_reader_sees_each_write: PASSED\n");
}

// The second claim ends the process, so it runs on a second thread of a
// child through expect_dies of the Check family.  The test process has one
// thread at the fork, so the child may start the second thread.
void test_the_reader_sees_the_claim(::fixy::TestRunnerCtx const& test, const Library& writer, const Library& reader) {
    auto* const is_usable = reader.symbol<decltype(process_wide_is_brand_usable)>("process_wide_is_brand_usable");
    auto* const claim_a_second_brand =
        reader.symbol<decltype(process_wide_claim_a_second_brand)>("process_wide_claim_a_second_brand");

    void* const brand = writer.symbol<decltype(process_wide_claim_brand)>("process_wide_claim_brand")();
    require(is_usable() == 1, "the thread that holds the claim can use the brand");
    require(on_other_thread(is_usable) == 0,
            "the reader library refuses a thread that holds no claim, because it sees the writer's claim");
    ::fixy::check::expect_dies(
        test, [claim_a_second_brand] noexcept { std::jthread{claim_a_second_brand}.join(); },
        ::fixy::TextView{"foundation: fatal invariant failed: record != nullptr"});

    writer.symbol<decltype(process_wide_release_brand)>("process_wide_release_brand")(brand);
    require(on_other_thread(is_usable) == 1, "the claim ends when its brand is destroyed");
    ::fixy::check::pass(test, "  test_the_reader_sees_the_claim: PASSED\n");
}

// The run-time witness of the rule static-tls.  The control and the plant come
// from one source file, and only the size of their initial-exec block is
// different.  The block of the control holds the limit of the rule,
// and it loads after the writer and the reader.  The block of the plant is
// larger than the static TLS that glibc keeps free, and dlopen refuses it.  The
// two sizes are the only difference, so the test does not read the text of
// dlerror.  This case runs last, after the writer and the reader are loaded.
void test_static_tls_holds_the_rule_threshold() {
    const Library control{STATIC_TLS_CONTROL_PATH, false};
    require(control.handle != nullptr,
            "a library whose initial-exec block holds the limit of the rule static-tls loads after the "
            "writer and the reader");
    const Library plant{STATIC_TLS_PLANT_PATH, false};
    require(plant.handle == nullptr,
            "dlopen refuses a library whose initial-exec block is larger than the free static TLS of glibc");
    ::fixy::check::pass(kTest, "  test_static_tls_holds_the_rule_threshold: PASSED\n");
}

}  // namespace

// With no argument, every case runs.  One argument names the one case to run,
// so that each case can be seen to fail on its own.  The argument door of
// fixy/os/Args.h reads the argument.
int main(int argc, char** argv) {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::os::Args const arguments = ::fixy::os::args(argc, argv);
    const auto is_selected = [&arguments](::fixy::TextView const name) noexcept {
        return arguments.get(1).match([name](::fixy::CText const argument) noexcept { return argument.view() == name; },
                                      [] noexcept { return true; });
    };
    ::fixy::report(::fixy::Sink::Out, "test_process_wide_across_libraries:\n");
    // The children of this case load the two libraries themselves, so it runs
    // before this process loads them.
    if (is_selected(::fixy::TextView{"fallbacks"})) test_one_fallback_report_for_the_process(test);
    const Library writer{PROCESS_WIDE_WRITER_PATH};
    const Library reader{PROCESS_WIDE_READER_PATH};
    if (is_selected(::fixy::TextView{"addresses"})) test_each_object_has_one_address(writer, reader);
    if (is_selected(::fixy::TextView{"threads"})) test_each_thread_object_is_one_for_the_thread(writer, reader);
    if (is_selected(::fixy::TextView{"writes"})) test_the_reader_sees_each_write(writer, reader);
    if (is_selected(::fixy::TextView{"claim"})) test_the_reader_sees_the_claim(test, writer, reader);
    if (is_selected(::fixy::TextView{"static_tls"})) test_static_tls_holds_the_rule_threshold();
    ::fixy::check::pass(test, "test_process_wide_across_libraries: all tests passed\n");
    return 0;
}
