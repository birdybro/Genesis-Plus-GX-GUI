#include "genplusgx/library/game_metadata.h"

#include "genplusgx/game_file.h"
#include "genplusgx/persistence.h"

#include <QFile>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <utility>
#include <vector>

namespace genplusgx::library {
namespace {

GameMetadataResult failure(GameMetadataError error, std::string message)
{
  return {
    .status = {.error = error, .message = std::move(message)},
    .metadata = {},
  };
}

std::string lowercase(std::string text)
{
  std::ranges::transform(text, text.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return text;
}

std::string normalizedExtension(std::string_view extension)
{
  std::string result{extension};
  if (!result.empty() && result.front() != '.') {
    result.insert(result.begin(), '.');
  }
  return lowercase(std::move(result));
}

bool hasRange(
  std::span<const std::uint8_t> bytes,
  std::size_t offset,
  std::size_t length) noexcept
{
  return offset <= bytes.size() && length <= bytes.size() - offset;
}

bool equalsAscii(
  std::span<const std::uint8_t> bytes,
  std::size_t offset,
  std::string_view expected) noexcept
{
  if (!hasRange(bytes, offset, expected.size())) {
    return false;
  }
  return std::equal(
    expected.begin(), expected.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::string cleanText(
  std::span<const std::uint8_t> bytes,
  std::size_t offset,
  std::size_t length)
{
  if (!hasRange(bytes, offset, length)) {
    return {};
  }
  std::string result;
  result.reserve(length);
  bool pendingSpace = false;
  for (const auto byte : bytes.subspan(offset, length)) {
    if (byte == 0U || byte == 0xffU) {
      pendingSpace = !result.empty();
      continue;
    }
    const auto character = static_cast<unsigned char>(byte);
    if (std::isspace(character) != 0) {
      pendingSpace = !result.empty();
      continue;
    }
    if (character < 0x20U || character > 0x7eU) {
      continue;
    }
    if (pendingSpace) {
      result.push_back(' ');
      pendingSpace = false;
    }
    result.push_back(static_cast<char>(character));
  }
  return result;
}

std::uint16_t readBig16(
  std::span<const std::uint8_t> bytes,
  std::size_t offset) noexcept
{
  return static_cast<std::uint16_t>(
    (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
    static_cast<std::uint16_t>(bytes[offset + 1U]));
}

std::uint32_t readBig32(
  std::span<const std::uint8_t> bytes,
  std::size_t offset) noexcept
{
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::string genesisRegion(std::string value)
{
  std::string result;
  const auto append = [&result](std::string_view region) {
    if (!result.empty()) {
      result += ", ";
    }
    result += region;
  };
  const auto upper = [&value](char character) {
    return value.find(character) != std::string::npos ||
           value.find(static_cast<char>(std::tolower(
             static_cast<unsigned char>(character)))) != std::string::npos;
  };
  if (upper('J')) {
    append("Japan");
  }
  if (upper('U')) {
    append("Americas");
  }
  if (upper('E')) {
    append("Europe/PAL");
  }
  return result.empty() ? std::move(value) : result;
}

void parseGenesisHeader(
  std::span<const std::uint8_t> bytes,
  std::size_t base,
  GameMetadata& metadata,
  bool segaCd)
{
  if (!hasRange(bytes, base + 0x100U, 0x100U)) {
    return;
  }
  const auto console = cleanText(bytes, base + 0x100U, 16U);
  if (!segaCd && console.rfind("SEGA", 0U) != 0U) {
    return;
  }
  metadata.headerRecognized = true;
  metadata.copyright = cleanText(bytes, base + 0x110U, 16U);
  metadata.domesticTitle = cleanText(bytes, base + 0x120U, 48U);
  metadata.internationalTitle = cleanText(bytes, base + 0x150U, 48U);
  metadata.romType = cleanText(bytes, base + 0x180U, 2U);
  metadata.productCode = cleanText(bytes, base + 0x182U, 12U);
  metadata.peripheralSupport = cleanText(bytes, base + 0x190U, 16U);
  const auto rawRegion = cleanText(bytes, base + 0x1f0U, 16U);
  metadata.region = genesisRegion(rawRegion);
  if (hasRange(bytes, base + 0x18eU, 2U)) {
    metadata.headerChecksum = readBig16(bytes, base + 0x18eU);
  }
  if (hasRange(bytes, base + 0x1a0U, 8U)) {
    const auto start = readBig32(bytes, base + 0x1a0U);
    const auto end = readBig32(bytes, base + 0x1a4U);
    if (end >= start) {
      metadata.declaredRomSize = static_cast<std::uint64_t>(end) - start + 1U;
    }
  }
  if (!segaCd && equalsAscii(bytes, base + 0x1b0U, "RA") &&
      hasRange(bytes, base + 0x1b4U, 8U)) {
    const auto start = readBig32(bytes, base + 0x1b4U);
    const auto end = readBig32(bytes, base + 0x1b8U);
    metadata.mapper = "Cartridge SRAM";
    if (end >= start) {
      metadata.mapper += " (" + std::to_string(start) + "-" +
                         std::to_string(end) + ")";
    }
  }
}

std::optional<std::size_t> findEightBitHeader(
  std::span<const std::uint8_t> bytes) noexcept
{
  constexpr std::array offsets{0x7ff0U, 0x3ff0U, 0x1ff0U};
  for (const auto offset : offsets) {
    if (equalsAscii(bytes, offset, "TMR SEGA")) {
      return offset;
    }
  }
  return std::nullopt;
}

void parseEightBitHeader(
  std::span<const std::uint8_t> bytes,
  GameMetadata& metadata)
{
  const auto offset = findEightBitHeader(bytes);
  if (!offset || !hasRange(bytes, *offset, 16U)) {
    return;
  }
  metadata.headerRecognized = true;
  metadata.headerChecksum = static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(bytes[*offset + 0x0aU]) |
    (static_cast<std::uint16_t>(bytes[*offset + 0x0bU]) << 8U));
  const auto product = static_cast<std::uint32_t>(bytes[*offset + 0x0cU]) |
                       (static_cast<std::uint32_t>(bytes[*offset + 0x0dU]) << 8U) |
                       (static_cast<std::uint32_t>(bytes[*offset + 0x0eU] & 0x0fU)
                        << 16U);
  metadata.productCode = std::to_string(product);
  const auto regionCode = static_cast<std::uint8_t>(bytes[*offset + 0x0fU] >> 4U);
  switch (regionCode) {
    case 3U:
      metadata.region = "Japan";
      metadata.system = GameSystem::masterSystem;
      break;
    case 4U:
      metadata.region = "Export";
      metadata.system = GameSystem::masterSystem;
      break;
    case 5U:
      metadata.region = "Japan";
      metadata.system = GameSystem::gameGear;
      break;
    case 6U:
      metadata.region = "Export";
      metadata.system = GameSystem::gameGear;
      break;
    case 7U:
      metadata.region = "International";
      metadata.system = GameSystem::gameGear;
      break;
    default:
      metadata.region = "Unknown";
      break;
  }
}

bool hasSegaCdSignature(std::span<const std::uint8_t> bytes) noexcept
{
  return equalsAscii(bytes, 0U, "SEGADISCSYSTEM") ||
         equalsAscii(bytes, 16U, "SEGADISCSYSTEM");
}

void parseSegaCdHeader(
  std::span<const std::uint8_t> bytes,
  GameMetadata& metadata)
{
  const auto base = equalsAscii(bytes, 0U, "SEGADISCSYSTEM") ? 0U : 16U;
  metadata.system = GameSystem::segaCd;
  metadata.mapper = "Optical disc";
  metadata.headerRecognized = hasSegaCdSignature(bytes);
  if (!metadata.headerRecognized) {
    return;
  }
  parseGenesisHeader(bytes, base, metadata, true);
  if (hasRange(bytes, base + 0x20bU, 1U)) {
    switch (bytes[base + 0x20bU]) {
      case 0x64U:
        metadata.region = "Europe/PAL";
        break;
      case 0xa1U:
        metadata.region = "Japan";
        break;
      default:
        metadata.region = "Americas";
        break;
    }
  }
}

std::vector<std::uint8_t> decodeSmdHeader(std::span<const std::uint8_t> bytes)
{
  constexpr std::size_t copierHeader = 512U;
  constexpr std::size_t blockSize = 16U * 1024U;
  constexpr std::size_t halfBlock = blockSize / 2U;
  if (!hasRange(bytes, copierHeader, blockSize)) {
    return {};
  }
  std::vector<std::uint8_t> decoded(blockSize);
  const auto block = bytes.subspan(copierHeader, blockSize);
  for (std::size_t index = 0U; index < halfBlock; ++index) {
    decoded[index * 2U] = block[halfBlock + index];
    decoded[index * 2U + 1U] = block[index];
  }
  return decoded;
}

bool isSupportedExtension(std::string_view extension) noexcept
{
  const auto extensions = supportedGameExtensions();
  return std::ranges::find(extensions, extension) != extensions.end();
}

QString pathToQString(const std::filesystem::path& path)
{
#if defined(Q_OS_WIN)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string());
#endif
}

std::filesystem::path pathFromUtf8(std::string_view path)
{
#if defined(Q_OS_WIN)
  return std::filesystem::path{QString::fromUtf8(
    path.data(), static_cast<qsizetype>(path.size())).toStdWString()};
#else
  return std::filesystem::path{std::string{path}};
#endif
}

std::filesystem::path cueDataFile(
  std::span<const std::uint8_t> bytes,
  std::uint32_t& trackCount)
{
  std::string text;
  text.reserve(bytes.size());
  for (const auto byte : bytes) {
    text.push_back(byte == 0U ? '\n' : static_cast<char>(byte));
  }

  std::filesystem::path dataPath;
  std::size_t position = 0U;
  while (position < text.size()) {
    const auto end = text.find_first_of("\r\n", position);
    auto line = text.substr(position, end == std::string::npos
      ? std::string::npos : end - position);
    position = end == std::string::npos ? text.size() : end + 1U;
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos) {
      continue;
    }
    line.erase(0U, first);
    const auto upperLine = lowercase(line);
    if (upperLine.rfind("track ", 0U) == 0U && trackCount < 99U) {
      ++trackCount;
    }
    if (!dataPath.empty() || upperLine.rfind("file ", 0U) != 0U) {
      continue;
    }
    auto value = line.substr(5U);
    const auto valueStart = value.find_first_not_of(" \t");
    if (valueStart == std::string::npos) {
      continue;
    }
    value.erase(0U, valueStart);
    if (!value.empty() && value.front() == '"') {
      const auto quote = value.find('"', 1U);
      if (quote != std::string::npos) {
        dataPath = pathFromUtf8(value.substr(1U, quote - 1U));
      }
    } else {
      const auto separator = value.find_first_of(" \t");
      dataPath = pathFromUtf8(value.substr(0U, separator));
    }
  }
  return dataPath;
}

bool isSafeRelativeCuePath(const std::filesystem::path& path)
{
  if (path.empty() || path.is_absolute()) {
    return false;
  }
  return std::ranges::none_of(path, [](const auto& component) {
    return component == "..";
  });
}

bool cuePathResolvesWithinDirectory(
  const std::filesystem::path& cuePath,
  const std::filesystem::path& dataPath)
{
  std::error_code error;
  const auto root = std::filesystem::weakly_canonical(cuePath.parent_path(), error);
  if (error) {
    return false;
  }
  const auto resolved = std::filesystem::weakly_canonical(dataPath, error);
  if (error) {
    return false;
  }
  const auto relative = resolved.lexically_relative(root);
  return !relative.empty() && !relative.is_absolute() &&
         std::ranges::none_of(relative, [](const auto& component) {
           return component == "..";
         });
}

void enrichCueMetadata(
  std::span<const std::uint8_t> cueBytes,
  const std::filesystem::path& cuePath,
  GameMetadata& metadata)
{
  auto referenced = cueDataFile(cueBytes, metadata.trackCount);
  if (!isSafeRelativeCuePath(referenced)) {
    metadata.notes = referenced.empty()
      ? "The CUE sheet does not name a data track."
      : "The CUE data path is absolute or escapes the CUE directory; metadata was not read.";
    return;
  }
  referenced = (cuePath.parent_path() / referenced).lexically_normal();
  if (!cuePathResolvesWithinDirectory(cuePath, referenced)) {
    metadata.notes = "The CUE data path resolves outside the CUE directory; metadata was not read.";
    return;
  }
  metadata.relatedDataPath = referenced;

  QFile file{pathToQString(referenced)};
  if (!file.open(QIODevice::ReadOnly)) {
    metadata.notes = "The referenced CUE data file is missing or unreadable.";
    return;
  }
  const auto data = file.read(4096);
  if (data.isEmpty()) {
    metadata.notes = "The referenced CUE data file is empty or unreadable.";
    return;
  }
  std::vector<std::uint8_t> bytes(
    reinterpret_cast<const std::uint8_t*>(data.constData()),
    reinterpret_cast<const std::uint8_t*>(data.constData()) + data.size());
  if (!hasSegaCdSignature(bytes)) {
    metadata.notes = "The referenced data track does not contain a recognized Sega CD header.";
    return;
  }
  GameMetadata disc;
  disc.format = "CUE/BIN disc";
  parseSegaCdHeader(bytes, disc);
  metadata.domesticTitle = std::move(disc.domesticTitle);
  metadata.internationalTitle = std::move(disc.internationalTitle);
  metadata.copyright = std::move(disc.copyright);
  metadata.productCode = std::move(disc.productCode);
  metadata.region = std::move(disc.region);
  metadata.romType = std::move(disc.romType);
  metadata.peripheralSupport = std::move(disc.peripheralSupport);
  metadata.mapper = std::move(disc.mapper);
  metadata.headerRecognized = disc.headerRecognized;
}

} // namespace

std::string_view gameSystemName(GameSystem system) noexcept
{
  switch (system) {
    case GameSystem::sg1000:
      return "Sega SG-1000";
    case GameSystem::masterSystem:
      return "Sega Master System / Mark III";
    case GameSystem::gameGear:
      return "Sega Game Gear";
    case GameSystem::genesis:
      return "Sega Genesis / Mega Drive";
    case GameSystem::segaCd:
      return "Sega CD / Mega CD";
    case GameSystem::unknown:
      return "Unknown";
  }
  return "Unknown";
}

std::string GameMetadata::displayTitle() const
{
  if (!internationalTitle.empty()) {
    return internationalTitle;
  }
  if (!domesticTitle.empty()) {
    return domesticTitle;
  }
  return path.filename().stem().string();
}

GameMetadataResult parseGameMetadataBytes(
  std::span<const std::uint8_t> data,
  std::string_view extension,
  std::uintmax_t fileSize)
{
  const auto normalized = normalizedExtension(extension);
  if (!isSupportedExtension(normalized)) {
    return failure(
      GameMetadataError::unsupportedFile,
      "The file extension is not supported by this desktop build.");
  }

  GameMetadataResult result;
  auto& metadata = result.metadata;
  metadata.fileSize = fileSize == 0U ? data.size() : fileSize;

  if (normalized == ".cue") {
    metadata.system = GameSystem::segaCd;
    metadata.format = "CUE sheet";
    metadata.mapper = "Optical disc";
    return result;
  }
  if (normalized == ".chd") {
    metadata.system = GameSystem::segaCd;
    metadata.format = "CHD disc image";
    metadata.mapper = "Optical disc";
    metadata.notes = "Embedded CHD metadata is resolved by the emulator core when loaded.";
    return result;
  }
  if (hasSegaCdSignature(data) || normalized == ".iso") {
    metadata.format = normalized == ".iso" ? "ISO disc image" : "Raw disc image";
    parseSegaCdHeader(data, metadata);
    return result;
  }
  if (normalized == ".smd") {
    metadata.system = GameSystem::genesis;
    metadata.format = "Super Magic Drive interleaved ROM";
    metadata.mapper = "Core auto-detection";
    const auto decoded = decodeSmdHeader(data);
    parseGenesisHeader(decoded, 0U, metadata, false);
    if (decoded.empty()) {
      metadata.notes = "The SMD image is too short to contain a complete interleaved header block.";
    }
    return result;
  }
  if (normalized == ".sg" || normalized == ".sgd") {
    metadata.system = GameSystem::sg1000;
    metadata.format = "SG-1000 ROM";
    metadata.mapper = "Core auto-detection";
    return result;
  }
  if (normalized == ".sms" || normalized == ".bms" || normalized == ".gg") {
    metadata.system = normalized == ".gg"
      ? GameSystem::gameGear : GameSystem::masterSystem;
    metadata.format = normalized == ".gg" ? "Game Gear ROM" : "Master System ROM";
    metadata.mapper = "Core auto-detection";
    parseEightBitHeader(data, metadata);
    return result;
  }

  if (const auto eightBitHeader = findEightBitHeader(data); eightBitHeader) {
    metadata.system = GameSystem::masterSystem;
    metadata.format = "8-bit Sega ROM";
    metadata.mapper = "Core auto-detection";
    parseEightBitHeader(data, metadata);
    return result;
  }

  metadata.system = GameSystem::genesis;
  metadata.format = "Genesis / Mega Drive ROM";
  metadata.mapper = "Core auto-detection";
  parseGenesisHeader(data, 0U, metadata, false);
  if (!metadata.headerRecognized) {
    metadata.notes = "No recognized cartridge header was found; the emulator core will perform final detection.";
  }
  return result;
}

GameMetadataResult readGameMetadata(
  const std::filesystem::path& path,
  const std::function<bool()>& cancellationRequested)
{
  if (path.empty()) {
    return failure(GameMetadataError::invalidPath, "No game file was selected.");
  }
  const auto validation = validateGameFile(path);
  if (!validation) {
    const auto error = validation.error == GameFileError::unsupportedExtension ||
                       validation.error == GameFileError::unsupportedDiscExtension
      ? GameMetadataError::unsupportedFile : GameMetadataError::invalidPath;
    return failure(error, validation.message);
  }

  const QFileInfo before{pathToQString(path)};
  if (before.size() < 0) {
    return failure(GameMetadataError::readFailed, "The game file size could not be read.");
  }
  const auto fileSize = static_cast<std::uintmax_t>(before.size());
  if (fileSize > maximumMetadataFileBytes) {
    return failure(
      GameMetadataError::fileTooLarge,
      "The selected file exceeds the 16 GiB metadata safety limit.");
  }

  std::vector<std::uint8_t> header;
  header.reserve(static_cast<std::size_t>(
    std::min<std::uintmax_t>(fileSize, maximumMetadataHeaderBytes)));
  std::uint16_t checksum = 0U;
  bool checksumHighBytePending = false;
  std::uint8_t checksumHighByte = 0U;
  const auto contentHash = hashGameContent(path,
    [&](std::span<const std::uint8_t> bytes, std::uintmax_t offset) {
      const auto headerRemaining = maximumMetadataHeaderBytes - header.size();
      const auto headerCount = std::min(headerRemaining, bytes.size());
      header.insert(header.end(), bytes.begin(),
        bytes.begin() + static_cast<std::ptrdiff_t>(headerCount));
      for (std::size_t index = 0U; index < bytes.size(); ++index) {
        const auto absoluteOffset = offset + index;
        if (absoluteOffset < 0x200U) {
          continue;
        }
        if (!checksumHighBytePending) {
          checksumHighByte = bytes[index];
          checksumHighBytePending = true;
        } else {
          checksum = static_cast<std::uint16_t>(
            checksum + (static_cast<std::uint16_t>(checksumHighByte) << 8U) +
            bytes[index]);
          checksumHighBytePending = false;
        }
      }
    }, cancellationRequested);
  if (!contentHash.status) {
    if (contentHash.status.error == PersistenceError::cancelled) {
      return failure(GameMetadataError::cancelled, contentHash.status.message);
    }
    const auto error = contentHash.status.error == PersistenceError::fileOpenFailed
      ? GameMetadataError::openFailed : GameMetadataError::readFailed;
    return failure(error, contentHash.status.message);
  }
  if (contentHash.primaryFileSize != fileSize) {
    return failure(GameMetadataError::readFailed,
      "The selected game file changed while metadata was being read.");
  }

  auto result = parseGameMetadataBytes(header, path.extension().string(), fileSize);
  if (!result.status) {
    return result;
  }
  result.metadata.path = path;
  result.metadata.sha256 = contentHash.sha256;
  const auto extension = normalizedExtension(path.extension().string());
  if (result.metadata.system == GameSystem::genesis &&
      result.metadata.headerRecognized && extension != ".smd" &&
      fileSize >= 0x200U) {
    result.metadata.computedChecksum = checksum;
  }
  if (extension == ".cue") {
    enrichCueMetadata(header, path, result.metadata);
  }
  return result;
}

} // namespace genplusgx::library
