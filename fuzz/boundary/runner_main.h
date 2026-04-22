#pragma once

// ═══════════════════════════════════════════════════════════════════
// runner_main.h — standalone replay driver for boundary fuzz harnesses.
//
// Each boundary harness defines:
//
//   extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data,
//                                          size_t size);
//
// When built standalone (CRUCIBLE_FUZZ_STANDALONE_MAIN defined; the
// default in our build), this header provides a main() that walks
// the corpus directory and feeds every regular file through the
// LLVMFuzzerTestOneInput entry point.  Sanitizers (ASan + UBSan)
// catch crashes; the preceding stderr line names the offending file.
//
// Coverage-guided fuzzers (AFL++ in QEMU mode, libFuzzer with Clang)
// can drive the SAME entry point — set CRUCIBLE_FUZZ_STANDALONE_MAIN
// off when linking those.
//
// AFL++ QEMU-mode usage (binary-only, no recompile):
//
//   cmake --build --preset default --target fuzz_serialize
//   afl-fuzz -Q -i fuzz/boundary/corpus -o /tmp/afl_serialize -- ./build/fuzz/fuzz_serialize @@
//
// AFL++ in QEMU mode instruments basic blocks at runtime.  No GCC
// plugin needed, no Clang needed.  Coverage feedback drives mutation.
//
// ═══════════════════════════════════════════════════════════════════

#ifdef CRUCIBLE_FUZZ_STANDALONE_MAIN

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

// LLVMFuzzerTestOneInput is declared by each harness source before
// including this header; no need to redeclare here (suppresses
// -Werror=redundant-decls).

namespace crucible::fuzz::boundary {

[[nodiscard]] inline std::vector<uint8_t>
read_file_bytes(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>{
        std::istreambuf_iterator<char>(f),
        std::istreambuf_iterator<char>()
    };
}

inline int run_corpus_or_file(const char* arg) {
    std::filesystem::path p{arg};
    if (!std::filesystem::exists(p)) {
        std::fprintf(stderr,
            "fuzz: '%s' does not exist\n", arg);
        return 1;
    }

    // Single-file mode (AFL++ persistent mode passes one file via @@).
    if (std::filesystem::is_regular_file(p)) {
        const auto buf = read_file_bytes(p);
        std::fprintf(stderr, "fuzz: %s (%zu bytes)\n",
            arg, buf.size());
        (void)LLVMFuzzerTestOneInput(buf.data(), buf.size());
        return 0;
    }

    // Directory mode (replay every file).
    if (std::filesystem::is_directory(p)) {
        int n = 0;
        for (const auto& entry : std::filesystem::directory_iterator(p)) {
            if (!entry.is_regular_file()) continue;
            const auto buf = read_file_bytes(entry.path());
            std::fprintf(stderr, "fuzz: %s (%zu bytes)\n",
                entry.path().string().c_str(), buf.size());
            (void)LLVMFuzzerTestOneInput(buf.data(), buf.size());
            ++n;
        }
        std::fprintf(stderr, "fuzz: replayed %d corpus entries\n", n);
        return 0;
    }

    std::fprintf(stderr,
        "fuzz: '%s' is neither a regular file nor a directory\n", arg);
    return 1;
}

}  // namespace crucible::fuzz::boundary

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "Usage: %s <file_or_corpus_dir>\n"
            "  Single file → feeds bytes through LLVMFuzzerTestOneInput once.\n"
            "  Directory   → feeds every regular file through it once.\n",
            argv[0]);
        return 1;
    }
    return crucible::fuzz::boundary::run_corpus_or_file(argv[1]);
}

#endif  // CRUCIBLE_FUZZ_STANDALONE_MAIN
