#pragma once

// This is not a hot path, so std::string is appropriate.  The bodies of the
// members are in src/vis/SvgRenderer.cpp.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace crucible::vis {

struct Color {
    uint8_t r = 0, g = 0, b = 0;

    [[nodiscard]] constexpr Color() = default;
    [[nodiscard]] constexpr Color(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}

    [[nodiscard]] static constexpr Color hex(uint32_t rgb) {
        return {static_cast<uint8_t>((rgb >> 16) & 0xFF), static_cast<uint8_t>((rgb >> 8) & 0xFF),
                static_cast<uint8_t>(rgb & 0xFF)};
    }

    [[nodiscard]] std::string to_svg() const;
};

namespace palette {
inline constexpr Color GEMM_FILL = Color::hex(0xDBEAFE);
inline constexpr Color GEMM_BORDER = Color::hex(0x1E40AF);
inline constexpr Color CONV_FILL = Color::hex(0xD1FAE5);
inline constexpr Color CONV_BORDER = Color::hex(0x065F46);
inline constexpr Color ATTN_FILL = Color::hex(0xFEF3C7);
inline constexpr Color ATTN_BORDER = Color::hex(0x92400E);
inline constexpr Color NORM_FILL = Color::hex(0xEDE9FE);
inline constexpr Color NORM_BORDER = Color::hex(0x5B21B6);
inline constexpr Color ACT_FILL = Color::hex(0xFFEDD5);
inline constexpr Color ACT_BORDER = Color::hex(0x9A3412);
inline constexpr Color ELEM_FILL = Color::hex(0xF3F4F6);
inline constexpr Color ELEM_BORDER = Color::hex(0x374151);
inline constexpr Color MOVE_FILL = Color::hex(0xE0F2FE);
inline constexpr Color MOVE_BORDER = Color::hex(0x0C4A6E);
inline constexpr Color LOSS_FILL = Color::hex(0xFFF1F2);
inline constexpr Color LOSS_BORDER = Color::hex(0xBE123C);
inline constexpr Color OPTIM_FILL = Color::hex(0xFDF2F8);
inline constexpr Color OPTIM_BORDER = Color::hex(0x9D174D);
inline constexpr Color REDUCE_FILL = Color::hex(0xFEE2E2);
inline constexpr Color REDUCE_BORDER = Color::hex(0x991B1B);
inline constexpr Color OTHER_FILL = Color::hex(0xF9FAFB);
inline constexpr Color OTHER_BORDER = Color::hex(0x9CA3AF);

inline constexpr Color BLOCK_RESBLOCK = Color::hex(0xD1FAE5);
inline constexpr Color BLOCK_ATTN = Color::hex(0xFEF3C7);
inline constexpr Color BLOCK_MLP = Color::hex(0xDBEAFE);
inline constexpr Color BLOCK_CONV = Color::hex(0xE0F2FE);
inline constexpr Color BLOCK_LOSS = Color::hex(0xFFF1F2);
inline constexpr Color BLOCK_OPTIM = Color::hex(0xFDF2F8);
inline constexpr Color BLOCK_GENERIC = Color::hex(0xF3F4F6);

inline constexpr Color BLOCK_BWD_RESBLOCK = Color::hex(0xA7F3D0);
inline constexpr Color BLOCK_BWD_ATTN = Color::hex(0xFDE68A);
inline constexpr Color BLOCK_BWD_MLP = Color::hex(0xBFDBFE);
inline constexpr Color BLOCK_BWD_GENERIC = Color::hex(0xE5E7EB);

inline constexpr Color EDGE_DATA_FLOW = Color::hex(0x94A3B8);
inline constexpr Color EDGE_SKIP = Color::hex(0xF97316);
inline constexpr Color BG = Color::hex(0xFCFCFC);
}  // namespace palette

class SvgRenderer {
public:
    SvgRenderer() = default;

    void begin(float width, float height, std::string_view title = {});

    void end() { buf_ += "</svg>\n"; }

    // Call this before end().
    void embed_interactivity();

    void begin_group(std::string_view id = {}, float tx = 0, float ty = 0);

    void end_group() { buf_ += "</g>\n"; }

    void begin_block_group(std::string_view info = {});

    void rect(float x, float y, float w, float h, Color fill, Color stroke, float rx = 4, float stroke_width = 0.8f,
              bool shadow = false);

    void text(float x, float y, std::string_view content, float font_size = 10, Color fill = Color::hex(0x1F2937),
              std::string_view anchor = "start", bool bold = false);

    void text_mono(float x, float y, std::string_view content, float font_size = 8, Color fill = Color::hex(0x6B7280));

    void line(float x1, float y1, float x2, float y2, Color stroke = palette::EDGE_DATA_FLOW, float width = 0.5f,
              bool dashed = false);

    void arrow(float x1, float y1, float x2, float y2, Color stroke = palette::EDGE_DATA_FLOW, float width = 0.6f,
               bool skip = false);

    void bezier_arrow(float x1, float y1, float cx1, float cy1, float cx2, float cy2, float x2, float y2,
                      Color stroke = palette::EDGE_DATA_FLOW, float width = 0.6f, bool skip = false);

    // The curve leaves the source downward, crosses horizontally at the
    // midpoint and enters the target downward, so it stays inside the gap
    // between the two rows and never runs across a block.
    void orthogonal_edge(float x1, float y1, float x2, float y2, Color stroke = palette::EDGE_DATA_FLOW,
                         float width = 0.5f, bool skip = false);

    void rect_dashed(float x, float y, float w, float h, Color stroke, float rx = 6, float stroke_width = 0.8f,
                     std::string_view dash = "6,3");

    void op_node(float x, float y, float w, float h, std::string_view label, Color fill, Color border,
                 float font_size = 7);

    void block_container(float x, float y, float w, float h, std::string_view title, Color fill, Color border,
                         std::string_view subtitle = {});

    void separator(float x, float y, float w, Color stroke = Color::hex(0xE5E7EB));

    [[nodiscard]] const std::string& str() const { return buf_; }
    [[nodiscard]] std::string&& take() { return std::move(buf_); }

private:
    std::string buf_;

    [[nodiscard]] static std::string ftoa(float v);

    void xml_escape(std::string_view s);
};

}  // namespace crucible::vis
