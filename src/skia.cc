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
#include <skia/core/SkTypeface.h>
#include <skia/core/SkVertices.h>
#include <skia/effects/SkGradient.h>
#include <skia/effects/SkRuntimeEffect.h>
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

export namespace skia {

template <class T> using Sp = ::sk_sp<T>;

using ::SkAlphaType;
using ::SkBitmap;
using ::SkBlendMode;
using ::SkCanvas;
using ::SkCodec;
using ::SkColor;
using ::SkColor4f;
using ::SkColorFilter;
using ::SkColorFilters;
using ::SkColorSpace;
using ::SkColorType;
using ::SkData;
using ::SkFilterMode;
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
using ::skgpu::graphite::ImageProvider;
using ::SkImages::TextureFromImage;
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
