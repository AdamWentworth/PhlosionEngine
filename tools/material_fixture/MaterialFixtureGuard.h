#pragma once

#include "MaterialFixtureCase.h"
#include "MaterialFixtureAssets.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace engine::material_fixture::verification {

// Half of one patch, left or right of the vertical centre line. The authored
// maps change value across that line so spatial variation is checkable. This is
// oracle vocabulary: it lives with the expectations, not with the sample.
enum class PatchHalf {
    Left = 0,
    Right = 1,
};

struct FixtureImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    bool empty() const noexcept {
        return width <= 0 || height <= 0 ||
            rgba.size() != static_cast<std::size_t>(width) *
                    static_cast<std::size_t>(height) * 4u;
    }
};

bool loadFixturePng(const std::string& path,
                    FixtureImage& outImage,
                    std::string* outError = nullptr);

bool saveFixturePng(const std::string& path,
                    const FixtureImage& image,
                    std::string* outError = nullptr);

struct FixtureCrop {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

FixtureImage cropFixtureImage(const FixtureImage& image, const FixtureCrop& crop);

// Detects the fixture's green background band so editor chrome never
// contaminates a comparison. Standalone hosts fill the whole window and resolve
// to the full image.
bool detectFixtureViewport(const FixtureImage& image,
                           FixtureCrop& outCrop,
                           std::string* outError = nullptr);

struct ImageCompareMetrics {
    double normalizedMae = 0.0;
    double normalizedRmse = 0.0;
    double changedPixelRatio = 0.0;
    int maxChannelDiff = 0;
};

bool compareFixtureImages(const FixtureImage& left,
                          const FixtureImage& right,
                          int channelTolerance,
                          ImageCompareMetrics& outMetrics,
                          std::string* outError = nullptr);

// Compares only the composed lit patch region of two same-size viewports. A lit
// region frozen across cases is rejected here even when every diagnostic patch is
// correct, since debug tiles alone can carry the whole-frame difference.
bool compareLitRegions(const FixtureImage& left,
                       const FixtureImage& right,
                       int channelTolerance,
                       ImageCompareMetrics& outMetrics,
                       std::string* outError = nullptr);

// Minimum changed-pixel ratio for the composed lit region between the authored
// case and either corrected fallback case.
inline constexpr double kLitCrossCaseChangedRatioMinimum = 0.5;

// A patch region in viewport pixels.
struct FixturePatchRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

FixturePatchRegion patchRegion(const FixtureImage& viewport,
                               const std::string& patchName);

// Signed channel means over the left or right interior half of a region.
struct ChannelMean {
    std::array<double, 3> rgb{0.0, 0.0, 0.0};
    std::size_t samples = 0u;
};

ChannelMean samplePatchHalf(const FixtureImage& viewport,
                            const FixturePatchRegion& region,
                            PatchHalf half);

// The authored displayed value for one patch half: the sample interpreted
// through the debug view's documented color-space treatment (identity for the
// sRGB role views, the sRGB transfer for the linear role views).
struct PatchExpectation {
    std::string patch;
    bool present = false;
    bool srgbView = false;
    std::string source;
    // Authored texel bytes for each half.
    std::array<int, 3> rawLeft{0, 0, 0};
    std::array<int, 3> rawRight{0, 0, 0};
    // The same samples after the debug view's documented color treatment.
    std::array<int, 3> left{0, 0, 0};
    std::array<int, 3> right{0, 0, 0};
};

std::vector<PatchExpectation> fixturePatchExpectations(FixtureCase fixtureCase);

// The authored sample value a diagnostic patch must present for one half.
// `srgbView` is the texture format the role is submitted with. For the lit patch
// there is no authored sample; the composed region is checked separately.
struct PatchSample {
    std::array<std::uint8_t, 3> rgb{0u, 0u, 0u};
    bool srgbView = false;
    std::string source;
};

bool fixturePatchSample(FixtureCase fixtureCase,
                        FixturePatch patch,
                        PatchHalf half,
                        PatchSample& outSample);

// The corrected pixels the decoder supplies when a role's input is unavailable:
// opaque white base, opaque black emissive, flat (128,128,255) normal and opaque
// white metallic-roughness/occlusion.
std::array<std::uint8_t, 4> fixtureRolePlaceholderPixel(FixtureRole role);

// The bounded literal metadata contract for one role: exact size, color-space
// flag, sampler enums, first texel, content hash and the exact cache identity the
// sample must submit.
struct RoleMetadataContract {
    int width = 0;
    int height = 0;
    std::uint32_t textureSrgb = 0u;
    int wrapS = 0;
    int wrapT = 0;
    int minFilter = 0;
    int magFilter = 0;
    std::array<std::uint8_t, 4> firstPixel{0u, 0u, 0u, 0u};
    std::uint32_t contentHash = 0u;
    std::string key;
};

RoleMetadataContract expectedRoleMetadata(FixtureCase fixtureCase,
                                          FixtureRole role);

// Authored channel tolerances for the interior-region means: 8-bit sRGB
// encode/decode round trips plus nearest/linear filtering and quantization.
inline constexpr int kPatchSrgbTolerance = 10;
inline constexpr int kPatchLinearTolerance = 14;
// Displayed-space minimum separation between two authored halves.
inline constexpr int kHalfSeparationMinimum = 25;
// Minimum distance between a patch region and the measured background band.
inline constexpr int kPatchBackgroundMinimum = 20;
// Per-channel tolerance for every interior pixel of a patch half. The authored
// expectations are literal, the region excludes the filter boundary, and the
// measured deviation is at most two counts, so this stays tight enough to reject
// a corruption that preserves the half mean.
inline constexpr int kInteriorPixelTolerance = 3;

struct ExpectedContentResult {
    bool ok = false;
    std::vector<std::string> failures;
    std::map<std::string, double> metrics;
    std::map<std::string, double> expectedGeometry;
};

// `viewport` must already be the cropped fixture viewport. Its dimensions are
// the surface dimensions used to project the authored layout.
bool validateFixtureExpectedContent(const FixtureImage& viewport,
                                    FixtureCase fixtureCase,
                                    ExpectedContentResult& outResult,
                                    std::string* outError = nullptr);

// The defect modes the hermetic synthetic captures model. Every mode except
// `None` must be rejected by validateFixtureExpectedContent.
enum class SynthDefect {
    None,
    Blank,
    MissingPatch,
    SwappedMetallicRoughness,
    NeutralizedNormal,
    NeutralizedAo,
    NeutralizedEmissive,
    MissingLitRegion,
    // The pre-correction decoder branch: every unavailable non-base role came
    // back opaque black, so normal/MR/AO patches went black.
    OldBlackFallback,
    // A linear role view rendered without its sRGB transfer, as if the role had
    // been submitted with an sRGB texture flag.
    WrongColorSpace,
    // A spatially corrupted half whose mean is preserved, so only a per-pixel
    // interior check can reject it.
    SameMeanCorruption,
    // The composed lit region held at the authored appearance in a fallback case,
    // so the whole-frame difference comes from the diagnostic patches alone.
    FrozenLitAcrossCases,
};

bool parseSynthDefect(const std::string& name, SynthDefect& outDefect);
const char* synthDefectName(SynthDefect defect);

bool buildSyntheticFixtureCapture(FixtureCase fixtureCase,
                                  SynthDefect defect,
                                  int width,
                                  int height,
                                  FixtureImage& outImage,
                                  std::string* outError = nullptr);

// Numeric provenance for one handshake document: case identity, host, backend,
// logical frame, per-role identity/color-space/origin, per-patch debug view and
// the submitted scalar factors.
struct HandshakeCheckResult {
    bool ok = false;
    std::vector<std::string> failures;
    std::map<std::string, double> metrics;
};

bool validateFixtureHandshake(const std::string& handshakePath,
                              FixtureCase fixtureCase,
                              int surfaceWidth,
                              int surfaceHeight,
                              HandshakeCheckResult& outResult,
                              std::string* outError = nullptr,
                              const std::string& expectedApi = std::string{},
                              const std::string& expectedHost = std::string{});

// Lowercase hexadecimal SHA-256 of the given bytes and of an on-disk file.
std::string sha256Hex(const std::string& bytes);
bool sha256FileHex(const std::string& path,
                   std::string& outHex,
                   std::string* outError = nullptr);

// Resolves a recorded path against `root` (the run directory) and requires it to
// stay inside that directory after canonicalization. Relative inputs resolve
// against `root`; ".." traversal, sibling names that merely share a prefix and
// junction escapes are all rejected.
bool resolveWithinRoot(const std::string& value,
                       const std::filesystem::path& root,
                       std::filesystem::path& outResolved,
                       std::string& outError);

// Builds a contact sheet from labelled, already-cropped viewport images. Labels
// are not drawn; the matrix JSON records each cell's identity.
FixtureImage buildFixtureContactSheet(
    const std::vector<std::pair<std::string, FixtureImage>>& images,
    int columns,
    int cellWidth,
    int cellHeight);

} // namespace engine::material_fixture::verification
