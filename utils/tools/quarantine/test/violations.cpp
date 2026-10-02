// Each class of finding that the quarantine plugin reports, and uses that it
// must not report.  check_plugin.py holds the line of each expectation.

#include <fixy/Shelf.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace probe {

std::vector<int> global_numbers;

struct Holder {
    int* raw_member = nullptr;
    int array_member[4] = {};
    void (*callback)(int) = nullptr;
    int Holder::* member_pointer = nullptr;
    std::string name;
};

struct Local {
    int value = 0;
};

std::string describe(const std::string& text);

void copy_bytes(char* target, const char* source) { std::memcpy(target, source, 4); }

int make_and_drop() {
    int* made = new int(3);
    int value = *made;
    delete made;
    return value;
}

std::size_t count_letters(const char* text) { return std::strlen(text); }

void swap_numbers() {
    std::vector<int> numbers{3, 1, 2};
    std::swap(numbers[0], numbers[1]);
}

int move_value(int value) {
    int moved = std::move(value);
    return moved;
}

template <class T>
void pattern_only(T value) {
    std::vector<T> items;
    items.push_back(value);
    if constexpr (std::is_trivially_copyable_v<T>) {
        std::memcpy(&value, &value, sizeof value);
    }
}

void use_the_base() {
    fixy::Shelf<Local> shelf;
    shelf.fill(Local{});
    char bytes[8] = {};
    fixy::clear_bytes(bytes, sizeof bytes);
}

void hold_a_c_struct() {
    std::max_align_t aligned_storage{};
    static_cast<void>(aligned_storage);
}

template <class T>
T* make_in_pattern(T value) {
    return new T(value);
}

template <class T>
auto size_of_dependent(const T& container) {
    return container.size();
}

std::size_t size_of_numbers() { return size_of_dependent(global_numbers); }

unsigned long limit_of_name() { return fixy::limit_of("name"); }

unsigned line_of_call() { return fixy::line_of(); }

std::string_view view_of(const std::string& text) { return text; }

}  // namespace probe

#include <atomic>

int load_acquired(const std::atomic<int>& counter) { return counter.load(std::memory_order_acquire); }

unsigned line_of_template_call() { return fixy::line_of_value(1); }

template <class T>
std::size_t two_views(const T& text) {
    std::size_t first = std::string_view{text}.size();
    std::size_t second = std::string_view{text}.size();
    return first + second;
}
