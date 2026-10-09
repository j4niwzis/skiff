module;

// Which backends this Skia has is the build's decision, and the build says
// so: the port puts Skia's own public defines on the interface, so SK_GANESH
// and SK_GRAPHITE are here to be asked rather than assumed. A module that
// named a backend the library was not built with would compile and then fail
// to link, in the program of whoever imported it.
#if defined(SK_GANESH)
#if defined(__ANDROID__)
#define SK_GLES 1
#include <GLES3/gl3.h>
#else
#define SK_GL 1
#include <GL/gl.h>
#endif
#endif

#include <skia/codec/SkCodec.h>
#if defined(SK_CODEC_DECODES_PNG)
#include <skia/codec/SkPngDecoder.h>
#endif
#if defined(SK_CODEC_DECODES_JPEG)
#include <skia/codec/SkJpegDecoder.h>
#endif
#if defined(SK_CODEC_DECODES_GIF)
#include <skia/codec/SkGifDecoder.h>
#endif
#if defined(SK_CODEC_DECODES_WEBP)
#include <skia/codec/SkWebpDecoder.h>
#endif
#if defined(SK_CODEC_ENCODES_PNG)
#include <skia/encode/SkPngEncoder.h>
#endif
#if defined(SK_CODEC_ENCODES_JPEG)
#include <skia/encode/SkJpegEncoder.h>
#endif
#include <skia/core/SkBitmap.h>
#include <skia/core/SkBlendMode.h>
#include <skia/core/SkCanvas.h>
#include <skia/core/SkColor.h>
#include <skia/core/SkColorFilter.h>
#include <skia/core/SkColorSpace.h>
#include <skia/core/SkData.h>
#include <skia/core/SkFont.h>
#include <skia/core/SkFontArguments.h>
#include <skia/core/SkFontMetrics.h>
#include <skia/core/SkFontMgr.h>
#include <skia/core/SkFontStyle.h>
#include <skia/core/SkImage.h>
#include <skia/core/SkMatrix.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkPicture.h>
#include <skia/core/SkPictureRecorder.h>
#include <skia/core/SkPath.h>
#include <skia/core/SkPathBuilder.h>
#include <skia/core/SkPixmap.h>
#include <skia/core/SkPoint.h>
#include <skia/core/SkRect.h>
#include <skia/core/SkRefCnt.h>
#include <skia/core/SkRegion.h>
#include <skia/core/SkSamplingOptions.h>
#include <skia/core/SkShader.h>
#include <skia/core/SkStream.h>
#include <skia/core/SkString.h>
#include <skia/core/SkSurface.h>
#include <skia/core/SkTextBlob.h>
#include <skia/core/SkTypeface.h>
#include <skia/core/SkVertices.h>
#include <skia/effects/SkGradient.h>
#include <skia/effects/SkRuntimeEffect.h>
#include <skia/effects/SkImageFilters.h>
#include <skia/encode/SkPngEncoder.h>
// Budgeted is a question about a surface, not about a backend: it is asked
// wherever an offscreen one is made, and this header is where it lives
// whichever backend is compiled in.
#include <skia/gpu/GpuTypes.h>
#if defined(SK_GANESH)
#include <skia/gpu/ganesh/GrBackendSurface.h>
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/SkSurfaceGanesh.h>
#include <skia/gpu/ganesh/gl/GrGLAssembleInterface.h>
#include <skia/gpu/ganesh/gl/GrGLBackendSurface.h>
#include <skia/gpu/ganesh/gl/GrGLDirectContext.h>
#include <skia/gpu/ganesh/gl/GrGLInterface.h>
#include <skia/gpu/ganesh/gl/GrGLTypes.h>
#endif
#if defined(SK_GRAPHITE)
#include <skia/gpu/graphite/BackendSemaphore.h>
#include <skia/gpu/graphite/BackendTexture.h>
#include <skia/gpu/graphite/Context.h>
#include <skia/gpu/graphite/ContextOptions.h>
#include <skia/gpu/graphite/GraphiteTypes.h>
#include <skia/gpu/graphite/Image.h>
#include <skia/gpu/graphite/ImageProvider.h>
#include <skia/gpu/graphite/Recorder.h>
#include <skia/gpu/graphite/Recording.h>
#include <skia/gpu/graphite/Surface.h>
#include <skia/gpu/graphite/TextureInfo.h>
#if defined(SK_VULKAN)
#include <skia/gpu/MutableTextureState.h>
#include <skia/gpu/graphite/vk/VulkanGraphiteContext.h>
#include <skia/gpu/graphite/vk/VulkanGraphiteTypes.h>
#include <skia/gpu/vk/VulkanBackendContext.h>
#include <skia/gpu/vk/VulkanExtensions.h>
#include <skia/gpu/vk/VulkanMemoryAllocator.h>
#include <skia/gpu/vk/VulkanMutableTextureState.h>
#include <skia/gpu/vk/VulkanTypes.h>
#endif
#endif
#include <skia/ports/SkFontMgr_data.h>
#include <skia/ports/SkFontMgr_directory.h>
#if defined(__ANDROID__)
// The reader of the font configuration every Android device carries. It is
// compiled into Skia only where it means something, so it is included only
// there: a build for anything else has no such header.
#include <skia/ports/SkFontMgr_android.h>
// What turns a font file into a typeface. The Android font manager reads the
// configuration and finds the files; a scanner is what it asks about each of
// them, and it takes one rather than choosing one, because which scanner a
// build has is the build's decision.
#include <skia/ports/SkFontScanner_FreeType.h>
#endif
#include <skia/sksl/SkSLVersion.h>

export module skia;


// sk_sp's comparisons, for those who import this: Skia's own are in its
// headers, which an importer does not see, and its global set of operator==
// cannot be exported whole -- some of it is static (SkISize's). These are
// the same ones, in the same namespace as sk_sp, so that sp == nullptr is
// found through sk_sp itself; != and nullptr == sp are C++20's rewritings of
// them. Constrained, so that where Skia's are seen as well -- here -- these
// are taken over them rather than clash.
export template <class T>
  requires true
[[nodiscard]] inline bool operator==(const ::sk_sp<T> &a, std::nullptr_t) noexcept {
  return !a;
}
export template <class T, class U>
  requires true
[[nodiscard]] inline bool operator==(const ::sk_sp<T> &a, const ::sk_sp<U> &b) noexcept {
  return a.get() == b.get();
}

export namespace skia {

template <class T> using Sp = ::sk_sp<T>;

// The formats the build decodes: PNG and JPEG, and GIF and WebP where Skia
// was built with them.
inline std::vector<::SkCodecs::Decoder> decoders() {
  std::vector<::SkCodecs::Decoder> out;
#if defined(SK_CODEC_DECODES_PNG)
  out.push_back(::SkPngDecoder::Decoder());
#endif
#if defined(SK_CODEC_DECODES_JPEG)
  out.push_back(::SkJpegDecoder::Decoder());
#endif
#if defined(SK_CODEC_DECODES_GIF)
  out.push_back(::SkGifDecoder::Decoder());
#endif
#if defined(SK_CODEC_DECODES_WEBP)
  out.push_back(::SkWebpDecoder::Decoder());
#endif
  return out;
}

// An image from the bytes of a file of it -- as the build decodes them; the
// first frame of an animated one -- or nothing where it is none of them, or
// broken.
// The most pixels a picture is decoded at: 64 Mi, 256 MB as RGBA. A file
// of a few kilobytes can say it is 20000 pixels square, and decoding it
// whole asked for gigabytes before anything scaled it down.
inline constexpr std::int64_t kMostPixels = std::int64_t{64} << 20;

inline ::sk_sp<::SkImage> decodeImage(const void *bytes, std::size_t size) {
  auto codec = ::SkCodec::MakeFromData(::SkData::MakeWithCopy(bytes, size), decoders());
  if (!codec) {
    return nullptr;
  }
  const ::SkISize dimensions = codec->dimensions();
  if (dimensions.isEmpty() || std::int64_t{dimensions.width()} * dimensions.height() > kMostPixels) {
    return nullptr;
  }
  auto [image, result] = codec->getImage();
  return result == ::SkCodec::kSuccess ? image : nullptr;
}

// A picture decoded no larger than it is shown: its longer side at most
// `most` pixels, scaled down smoothly (with mipmaps) where it is larger --
// an avatar of 1024 px, shown at 40, was kept and filtered down whole.
inline ::sk_sp<::SkImage> decodeImageAtMost(const void *bytes, std::size_t size, int most) {
  ::sk_sp<::SkImage> whole = decodeImage(bytes, size);
  if (!whole || most <= 0) {
    return whole;
  }
  const int longer = std::max(whole->width(), whole->height());
  if (longer <= most) {
    return whole;
  }
  const float scale = static_cast<float>(most) / static_cast<float>(longer);
  const int width = std::max(1, static_cast<int>(std::lround(static_cast<float>(whole->width()) * scale)));
  const int height = std::max(1, static_cast<int>(std::lround(static_cast<float>(whole->height()) * scale)));
  ::sk_sp<::SkSurface> surface = ::SkSurfaces::Raster(::SkImageInfo::MakeN32Premul(width, height));
  if (!surface) {
    return whole;
  }
  surface->getCanvas()->drawImageRect(whole, ::SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height)),
                                      ::SkSamplingOptions(::SkFilterMode::kLinear, ::SkMipmapMode::kLinear));
  return surface->makeImageSnapshot();
}

// One frame of an animated picture: whole, as it is shown -- drawn over the
// frame it is drawn on, where it is drawn on one -- and how long it stays.
struct Frame {
  ::sk_sp<::SkImage> image;
  int durationMs = 100;
};
// libpng's SkCodec exposes only the PNG default image. Reconstruct each
// APNG frame as a PNG, then compose it with its blend and disposal rules.
// https://www.w3.org/TR/png-3/#11APNG
namespace apng {
inline std::uint32_t word(std::string_view data, std::size_t at) {
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) value = (value << 8) | static_cast<unsigned char>(data[at + i]);
  return value;
}
inline void word(std::string& data, std::uint32_t value) {
  for (int i = 3; i >= 0; --i) data.push_back(static_cast<char>(value >> (i * 8)));
}
inline std::uint32_t crc(std::string_view bytes) {
  std::uint32_t value = 0xffffffffu;
  for (unsigned char byte : bytes) {
    value ^= byte;
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
  }
  return ~value;
}
inline void chunk(std::string& png, std::string_view type, std::string_view data) {
  word(png, static_cast<std::uint32_t>(data.size()));
  const auto at = png.size();
  png += type;
  png += data;
  word(png, crc(std::string_view(png).substr(at)));
}
inline std::optional<std::vector<Frame>> decode(std::string_view bytes, std::size_t budget) {
  constexpr std::string_view signature{"\x89PNG\r\n\x1a\n", 8};
  if (!bytes.starts_with(signature)) return std::nullopt;
  std::string header, shared, data;
  bool animated = false, controlled = false, seen_data = false;
  std::uint32_t width = 0, height = 0, w = 0, h = 0, x = 0, y = 0, sequence = 0, declared = 0;
  int duration = 100, disposal = 0, blend = 0;
  std::vector<Frame> out;
  ::sk_sp<::SkSurface> surface;
  std::size_t frame_bytes = 0;
  const auto finish = [&]() -> bool {
    if (!controlled) return true; // A default image outside the animation.
    if (data.empty() || !surface) return false;
    if (frame_bytes > budget || out.size() >= budget / frame_bytes) return false;
    std::string png(signature);
    std::string dimensions;
    word(dimensions, w); word(dimensions, h);
    dimensions += std::string_view(header).substr(8);
    chunk(png, "IHDR", dimensions);
    png += shared;
    png += data;
    chunk(png, "IEND", {});
    auto frame = decodeImage(png.data(), png.size());
    if (!frame) return false;
    auto before = disposal == 2 ? surface->makeImageSnapshot() : ::sk_sp<::SkImage>{};
    ::SkPaint paint;
    paint.setBlendMode(blend == 0 ? ::SkBlendMode::kSrc : ::SkBlendMode::kSrcOver);
    surface->getCanvas()->drawImage(frame, static_cast<float>(x), static_cast<float>(y), ::SkSamplingOptions{}, &paint);
    auto image = surface->makeImageSnapshot();
    if (!image) return false;
    out.push_back({std::move(image), duration});
    if (disposal == 1 || (disposal == 2 && out.size() == 1)) {
      ::SkPaint clear;
      clear.setBlendMode(::SkBlendMode::kClear);
      surface->getCanvas()->drawRect(::SkRect::MakeXYWH(x, y, w, h), clear);
    } else if (before) {
      ::SkPaint restore;
      restore.setBlendMode(::SkBlendMode::kSrc);
      surface->getCanvas()->drawImage(before, 0, 0, ::SkSamplingOptions{}, &restore);
    }
    data.clear();
    return true;
  };
  for (std::size_t at = 8; at + 12 <= bytes.size();) {
    const auto length = word(bytes, at);
    if (length > bytes.size() - at - 12) return animated ? std::optional(out) : std::nullopt;
    const auto type = bytes.substr(at + 4, 4), payload = bytes.substr(at + 8, length);
    if (crc(bytes.substr(at + 4, length + 4)) != word(bytes, at + 8 + length))
      return animated ? std::optional(out) : std::nullopt;
    if (type == "IHDR") {
      if (at != 8 || length != 13) return std::nullopt;
      header = payload; width = word(payload, 0); height = word(payload, 4);
      if (!width || !height || std::uint64_t(width) * height > kMostPixels) return std::vector<Frame>{};
      frame_bytes = std::size_t(width) * height * 4;
    } else if (type == "acTL") {
      if (length != 8 || seen_data || header.empty() || !(declared = word(payload, 0))) return std::vector<Frame>{};
      animated = true;
      if (frame_bytes > budget) return std::vector<Frame>{};
      surface = ::SkSurfaces::Raster(::SkImageInfo::MakeN32Premul(static_cast<int>(width), static_cast<int>(height)));
      if (!surface) return std::vector<Frame>{};
      surface->getCanvas()->clear(::SK_ColorTRANSPARENT);
    } else if (type == "fcTL") {
      if (!animated || length != 26 || word(payload, 0) != sequence++) return animated ? std::optional(out) : std::nullopt;
      if (!finish()) return out;
      w = word(payload, 4); h = word(payload, 8); x = word(payload, 12); y = word(payload, 16);
      if (!w || !h || w > width || h > height || x > width - w || y > height - h || out.size() >= declared)
        return out;
      if (!seen_data && (w != width || h != height || x || y)) return out;
      const auto numerator = (static_cast<unsigned char>(payload[20]) << 8) | static_cast<unsigned char>(payload[21]);
      auto denominator = (static_cast<unsigned char>(payload[22]) << 8) | static_cast<unsigned char>(payload[23]);
      if (!denominator) denominator = 100;
      duration = std::max(1, (1000 * numerator + denominator / 2) / denominator);
      disposal = static_cast<unsigned char>(payload[24]); blend = static_cast<unsigned char>(payload[25]);
      if (disposal > 2 || blend > 1) return out;
      controlled = true;
    } else if (type == "IDAT") {
      seen_data = true;
      if (controlled) chunk(data, "IDAT", payload);
    } else if (type == "fdAT") {
      if (!animated || !controlled || length < 4 || word(payload, 0) != sequence++) return animated ? std::optional(out) : std::nullopt;
      chunk(data, "IDAT", payload.substr(4));
    } else if (type == "IEND") {
      if (!animated) return std::nullopt;
      finish();
      return out;
    } else if (!seen_data && type != "IHDR") {
      shared += bytes.substr(at, length + 12);
    }
    at += length + 12;
  }
  return animated ? std::optional(out) : std::nullopt;
}
} // namespace apng
// Every frame of an animated picture (a GIF, an animated WebP), in order; the
// one of a still picture; none where it cannot be read. Frames stop where
// they would pass `budget` bytes of pixels in all: what came so far plays.
inline std::vector<Frame> decodeFrames(const void *bytes, std::size_t size,
                                       std::size_t budget = 64u << 20) {
  if (auto png = apng::decode({static_cast<const char*>(bytes), size}, budget)) return std::move(*png);
  std::vector<Frame> out;
  auto codec = ::SkCodec::MakeFromData(::SkData::MakeWithCopy(bytes, size), decoders());
  if (!codec) {
    return out;
  }
  const ::SkImageInfo info = codec->getInfo().makeColorType(::kN32_SkColorType).makeAlphaType(::kPremul_SkAlphaType);
  if (info.isEmpty() || std::int64_t{info.width()} * info.height() > kMostPixels) {
    return out;
  }
  const int count = std::max(1, codec->getFrameCount());
  std::size_t used = 0;
  for (int i = 0; i < count; ++i) {
    if (used + info.computeMinByteSize() > budget) {
      break;
    }
    ::SkCodec::FrameInfo frame{};
    const bool described = codec->getFrameInfo(i, &frame);
    ::SkBitmap bitmap;
    if (!bitmap.tryAllocPixels(info)) {
      break;
    }
    ::SkCodec::Options options;
    options.fFrameIndex = i;
    // Drawn over another: that one's pixels first.
    if (described && frame.fRequiredFrame != ::SkCodec::kNoFrame &&
        frame.fRequiredFrame < static_cast<int>(out.size())) {
      out[static_cast<std::size_t>(frame.fRequiredFrame)].image->readPixels(bitmap.pixmap(), 0, 0);
      options.fPriorFrame = frame.fRequiredFrame;
    }
    const ::SkCodec::Result result = codec->getPixels(bitmap.pixmap(), &options);
    if (result != ::SkCodec::kSuccess && result != ::SkCodec::kIncompleteInput) {
      break;
    }
    bitmap.setImmutable();
    out.push_back(Frame{::SkImages::RasterFromBitmap(bitmap), described && frame.fDuration > 0 ? frame.fDuration : 100});
    used += info.computeMinByteSize();
  }
  return out;
}

// An image from pixels made here, four bytes each -- red, green, blue,
// alpha, not premultiplied -- row after row: copied.
inline ::sk_sp<::SkImage> imageFromRGBA(int width, int height, const std::uint8_t *rgba) {
  if (width <= 0 || height <= 0 || rgba == nullptr) {
    return nullptr;
  }
  const ::SkImageInfo info = ::SkImageInfo::Make(width, height, ::kRGBA_8888_SkColorType, ::kUnpremul_SkAlphaType);
  return ::SkImages::RasterFromPixmapCopy(::SkPixmap(info, rgba, static_cast<std::size_t>(width) * 4u));
}

// The bytes of a file of an image, written anew -- PNG, or JPEG at a good
// quality -- from its pixels alone: nothing of the file it came from, its
// metadata among it, goes with them. Empty where the build cannot write
// that format.
inline std::string encodeImage(const ::SkImage &image, bool jpeg) {
  ::SkPixmap pixels;
  ::SkBitmap bitmap;
  if (!image.peekPixels(&pixels)) {
    if (!bitmap.tryAllocPixels(image.imageInfo()) || !image.readPixels(nullptr, bitmap.pixmap(), 0, 0)) {
      return {};
    }
    pixels = bitmap.pixmap();
  }
  ::SkDynamicMemoryWStream out;
  bool written = false;
#if defined(SK_CODEC_ENCODES_JPEG)
  if (jpeg) {
    ::SkJpegEncoder::Options options;
    options.fQuality = 92;
    written = ::SkJpegEncoder::Encode(&out, pixels, options);
  }
#endif
#if defined(SK_CODEC_ENCODES_PNG)
  if (!jpeg) {
    written = ::SkPngEncoder::Encode(&out, pixels, {});
  }
#endif
  if (!written) {
    return {};
  }
  const auto data = out.detachAsData();
  return std::string(static_cast<const char *>(data->data()), data->size());
}

using ::SkAlphaType;
using ::SkBitmap;
using ::SkBlendMode;
using ::SkCanvas;
using ::SkCodec;
using ::SkColor;
using ::SkClipOp;
using ::SkColor4f;
using ::SkColorFilter;
using ::SkColorFilters;
using ::SkColorSpace;
using ::SkColorType;
using ::SkData;
using ::SkGlyphID;
using ::SkTextBlob;
using ::SkTextBlobBuilder;
using ::SkFilterMode;
using ::SkImageFilter;
using ::SkImageFilters;
using ::SkFont;
using ::SkFontArguments;
using ::SkFontHinting;
using ::SkFontMetrics;
using ::SkFontMgr;
using ::SkFontStyle;
using ::SkFontStyleSet;
using ::SkGradient;
using ::SkImage;
using ::SkImageInfo;
using ::SkIRect;
using ::SkISize;
using ::SkMatrix;
using ::SkMipmapMode;
using ::SkPaint;
using ::SkPicture;
using ::SkPictureRecorder;
using ::SkPath;
using ::SkPathBuilder;
using ::SkPixmap;
using ::SkPoint;
using ::SkRect;
using ::SkRegion;
using ::SkRRect;
using ::SkRuntimeEffect;
using ::SkRuntimeEffectBuilder;
using ::SkSamplingOptions;
using ::SkShader;
using ::SkStream;
using ::SkStreamAsset;
using ::SkString;
using ::SkSurface;
using ::SkSurfaceProps;
using ::SkTextEncoding;
using ::SkTileMode;
using ::SkShaders::LinearGradient;

using ::SkTypeface;
using ::SkVertices;

namespace png {
using ::SkPngEncoder::Encode;
using Options = ::SkPngEncoder::Options;
} // namespace png

using ::SkImages::RasterFromBitmap;

#if defined(SK_GANESH)
using ::GrBackendRenderTarget;
using ::GrBackendTexture;
using ::GrDirectContext;
using ::GrGLenum;
using ::GrGLFramebufferInfo;
using ::GrGLMakeNativeInterface;
// The interface assembled from a loader the caller provides, which is the
// one that exists whatever a Skia was built to reach GL with. The native
// factory is compiled per platform from the egl or glx sources, and a Skia
// built with neither -- Debian's is -- has the variant that returns nothing.
using ::GrGLFuncPtr;
using ::GrGLGetProc;
using ::GrGLMakeAssembledInterface;
using ::GrGLuint;
using ::GrSurfaceOrigin;

using ::GrBackendRenderTargets::MakeGL;
using ::GrDirectContexts::MakeGL;
using ::SkSurfaces::WrapBackendRenderTarget;
#endif

using ::skgpu::Budgeted;
using ::skgpu::Budgeted::kNo;
using ::SkSurfaces::Raster;
using ::SkSurfaces::WrapPixels;
// One name, both backends: the overload taking a Ganesh context and the one
// taking a Graphite recorder are the same function to whoever asks for an
// offscreen surface, and which of them exists is what the build decided.
//
// Named at all only where there is a GPU in this Skia. A build with neither
// backend has no SkSurfaces::RenderTarget of any kind -- the declarations
// are behind the same macros -- and naming it there is an error about a
// member that does not exist.
#if defined(SK_GANESH) || defined(SK_GRAPHITE)
using ::SkSurfaces::RenderTarget;
#endif

#if defined(SK_GRAPHITE)
// Graphite draws into a recording rather than into the device: a Recorder
// takes the calls, snap() turns what it took into a Recording, and the
// Context plays it. Everything else here is the same Skia.
namespace graphite {
using ::skgpu::graphite::BackendSemaphore;
using ::skgpu::graphite::BackendTexture;
using ::skgpu::graphite::Context;
using ::skgpu::graphite::ContextOptions;
// What turns an image this program made into one the recorder can draw.
// Graphite does not do it by itself: an image that is not already its own is
// dropped, with a line on the console, unless the context was given one of
// these.
//
// Declared here rather than exported by name. Skia's own is declared in the
// global module fragment, and a using-declaration carries the name across
// without carrying the definition: whoever imports this module can say the
// name and cannot derive from it -- "expected class name", about a class the
// module itself can see. A class declared in the module's own purview is
// reachable wherever the module is imported, and this one is Skia's with
// nothing added, so an sk_sp of it is an sk_sp of Skia's wherever one is
// wanted.
class ImageProvider : public ::skgpu::graphite::ImageProvider {
public:
  using ::skgpu::graphite::ImageProvider::ImageProvider;
};
// The same for the call that makes one: a using-declaration carries the name
// of something declared in the global module fragment, and a name is all a
// caller of it does not need. Declared here, calling Skia's where Skia's is
// visible, so importing this module is enough to call it.
[[nodiscard]] inline ::sk_sp<::SkImage>
TextureFromImage(::skgpu::graphite::Recorder *recorder, const ::SkImage *image,
                 ::SkImage::RequiredProperties required) {
  return ::SkImages::TextureFromImage(recorder, image, required);
}
using ::skgpu::graphite::InsertRecordingInfo;
using ::skgpu::graphite::InsertStatus;
using ::skgpu::graphite::Recorder;
using ::skgpu::graphite::RecorderOptions;
using ::skgpu::graphite::Recording;
using ::skgpu::graphite::SyncToCpu;
using ::skgpu::graphite::TextureInfo;
using ::SkSurfaces::WrapBackendTexture;
// What a finished callback is told, and whether a texture carries mip
// levels: both are said in skgpu's vocabulary rather than Graphite's.
using ::skgpu::CallbackResult;
using ::skgpu::Mipmapped;
#if defined(SK_VULKAN)
// What a Vulkan program hands over: the device it made, the queue it will
// submit on, and an allocator, which Skia will not make one of for itself.
using ::skgpu::MutableTextureState;
using ::skgpu::Protected;
using ::skgpu::VulkanAlloc;
using ::skgpu::VulkanBackendContext;
using ::skgpu::VulkanExtensions;
using ::skgpu::VulkanGetProc;
using ::skgpu::VulkanMemoryAllocator;
using ::skgpu::VulkanYcbcrConversionInfo;
using ::skgpu::graphite::VulkanTextureInfo;

// Skia spells each of these MakeVulkan, in a namespace saying what is being
// made. The namespaces are kept, because five factories under one name is
// five overloads a reader has to resolve by argument.
namespace contextFactory {
using ::skgpu::graphite::ContextFactory::MakeVulkan;
} // namespace contextFactory
namespace backendTextures {
using ::skgpu::graphite::BackendTextures::MakeVulkan;
} // namespace backendTextures
namespace backendSemaphores {
using ::skgpu::graphite::BackendSemaphores::GetVkSemaphore;
using ::skgpu::graphite::BackendSemaphores::MakeVulkan;
} // namespace backendSemaphores
namespace textureInfos {
using ::skgpu::graphite::TextureInfos::GetVulkanTextureInfo;
using ::skgpu::graphite::TextureInfos::MakeVulkan;
} // namespace textureInfos
namespace mutableTextureStates {
using ::skgpu::MutableTextureStates::MakeVulkan;
} // namespace mutableTextureStates
#endif
} // namespace graphite
#endif

using ::SkFontMgr_New_Custom_Data;
using ::SkFontMgr_New_Custom_Directory;
#if defined(__ANDROID__)
using ::SkFontMgr_Android_CustomFonts;
using ::SkFontMgr_New_Android;
using ::SkFontScanner;
using ::SkFontScanner_Make_FreeType;
#endif

inline constexpr SkColor colorSetARGB(uint8_t a, uint8_t r, uint8_t g,
                                      uint8_t b) noexcept {
  return (static_cast<SkColor>(a) << 24) | (static_cast<SkColor>(r) << 16) |
         (static_cast<SkColor>(g) << 8) | static_cast<SkColor>(b);
}

#if defined(SK_GANESH)
inline constexpr GrGLenum kGlRgba8 = GL_RGBA8;

// Which corner a backend surface counts from. Ganesh asks because GL and
// everything else disagree about it; Graphite does not ask at all.
using ::kBottomLeft_GrSurfaceOrigin;
using ::kTopLeft_GrSurfaceOrigin;
#endif

using ::kBGRA_8888_SkColorType;
using ::kN32_SkColorType;
using ::kOpaque_SkAlphaType;
using ::kPremul_SkAlphaType;
using ::kRGBA_8888_SkColorType;
using ::kRGBA_F32_SkColorType;
using ::kUnpremul_SkAlphaType;

inline constexpr SkColor kBlack = SK_ColorBLACK;
inline constexpr SkColor kWhite = SK_ColorWHITE;
inline constexpr SkColor kLTGray = SK_ColorLTGRAY;
inline constexpr SkColor kDKGray = SK_ColorDKGRAY;
inline constexpr SkColor kGray = SK_ColorGRAY;
inline constexpr SkColor kRed = SK_ColorRED;
inline constexpr SkColor kGreen = SK_ColorGREEN;
inline constexpr SkColor kBlue = SK_ColorBLUE;
inline constexpr SkColor kYellow = SK_ColorYELLOW;
inline constexpr SkColor kCyan = SK_ColorCYAN;
inline constexpr SkColor kMagenta = SK_ColorMAGENTA;

using Style = ::SkPaint::Style;
inline constexpr Style kFillStyle = ::SkPaint::Style::kFill_Style;
inline constexpr Style kStrokeStyle = ::SkPaint::Style::kStroke_Style;

// Grid fitting is what makes small static text crisp, and what makes text
// whose size is animating jump a glyph at a time: each outline snaps to the
// pixel grid at its own threshold as the size passes through it.
inline constexpr ::SkFontHinting kNoHinting = ::SkFontHinting::kNone;
inline constexpr Style kStrokeAndFillStyle =
    ::SkPaint::Style::kStrokeAndFill_Style;

using Join = ::SkPaint::Join;
inline constexpr Join kMiterJoin = ::SkPaint::Join::kMiter_Join;
inline constexpr Join kRoundJoin = ::SkPaint::Join::kRound_Join;
inline constexpr Join kBevelJoin = ::SkPaint::Join::kBevel_Join;

using Cap = ::SkPaint::Cap;
inline constexpr Cap kButtCap = ::SkPaint::Cap::kButt_Cap;
inline constexpr Cap kRoundCap = ::SkPaint::Cap::kRound_Cap;
inline constexpr Cap kSquareCap = ::SkPaint::Cap::kSquare_Cap;

using Version = ::SkSL::Version;
inline constexpr Version kSL300 = ::SkSL::Version::k300;

} // namespace skia

// The smart pointer, instantiated here rather than in whoever imports this.
//
// sk_sp's bodies call SkRef, SkSafeRef and SkSafeUnref, which Skia declares
// static inline: each translation unit that includes SkRefCnt.h has its own,
// and a module unit's are local to it. An importer that instantiates one of
// those bodies -- assigning one surface to another does it -- looks for a
// SkSafeRef it cannot see, and is told there is no such function at all,
// without a single candidate named.
//
// Instantiated here, the bodies are compiled where those helpers are
// visible, and an importer uses what this module already has. One line per
// type this library and its consumers hold by reference; a type that is not
// listed is one nobody has needed to copy yet, and the error above is what
// says so.
template class ::sk_sp<SkSurface>;
template class ::sk_sp<SkImage>;
template class ::sk_sp<SkData>;
template class ::sk_sp<SkTextBlob>;
template class ::sk_sp<SkTypeface>;
template class ::sk_sp<SkFontMgr>;
template class ::sk_sp<SkFontStyleSet>;
template class ::sk_sp<SkColorSpace>;
template class ::sk_sp<SkShader>;
template class ::sk_sp<SkColorFilter>;
template class ::sk_sp<SkRuntimeEffect>;
template class ::sk_sp<SkVertices>;
