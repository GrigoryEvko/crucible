// Each process-wide object of the runtime, written in one shared library and
// read in another.
//
// crucible_native.py loads libcrucible_vessel.so and libcrucible_dispatch.so
// with ctypes, which calls dlopen with RTLD_LOCAL, and neither library names
// the other.  The project compiles with -fvisibility=hidden.  An object that
// the two libraries both define, as every inline variable and every static of
// an inline function is defined, then has one copy in each library, and a
// write in one library is not seen in the other.  This test loads two real
// libraries in the same way, through the loader door of fixy/os/Loader.h, and
// checks that each process-wide object has one address, that each object for
// a thread is one object for that thread, and that each write reaches the
// other library.  Two objects with no marker are the control: each has a copy
// in each library.  The first case shows that the report of the fallback rows
// of the kernel floor is one for each process.  The last case is the run-time
// witness of the rule static-tls.

#include "process_wide_libraries.h"

#include <crucible/CKernel.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Args.h>
#include <fixy/os/Loader.h>
#include <fixy/os/Process.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/KernelFallbacks.h>
#include <foundation/os/KernelFloor.h>

#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace fo = ::fixy::os;
namespace pwl = process_wide_libraries;

// One library, loaded as the vessel libraries are loaded, or no value when
// the loader refuses it.  The end of the owner closes the library.
[[nodiscard]] ::fixy::Option<fo::Library> try_load(::fixy::CText path) {
    fo::LoaderDiagnostic diagnostic{};
    return fo::mint_library(kTest, path, ::fixy::to(diagnostic))
        .match(
            [](::fixy::Linear<fo::Library> loaded) {
                return ::fixy::Option<fo::Library>::some(std::move(loaded).consume());
            },
            [&diagnostic, path](fo::LoaderFailure) {
                ::fixy::report(::fixy::Sink::Err, "dlopen of {} failed: {}\n", path.view(), diagnostic.text());
                return ::fixy::Option<fo::Library>{};
            });
}

// One library that the test needs.
[[nodiscard]] fo::Library load(::fixy::CText path) {
    return try_load(path).expect("the test loads each library that it needs");
}

// The function name of the library, of the signature of its declaration in
// process_wide_libraries.h.
template <class Signature>
[[nodiscard]] fo::LibraryFunction<Signature> find(fo::Library const& library, ::fixy::CText name) {
    fo::LoaderDiagnostic diagnostic{};
    return fo::find_function<Signature>(kTest, library, name, ::fixy::to(diagnostic))
        .match([](fo::LibraryFunction<Signature> function) { return function; },
               [&diagnostic, name](fo::LoaderFailure) -> fo::LibraryFunction<Signature> {
                   ::fixy::report(::fixy::Sink::Err, "dlsym of {} failed: {}\n", name.view(), diagnostic.text());
                   ::fixy::fatal("the test finds each entry point of its libraries");
               });
}

// The assert of this tree ends the process with no message, so the claim is
// written first.
[[noreturn]] void fail_claim(::fixy::TextView claim) {
    ::fixy::report(::fixy::Sink::Err, "  FAILED: {}\n", claim);
    ::fixy::fatal("a claim of test_process_wide_across_libraries failed");
}

// The value of body, which runs on a second thread that the call joins.
template <class Body>
[[nodiscard]] auto on_other_thread(Body body) {
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto const values = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, body)
                            .expect("the host starts the thread of the test");
    return values.at(::fixy::index<0>());
}

// The report of a child that loads the writer and the reader, as the vessel
// loads its two libraries, and then gives the two to body.  The child makes
// the loads itself, so its standard error also holds each write of a load.
template <class Body>
[[nodiscard]] ::fixy::process::ChildReport run_with_libraries(::fixy::TestRunnerCtx const& test, Body body) {
    ::fixy::process::Child child = ::fixy::process::spawn_child(test, [&body] noexcept {
                                       fo::Library const writer = load(PROCESS_WIDE_WRITER_PATH);
                                       fo::Library const reader = load(PROCESS_WIDE_READER_PATH);
                                       body(writer, reader);
                                   }).expect("the test starts a child");
    return ::fixy::process::wait(std::move(child)).expect("the test waits for its child");
}

void report_floor_fallbacks_in(fo::Library const& library) noexcept {
    find<decltype(process_wide_report_floor_fallbacks)>(library, "process_wide_report_floor_fallbacks")();
}

void open_init_door_in(fo::Library const& library) noexcept {
    find<decltype(process_wide_open_init_door)>(library, "process_wide_open_init_door")();
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
    if (floor_text.size() == 0) fail_claim("the release of the floor lacks a fallback row");
    ::fixy::FixedText<os::kernel_fallback_text_capacity> running_text{};
    static_cast<void>(
        os::format_kernel_fallbacks(os::running_kernel(), running_text).expect("the rows of this host fit"));

    ::fixy::process::ChildReport const loaded =
        run_with_libraries(test, [](fo::Library const&, fo::Library const&) noexcept {});
    if (!(loaded.is_exit_with(0) && loaded.output().size() == 0)) {
        fail_claim("the load of the two libraries writes nothing before the init door");
    }

    ::fixy::process::ChildReport const two_units =
        run_with_libraries(test, [](fo::Library const& writer, fo::Library const& reader) noexcept {
            report_floor_fallbacks_in(writer);
            report_floor_fallbacks_in(reader);
            open_init_door_in(reader);
            open_init_door_in(writer);
        });
    if (!(two_units.is_exit_with(0) && two_units.output() == floor_text.view())) {
        fail_claim("of four reports in two libraries, only the first writes");
    }

    ::fixy::process::ChildReport const door_first =
        run_with_libraries(test, [](fo::Library const& writer, fo::Library const& reader) noexcept {
            open_init_door_in(writer);
            report_floor_fallbacks_in(reader);
            open_init_door_in(reader);
        });
    if (!(door_first.is_exit_with(0) && door_first.output() == running_text.view())) {
        fail_claim(
            "the init door of one library writes the report of the running kernel and spends the report of the other");
    }
    ::fixy::check::pass(test, "  test_one_fallback_report_for_the_process: PASSED\n");
}

void test_each_object_has_one_address(fo::Library const& writer, fo::Library const& reader) {
    auto const writer_address = find<decltype(process_wide_object_address)>(writer, "process_wide_object_address");
    auto const reader_address = find<decltype(process_wide_object_address)>(reader, "process_wide_object_address");
    unsigned index = 0;
    while (writer_address(index) != nullptr) {
        if (writer_address(index) != reader_address(index)) {
            ::fixy::report(::fixy::Sink::Err, "  object {}: the writer and the reader see two addresses\n", index);
        }
        CRUCIBLE_TEST_ASSERT(kTest, writer_address(index) == reader_address(index));
        ++index;
    }
    CRUCIBLE_TEST_ASSERT(kTest, reader_address(index) == nullptr);
    CRUCIBLE_TEST_ASSERT(kTest, index > 0);

    auto const writer_control = find<decltype(process_wide_control_address)>(writer, "process_wide_control_address");
    auto const reader_control = find<decltype(process_wide_control_address)>(reader, "process_wide_control_address");
    for (unsigned control = 0; writer_control(control) != nullptr; ++control) {
        if (writer_control(control) == reader_control(control)) {
            fail_claim("an object with no marker has a copy in each library, so the two libraries are loaded apart");
        }
    }
    ::fixy::check::pass(kTest, "  test_each_object_has_one_address: PASSED ({} objects)\n", index);
}

// A marked thread_local object is one object for each thread, whatever
// library reads it: the two libraries see one address on one thread, a write
// in the writer is read in the reader on that thread, and a second thread has
// an object of its own.
void test_each_thread_object_is_one_for_the_thread(fo::Library const& writer, fo::Library const& reader) {
    auto const writer_address =
        find<decltype(process_wide_thread_object_address)>(writer, "process_wide_thread_object_address");
    auto const reader_address =
        find<decltype(process_wide_thread_object_address)>(reader, "process_wide_thread_object_address");
    auto const writer_set = find<decltype(process_wide_set_thread_objects)>(writer, "process_wide_set_thread_objects");
    auto const reader_value =
        find<decltype(process_wide_thread_object_value)>(reader, "process_wide_thread_object_value");

    writer_set(7);
    unsigned count = 0;
    while (writer_address(count) != nullptr) {
        if (writer_address(count) != reader_address(count)) {
            fail_claim("the two libraries see one address for a thread object on the main thread");
        }
        ++count;
    }
    if (count != 3) fail_claim("three thread objects are listed");
    if (!(reader_value(0) == 7 && reader_value(1) == 7 && reader_value(2) == 1)) {
        fail_claim("the reader reads the values that the writer wrote on the main thread");
    }
    if (reader_value(3) != 7) {
        fail_claim("the reader library reads the ThreadLocalRef cell that the writer library wrote on this thread");
    }

    ::fixy::AddressKey const main_address = ::fixy::mint_address_key(writer_address(0));
    bool const is_other_thread_right = on_other_thread([writer_address, reader_address, writer_set, reader_value, count,
                                                        main_address] noexcept {
        bool is_right = !(::fixy::mint_address_key(writer_address(0)) == main_address);
        for (unsigned index = 0; index < count; ++index) {
            is_right = is_right && writer_address(index) == reader_address(index);
            is_right = is_right && reader_value(index) == 0;
        }
        is_right = is_right && reader_value(3) == 0;
        writer_set(9);
        return is_right && reader_value(0) == 9 && reader_value(1) == 9 && reader_value(2) == 1 && reader_value(3) == 9;
    });
    if (!is_other_thread_right) {
        fail_claim("a second thread has objects of its own, one address in the two libraries, and sees its own writes");
    }
    if (!(reader_value(0) == 7 && reader_value(1) == 7)) {
        fail_claim("the write of the second thread did not reach the main thread");
    }
    ::fixy::check::pass(kTest, "  test_each_thread_object_is_one_for_the_thread: PASSED ({} objects)\n", count);
}

// The cell of the heap of a thread and the root of the allocator of the base
// (foundation/alloc/Root.h).  The cell is one object for each thread, whatever
// library reads it, and its start value is the sentinel in each library: on
// the main thread, which exists before the load of the two libraries, and on
// a new thread.  After the init of the root in the writer, the reader sees the
// map of the host window in the sentinel.  After a claim in the writer, the
// reader sees the same heap id on that thread.
void test_one_heap_cell_for_the_thread(fo::Library const& writer, fo::Library const& reader) {
    auto const writer_cell = find<decltype(process_wide_heap_cell_key)>(writer, "process_wide_heap_cell_key");
    auto const reader_cell = find<decltype(process_wide_heap_cell_key)>(reader, "process_wide_heap_cell_key");
    auto const writer_sentinel = find<decltype(process_wide_heap_is_sentinel)>(writer, "process_wide_heap_is_sentinel");
    auto const reader_sentinel = find<decltype(process_wide_heap_is_sentinel)>(reader, "process_wide_heap_is_sentinel");
    if (!(writer_cell() == reader_cell())) fail_claim("the two libraries see one heap cell on the main thread");
    if (!(writer_sentinel() == 1 && reader_sentinel() == 1)) {
        fail_claim("the heap cell of the main thread names the sentinel in each library before the claim");
    }
    ::fixy::AddressKey const main_cell = writer_cell();
    bool const is_other_thread_right =
        on_other_thread([writer_cell, reader_cell, writer_sentinel, reader_sentinel, main_cell] noexcept {
            bool const is_one_cell = writer_cell() == reader_cell() && !(writer_cell() == main_cell);
            return is_one_cell && writer_sentinel() == 1 && reader_sentinel() == 1;
        });
    if (!is_other_thread_right) {
        fail_claim("a new thread has a heap cell of its own, one in the two libraries, that names the sentinel");
    }

    find<decltype(process_wide_start_alloc_root)>(writer, "process_wide_start_alloc_root")();
    auto const reader_map =
        find<decltype(process_wide_sentinel_holds_host_map)>(reader, "process_wide_sentinel_holds_host_map");
    if (reader_map() != 1) fail_claim("the reader sees the map of the host window in the sentinel after the init");

    find<decltype(process_wide_claim_heap)>(writer, "process_wide_claim_heap")();
    auto const writer_id = find<decltype(process_wide_heap_id)>(writer, "process_wide_heap_id");
    auto const reader_id = find<decltype(process_wide_heap_id)>(reader, "process_wide_heap_id");
    if (!(writer_id() != 0 && writer_id() == reader_id() && reader_sentinel() == 0)) {
        fail_claim("after a claim in the writer, the reader sees the same heap id on the thread");
    }
    ::fixy::check::pass(kTest, "  test_one_heap_cell_for_the_thread: PASSED (heap id {})\n", reader_id());
}

void test_the_reader_sees_each_write(fo::Library const& writer, fo::Library const& reader) {
    find<decltype(process_wide_write)>(writer, "process_wide_write")();
    auto const classify = find<decltype(process_wide_classify)>(reader, "process_wide_classify");
    auto const schema_name = find<decltype(process_wide_schema_name)>(reader, "process_wide_schema_name");

    if (classify(pwl::kPublishedHash) != std::to_underlying(crucible::CKernelId::GEMM_MM)) {
        fail_claim("the reader library sees the static kernel table that the writer published");
    }
    if (classify(pwl::kRegisteredHash) != std::to_underlying(crucible::CKernelId::EWISE_ADD)) {
        fail_claim("the reader library sees the kernel that the writer registered");
    }
    if (!(schema_name(pwl::kNamedHash) == ::fixy::TextView{pwl::kSchemaName})) {
        fail_claim("the reader library sees the schema name that the writer registered");
    }
    ::fixy::check::pass(kTest, "  test_the_reader_sees_each_write: PASSED\n");
}

// The second claim ends the process, so it runs on a second thread of a
// child through expect_dies of the Check family.  The test process has one
// thread at the fork, so the child may start the second thread.
void test_the_reader_sees_the_claim(::fixy::TestRunnerCtx const& test, fo::Library const& writer,
                                    fo::Library const& reader) {
    auto const is_usable = find<decltype(process_wide_is_brand_usable)>(reader, "process_wide_is_brand_usable");
    auto const claim_a_second_brand =
        find<decltype(process_wide_claim_a_second_brand)>(reader, "process_wide_claim_a_second_brand");

    void* const brand = find<decltype(process_wide_claim_brand)>(writer, "process_wide_claim_brand")();
    if (is_usable() != 1) fail_claim("the thread that holds the claim can use the brand");
    if (on_other_thread(is_usable) != 0) {
        fail_claim("the reader library refuses a thread that holds no claim, because it sees the writer's claim");
    }
    ::fixy::check::expect_dies(
        test,
        [claim_a_second_brand] noexcept {
            ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
            auto body = claim_a_second_brand;
            static_cast<void>(::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, body));
        },
        ::fixy::TextView{"foundation: fatal invariant failed: record != nullptr"});

    find<decltype(process_wide_release_brand)>(writer, "process_wide_release_brand")(brand);
    if (on_other_thread(is_usable) != 1) fail_claim("the claim ends when its brand is destroyed");
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
    ::fixy::Option<fo::Library> const control = try_load(STATIC_TLS_CONTROL_PATH);
    if (control.is_none()) {
        fail_claim("a library whose initial-exec block holds the limit of the rule static-tls loads after the "
                   "writer and the reader");
    }
    ::fixy::Option<fo::Library> const plant = try_load(STATIC_TLS_PLANT_PATH);
    if (plant.is_some()) {
        fail_claim("dlopen refuses a library whose initial-exec block is larger than the free static TLS of glibc");
    }
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
    fo::Library const writer = load(PROCESS_WIDE_WRITER_PATH);
    fo::Library const reader = load(PROCESS_WIDE_READER_PATH);
    if (is_selected(::fixy::TextView{"addresses"})) test_each_object_has_one_address(writer, reader);
    if (is_selected(::fixy::TextView{"threads"})) test_each_thread_object_is_one_for_the_thread(writer, reader);
    if (is_selected(::fixy::TextView{"heaps"})) test_one_heap_cell_for_the_thread(writer, reader);
    if (is_selected(::fixy::TextView{"writes"})) test_the_reader_sees_each_write(writer, reader);
    if (is_selected(::fixy::TextView{"claim"})) test_the_reader_sees_the_claim(test, writer, reader);
    if (is_selected(::fixy::TextView{"static_tls"})) test_static_tls_holds_the_rule_threshold();
    ::fixy::check::pass(test, "test_process_wide_across_libraries: all tests passed\n");
    return 0;
}
