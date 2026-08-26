#include "genplusgx/game_file.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <optional>
#include <ranges>
#include <system_error>
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

constexpr std::array discExtensions{
  std::string_view{".bin"},
  std::string_view{".cue"},
  std::string_view{".iso"},
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

std::string_view trim(std::string_view value)
{
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1U);
  }
  while (!value.empty() &&
      (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
    value.remove_suffix(1U);
  }
  return value;
}

bool startsWithCommand(std::string_view line, std::string_view command)
{
  return line.starts_with(command) &&
    (line.size() == command.size() || line[command.size()] == ' ' ||
      line[command.size()] == '\t');
}

std::optional<std::string_view> takeToken(std::string_view& value)
{
  value = trim(value);
  if (value.empty()) {
    return std::nullopt;
  }
  const auto end = value.find_first_of(" \t\r");
  const auto token = value.substr(0U, end);
  value = end == std::string_view::npos ? std::string_view{} : value.substr(end);
  return token;
}

std::optional<unsigned int> parseUnsigned(std::string_view token)
{
  if (token.empty()) {
    return std::nullopt;
  }
  unsigned int value = 0U;
  const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
  if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<unsigned int> parseCueTime(std::string_view token)
{
  const auto firstColon = token.find(':');
  const auto secondColon = firstColon == std::string_view::npos
    ? std::string_view::npos
    : token.find(':', firstColon + 1U);
  if (firstColon == std::string_view::npos ||
      secondColon == std::string_view::npos ||
      token.find(':', secondColon + 1U) != std::string_view::npos) {
    return std::nullopt;
  }
  const auto minutes = parseUnsigned(token.substr(0U, firstColon));
  const auto seconds = parseUnsigned(
    token.substr(firstColon + 1U, secondColon - firstColon - 1U));
  const auto frames = parseUnsigned(token.substr(secondColon + 1U));
  if (!minutes || !seconds || !frames || *minutes > 99U ||
      *seconds > 59U || *frames > 74U) {
    return std::nullopt;
  }
  return ((*minutes * 60U) + *seconds) * 75U + *frames;
}

GameFileStatus cueFailure(std::size_t line, std::string message,
  GameFileError error = GameFileError::invalidCueSheet)
{
  return failure(error,
    "Invalid CUE sheet at line " + std::to_string(line) + ": " +
      std::move(message));
}

bool isSafeRelativeCuePath(std::string_view value)
{
  if (value.empty() || value.front() == '/' || value.front() == '\\' ||
      value.find(':') != std::string_view::npos) {
    return false;
  }
  std::string portable{value};
  std::ranges::replace(portable, '\\', '/');
  const std::filesystem::path path{portable};
  if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
    return false;
  }
  return std::ranges::none_of(path, [](const auto& component) {
    return component == "..";
  });
}

bool pathIsWithin(const std::filesystem::path& child,
  const std::filesystem::path& parent)
{
  auto childPart = child.begin();
  for (auto parentPart = parent.begin(); parentPart != parent.end();
       ++parentPart, ++childPart) {
    if (childPart == child.end() || *childPart != *parentPart) {
      return false;
    }
  }
  return true;
}

GameFileStatus validateRegularGameFile(const std::filesystem::path& path)
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
    return failure(GameFileError::notRegularFile,
      "The selected game path is not a regular file.");
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return failure(GameFileError::unreadable, "The selected game file cannot be read.");
  }
  return {};
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

std::span<const std::string_view> supportedDiscExtensions() noexcept
{
  return discExtensions;
}

bool hasSupportedDiscExtension(const std::filesystem::path& path)
{
  const auto extension = lowercase(path.extension().string());
  return std::ranges::find(discExtensions, extension) != discExtensions.end();
}

GameFileStatus validateCueSheetText(
  std::string_view text,
  CueSheetInfo& information)
{
  information = {};
  if (text.empty()) {
    return failure(GameFileError::invalidCueSheet, "The CUE sheet is empty.");
  }
  if (text.size() > maximumCueSheetBytes) {
    return failure(GameFileError::fileTooLarge,
      "The CUE sheet exceeds the 1 MiB safety limit.");
  }

  for (const unsigned char character : text) {
    if ((character < 0x20U && character != '\r' && character != '\n') ||
        character == 0x7FU) {
      return failure(GameFileError::invalidCueSheet,
        "The CUE sheet contains an unsupported control character.");
    }
  }

  std::size_t lineNumber = 0U;
  std::size_t offset = 0U;
  bool haveFile = false;
  bool currentFileHasTrack = false;
  bool currentFileIsAudio = false;
  bool haveTrack = false;
  bool currentTrackHasIndex = false;
  bool currentTrackHasPregap = false;
  bool currentTrackHasIndexZero = false;
  std::size_t expectedTrack = 1U;
  std::size_t currentFileNumber = 0U;
  std::size_t previousIndexFileNumber = 0U;
  std::optional<unsigned int> previousIndex;
  std::optional<unsigned int> currentIndexZero;

  while (offset < text.size()) {
    const auto end = text.find('\n', offset);
    const auto rawLength = (end == std::string_view::npos ? text.size() : end) - offset;
    ++lineNumber;
    if (rawLength > maximumCueLineBytes) {
      return cueFailure(lineNumber,
        "the line exceeds the Genesis Plus GX 127-byte parser limit.");
    }
    auto line = trim(text.substr(offset, rawLength));
    offset = end == std::string_view::npos ? text.size() : end + 1U;
    if (line.empty() || startsWithCommand(line, "REM")) {
      continue;
    }

    if (startsWithCommand(line, "FILE")) {
      if (haveFile && !currentFileHasTrack) {
        return cueFailure(lineNumber, "the previous FILE has no TRACK.");
      }
      if (haveTrack && !currentTrackHasIndex) {
        return cueFailure(lineNumber, "the previous TRACK has no INDEX 01.");
      }
      auto remainder = trim(line.substr(4U));
      std::string_view reference;
      if (!remainder.empty() && remainder.front() == '"') {
        const auto close = remainder.find('"', 1U);
        if (close == std::string_view::npos) {
          return cueFailure(lineNumber, "the FILE path has no closing quote.");
        }
        reference = remainder.substr(1U, close - 1U);
        remainder = remainder.substr(close + 1U);
      } else {
        const auto token = takeToken(remainder);
        if (!token) {
          return cueFailure(lineNumber, "the FILE path is missing.");
        }
        reference = *token;
      }
      const auto type = takeToken(remainder);
      if (!type || !trim(remainder).empty()) {
        return cueFailure(lineNumber, "the FILE type is missing or malformed.");
      }
      if (*type != "BINARY" && *type != "MOTOROLA" &&
          *type != "WAVE" && *type != "OGG") {
        return cueFailure(lineNumber, "the FILE type is not supported.");
      }
      if (!isSafeRelativeCuePath(reference)) {
        return cueFailure(lineNumber,
          "the FILE reference must stay within the CUE directory.",
          GameFileError::unsafeCueReference);
      }
      if (information.referencedFiles.size() >= 99U) {
        return cueFailure(lineNumber, "more than 99 FILE entries are not supported.");
      }
      try {
        information.referencedFiles.emplace_back(std::string{reference});
      } catch (const std::filesystem::filesystem_error&) {
        return cueFailure(lineNumber,
          "the FILE reference cannot be represented on this platform.");
      }
      haveFile = true;
      currentFileHasTrack = false;
      currentFileIsAudio = *type == "WAVE" || *type == "OGG";
      ++currentFileNumber;
      continue;
    }

    if (startsWithCommand(line, "TRACK")) {
      if (!haveFile) {
        return cueFailure(lineNumber, "TRACK appears before FILE.");
      }
      if (haveTrack && !currentTrackHasIndex) {
        return cueFailure(lineNumber, "the previous TRACK has no INDEX 01.");
      }
      auto remainder = line.substr(5U);
      const auto numberToken = takeToken(remainder);
      const auto type = takeToken(remainder);
      const auto number = numberToken ? parseUnsigned(*numberToken) : std::nullopt;
      if (!number || !type || !trim(remainder).empty() ||
          *number != expectedTrack || *number == 0U || *number > 99U) {
        return cueFailure(lineNumber, "TRACK numbers must be sequential from 01.");
      }
      const bool dataTrack = *type == "MODE1/2048" ||
        *type == "MODE1/2352" || *type == "MODE2/2352";
      if ((!haveTrack && !dataTrack) ||
          (haveTrack && *type != "AUDIO") ||
          (dataTrack && currentFileIsAudio) ||
          (!dataTrack && *type != "AUDIO")) {
        return cueFailure(lineNumber,
          "the first track must be supported CD data and later tracks must be AUDIO.");
      }
      haveTrack = true;
      currentFileHasTrack = true;
      currentTrackHasIndex = false;
      currentTrackHasPregap = false;
      currentTrackHasIndexZero = false;
      currentIndexZero.reset();
      ++expectedTrack;
      information.trackCount = expectedTrack - 1U;
      continue;
    }

    if (startsWithCommand(line, "PREGAP")) {
      if (!haveTrack || currentTrackHasIndex || currentTrackHasPregap) {
        return cueFailure(lineNumber, "PREGAP is misplaced or duplicated.");
      }
      auto remainder = line.substr(6U);
      const auto time = takeToken(remainder);
      if (!time || !parseCueTime(*time) || !trim(remainder).empty()) {
        return cueFailure(lineNumber, "PREGAP must use a valid MM:SS:FF time.");
      }
      currentTrackHasPregap = true;
      continue;
    }

    if (startsWithCommand(line, "INDEX")) {
      if (!haveTrack) {
        return cueFailure(lineNumber, "INDEX appears before TRACK.");
      }
      auto remainder = line.substr(5U);
      const auto indexToken = takeToken(remainder);
      const auto timeToken = takeToken(remainder);
      const auto index = indexToken ? parseUnsigned(*indexToken) : std::nullopt;
      const auto time = timeToken ? parseCueTime(*timeToken) : std::nullopt;
      if (!index || !time || !trim(remainder).empty() || *index > 1U) {
        return cueFailure(lineNumber,
          "INDEX must be 00 or 01 with a valid MM:SS:FF time.");
      }
      if (*index == 0U) {
        if (currentTrackHasIndexZero || currentTrackHasIndex) {
          return cueFailure(lineNumber, "INDEX 00 is misplaced or duplicated.");
        }
        currentTrackHasIndexZero = true;
        currentIndexZero = *time;
      } else {
        if (currentTrackHasIndex) {
          return cueFailure(lineNumber, "INDEX 01 is duplicated.");
        }
        if (currentIndexZero && *time <= *currentIndexZero) {
          return cueFailure(lineNumber, "INDEX 01 must follow INDEX 00.");
        }
        if (information.trackCount > 1U &&
            previousIndexFileNumber == currentFileNumber && previousIndex &&
            *time <= *previousIndex) {
          return cueFailure(lineNumber,
            "track indexes in a shared file must increase.");
        }
        currentTrackHasIndex = true;
        previousIndex = *time;
        previousIndexFileNumber = currentFileNumber;
      }
      continue;
    }

    if (line.starts_with("FILE")) {
      return cueFailure(lineNumber, "the FILE directive is malformed.");
    }

    // Genesis Plus GX ignores optional CUE directives it does not recognize.
  }

  if (!haveFile || !haveTrack || !currentFileHasTrack ||
      !currentTrackHasIndex) {
    return failure(GameFileError::invalidCueSheet,
      "The CUE sheet needs a FILE, a data TRACK 01, and INDEX 01 entries.");
  }
  return {};
}

GameFileStatus validateCueSheetFile(const std::filesystem::path& path)
{
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return failure(GameFileError::unreadable, "The CUE sheet size cannot be read.");
  }
  if (size > maximumCueSheetBytes) {
    return failure(GameFileError::fileTooLarge,
      "The CUE sheet exceeds the 1 MiB safety limit.");
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return failure(GameFileError::unreadable, "The CUE sheet cannot be read.");
  }
  std::string text(static_cast<std::size_t>(size), '\0');
  stream.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (!stream && !stream.eof()) {
    return failure(GameFileError::unreadable, "The CUE sheet could not be read completely.");
  }
  if (static_cast<std::size_t>(stream.gcount()) != text.size()) {
    return failure(GameFileError::unreadable,
      "The CUE sheet changed while it was being read.");
  }

  CueSheetInfo information;
  if (auto status = validateCueSheetText(text, information); !status) {
    return status;
  }

  auto parentPath = path.parent_path();
  if (parentPath.empty()) {
    parentPath = std::filesystem::current_path(error);
    if (error) {
      return failure(GameFileError::unreadable,
        "The current directory cannot be resolved safely.");
    }
  }
  const auto cueParent = std::filesystem::weakly_canonical(parentPath, error);
  if (error) {
    return failure(GameFileError::unreadable,
      "The CUE sheet directory cannot be resolved safely.");
  }
  for (const auto& reference : information.referencedFiles) {
    const auto candidate = parentPath / reference;
    if (candidate.string().size() > maximumCorePathBytes) {
      return failure(GameFileError::pathTooLong,
        "A CUE track path exceeds the Genesis Plus GX 255-byte path limit.");
    }
    const auto resolved = std::filesystem::weakly_canonical(candidate, error);
    if (error || !pathIsWithin(resolved, cueParent)) {
      return failure(GameFileError::unsafeCueReference,
        "A CUE track reference escapes the CUE sheet directory.");
    }
    if (!std::filesystem::is_regular_file(resolved, error) || error) {
      return failure(GameFileError::missingCueTrackFile,
        "A referenced CUE track file is missing or is not a regular file: " +
          reference.generic_string());
    }
    const auto referencedSize = std::filesystem::file_size(resolved, error);
    if (error || referencedSize == 0U) {
      return failure(GameFileError::missingCueTrackFile,
        "A referenced CUE track file is empty or unreadable: " +
          reference.generic_string());
    }
    std::ifstream referencedStream(resolved, std::ios::binary);
    if (!referencedStream) {
      return failure(GameFileError::missingCueTrackFile,
        "A referenced CUE track file cannot be read: " +
          reference.generic_string());
    }
  }
  return {};
}

GameFileStatus validateGameFile(const std::filesystem::path& path)
{
  if (auto status = validateRegularGameFile(path); !status) {
    return status;
  }
  if (lowercase(path.extension().string()) == ".cue") {
    return validateCueSheetFile(path);
  }
  return {};
}

GameFileStatus validateDiscImageFile(const std::filesystem::path& path)
{
  if (path.empty()) {
    return failure(GameFileError::emptyPath, "No disc image was selected.");
  }
  const auto nativePath = path.string();
  if (nativePath.size() < 3U || nativePath.size() > maximumCorePathBytes) {
    return failure(
      GameFileError::pathTooLong,
      "The disc image path must be between 3 and 255 bytes for the emulator core.");
  }
  if (!hasSupportedDiscExtension(path)) {
    return failure(GameFileError::unsupportedDiscExtension,
      "The selected file type is not a supported Sega CD / Mega CD image.");
  }
  return validateGameFile(path);
}

} // namespace genplusgx
