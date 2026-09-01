#include "genplusgx/game_patch.h"

#include "genplusgx/game_file.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <fstream>
#include <limits>
#include <ranges>
#include <system_error>
#include <utility>

namespace genplusgx {
namespace {

constexpr std::array patchExtensions{
  std::string_view{".ips"}, std::string_view{".bps"},
  std::string_view{".ups"},
};
std::atomic<std::uint64_t> temporarySequence{0U};

GamePatchStatus failure(GamePatchError error, std::string message)
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

std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept
{
  std::uint32_t value = 0xffffffffU;
  for (const auto byte : bytes) {
    value ^= byte;
    for (unsigned bit = 0U; bit < 8U; ++bit) {
      value = (value >> 1U) ^
        (0xedb88320U & (0U - static_cast<std::uint32_t>(value & 1U)));
    }
  }
  return value ^ 0xffffffffU;
}

std::uint32_t readLe32(std::span<const std::uint8_t> bytes) noexcept
{
  return static_cast<std::uint32_t>(bytes[0]) |
    (static_cast<std::uint32_t>(bytes[1]) << 8U) |
    (static_cast<std::uint32_t>(bytes[2]) << 16U) |
    (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

class PatchReader final {
public:
  PatchReader(std::span<const std::uint8_t> bytes, std::size_t end) noexcept
    : bytes_(bytes), end_(std::min(end, bytes.size()))
  {
  }

  [[nodiscard]] std::size_t position() const noexcept { return position_; }
  [[nodiscard]] std::size_t remaining() const noexcept
  {
    return end_ - position_;
  }

  [[nodiscard]] bool readByte(std::uint8_t& value) noexcept
  {
    if (position_ >= end_) {
      return false;
    }
    value = bytes_[position_++];
    return true;
  }

  [[nodiscard]] bool skip(std::uint64_t count) noexcept
  {
    if (count > remaining()) {
      return false;
    }
    position_ += static_cast<std::size_t>(count);
    return true;
  }

  [[nodiscard]] bool readVariable(std::uint64_t& value) noexcept
  {
    value = 0U;
    std::uint64_t shift = 1U;
    for (unsigned count = 0U; count < 10U; ++count) {
      std::uint8_t byte{};
      if (!readByte(byte)) {
        return false;
      }
      const auto digit = static_cast<std::uint64_t>(byte & 0x7fU);
      if (digit != 0U && shift >
          (std::numeric_limits<std::uint64_t>::max() - value) / digit) {
        return false;
      }
      value += digit * shift;
      if ((byte & 0x80U) != 0U) {
        return true;
      }
      if (shift > (std::numeric_limits<std::uint64_t>::max() >> 7U)) {
        return false;
      }
      shift <<= 7U;
      if (value > std::numeric_limits<std::uint64_t>::max() - shift) {
        return false;
      }
      value += shift;
    }
    return false;
  }

private:
  std::span<const std::uint8_t> bytes_;
  std::size_t end_{0U};
  std::size_t position_{0U};
};

GamePatchDataResult invalidPatch(std::string message, GamePatchFormat format)
{
  return {
    .status = failure(GamePatchError::invalidPatch, std::move(message)),
    .data = {},
    .format = format,
  };
}

GamePatchDataResult applyIps(
  std::span<const std::uint8_t> source,
  std::span<const std::uint8_t> patch)
{
  constexpr auto format = GamePatchFormat::ips;
  constexpr std::array<std::uint8_t, 5U> header{'P', 'A', 'T', 'C', 'H'};
  if (patch.size() < 8U ||
      !std::equal(header.begin(), header.end(), patch.begin())) {
    return invalidPatch("The IPS header is missing or truncated.", format);
  }
  std::vector<std::uint8_t> output(source.begin(), source.end());
  std::size_t position = header.size();
  bool foundEnd = false;
  while (position + 3U <= patch.size()) {
    if (patch[position] == 'E' && patch[position + 1U] == 'O' &&
        patch[position + 2U] == 'F') {
      position += 3U;
      foundEnd = true;
      break;
    }
    const auto offset = (static_cast<std::size_t>(patch[position]) << 16U) |
      (static_cast<std::size_t>(patch[position + 1U]) << 8U) |
      static_cast<std::size_t>(patch[position + 2U]);
    position += 3U;
    if (position + 2U > patch.size()) {
      return invalidPatch("An IPS record size is truncated.", format);
    }
    const auto size = (static_cast<std::size_t>(patch[position]) << 8U) |
      static_cast<std::size_t>(patch[position + 1U]);
    position += 2U;
    std::size_t writeSize = size;
    std::uint8_t repeatedValue{};
    if (size == 0U) {
      if (position + 3U > patch.size()) {
        return invalidPatch("An IPS RLE record is truncated.", format);
      }
      writeSize = (static_cast<std::size_t>(patch[position]) << 8U) |
        static_cast<std::size_t>(patch[position + 1U]);
      repeatedValue = patch[position + 2U];
      position += 3U;
      if (writeSize == 0U) {
        return invalidPatch("An IPS RLE record has zero length.", format);
      }
    } else if (position + writeSize > patch.size()) {
      return invalidPatch("An IPS literal record is truncated.", format);
    }
    if (offset > maximumPatchedGameBytes ||
        writeSize > maximumPatchedGameBytes - offset) {
      return {
        .status = failure(GamePatchError::outputTooLarge,
          "The IPS output exceeds the 32 MiB cartridge limit."),
        .data = {},
        .format = format,
      };
    }
    const auto required = offset + writeSize;
    if (output.size() < required) {
      output.resize(required, 0U);
    }
    if (size == 0U) {
      std::fill_n(output.begin() + static_cast<std::ptrdiff_t>(offset),
        static_cast<std::ptrdiff_t>(writeSize), repeatedValue);
    } else {
      std::copy_n(patch.begin() + static_cast<std::ptrdiff_t>(position),
        static_cast<std::ptrdiff_t>(writeSize),
        output.begin() + static_cast<std::ptrdiff_t>(offset));
      position += writeSize;
    }
  }
  if (!foundEnd) {
    return invalidPatch("The IPS end marker is missing.", format);
  }
  if (patch.size() - position == 3U) {
    const auto finalSize = (static_cast<std::size_t>(patch[position]) << 16U) |
      (static_cast<std::size_t>(patch[position + 1U]) << 8U) |
      static_cast<std::size_t>(patch[position + 2U]);
    if (finalSize > maximumPatchedGameBytes) {
      return {
        .status = failure(GamePatchError::outputTooLarge,
          "The IPS output exceeds the 32 MiB cartridge limit."),
        .data = {},
        .format = format,
      };
    }
    output.resize(finalSize, 0U);
    position += 3U;
  }
  if (position != patch.size()) {
    return invalidPatch("The IPS patch has unexpected trailing data.", format);
  }
  if (output.empty()) {
    return invalidPatch("The IPS patch produces an empty game image.", format);
  }
  return {.status = {}, .data = std::move(output), .format = format};
}

bool addRelative(std::int64_t& position, std::uint64_t encoded) noexcept
{
  const auto magnitude = encoded >> 1U;
  if (magnitude > static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max())) {
    return false;
  }
  const auto delta = static_cast<std::int64_t>(magnitude);
  if ((encoded & 1U) != 0U) {
    if (position < std::numeric_limits<std::int64_t>::min() + delta) {
      return false;
    }
    position -= delta;
  } else {
    if (position > std::numeric_limits<std::int64_t>::max() - delta) {
      return false;
    }
    position += delta;
  }
  return true;
}

GamePatchDataResult applyBps(
  std::span<const std::uint8_t> source,
  std::span<const std::uint8_t> patch)
{
  constexpr auto format = GamePatchFormat::bps;
  constexpr std::array<std::uint8_t, 4U> header{'B', 'P', 'S', '1'};
  if (patch.size() < 16U ||
      !std::equal(header.begin(), header.end(), patch.begin())) {
    return invalidPatch("The BPS header is missing or truncated.", format);
  }
  const auto footer = patch.size() - 12U;
  if (readLe32(patch.subspan(patch.size() - 4U, 4U)) !=
      crc32(patch.first(patch.size() - 4U))) {
    return {
      .status = failure(GamePatchError::checksumMismatch,
        "The BPS patch checksum does not match its contents."),
      .data = {},
      .format = format,
    };
  }
  PatchReader reader{patch, footer};
  static_cast<void>(reader.skip(header.size()));
  std::uint64_t sourceSize{};
  std::uint64_t targetSize{};
  std::uint64_t metadataSize{};
  if (!reader.readVariable(sourceSize) || !reader.readVariable(targetSize) ||
      !reader.readVariable(metadataSize) || !reader.skip(metadataSize)) {
    return invalidPatch("The BPS sizes or metadata are malformed.", format);
  }
  if (sourceSize != source.size() ||
      readLe32(patch.subspan(footer, 4U)) != crc32(source)) {
    return {
      .status = failure(GamePatchError::sourceMismatch,
        "The BPS patch was created for different source content."),
      .data = {},
      .format = format,
    };
  }
  if (targetSize == 0U) {
    return invalidPatch("The BPS patch declares an empty output.", format);
  }
  if (targetSize > maximumPatchedGameBytes) {
    return {
      .status = failure(GamePatchError::outputTooLarge,
        "The BPS output exceeds the 32 MiB cartridge limit."),
      .data = {},
      .format = format,
    };
  }
  std::vector<std::uint8_t> output;
  output.reserve(static_cast<std::size_t>(targetSize));
  std::int64_t sourceRelative = 0;
  std::int64_t targetRelative = 0;
  while (output.size() < targetSize) {
    std::uint64_t command{};
    if (!reader.readVariable(command)) {
      return invalidPatch("A BPS action is truncated or malformed.", format);
    }
    const auto length = (command >> 2U) + 1U;
    if (length > targetSize - output.size()) {
      return invalidPatch("A BPS action exceeds the declared output size.", format);
    }
    switch (command & 3U) {
    case 0U: {
      if (output.size() > source.size() ||
          length > source.size() - output.size()) {
        return invalidPatch("A BPS source-read action is out of bounds.", format);
      }
      const auto begin = source.begin() +
        static_cast<std::ptrdiff_t>(output.size());
      output.insert(output.end(), begin,
        begin + static_cast<std::ptrdiff_t>(length));
      break;
    }
    case 1U:
      if (length > reader.remaining()) {
        return invalidPatch("A BPS target-read action is truncated.", format);
      }
      for (std::uint64_t index = 0U; index < length; ++index) {
        std::uint8_t value{};
        static_cast<void>(reader.readByte(value));
        output.push_back(value);
      }
      break;
    case 2U: {
      std::uint64_t relative{};
      if (!reader.readVariable(relative) ||
          !addRelative(sourceRelative, relative) || sourceRelative < 0 ||
          static_cast<std::uint64_t>(sourceRelative) > source.size() ||
          length > source.size() - static_cast<std::size_t>(sourceRelative)) {
        return invalidPatch("A BPS source-copy action is out of bounds.", format);
      }
      for (std::uint64_t index = 0U; index < length; ++index) {
        output.push_back(source[static_cast<std::size_t>(sourceRelative++)]);
      }
      break;
    }
    case 3U: {
      std::uint64_t relative{};
      if (!reader.readVariable(relative) ||
          !addRelative(targetRelative, relative) || targetRelative < 0) {
        return invalidPatch("A BPS target-copy action is out of bounds.", format);
      }
      for (std::uint64_t index = 0U; index < length; ++index) {
        if (static_cast<std::uint64_t>(targetRelative) >= output.size()) {
          return invalidPatch("A BPS target-copy action reads unwritten data.", format);
        }
        output.push_back(output[static_cast<std::size_t>(targetRelative++)]);
      }
      break;
    }
    }
  }
  if (reader.position() != footer) {
    return invalidPatch("The BPS patch has unused action data.", format);
  }
  if (readLe32(patch.subspan(footer + 4U, 4U)) != crc32(output)) {
    return {
      .status = failure(GamePatchError::checksumMismatch,
        "The BPS output checksum does not match the patched content."),
      .data = {},
      .format = format,
    };
  }
  return {.status = {}, .data = std::move(output), .format = format};
}

GamePatchDataResult applyUps(
  std::span<const std::uint8_t> source,
  std::span<const std::uint8_t> patch)
{
  constexpr auto format = GamePatchFormat::ups;
  constexpr std::array<std::uint8_t, 4U> header{'U', 'P', 'S', '1'};
  if (patch.size() < 18U ||
      !std::equal(header.begin(), header.end(), patch.begin())) {
    return invalidPatch("The UPS header is missing or truncated.", format);
  }
  const auto footer = patch.size() - 12U;
  if (readLe32(patch.subspan(patch.size() - 4U, 4U)) !=
      crc32(patch.first(patch.size() - 4U))) {
    return {
      .status = failure(GamePatchError::checksumMismatch,
        "The UPS patch checksum does not match its contents."),
      .data = {},
      .format = format,
    };
  }
  PatchReader reader{patch, footer};
  static_cast<void>(reader.skip(header.size()));
  std::uint64_t inputSize{};
  std::uint64_t outputSize{};
  if (!reader.readVariable(inputSize) || !reader.readVariable(outputSize)) {
    return invalidPatch("The UPS sizes are malformed.", format);
  }
  if (inputSize > maximumPatchedGameBytes ||
      outputSize > maximumPatchedGameBytes) {
    return {
      .status = failure(GamePatchError::outputTooLarge,
        "The UPS source or output exceeds the 32 MiB cartridge limit."),
      .data = {},
      .format = format,
    };
  }
  if (inputSize == 0U || outputSize == 0U) {
    return invalidPatch(
      "The UPS patch declares an empty source or output.", format);
  }
  const auto inputCrc = readLe32(patch.subspan(footer, 4U));
  const auto outputCrc = readLe32(patch.subspan(footer + 4U, 4U));
  const auto sourceCrc = crc32(source);
  std::uint64_t targetSize{};
  std::uint32_t targetCrc{};
  if (source.size() == inputSize && sourceCrc == inputCrc) {
    targetSize = outputSize;
    targetCrc = outputCrc;
  } else if (source.size() == outputSize && sourceCrc == outputCrc) {
    targetSize = inputSize;
    targetCrc = inputCrc;
  } else {
    return {
      .status = failure(GamePatchError::sourceMismatch,
        "The UPS patch does not match either supported source direction."),
      .data = {},
      .format = format,
    };
  }
  std::vector<std::uint8_t> output(static_cast<std::size_t>(targetSize), 0U);
  std::copy_n(source.begin(),
    static_cast<std::ptrdiff_t>(
      std::min<std::uint64_t>(source.size(), targetSize)),
    output.begin());
  const auto workingSize = std::max(inputSize, outputSize);
  std::uint64_t offset = 0U;
  while (reader.position() < footer) {
    std::uint64_t relative{};
    if (!reader.readVariable(relative) || relative > workingSize ||
        offset > workingSize - relative) {
      return invalidPatch("A UPS relative offset is out of bounds.", format);
    }
    offset += relative;
    bool terminated = false;
    while (reader.position() < footer) {
      std::uint8_t value{};
      static_cast<void>(reader.readByte(value));
      if (value == 0U) {
        if (offset > workingSize) {
          return invalidPatch("A UPS record terminator is out of bounds.", format);
        }
        ++offset;
        terminated = true;
        break;
      }
      if (offset >= workingSize) {
        return invalidPatch("A UPS XOR record is out of bounds.", format);
      }
      if (offset < output.size()) {
        output[static_cast<std::size_t>(offset)] ^= value;
      }
      ++offset;
    }
    if (!terminated) {
      return invalidPatch("A UPS XOR record is missing its terminator.", format);
    }
  }
  if (crc32(output) != targetCrc) {
    return {
      .status = failure(GamePatchError::checksumMismatch,
        "The UPS output checksum does not match the patched content."),
      .data = {},
      .format = format,
    };
  }
  return {.status = {}, .data = std::move(output), .format = format};
}

struct BoundedFile final {
  GamePatchStatus status;
  std::vector<std::uint8_t> bytes;
};

BoundedFile readBoundedFile(
  const std::filesystem::path& path,
  std::size_t maximum,
  std::string_view description)
{
  if (path.empty()) {
    return {failure(GamePatchError::emptyPath,
      "No " + std::string{description} + " was selected."), {}};
  }
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error) {
    return {failure(GamePatchError::notFound,
      "The selected " + std::string{description} +
        " does not exist or is not a regular file."), {}};
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return {failure(GamePatchError::unreadable,
      "The selected " + std::string{description} +
        " size could not be read."), {}};
  }
  if (size == 0U || size > maximum) {
    return {failure(GamePatchError::fileTooLarge,
      "The selected " + std::string{description} +
        " is empty or exceeds its safety limit."), {}};
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {failure(GamePatchError::unreadable,
      "The selected " + std::string{description} + " could not be opened."), {}};
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  if (!input || input.peek() != std::ifstream::traits_type::eof()) {
    return {failure(GamePatchError::unreadable,
      "The selected " + std::string{description} +
        " changed or could not be read completely."), {}};
  }
  return {{}, std::move(bytes)};
}

std::string hexadecimal(std::uint32_t value)
{
  constexpr std::array digits{'0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string result(8U, '0');
  for (auto cursor = result.rbegin(); cursor != result.rend(); ++cursor) {
    *cursor = digits[value & 0xfU];
    value >>= 4U;
  }
  return result;
}

std::string safeStem(const std::filesystem::path& path)
{
  auto result = path.stem().string();
  for (auto& character : result) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isalnum(byte) == 0 && character != '-' && character != '_') {
      character = '_';
    }
  }
  if (result.empty()) {
    result = "patched-game";
  }
  result.resize(std::min<std::size_t>(result.size(), 48U));
  return result;
}

bool filesEqual(
  const std::filesystem::path& path,
  std::span<const std::uint8_t> expected)
{
  std::error_code error;
  if (std::filesystem::file_size(path, error) != expected.size() || error) {
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  std::array<std::uint8_t, 64U * 1024U> buffer{};
  std::size_t offset = 0U;
  while (offset < expected.size()) {
    const auto count = std::min(buffer.size(), expected.size() - offset);
    input.read(reinterpret_cast<char*>(buffer.data()),
      static_cast<std::streamsize>(count));
    if (!input || !std::equal(buffer.begin(), buffer.begin() +
          static_cast<std::ptrdiff_t>(count),
          expected.begin() + static_cast<std::ptrdiff_t>(offset))) {
      return false;
    }
    offset += count;
  }
  return input.peek() == std::ifstream::traits_type::eof();
}

} // namespace

std::span<const std::string_view> supportedGamePatchExtensions() noexcept
{
  return patchExtensions;
}

bool hasSupportedGamePatchExtension(
  const std::filesystem::path& path) noexcept
{
  const auto extension = lowercase(path.extension().string());
  return std::ranges::find(patchExtensions, extension) != patchExtensions.end();
}

std::string_view gamePatchFormatName(GamePatchFormat format) noexcept
{
  switch (format) {
  case GamePatchFormat::ips:
    return "IPS";
  case GamePatchFormat::bps:
    return "BPS";
  case GamePatchFormat::ups:
    return "UPS";
  }
  return "Unknown";
}

GamePatchDiscoveryResult discoverGamePatchSidecar(
  const std::filesystem::path& gamePath)
{
  if (gamePath.empty()) {
    return {
      .status = failure(GamePatchError::emptyPath,
        "A game path is required before searching for a sidecar patch."),
      .path = std::nullopt,
    };
  }
  std::vector<std::filesystem::path> found;
  for (const auto extension : patchExtensions) {
    for (const auto uppercase : {false, true}) {
      auto suffix = std::string{extension};
      if (uppercase) {
        std::ranges::transform(suffix, suffix.begin(), [](unsigned char byte) {
          return static_cast<char>(std::toupper(byte));
        });
      }
      auto replaced = gamePath;
      replaced.replace_extension(suffix);
      auto appended = gamePath;
      appended += suffix;
      for (const auto& candidate : {replaced, appended}) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error &&
            std::ranges::find(found, candidate) == found.end()) {
          found.push_back(candidate);
        }
      }
    }
  }
  if (found.size() > 1U) {
    return {
      .status = failure(GamePatchError::ambiguousSidecar,
        "Multiple sidecar patches match this game. Use Open Game with Patch "
        "to select one explicitly."),
      .path = std::nullopt,
    };
  }
  return {
    .status = {},
    .path = found.empty()
      ? std::optional<std::filesystem::path>{}
      : std::optional<std::filesystem::path>{found.front()},
  };
}

GamePatchDataResult applyGamePatch(
  std::span<const std::uint8_t> source,
  std::span<const std::uint8_t> patch,
  const std::filesystem::path& patchPath)
{
  if (source.empty()) {
    return {
      .status = failure(GamePatchError::sourceMismatch,
        "An empty game image cannot be patched."),
      .data = {},
    };
  }
  if (source.size() > maximumPatchedGameBytes) {
    return {
      .status = failure(GamePatchError::outputTooLarge,
        "The source image exceeds the 32 MiB cartridge limit."),
      .data = {},
    };
  }
  if (patch.empty() || patch.size() > maximumGamePatchBytes) {
    return {
      .status = failure(GamePatchError::fileTooLarge,
        "The patch is empty or exceeds the 64 MiB safety limit."),
      .data = {},
    };
  }
  const auto extension = lowercase(patchPath.extension().string());
  if (extension == ".ips") {
    return applyIps(source, patch);
  }
  if (extension == ".bps") {
    return applyBps(source, patch);
  }
  if (extension == ".ups") {
    return applyUps(source, patch);
  }
  return {
    .status = failure(GamePatchError::unsupportedFormat,
      "Only IPS, BPS, and UPS soft patches are supported."),
    .data = {},
  };
}

GamePatchFileResult applyGamePatchFile(
  const std::filesystem::path& sourcePath,
  const std::filesystem::path& patchPath,
  const std::filesystem::path& cacheDirectory)
{
  if (!hasSupportedGamePatchExtension(patchPath)) {
    return {
      .status = failure(GamePatchError::unsupportedFormat,
        "Only IPS, BPS, and UPS soft patches are supported."),
      .path = {},
    };
  }
  const auto source = readBoundedFile(
    sourcePath, maximumPatchedGameBytes, "cartridge image");
  if (!source.status) {
    return {.status = source.status, .path = {}};
  }
  const auto patch = readBoundedFile(
    patchPath, maximumGamePatchBytes, "patch file");
  if (!patch.status) {
    return {.status = patch.status, .path = {}};
  }
  auto patched = applyGamePatch(source.bytes, patch.bytes, patchPath);
  if (!patched.status) {
    return {
      .status = std::move(patched.status),
      .path = {},
      .format = patched.format,
    };
  }
  if (cacheDirectory.empty() || !cacheDirectory.is_absolute()) {
    return {
      .status = failure(GamePatchError::unwritableCache,
        "The soft-patch cache directory must be an absolute path."),
      .path = {},
      .format = patched.format,
    };
  }
  std::error_code error;
  std::filesystem::create_directories(cacheDirectory, error);
  if (error || !std::filesystem::is_directory(cacheDirectory, error)) {
    return {
      .status = failure(GamePatchError::unwritableCache,
        "The soft-patch cache directory could not be created."),
      .path = {},
      .format = patched.format,
    };
  }
  const auto destination = cacheDirectory /
    (safeStem(sourcePath) + "-" + hexadecimal(crc32(source.bytes)) + "-" +
      hexadecimal(crc32(patch.bytes)) + "-" + hexadecimal(crc32(patched.data)) +
      sourcePath.extension().string());
  const auto temporary = cacheDirectory /
    (".patch-" + std::to_string(++temporarySequence) + ".tmp");
  if (destination.string().size() > maximumCorePathBytes ||
      temporary.string().size() > maximumCorePathBytes) {
    return {
      .status = failure(GamePatchError::pathTooLong,
        "The patched game path exceeds the core's 255-byte limit."),
      .path = {},
      .format = patched.format,
    };
  }
  if (std::filesystem::exists(destination, error) && !error &&
      filesEqual(destination, patched.data)) {
    return {.status = {}, .path = destination, .format = patched.format};
  }
  if (error) {
    return {
      .status = failure(GamePatchError::unwritableCache,
        "The soft-patch cache destination could not be inspected."),
      .path = {},
      .format = patched.format,
    };
  }
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(patched.data.data()),
    static_cast<std::streamsize>(patched.data.size()));
  output.close();
  if (!output) {
    std::filesystem::remove(temporary, error);
    return {
      .status = failure(GamePatchError::unwritableCache,
        "The patched game could not be written to the cache."),
      .path = {},
      .format = patched.format,
    };
  }
  if (std::filesystem::exists(destination, error)) {
    if (!error && filesEqual(destination, patched.data)) {
      std::filesystem::remove(temporary, error);
      return {.status = {}, .path = destination, .format = patched.format};
    }
    for (std::size_t suffix = 1U; suffix <= 16U; ++suffix) {
      const auto alternate = destination.parent_path() /
        (destination.stem().string() + "-" + std::to_string(suffix) +
          destination.extension().string());
      if (alternate.string().size() > maximumCorePathBytes) {
        continue;
      }
      if (!std::filesystem::exists(alternate, error)) {
        std::filesystem::rename(temporary, alternate, error);
        if (!error) {
          return {.status = {}, .path = alternate, .format = patched.format};
        }
      } else if (!error && filesEqual(alternate, patched.data)) {
        std::filesystem::remove(temporary, error);
        return {.status = {}, .path = alternate, .format = patched.format};
      }
    }
    std::filesystem::remove(temporary, error);
    return {
      .status = failure(GamePatchError::unwritableCache,
        "A collision-safe soft-patch cache filename could not be reserved."),
      .path = {},
      .format = patched.format,
    };
  }
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return {
      .status = failure(GamePatchError::unwritableCache,
        "The patched game could not be committed to the cache."),
      .path = {},
      .format = patched.format,
    };
  }
  return {.status = {}, .path = destination, .format = patched.format};
}

} // namespace genplusgx
