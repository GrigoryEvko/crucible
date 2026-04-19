// render_trace: visualize .crtrace files as SVG.
//
// Usage:
//   render_trace <trace.crtrace> [-o output.svg] [--depth N]
//
// Options:
//   -o FILE      Output SVG file (default: <trace>.svg next to the input).
//   --depth N    Scope grouping depth (default 4, clamped to [1, 16]).
//                  3 = one block per transformer layer
//                  4 = attention/mlp/norm separated
//                  5 = individual submodules (query/key/value)
//
// Input format — .crtrace binary (little-endian), produced by Vessel.
// Full schema in include/crucible/TraceLoader.h:
//   Header (16B):   "CRTR" magic | version | num_ops | num_metas
//   Op records:     num_ops × 80B (schema/shape/scope/callsite hashes,
//                   scalar args, tensor counts, op_flags)
//   Meta records:   num_metas × 168B (TensorMeta; 144/160B legacy
//                   layouts are auto-detected)
//   Schema names:   optional trailing table, length-prefixed ASCII
//                   registered into the global SchemaTable on load.
//
// Open the emitted .svg in a browser for zoom/pan and hover tooltips.
//
// Exit codes: 0 on success, 1 on any error (bad args, I/O failure,
// malformed trace). Error diagnostics go to stderr.

// ── Includes: stdlib → crucible → (no local) ─────────────────────────

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include <crucible/vis/TraceVisualizer.h>

namespace {

// ── Small helpers (all [[nodiscard]]) ────────────────────────────────

[[nodiscard]] constexpr std::string_view program_name(int argc, char** argv) {
  // POSIX allows argc==0; fall back to a fixed name rather than deref
  // a possibly-null argv[0].
  return (argc > 0 && argv != nullptr && argv[0] != nullptr)
      ? std::string_view{argv[0]}
      : std::string_view{"render_trace"};
}

[[nodiscard]] int print_usage(std::string_view prog, std::FILE* stream) {
  std::fprintf(stream,
      "Usage: %.*s <trace.crtrace> [-o output.svg] [--depth N]\n"
      "\n"
      "Options:\n"
      "  -o FILE      Output SVG file (default: <trace>.svg)\n"
      "  --depth N    Scope grouping depth (default: 4, range [1,16])\n"
      "                 3 = one block per transformer layer\n"
      "                 4 = attention/mlp/norm separated\n"
      "                 5 = individual submodules\n",
      static_cast<int>(prog.size()), prog.data());
  return 1;
}

// Parse an unsigned integer in [lo, hi]. Returns false on garbage,
// overflow, trailing characters, or range violation — every failure
// mode surfaces as an actionable stderr message.
[[nodiscard]] bool parse_depth(const char* text, uint32_t lo, uint32_t hi,
                               uint32_t& out) {
  if (text == nullptr || *text == '\0') return false;
  uint32_t value = 0;
  const char* const end = text + std::strlen(text);
  const auto [ptr, ec] = std::from_chars(text, end, value);
  if (ec != std::errc{} || ptr != end) return false;
  if (value < lo || value > hi) return false;
  out = value;
  return true;
}

// Derive a default output path by swapping the trace file's extension
// to .svg. Handles paths with dots inside directory components
// correctly (e.g. "./x.dir/trace" → "./x.dir/trace.svg").
[[nodiscard]] std::string default_svg_path(std::string_view trace_path) {
  std::string out(trace_path);
  // rfind('.') must come AFTER the last path separator — otherwise a
  // dot in a directory name would truncate the basename.
  const auto last_sep = out.find_last_of("/\\");
  const auto search_from = (last_sep == std::string::npos) ? 0 : last_sep + 1;
  const auto dot = out.find_last_of('.');
  if (dot != std::string::npos && dot >= search_from) {
    out.resize(dot);
  }
  out.append(".svg");
  return out;
}

// Basename — used as the embedded SVG <title>. String_view into the
// caller's buffer; caller keeps the original argv alive for the whole
// process lifetime, so no dangling.
[[nodiscard]] std::string_view basename_of(std::string_view path) {
  const auto slash = path.find_last_of("/\\");
  return (slash == std::string_view::npos) ? path : path.substr(slash + 1);
}

[[nodiscard]] const char* arch_label(crucible::vis::Architecture a) {
  switch (a) {
    case crucible::vis::Architecture::UNET: return "UNet";
    case crucible::vis::Architecture::VIT:  return "ViT";
    default:                                return "generic";
  }
}

} // namespace

int main(int argc, char** argv) {
  const std::string_view prog = program_name(argc, argv);

  if (argc < 2) {
    return print_usage(prog, stderr);
  }

  // Handle --help / -h anywhere on the command line before they get
  // misinterpreted as a trace path.
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == nullptr) continue;  // paranoia vs nonconforming launcher
    const std::string_view arg{argv[i]};
    if (arg == "-h" || arg == "--help") {
      (void)print_usage(prog, stdout);
      return 0;
    }
  }

  // ── Parse CLI (bounds-checked throughout) ─────────────────────────
  const char* trace_path = argv[1];
  const char* output_path_cli = nullptr;
  uint32_t scope_depth = 4;

  for (int i = 2; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "-o") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "render_trace: -o requires a path argument\n");
        return print_usage(prog, stderr);
      }
      output_path_cli = argv[++i];
    } else if (arg == "--depth") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "render_trace: --depth requires an integer\n");
        return print_usage(prog, stderr);
      }
      if (!parse_depth(argv[++i], 1u, 16u, scope_depth)) {
        std::fprintf(stderr,
            "render_trace: --depth must be an integer in [1, 16], got '%s'\n",
            argv[i]);
        return 1;
      }
    } else {
      std::fprintf(stderr, "render_trace: unknown argument '%.*s'\n",
                   static_cast<int>(arg.size()), arg.data());
      return print_usage(prog, stderr);
    }
  }

  // Default output path: same stem, .svg extension.
  std::string default_out;
  if (output_path_cli == nullptr) {
    default_out = default_svg_path(trace_path);
  }
  const char* const output_path =
      (output_path_cli != nullptr) ? output_path_cli : default_out.c_str();

  // ── Load trace ────────────────────────────────────────────────────
  std::fprintf(stderr, "Loading %s...\n", trace_path);
  auto trace = crucible::load_trace(trace_path);
  if (!trace) {
    // load_trace already prints a specific reason to stderr; add a
    // top-level marker so scripts parsing our output can key off it.
    std::fprintf(stderr, "render_trace: failed to load '%s'\n", trace_path);
    return 1;
  }
  std::fprintf(stderr, "  %u ops, %u metas\n",
               trace->num_ops, trace->num_metas);

  // ── Detect blocks ─────────────────────────────────────────────────
  std::fprintf(stderr, "Detecting blocks (depth=%u)...\n", scope_depth);
  const auto ops = crucible::vis::build_ops(*trace);
  const auto detection = crucible::vis::detect_blocks(*trace, scope_depth);
  std::fprintf(stderr, "  %zu blocks (%s)\n",
               detection.blocks.size(), arch_label(detection.architecture));

  // ── Render SVG ────────────────────────────────────────────────────
  std::fprintf(stderr, "Rendering...\n");
  const std::string_view title = basename_of(trace_path);
  const std::string svg =
      crucible::vis::render_block_svg(detection, ops, title);

  // ── Write output ──────────────────────────────────────────────────
  // Open + write + close explicitly so we can report the exact fopen
  // errno and catch any stream failure at close time (disk full,
  // short write, etc.) — ofstream destructors swallow errors.
  std::ofstream out(output_path, std::ios::out | std::ios::binary |
                                 std::ios::trunc);
  if (!out) {
    const int saved_errno = errno;
    std::fprintf(stderr, "render_trace: cannot open '%s' for writing: %s\n",
                 output_path, std::strerror(saved_errno));
    return 1;
  }
  out.write(svg.data(), static_cast<std::streamsize>(svg.size()));
  out.close();  // may set failbit on flush failure
  if (out.fail()) {
    std::fprintf(stderr, "render_trace: write failed for '%s'\n", output_path);
    return 1;
  }

  std::fprintf(stderr, "  %zu bytes -> %s\n", svg.size(), output_path);
  return 0;
}
