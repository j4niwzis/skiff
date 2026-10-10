import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.text;
import gtest;
#include "gtest/gtest-macros.h"

namespace {
struct inline_images {
  static inline skia::Sp<skia::SkImage> image;
  static inline int drawn = 0;
  static std::optional<skiff::scene::PillPicture> pill(std::string_view) { return std::nullopt; }
  static const skia::Sp<skia::SkImage>* picture(std::string_view) { ++drawn; return &image; }
};
std::string frame_data(std::string_view png) {
  std::string out;
  for (std::size_t at = 8; at + 12 <= png.size();) {
    const auto length = skia::apng::word(png, at);
    if (png.substr(at + 4, 4) == "IDAT") out += png.substr(at + 8, length);
    at += length + 12;
  }
  return out;
}

TEST(Images, SingleLineTextDrawsBothRepeatedInlineImagesWithOrWithoutElision) {
  auto manager = skia::SkFontMgr_New_Custom_Directory("/usr/share/fonts");
  auto face = manager ? manager->matchFamilyStyle("DejaVu Sans", skia::SkFontStyle()) : nullptr;
  if (!face) GTEST_SKIP() << "Needs a font for inline image advances";
  skiff::paint::fonts().setPrimary(face);
  skia::SkFont font(face);
  skiff::paint::defaultFont() = &font;
  struct cleanup { ~cleanup() { skiff::paint::defaultFont() = nullptr; inline_images::image.reset(); } } clear;
  const std::array<std::uint8_t, 4> red{255, 0, 0, 255};
  inline_images::image = skia::imageFromRGBA(1, 1, red.data());
  for (const bool elided : {false, true}) {
    skiff::nodes::BasicText<inline_images> text("\u2003 \u2003", 13.0f, skia::colorSetARGB(255, 0, 0, 0));
    text.setWrapped(false);
    text.setElided(elided);
    text.setLinks({{0, 3, "image", false, true}, {4, 7, "image", false, true}}, skia::colorSetARGB(255, 0, 0, 0));
    skiff::scene::layout(text, skia::SkRect::MakeWH(64.0f, 32.0f));
    skia::SkBitmap pixels;
    ASSERT_TRUE(pixels.tryAllocN32Pixels(64, 32));
    pixels.eraseColor(0);
    skia::SkCanvas canvas(pixels);
    inline_images::drawn = 0;
    text.drawSelf(&canvas, 1.0f);
    EXPECT_EQ(inline_images::drawn, 2);
    int red_pixels = 0;
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 64; ++x)
        red_pixels += pixels.getColor(x, y) == skia::colorSetARGB(255, 255, 0, 0);
    EXPECT_GT(red_pixels, 0);
  }
}
TEST(Images, MentionPillsReserveTheirFullHeight) {
  skiff::nodes::Text plain("Name", 16.0f, skia::colorSetARGB(255, 0, 0, 0));
  skiff::nodes::Text pill("Name", 16.0f, skia::colorSetARGB(255, 0, 0, 0));
  pill.setLinks({{0, 4, "user", true, false}}, skia::colorSetARGB(255, 0, 0, 0));
  EXPECT_FLOAT_EQ(plain.lineHeight(), 20.0f);
  EXPECT_FLOAT_EQ(pill.lineHeight(), 24.0f);
}

void control(std::string& png, std::uint32_t seq, std::uint32_t width, std::uint32_t x,
             int numerator, int denominator, int disposal, int blend) {
  std::string bytes;
  for (auto value : {seq, width, 1u, x, 0u}) skia::apng::word(bytes, value);
  for (auto value : {numerator, denominator}) {
    bytes += static_cast<char>(value >> 8); bytes += static_cast<char>(value);
  }
  bytes += static_cast<char>(disposal); bytes += static_cast<char>(blend);
  skia::apng::chunk(png, "fcTL", bytes);
}
}

TEST(Images, AnimatedPngUsesFrameDurationsOffsetsAndBackgroundDisposal) {
  const std::array<std::uint8_t, 8> red{255, 0, 0, 255, 255, 0, 0, 255};
  const std::array<std::uint8_t, 4> blue{0, 0, 255, 255};
  auto first = skia::imageFromRGBA(2, 1, red.data());
  auto second = skia::imageFromRGBA(1, 1, blue.data());
  auto encoded = skia::encodeImage(*first, false);
  auto encoded_second = skia::encodeImage(*second, false);
  if (encoded.empty() || encoded_second.empty()) GTEST_SKIP() << "PNG codec not enabled";
  std::string png = encoded.substr(0, 8 + 25); // PNG signature and IHDR.
  std::string animation;
  skia::apng::word(animation, 2); skia::apng::word(animation, 0);
  skia::apng::chunk(png, "acTL", animation);
  control(png, 0, 2, 0, 1, 10, 1, 0);
  skia::apng::chunk(png, "IDAT", frame_data(encoded));
  control(png, 1, 1, 1, 1, 20, 0, 1);
  std::string data;
  skia::apng::word(data, 2); data += frame_data(encoded_second);
  skia::apng::chunk(png, "fdAT", data);
  skia::apng::chunk(png, "IEND", {});
  const auto frames = skia::decodeFrames(png.data(), png.size());
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0].durationMs, 100);
  EXPECT_EQ(frames[1].durationMs, 50);
  skia::SkBitmap pixels;
  ASSERT_TRUE(pixels.tryAllocPixels(frames[1].image->imageInfo()));
  ASSERT_TRUE(frames[1].image->readPixels(pixels.pixmap(), 0, 0));
  EXPECT_EQ(pixels.getColor(0, 0), skia::colorSetARGB(0, 0, 0, 0));
  EXPECT_EQ(pixels.getColor(1, 0), skia::colorSetARGB(255, 0, 0, 255));
  EXPECT_EQ(skia::decodeFrames(png.data(), png.size(), 8).size(), 1u);
  // A truncated frame must not read outside the encoded bytes.
  EXPECT_LE(skia::decodeFrames(png.data(), png.size() - 15).size(), 1u);
}

TEST(Text, SelectedFragmentKeepsInlineImageSources) {
  skiff::nodes::BasicText<inline_images> text("Hi \u2003!", 13.0f, skia::SkColor{0});
  text.setSelectable(true);
  text.setLinks({{.first = 3, .last = 6, .target = "mxc://remote/neocat", .picture = true, .plain = ":neocat:"}}, skia::SkColor{0});
  skiff::scene::Reply reply;
  text.onKey(skiff::scene::phase::target{}, skiff::scene::key::down{skiff::scene::keys::kA,
      skiff::scene::Modifiers{}.with<skiff::scene::modifier::control>()}, reply);
  const auto selected = text.selectedFragment();
  EXPECT_EQ(selected.text, "Hi :neocat:!");
  ASSERT_EQ(selected.atoms.size(), 1u);
  EXPECT_EQ(selected.atoms[0].target, "mxc://remote/neocat");
  EXPECT_EQ(selected.atoms[0].first, 3u);
  EXPECT_EQ(selected.atoms[0].last, 6u);
}
