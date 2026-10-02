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
// the control: each has a copy in each library.

#include "process_wide_libraries.h"
#include "test_assert.h"

#include <crucible/CKernel.h>

#include <dlfcn.h>

#include <bit>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>

namespace {

namespace pwl = process_wide_libraries;

// One library, loaded as the vessel libraries are loaded.  The handle is
// never closed: a library that defines a process-wide object stays loaded.
struct Library {
    void* handle = nullptr;

    explicit Library(const char* path) noexcept : handle{dlopen(path, RTLD_NOW | RTLD_LOCAL)} {
        if (handle == nullptr) std::fprintf(stderr, "dlopen of %s failed: %s\n", path, dlerror());
        assert(handle != nullptr);
    }

    template <class Function>
    [[nodiscard]] Function* symbol(const char* name) const noexcept {
        void* const address = dlsym(handle, name);
        if (address == nullptr) std::fprintf(stderr, "dlsym of %s failed: %s\n", name, dlerror());
        assert(address != nullptr);
        return std::bit_cast<Function*>(address);
    }
};

// The assert of this tree ends the process with no message, so the claim is
// printed first.
void require(bool is_holding, const char* claim) {
    if (!is_holding) std::fprintf(stderr, "  FAILED: %s\n", claim);
    assert(is_holding);
}

template <class Body>
[[nodiscard]] auto on_other_thread(Body body) {
    decltype(body()) result{};
    std::jthread{[&] { result = body(); }}.join();
    return result;
}

void test_each_object_has_one_address(const Library& writer, const Library& reader) {
    auto* const writer_address = writer.symbol<decltype(process_wide_object_address)>("process_wide_object_address");
    auto* const reader_address = reader.symbol<decltype(process_wide_object_address)>("process_wide_object_address");
    unsigned index = 0;
    while (writer_address(index) != nullptr) {
        if (writer_address(index) != reader_address(index))
            std::fprintf(stderr, "  object %u: the writer sees %p, the reader sees %p\n", index, writer_address(index),
                         reader_address(index));
        assert(writer_address(index) == reader_address(index));
        ++index;
    }
    assert(reader_address(index) == nullptr);
    assert(index > 0);

    auto* const writer_control = writer.symbol<decltype(process_wide_control_address)>("process_wide_control_address");
    auto* const reader_control = reader.symbol<decltype(process_wide_control_address)>("process_wide_control_address");
    for (unsigned control = 0; writer_control(control) != nullptr; ++control)
        require(writer_control(control) != reader_control(control),
                "an object with no marker has a copy in each library, so the two libraries are loaded apart");
    crucible::test::pass("  test_each_object_has_one_address: PASSED ({} objects)\n", index);
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

    const void* const main_address = writer_address(0);
    const bool is_other_thread_right = on_other_thread([&] {
        bool is_right = writer_address(0) != main_address;
        for (unsigned index = 0; index < count; ++index) {
            is_right = is_right && writer_address(index) == reader_address(index);
            is_right = is_right && reader_value(index) == 0;
        }
        writer_set(9);
        return is_right && reader_value(0) == 9 && reader_value(1) == 9 && reader_value(2) == 1;
    });
    require(is_other_thread_right,
            "a second thread has objects of its own, one address in the two libraries, and sees its own writes");
    require(reader_value(0) == 7 && reader_value(1) == 7,
            "the write of the second thread did not reach the main thread");
    crucible::test::pass("  test_each_thread_object_is_one_for_the_thread: PASSED ({} objects)\n", count);
}

void test_the_reader_sees_each_write(const Library& writer, const Library& reader) {
    writer.symbol<decltype(process_wide_write)>("process_wide_write")();
    auto* const classify = reader.symbol<decltype(process_wide_classify)>("process_wide_classify");
    auto* const schema_name = reader.symbol<decltype(process_wide_schema_name)>("process_wide_schema_name");

    require(classify(pwl::kPublishedHash) == std::to_underlying(crucible::CKernelId::GEMM_MM),
            "the reader library sees the static kernel table that the writer published");
    require(classify(pwl::kRegisteredHash) == std::to_underlying(crucible::CKernelId::EWISE_ADD),
            "the reader library sees the kernel that the writer registered");
    const char* const name = schema_name(pwl::kNamedHash);
    require(name != nullptr && std::strcmp(name, pwl::kSchemaName) == 0,
            "the reader library sees the schema name that the writer registered");
    crucible::test::pass("  test_the_reader_sees_each_write: PASSED\n");
}

void test_the_reader_sees_the_claim(const Library& writer, const Library& reader) {
    auto* const is_usable = reader.symbol<decltype(process_wide_is_brand_usable)>("process_wide_is_brand_usable");
    auto* const does_second_claim_abort =
        reader.symbol<decltype(process_wide_does_second_claim_abort)>("process_wide_does_second_claim_abort");

    void* const brand = writer.symbol<decltype(process_wide_claim_brand)>("process_wide_claim_brand")();
    require(is_usable() == 1, "the thread that holds the claim can use the brand");
    require(on_other_thread(is_usable) == 0,
            "the reader library refuses a thread that holds no claim, because it sees the writer's claim");
    require(on_other_thread(does_second_claim_abort) == 1,
            "the reader library refuses a second thread's claim while the writer's claim is live");

    writer.symbol<decltype(process_wide_release_brand)>("process_wide_release_brand")(brand);
    require(on_other_thread(is_usable) == 1, "the claim ends when its brand is destroyed");
    crucible::test::pass("  test_the_reader_sees_the_claim: PASSED\n");
}

}  // namespace

// With no argument, every case runs.  One argument names the one case to run,
// so that each case can be seen to fail on its own.
int main(int argc, char** argv) {
    const Library writer{PROCESS_WIDE_WRITER_PATH};
    const Library reader{PROCESS_WIDE_READER_PATH};
    const auto is_selected = [argc, argv](const char* name) { return argc < 2 || std::strcmp(argv[1], name) == 0; };
    ::fixy::report(::fixy::Sink::Out, "test_process_wide_across_libraries:\n");
    if (is_selected("addresses")) test_each_object_has_one_address(writer, reader);
    if (is_selected("threads")) test_each_thread_object_is_one_for_the_thread(writer, reader);
    if (is_selected("writes")) test_the_reader_sees_each_write(writer, reader);
    if (is_selected("claim")) test_the_reader_sees_the_claim(writer, reader);
    crucible::test::pass("test_process_wide_across_libraries: all tests passed\n");
    return 0;
}
