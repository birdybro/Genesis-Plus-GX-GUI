#include "genplusgx/game_file.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <ranges>
#include <utility>

namespace genplusgx {
namespace {

constexpr std::array baseExtensions{
  std::string_view{".68k"},
  std::string_view{".bin"},
  std::string_view{".bms"},
  std::string_view{".cue"},
  std::string_view{".gen"},
  std::string_view{".gg"},
  std::string_view{".iso"},
  std::string_view{".md"},
  std::string_view{".mdx"},
  std::string_view{".sg"},
  std::string_view{".sgd"},
  std::string_view{".smd"},
  std::string_view{".sms"},
#if defined(GENPLUSGX_HAVE_CHD)
  std::string_view{".chd"},
#endif
};

GameFileStatus failure(GameFileError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

std::string lowercase(std::string text)
{
  std::ranges::transform(text, text.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return text;
}

} // namespace

std::span<const std::string_view> supportedGameExtensions() noexcept
{
  return baseExtensions;
}

bool hasSupportedGameExtension(const std::filesystem::path& path)
{
  const auto extension = lowercase(path.extension().string());
  return std::ranges::find(baseExtensions, extension) != baseExtensions.end();
}

GameFileStatus validateGameFile(const std::filesystem::path& path)
{
  if (path.empty()) {
    return failure(GameFileError::emptyPath, "No game file was selected.");
  }

  const auto nativePath = path.string();
  if (nativePath.size() < 3U || nativePath.size() > maximumCorePathBytes) {
    return failure(
      GameFileError::pathTooLong,
      "The game path must be between 3 and 255 bytes for the emulator core.");
  }
  if (!hasSupportedGameExtension(path)) {
    return failure(
      GameFileError::unsupportedExtension,
      "The selected file type is not supported by this desktop build.");
  }

  std::error_code error;
  const bool exists = std::filesystem::exists(path, error);
  if (error || !exists) {
    return failure(GameFileError::notFound, "The selected game file does not exist.");
  }
  const bool regular = std::filesystem::is_regular_file(path, error);
  if (error || !regular) {
    return failure(GameFileError::notRegularFile, "The selected game path is not a regular file.");
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return failure(GameFileError::unreadable, "The selected game file cannot be read.");
  }
  return {};
}

} // namespace genplusgx
