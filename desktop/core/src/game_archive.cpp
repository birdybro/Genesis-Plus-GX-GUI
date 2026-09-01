#include "genplusgx/game_archive.h"

extern "C" {
#include "unzip.h"
#if defined(_WIN32)
#include "iowin32.h"
#endif
}

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <fstream>
#include <limits>
#include <memory>
#include <ranges>
#include <system_error>
#include <utility>

namespace genplusgx {
namespace {

constexpr std::array archivedGameExtensions{
  std::string_view{".68k"}, std::string_view{".bin"},
  std::string_view{".bms"}, std::string_view{".gen"},
  std::string_view{".gg"}, std::string_view{".md"},
  std::string_view{".mdx"}, std::string_view{".sg"},
  std::string_view{".sgd"}, std::string_view{".smd"},
  std::string_view{".sms"},
};

std::atomic<std::uint64_t> temporarySequence{0U};

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

bool isArchivedGameName(std::string_view name)
{
  const auto slash = name.find_last_of("/\\");
  const auto dot = name.find_last_of('.');
  if (dot == std::string_view::npos ||
      (slash != std::string_view::npos && dot < slash)) {
    return false;
  }
  const auto extension = lowercase(std::string{name.substr(dot)});
  return std::ranges::find(archivedGameExtensions, extension) !=
    archivedGameExtensions.end();
}

bool isSafeEntryName(std::string_view name)
{
  if (name.empty() || name.size() > maximumZipEntryNameBytes ||
      name.front() == '/' || name.front() == '\\' ||
      name.find(':') != std::string_view::npos) {
    return false;
  }
  std::string portable{name};
  std::ranges::replace(portable, '\\', '/');
  std::size_t offset = 0U;
  while (offset < portable.size()) {
    const auto end = portable.find('/', offset);
    const auto component = portable.substr(
      offset, (end == std::string::npos ? portable.size() : end) - offset);
    if (component.empty() || component == "." || component == "..") {
      return false;
    }
    offset = end == std::string::npos ? portable.size() : end + 1U;
  }
  return std::ranges::none_of(name, [](unsigned char character) {
    return character < 0x20U || character == 0x7fU;
  });
}

struct ZipCloser final {
  void operator()(void* archive) const noexcept
  {
    if (archive != nullptr) {
      static_cast<void>(unzClose(archive));
    }
  }
};

using ZipHandle = std::unique_ptr<void, ZipCloser>;

ZipHandle openArchive(const std::filesystem::path& path)
{
#if defined(_WIN32)
  zlib_filefunc64_def functions{};
  fill_win32_filefunc64W(&functions);
  return ZipHandle{unzOpen2_64(path.c_str(), &functions)};
#else
  const auto native = path.string();
  return ZipHandle{unzOpen64(native.c_str())};
#endif
}

GameFileStatus validateArchiveContainer(const std::filesystem::path& path)
{
  if (path.empty()) {
    return failure(GameFileError::emptyPath, "No ZIP archive was selected.");
  }
  if (!hasZipArchiveExtension(path)) {
    return failure(GameFileError::unsupportedExtension,
      "The selected file is not a ZIP archive.");
  }
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error) {
    return failure(GameFileError::notFound,
      "The selected ZIP archive does not exist or is not a regular file.");
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return failure(GameFileError::unreadable,
      "The ZIP archive size could not be read.");
  }
  if (size == 0U || size > maximumZipArchiveBytes) {
    return failure(GameFileError::fileTooLarge,
      "The ZIP archive is empty or exceeds the 512 MiB safety limit.");
  }
  return {};
}

GameFileStatus validateEntry(
  std::string_view name,
  const unz_file_info64& information)
{
  if (!isSafeEntryName(name)) {
    return failure(GameFileError::unsafeArchiveEntry,
      "The ZIP archive contains an unsafe entry name.");
  }
  if ((information.flag & 1U) != 0U) {
    return failure(GameFileError::encryptedArchiveEntry,
      "Encrypted ZIP entries are not supported.");
  }
  if (information.compression_method != 0U &&
      information.compression_method != Z_DEFLATED) {
    return failure(GameFileError::unsupportedArchiveCompression,
      "The ZIP archive uses an unsupported compression method.");
  }
  if (information.uncompressed_size == 0U ||
      information.uncompressed_size > maximumArchivedGameBytes) {
    return failure(GameFileError::archiveEntryTooLarge,
      "An archived game is empty or exceeds the 32 MiB core limit.");
  }
  if (information.compressed_size == 0U ||
      information.uncompressed_size / information.compressed_size >
        maximumZipCompressionRatio) {
    return failure(GameFileError::unsafeArchiveEntry,
      "The ZIP entry exceeds the supported compression-ratio limit.");
  }
  return {};
}

std::string cacheFileName(const ArchivedGameEntry& entry)
{
  const auto slash = entry.name.find_last_of("/\\");
  std::string base = slash == std::string::npos
    ? entry.name : entry.name.substr(slash + 1U);
  for (auto& character : base) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isalnum(byte) == 0 && character != '-' && character != '_' &&
        character != '.') {
      character = '_';
    }
  }
  if (base.size() > 80U) {
    const auto dot = base.find_last_of('.');
    const auto extension = dot == std::string::npos
      ? std::string{} : base.substr(dot);
    base.resize(80U - std::min<std::size_t>(extension.size(), 12U));
    base += extension.substr(0U, 12U);
  }
  std::array<char, 9U> crc{};
  const auto converted = std::to_chars(
    crc.data(), crc.data() + 8U, entry.crc32, 16);
  const std::string_view crcText{
    crc.data(), static_cast<std::size_t>(converted.ptr - crc.data())};
  return std::string(8U - crcText.size(), '0') + std::string{crcText} + "-" +
    std::to_string(entry.uncompressedSize) + "-" + base;
}

bool filesEqual(
  const std::filesystem::path& left,
  const std::filesystem::path& right)
{
  std::error_code error;
  if (std::filesystem::file_size(left, error) !=
      std::filesystem::file_size(right, error) || error) {
    return false;
  }
  std::ifstream first(left, std::ios::binary);
  std::ifstream second(right, std::ios::binary);
  if (!first || !second) {
    return false;
  }
  std::array<char, 64U * 1024U> firstBytes{};
  std::array<char, 64U * 1024U> secondBytes{};
  do {
    first.read(firstBytes.data(), static_cast<std::streamsize>(firstBytes.size()));
    second.read(secondBytes.data(), static_cast<std::streamsize>(secondBytes.size()));
    if (first.gcount() != second.gcount() ||
        !std::equal(firstBytes.begin(), firstBytes.begin() + first.gcount(),
          secondBytes.begin())) {
      return false;
    }
  } while (first.gcount() != 0);
  return !first.bad() && !second.bad();
}

} // namespace

bool hasZipArchiveExtension(const std::filesystem::path& path) noexcept
{
  return lowercase(path.extension().string()) == ".zip";
}

ZipArchiveInspection inspectZipArchive(const std::filesystem::path& path)
{
  if (auto status = validateArchiveContainer(path); !status) {
    return {.status = std::move(status), .entries = {}};
  }
  auto archive = openArchive(path);
  if (!archive) {
    return {
      .status = failure(GameFileError::invalidArchive,
        "The selected ZIP archive is malformed or unreadable."),
      .entries = {},
    };
  }
  unz_global_info64 global{};
  if (unzGetGlobalInfo64(archive.get(), &global) != UNZ_OK ||
      global.number_entry == 0U || global.number_entry > maximumZipEntries) {
    return {
      .status = failure(GameFileError::invalidArchive,
        "The ZIP archive has no entries or exceeds the 4096-entry limit."),
      .entries = {},
    };
  }

  std::vector<ArchivedGameEntry> entries;
  entries.reserve(std::min<std::uint64_t>(global.number_entry, 64U));
  int positionStatus = unzGoToFirstFile(archive.get());
  for (std::uint64_t index = 0U; index < global.number_entry; ++index) {
    if (positionStatus != UNZ_OK) {
      return {
        .status = failure(GameFileError::invalidArchive,
          "The ZIP central directory ended unexpectedly."),
        .entries = {},
      };
    }
    unz_file_info64 information{};
    if (unzGetCurrentFileInfo64(
          archive.get(), &information, nullptr, 0U, nullptr, 0U, nullptr, 0U) !=
        UNZ_OK || information.size_filename == 0U ||
        information.size_filename > maximumZipEntryNameBytes) {
      return {
        .status = failure(GameFileError::invalidArchive,
          "A ZIP entry name is missing or exceeds the 512-byte limit."),
        .entries = {},
      };
    }
    std::string name(static_cast<std::size_t>(information.size_filename) + 1U, '\0');
    if (unzGetCurrentFileInfo64(archive.get(), &information, name.data(),
          static_cast<uLong>(name.size()), nullptr, 0U, nullptr, 0U) !=
        UNZ_OK) {
      return {
        .status = failure(GameFileError::invalidArchive,
          "A ZIP entry name could not be read."),
        .entries = {},
      };
    }
    name.resize(static_cast<std::size_t>(information.size_filename));
    if (name.find('\0') != std::string::npos ||
        !isSafeEntryName(name)) {
      return {
        .status = failure(GameFileError::unsafeArchiveEntry,
          "The ZIP archive contains an unsafe entry name."),
        .entries = {},
      };
    }
    const bool directory = name.ends_with('/') || name.ends_with('\\');
    if (!directory && isArchivedGameName(name)) {
      if (auto status = validateEntry(name, information); !status) {
        return {.status = std::move(status), .entries = {}};
      }
      if (std::ranges::any_of(entries, [&name](const auto& entry) {
            return entry.name == name;
          })) {
        return {
          .status = failure(GameFileError::invalidArchive,
            "The ZIP archive contains duplicate game entry names."),
          .entries = {},
        };
      }
      entries.push_back({
        .name = std::move(name),
        .compressedSize = information.compressed_size,
        .uncompressedSize = information.uncompressed_size,
        .crc32 = static_cast<std::uint32_t>(information.crc),
      });
    }
    positionStatus = index + 1U < global.number_entry
      ? unzGoToNextFile(archive.get()) : UNZ_END_OF_LIST_OF_FILE;
  }
  if (entries.empty()) {
    return {
      .status = failure(GameFileError::archiveHasNoGames,
        "The ZIP archive contains no supported cartridge game images."),
      .entries = {},
    };
  }
  std::ranges::sort(entries, {}, &ArchivedGameEntry::name);
  return {.status = {}, .entries = std::move(entries)};
}

ZipExtractionResult extractZipGame(
  const std::filesystem::path& archivePath,
  std::string_view entryName,
  const std::filesystem::path& cacheDirectory)
{
  const auto inspection = inspectZipArchive(archivePath);
  if (!inspection.status) {
    return {.status = inspection.status, .path = {}};
  }
  const auto selected = std::ranges::find(
    inspection.entries, entryName, &ArchivedGameEntry::name);
  if (selected == inspection.entries.end()) {
    return {
      .status = failure(GameFileError::archiveEntryNotFound,
        "The selected game entry is no longer present in the ZIP archive."),
      .path = {},
    };
  }
  if (cacheDirectory.empty() || !cacheDirectory.is_absolute()) {
    return {
      .status = failure(GameFileError::unwritableCache,
        "The archive cache directory must be an absolute path."),
      .path = {},
    };
  }
  std::error_code error;
  std::filesystem::create_directories(cacheDirectory, error);
  if (error || !std::filesystem::is_directory(cacheDirectory, error)) {
    return {
      .status = failure(GameFileError::unwritableCache,
        "The archive cache directory could not be created."),
      .path = {},
    };
  }

  auto archive = openArchive(archivePath);
  if (!archive || unzGoToFirstFile(archive.get()) != UNZ_OK) {
    return {
      .status = failure(GameFileError::invalidArchive,
        "The ZIP archive changed while it was being opened."),
      .path = {},
    };
  }
  bool found = false;
  for (std::size_t index = 0U; index < maximumZipEntries; ++index) {
    unz_file_info64 information{};
    if (unzGetCurrentFileInfo64(
          archive.get(), &information, nullptr, 0U, nullptr, 0U, nullptr, 0U) !=
        UNZ_OK || information.size_filename > maximumZipEntryNameBytes) {
      break;
    }
    std::string name(static_cast<std::size_t>(information.size_filename) + 1U, '\0');
    if (unzGetCurrentFileInfo64(archive.get(), &information, name.data(),
          static_cast<uLong>(name.size()), nullptr, 0U, nullptr, 0U) !=
        UNZ_OK) {
      break;
    }
    name.resize(static_cast<std::size_t>(information.size_filename));
    if (name == entryName) {
      if (information.crc != selected->crc32 ||
          information.uncompressed_size != selected->uncompressedSize ||
          information.compressed_size != selected->compressedSize) {
        return {
          .status = failure(GameFileError::invalidArchive,
            "The ZIP archive changed during validation."),
          .path = {},
        };
      }
      found = true;
      break;
    }
    if (unzGoToNextFile(archive.get()) != UNZ_OK) {
      break;
    }
  }
  if (!found || unzOpenCurrentFile(archive.get()) != UNZ_OK) {
    return {
      .status = failure(GameFileError::archiveEntryNotFound,
        "The selected ZIP entry could not be opened."),
      .path = {},
    };
  }

  const auto destination = cacheDirectory / cacheFileName(*selected);
  const auto temporary = cacheDirectory /
    (".extract-" + std::to_string(++temporarySequence) + ".tmp");
  if (destination.string().size() > maximumCorePathBytes ||
      temporary.string().size() > maximumCorePathBytes) {
    static_cast<void>(unzCloseCurrentFile(archive.get()));
    return {
      .status = failure(GameFileError::pathTooLong,
        "The extracted game path exceeds the core's 255-byte limit."),
      .path = {},
    };
  }
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) {
    static_cast<void>(unzCloseCurrentFile(archive.get()));
    return {
      .status = failure(GameFileError::unwritableCache,
        "The selected game could not be written to the archive cache."),
      .path = {},
    };
  }
  std::array<unsigned char, 64U * 1024U> buffer{};
  std::uint64_t written = 0U;
  bool readFailed = false;
  while (written < selected->uncompressedSize) {
    const auto remaining = selected->uncompressedSize - written;
    const auto requested = static_cast<unsigned int>(
      std::min<std::uint64_t>(remaining, buffer.size()));
    const int read = unzReadCurrentFile(archive.get(), buffer.data(), requested);
    if (read <= 0 || static_cast<unsigned int>(read) > requested) {
      readFailed = true;
      break;
    }
    output.write(reinterpret_cast<const char*>(buffer.data()), read);
    if (!output) {
      readFailed = true;
      break;
    }
    written += static_cast<std::uint64_t>(read);
  }
  unsigned char extra{};
  const int trailing = readFailed
    ? 0 : unzReadCurrentFile(archive.get(), &extra, 1U);
  output.close();
  const int closeStatus = unzCloseCurrentFile(archive.get());
  if (readFailed || written != selected->uncompressedSize || trailing != 0 ||
      closeStatus != UNZ_OK || !output) {
    std::filesystem::remove(temporary, error);
    return {
      .status = failure(GameFileError::invalidArchive,
        "The ZIP entry was truncated, corrupt, or failed its CRC check."),
      .path = {},
    };
  }

  if (std::filesystem::exists(destination, error)) {
    if (!error && filesEqual(destination, temporary)) {
      std::filesystem::remove(temporary, error);
      return {.status = {}, .path = destination};
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
          return {.status = {}, .path = alternate};
        }
      } else if (!error && filesEqual(alternate, temporary)) {
        std::filesystem::remove(temporary, error);
        return {.status = {}, .path = alternate};
      }
    }
    std::filesystem::remove(temporary, error);
    return {
      .status = failure(GameFileError::unwritableCache,
        "A collision-safe archive cache filename could not be reserved."),
      .path = {},
    };
  }
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return {
      .status = failure(GameFileError::unwritableCache,
        "The extracted game could not be committed to the archive cache."),
      .path = {},
    };
  }
  return {.status = {}, .path = destination};
}

} // namespace genplusgx
