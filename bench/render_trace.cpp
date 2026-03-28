// render_trace: visualize .crtrace files as SVG.
//
// Usage:
//   render_trace <trace.crtrace> [-o output.svg] [--depth N]
//
// Options:
//   -o FILE      Output SVG file (default: <trace>.svg)
//   --depth N    Scope grouping depth (default: 4).
//                  3 = one block per transformer layer
//                  4 = attention/mlp/norm separated
//                  5 = individual submodules (query/key/value)
//
// Open the .svg in a browser for zoom/pan and hover tooltips.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include <crucible/vis/TraceVisualizer.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
        "Usage: %s <trace.crtrace> [-o output.svg] [--depth N]\n"
        "\n"
        "Options:\n"
        "  -o FILE      Output SVG file (default: <trace>.svg)\n"
        "  --depth N    Scope grouping depth (default: 4)\n"
        "                 3 = one block per transformer layer\n"
        "                 4 = attention/mlp/norm separated\n"
        "                 5 = individual submodules\n",
        argv[0]);
    return 1;
  }

  const char* trace_path = argv[1];
  const char* output_path = nullptr;
  uint32_t scope_depth = 4;

  for (int i = 2; i < argc; i++) {
    if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc)
      output_path = argv[++i];
    else if (std::strcmp(argv[i], "--depth") == 0 && i + 1 < argc)
      scope_depth = static_cast<uint32_t>(std::atoi(argv[++i]));
  }

  // Default output: same stem with .svg extension
  std::string default_out;
  if (!output_path) {
    default_out = trace_path;
    auto dot = default_out.rfind('.');
    if (dot != std::string::npos)
      default_out = default_out.substr(0, dot);
    default_out += ".svg";
    output_path = default_out.c_str();
  }

  // Load trace
  std::fprintf(stderr, "Loading %s...\n", trace_path);
  auto trace = crucible::load_trace(trace_path);
  if (!trace) return 1;
  std::fprintf(stderr, "  %u ops, %u metas\n",
               trace->num_ops, trace->num_metas);

  // Detect blocks at configured scope depth
  std::fprintf(stderr, "Detecting blocks (depth=%u)...\n", scope_depth);
  auto ops = crucible::vis::build_ops(*trace);
  auto detection = crucible::vis::detect_blocks(*trace, scope_depth);
  std::fprintf(stderr, "  %zu blocks (%s)\n",
               detection.blocks.size(),
               detection.architecture == crucible::vis::Architecture::UNET
                   ? "UNet" :
               detection.architecture == crucible::vis::Architecture::VIT
                   ? "ViT" : "generic");

  // Render SVG
  std::fprintf(stderr, "Rendering...\n");
  std::string title = trace_path;
  auto slash = title.rfind('/');
  if (slash != std::string::npos) title = title.substr(slash + 1);

  auto svg = crucible::vis::render_block_svg(detection, ops, title);

  // Write output
  std::ofstream out(output_path);
  if (!out) {
    std::fprintf(stderr, "Error: cannot write %s\n", output_path);
    return 1;
  }
  out << svg;
  std::fprintf(stderr, "  %zu bytes -> %s\n", svg.size(), output_path);
  return 0;
}
