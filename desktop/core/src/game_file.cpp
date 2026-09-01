#include "genplusgx/game_file.h"

#include "genplusgx/game_archive.h"

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
  std::string_view{".m3u"},
  std::string_view{".m3u8"},
  std::string_view{".md"},
  std::string_view{".mdx"},
  std::string_view{".sg"},
  std::string_view{".sgd"},
  std::string_view{".smd"},
  std::string_view{".sms"},
  std::string_view{".zip"},
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

bool validUtf8(std::string_view text)
{
  std::size_t index = 0U;
  while (index < text.size()) {
    const auto first = static_cast<unsigned char>(text[index]);
    std::size_t continuation = 0U;
    std::uint32_t value = 0U;
    if (first < 0x80U) {
      ++index;
      continue;
    }
    if ((first & 0xe0U) == 0xc0U) {
      continuation = 1U;
      value = first & 0x1fU;
      if (value < 2U) {
        return false;
      }
    } else if ((first & 0xf0U) == 0xe0U) {
      continuation = 2U;
      value = first & 0x0fU;
    } else if ((first & 0xf8U) == 0xf0U) {
      continuation = 3U;
      value = first & 0x07U;
      if (value > 4U) {
        return false;
      }
    } else {
      return false;
    }
    if (index + continuation >= text.size()) {
      return false;
    }
    for (std::size_t offset = 1U; offset <= continuation; ++offset) {
      const auto byte = static_cast<unsigned char>(text[index + offset]);
      if ((byte & 0xc0U) != 0x80U) {
        return false;
      }
      value = (value << 6U) | (byte & 0x3fU);
    }
    if ((continuation == 2U && value < 0x800U) ||
        (continuation == 3U && value < 0x10000U) ||
        value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU)) {
      return false;
    }
    index += continuation + 1U;
  }
  return true;
}

std::filesystem::path pathFromUtf8(std::string_view text)
{
  std::u8string value;
  value.reserve(text.size());
  for (const char character : text) {
    value.push_back(static_cast<char8_t>(character));
  }
  return std::filesystem::path{value};
}

GameFileStatus inspectDiscPlaylist(
  const std::filesystem::path& path,
  DiscPlaylistInfo& information)
{
  information = {};
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return failure(GameFileError::unreadable,
      "The M3U playlist size cannot be read.");
  }
  if (size == 0U || size > maximumDiscPlaylistBytes) {
    return failure(GameFileError::fileTooLarge,
      "The M3U playlist is empty or exceeds the 256 KiB safety limit.");
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return failure(GameFileError::unreadable,
      "The M3U playlist cannot be read.");
  }
  std::string text(static_cast<std::size_t>(size), '\0');
  stream.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (static_cast<std::size_t>(stream.gcount()) != text.size()) {
    return failure(GameFileError::unreadable,
      "The M3U playlist changed while it was being read.");
  }
  auto directory = path.parent_path();
  if (directory.empty()) {
    directory = std::filesystem::current_path(error);
    if (error) {
      return failure(GameFileError::unreadable,
        "The M3U playlist directory cannot be resolved.");
    }
  }
  return validateDiscPlaylistText(text, directory, information);
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

GameFileStatus inspectCueSheetFile(
  const std::filesystem::path& path,
  std::vector<std::filesystem::path>* resolvedFiles)
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
    return failure(GameFileError::unreadable,
      "The CUE sheet could not be read completely.");
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
    if (resolvedFiles != nullptr) {
      resolvedFiles->push_back(resolved);
    }
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

bool hasDiscPlaylistExtension(const std::filesystem::path& path) noexcept
{
  const auto extension = lowercase(path.extension().string());
  return extension == ".m3u" || extension == ".m3u8";
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

  for (const char rawCharacter : text) {
    const auto character = static_cast<unsigned char>(rawCharacter);
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
  return inspectCueSheetFile(path, nullptr);
}

GameFileStatus validateDiscPlaylistText(
  std::string_view text,
  const std::filesystem::path& playlistDirectory,
  DiscPlaylistInfo& information)
{
  information = {};
  if (text.starts_with("\xef\xbb\xbf")) {
    text.remove_prefix(3U);
  }
  if (text.empty() || text.size() > maximumDiscPlaylistBytes ||
      !validUtf8(text)) {
    return failure(GameFileError::invalidDiscPlaylist,
      "The M3U playlist is empty, too large, or not valid UTF-8.");
  }
  std::error_code error;
  const auto parent = std::filesystem::weakly_canonical(
    playlistDirectory, error);
  if (error || !std::filesystem::is_directory(parent, error)) {
    return failure(GameFileError::unreadable,
      "The M3U playlist directory cannot be resolved safely.");
  }

  std::size_t lineNumber = 0U;
  std::size_t offset = 0U;
  while (offset < text.size()) {
    const auto end = text.find('\n', offset);
    const auto rawLength =
      (end == std::string_view::npos ? text.size() : end) - offset;
    ++lineNumber;
    if (rawLength > maximumDiscPlaylistLineBytes) {
      return failure(GameFileError::invalidDiscPlaylist,
        "Invalid M3U playlist at line " + std::to_string(lineNumber) +
          ": the line exceeds 1024 bytes.");
    }
    auto line = trim(text.substr(offset, rawLength));
    offset = end == std::string_view::npos ? text.size() : end + 1U;
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (line.front() == '/' || line.front() == '\\' ||
        line.find(':') != std::string_view::npos ||
        std::ranges::any_of(line, [](unsigned char character) {
          return character < 0x20U || character == 0x7fU;
        })) {
      return failure(GameFileError::unsafePlaylistReference,
        "Invalid M3U playlist at line " + std::to_string(lineNumber) +
          ": only local relative disc paths are allowed.");
    }
    std::string portable{line};
    std::ranges::replace(portable, '\\', '/');
    const auto relative = pathFromUtf8(portable);
    if (relative.is_absolute() || relative.has_root_name() ||
        relative.has_root_directory() ||
        std::ranges::any_of(relative, [](const auto& component) {
          return component == "..";
        })) {
      return failure(GameFileError::unsafePlaylistReference,
        "Invalid M3U playlist at line " + std::to_string(lineNumber) +
          ": the path escapes the playlist directory.");
    }
    if (information.discs.size() >= maximumDiscPlaylistEntries) {
      return failure(GameFileError::invalidDiscPlaylist,
        "The M3U playlist exceeds the 32-disc limit.");
    }
    const auto candidate = playlistDirectory / relative;
    const auto resolved = std::filesystem::weakly_canonical(candidate, error);
    if (error || !pathIsWithin(resolved, parent)) {
      return failure(GameFileError::unsafePlaylistReference,
        "Invalid M3U playlist at line " + std::to_string(lineNumber) +
          ": the path escapes through a link or missing parent.");
    }
    if (std::ranges::find(information.discs, resolved) !=
        information.discs.end()) {
      return failure(GameFileError::invalidDiscPlaylist,
        "Invalid M3U playlist at line " + std::to_string(lineNumber) +
          ": duplicate discs are not allowed.");
    }
    if (auto status = validateDiscImageFile(resolved); !status) {
      return failure(GameFileError::missingPlaylistDisc,
        "Invalid M3U playlist at line " + std::to_string(lineNumber) +
          ": " + status.message);
    }
    information.discs.push_back(resolved);
  }
  if (information.discs.empty()) {
    return failure(GameFileError::invalidDiscPlaylist,
      "The M3U playlist does not contain a disc image.");
  }
  return {};
}

GameFileStatus validateDiscPlaylistFile(
  const std::filesystem::path& path,
  DiscPlaylistInfo& information)
{
  if (!hasDiscPlaylistExtension(path)) {
    information = {};
    return failure(GameFileError::invalidDiscPlaylist,
      "The selected file is not an M3U playlist.");
  }
  if (auto status = validateRegularGameFile(path); !status) {
    information = {};
    return status;
  }
  return inspectDiscPlaylist(path, information);
}

GameFileStatus validateGameFile(const std::filesystem::path& path)
{
  if (auto status = validateRegularGameFile(path); !status) {
    return status;
  }
  if (hasZipArchiveExtension(path)) {
    return inspectZipArchive(path).status;
  }
  if (hasDiscPlaylistExtension(path)) {
    DiscPlaylistInfo information;
    return inspectDiscPlaylist(path, information);
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

GameContentFilesResult gameContentFiles(const std::filesystem::path& path)
{
  if (auto status = validateRegularGameFile(path); !status) {
    return {.status = std::move(status), .files = {}};
  }

  std::vector<std::filesystem::path> files{path};
  if (lowercase(path.extension().string()) == ".cue") {
    if (auto status = inspectCueSheetFile(path, &files); !status) {
      return {.status = std::move(status), .files = {}};
    }
  }
  if (hasDiscPlaylistExtension(path)) {
    DiscPlaylistInfo information;
    if (auto status = inspectDiscPlaylist(path, information); !status) {
      return {.status = std::move(status), .files = {}};
    }
    files.insert(files.end(), information.discs.begin(), information.discs.end());
  }
  return {.status = {}, .files = std::move(files)};
}

} // namespace genplusgx
