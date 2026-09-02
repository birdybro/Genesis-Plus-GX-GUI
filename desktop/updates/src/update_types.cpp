#include "genplusgx/updates/update_types.h"

#include <charconv>
#include <limits>

namespace genplusgx::updates {

std::string SemanticVersion::toString() const
{
  return std::to_string(major) + '.' + std::to_string(minor) + '.' +
    std::to_string(patch);
}

std::optional<SemanticVersion> parseSemanticVersion(std::string_view text) noexcept
{
  SemanticVersion result;
  std::uint32_t* fields[]{&result.major, &result.minor, &result.patch};
  std::size_t begin = 0U;
  for (std::size_t index = 0U; index < 3U; ++index) {
    const auto end = index == 2U ? text.size() : text.find('.', begin);
    if (end == std::string_view::npos || end == begin || end - begin > 10U ||
        (end - begin > 1U && text[begin] == '0')) {
      return std::nullopt;
    }
    const auto [pointer, error] = std::from_chars(
      text.data() + begin, text.data() + end, *fields[index]);
    if (error != std::errc{} || pointer != text.data() + end) {
      return std::nullopt;
    }
    begin = end + 1U;
  }
  return result;
}

Trust productionTrust()
{
  return {
    .publicKeyHex =
      "e05e60dccf728190a553de23d934a66136467c31281eebbffe876f488be83325",
    .keyId = "704e04b184a939a4",
    .manifestUrl = "https://github.com/birdybro/Genesis-Plus-GX-GUI/"
      "releases/latest/download/update-manifest.json",
    .signatureUrl = "https://github.com/birdybro/Genesis-Plus-GX-GUI/"
      "releases/latest/download/update-manifest.json.sig",
    .repositoryUrl = "https://github.com/birdybro/Genesis-Plus-GX-GUI",
    .allowedHosts = {"github.com", "objects.githubusercontent.com",
      "release-assets.githubusercontent.com"},
  };
}

std::string currentPlatform()
{
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unsupported";
#endif
}

std::string currentArchitecture()
{
#if defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#else
  return "unsupported";
#endif
}

} // namespace genplusgx::updates
