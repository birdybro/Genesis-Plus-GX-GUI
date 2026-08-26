#include "genplusgx/library/game_metadata.h"
#include "genplusgx/library/game_metadata_service.h"
#include "genplusgx/persistence.h"

#include "synthetic_rom.h"

#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::chrono_literals;

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::filesystem::path pathIn(
  const QTemporaryDir& directory,
  std::string_view name)
{
  return std::filesystem::path{directory.path().toStdString()} / std::string{name};
}

bool writeBytes(
  const std::filesystem::path& path,
  std::span<const std::uint8_t> bytes)
{
  std::ofstream stream{path, std::ios::binary | std::ios::trunc};
  stream.write(
    reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(stream);
}

bool writeText(const std::filesystem::path& path, std::string_view text)
{
  return writeBytes(path, {
    reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

std::vector<std::uint8_t> makeEightBitRom(
  std::uint8_t region,
  std::uint16_t checksum)
{
  std::vector<std::uint8_t> bytes(32U * 1024U, 0U);
  constexpr std::size_t header = 0x7ff0U;
  constexpr std::string_view signature{"TMR SEGA"};
  std::copy(signature.begin(), signature.end(),
    bytes.begin() + static_cast<std::ptrdiff_t>(header));
  bytes[header + 0x0aU] = static_cast<std::uint8_t>(checksum & 0xffU);
  bytes[header + 0x0bU] = static_cast<std::uint8_t>(checksum >> 8U);
  bytes[header + 0x0cU] = 0x34U;
  bytes[header + 0x0dU] = 0x12U;
  bytes[header + 0x0eU] = 0x02U;
  bytes[header + 0x0fU] = static_cast<std::uint8_t>(
    (static_cast<unsigned int>(region) << 4U) | 0x0cU);
  return bytes;
}

std::vector<std::uint8_t> encodeSmd(std::span<const std::uint8_t> rom)
{
  constexpr std::size_t blockSize = 16U * 1024U;
  constexpr std::size_t halfBlock = blockSize / 2U;
  std::vector<std::uint8_t> result(512U + blockSize, 0U);
  for (std::size_t index = 0U; index < halfBlock; ++index) {
    result[512U + index] = rom[index * 2U + 1U];
    result[512U + halfBlock + index] = rom[index * 2U];
  }
  return result;
}

} // namespace

int main()
{
  using namespace genplusgx::library;

  const auto genesisBytes = genplusgx::test::makeGenesisRamMarkerRom();
  const auto genesis = parseGameMetadataBytes(
    genesisBytes, ".md", genesisBytes.size());
  if (!check(genesis.status && genesis.metadata.headerRecognized &&
      genesis.metadata.system == GameSystem::genesis &&
      genesis.metadata.domesticTitle == "GENPLUSGX CORE TEST" &&
      genesis.metadata.internationalTitle ==
        "GENPLUSGX SYNTHETIC RAM MARKER" &&
      genesis.metadata.productCode == "TEST-000001" &&
      genesis.metadata.region == "Americas" &&
      genesis.metadata.headerChecksum.has_value() &&
      genesis.metadata.declaredRomSize == genesisBytes.size() &&
      genesis.metadata.displayTitle() ==
        "GENPLUSGX SYNTHETIC RAM MARKER",
      "Genesis header metadata was not decoded")) {
    return 1;
  }

  const auto sramBytes = genplusgx::test::makeGenesisSramWriterRom();
  const auto sram = parseGameMetadataBytes(sramBytes, "gen", sramBytes.size());
  if (!check(sram.status && sram.metadata.mapper.find("SRAM") != std::string::npos,
      "Genesis SRAM mapper metadata was not decoded")) {
    return 2;
  }

  const auto smsBytes = makeEightBitRom(4U, 0x5a3cU);
  const auto sms = parseGameMetadataBytes(smsBytes, ".sms", smsBytes.size());
  const auto gameGear = parseGameMetadataBytes(
    makeEightBitRom(6U, 0x1234U), ".gg");
  const std::array<std::uint8_t, 3> sgBytes{1U, 2U, 3U};
  const auto sg = parseGameMetadataBytes(sgBytes, ".sg");
  if (!check(sms.status && sms.metadata.headerRecognized &&
      sms.metadata.system == GameSystem::masterSystem &&
      sms.metadata.region == "Export" &&
      sms.metadata.headerChecksum == 0x5a3cU &&
      gameGear.metadata.system == GameSystem::gameGear &&
      gameGear.metadata.region == "Export" &&
      sg.metadata.system == GameSystem::sg1000,
      "8-bit Sega system/header detection failed")) {
    return 3;
  }

  const auto smd = parseGameMetadataBytes(
    encodeSmd(genesisBytes), ".smd", genesisBytes.size() + 512U);
  if (!check(smd.status && smd.metadata.headerRecognized &&
      smd.metadata.internationalTitle == "GENPLUSGX SYNTHETIC RAM MARKER",
      "SMD header deinterleaving failed")) {
    return 4;
  }

  const auto discBytes = genplusgx::test::makeSegaCdDiscImage(
    genplusgx::test::SyntheticSegaCdRegion::europe);
  const auto disc = parseGameMetadataBytes(discBytes, ".iso", discBytes.size());
  if (!check(disc.status && disc.metadata.headerRecognized &&
      disc.metadata.system == GameSystem::segaCd &&
      disc.metadata.region == "Europe/PAL" &&
      disc.metadata.internationalTitle == "GENPLUSGX TEST DISC" &&
      disc.metadata.mapper == "Optical disc",
      "Sega CD disc metadata was not decoded")) {
    return 5;
  }

  const std::array<std::uint8_t, 3> abc{
    static_cast<std::uint8_t>('a'), static_cast<std::uint8_t>('b'),
    static_cast<std::uint8_t>('c')};
  QTemporaryDir directory;
  if (!check(directory.isValid(), "Temporary directory was unavailable") ||
      !check(writeBytes(pathIn(directory, "tiny.sg"), abc),
        "SHA fixture could not be written")) {
    return 6;
  }
  const auto hashed = readGameMetadata(pathIn(directory, "tiny.sg"));
  if (!check(hashed.status && hashed.metadata.sha256 ==
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" &&
      hashed.metadata.fileSize == 3U,
      "Streaming SHA-256 or file-size metadata was incorrect")) {
    return 7;
  }

  const auto genesisPath = pathIn(directory, "metadata.md");
  if (!check(writeBytes(genesisPath, genesisBytes),
      "Genesis metadata fixture could not be written")) {
    return 8;
  }
  const auto streamedGenesis = readGameMetadata(genesisPath);
  const auto cancelledMetadata = readGameMetadata(
    genesisPath, [] { return true; });
  if (!check(streamedGenesis.status &&
      streamedGenesis.metadata.computedChecksum ==
        streamedGenesis.metadata.headerChecksum &&
      streamedGenesis.metadata.sha256.size() == 64U,
      "Streaming Genesis checksum did not match its generated header") ||
      !check(cancelledMetadata.status.error == GameMetadataError::cancelled,
        "Metadata hashing ignored a cooperative cancellation request")) {
    return 9;
  }

  const auto discPath = pathIn(directory, "track01.bin");
  const auto cuePath = pathIn(directory, "fixture.cue");
  constexpr std::string_view cue{
    "FILE \"track01.bin\" BINARY\n"
    "  TRACK 01 MODE1/2048\n"
    "    INDEX 01 00:00:00\n"
    "  TRACK 02 AUDIO\n"
    "    INDEX 01 00:02:00\n"};
  if (!check(writeBytes(discPath, discBytes) && writeText(cuePath, cue),
      "CUE/BIN fixtures could not be written")) {
    return 10;
  }
  const auto cueMetadata = readGameMetadata(cuePath);
  if (!check(cueMetadata.status &&
      cueMetadata.metadata.system == GameSystem::segaCd &&
      cueMetadata.metadata.trackCount == 2U &&
      cueMetadata.metadata.relatedDataPath == discPath &&
      cueMetadata.metadata.headerRecognized &&
      cueMetadata.metadata.internationalTitle == "GENPLUSGX TEST DISC",
      "CUE/BIN metadata workflow failed")) {
    return 11;
  }

  const auto changedDirectory = pathIn(directory, "changed");
  const auto relocatedDirectory = pathIn(directory, "relocated");
  std::error_code directoryError;
  std::filesystem::create_directories(changedDirectory, directoryError);
  std::filesystem::create_directories(relocatedDirectory, directoryError);
  auto changedDiscBytes = discBytes;
  changedDiscBytes.back() ^= 0x01U;
  const auto changedCuePath = changedDirectory / "fixture.cue";
  const auto relocatedCuePath = relocatedDirectory / "fixture.cue";
  if (!check(!directoryError &&
      writeText(changedCuePath, cue) &&
      writeBytes(changedDirectory / "track01.bin", changedDiscBytes) &&
      writeText(relocatedCuePath, cue) &&
      writeBytes(relocatedDirectory / "track01.bin", discBytes),
      "Composite metadata identity fixtures could not be written")) {
    return 12;
  }
  const auto changedCueMetadata = readGameMetadata(changedCuePath);
  const auto relocatedCueMetadata = readGameMetadata(relocatedCuePath);
  const auto persistenceIdentity = genplusgx::identifyGame(cuePath);
  if (!check(changedCueMetadata.status && relocatedCueMetadata.status &&
          persistenceIdentity.status,
        "Composite metadata identities could not be read") ||
      !check(cueMetadata.metadata.sha256 != changedCueMetadata.metadata.sha256,
        "Metadata identity ignored changed CUE track content") ||
      !check(cueMetadata.metadata.sha256 == relocatedCueMetadata.metadata.sha256,
        "Metadata identity changed when identical CUE content was relocated") ||
      !check(cueMetadata.metadata.sha256 == persistenceIdentity.identity.sha256,
        "Library and persistence derived different CUE identities")) {
    return 13;
  }

  const auto unsafeCuePath = pathIn(directory, "unsafe.cue");
  constexpr std::string_view unsafeCue{
    "FILE \"../outside.bin\" BINARY\n  TRACK 01 MODE1/2048\n"};
  if (!check(writeText(unsafeCuePath, unsafeCue), "Unsafe CUE fixture failed") ||
      !check(readGameMetadata(unsafeCuePath).metadata.relatedDataPath.empty(),
        "CUE path traversal was followed by the metadata reader")) {
    return 14;
  }

  if (!check(!parseGameMetadataBytes(abc, ".txt").status,
      "Unsupported metadata input was accepted")) {
    return 15;
  }

  // A fixed-seed bounded property corpus exercises every offset parser with
  // truncated and arbitrary data. Successful recognition is not required; a
  // deterministic, bounded result without out-of-range access is.
  std::mt19937 random{0x4750584dU};
  constexpr std::array fuzzExtensions{
    std::string_view{".bin"}, std::string_view{".smd"},
    std::string_view{".sms"}, std::string_view{".gg"},
    std::string_view{".cue"}, std::string_view{".iso"}};
  for (std::size_t iteration = 0U; iteration < 2'000U; ++iteration) {
    const auto length = static_cast<std::size_t>(random() % 70'000U);
    std::vector<std::uint8_t> bytes(length);
    std::ranges::generate(bytes, [&random] {
      return static_cast<std::uint8_t>(random() & 0xffU);
    });
    const auto parsed = parseGameMetadataBytes(
      bytes, fuzzExtensions[iteration % fuzzExtensions.size()], bytes.size());
    if (!check(parsed.status && parsed.metadata.fileSize == bytes.size(),
        "Bounded metadata property corpus produced an invalid result")) {
      return 16;
    }
  }

  GameMetadataService service{2U, 4U};
  if (!check(service.start(), "Metadata service did not start")) {
    return 17;
  }
  const auto started = service.waitForEvent(2s);
  if (!check(started && started->type == GameMetadataEventType::serviceStarted,
      "Metadata service start event was not published") ||
      !check(service.request(41U, cuePath), "Metadata request was rejected")) {
    return 18;
  }
  const auto ready = service.waitForEvent(2s);
  if (!check(ready && ready->succeeded() && ready->operationId == 41U &&
      ready->path == cuePath && ready->metadata.trackCount == 2U,
      "Background metadata result was not delivered")) {
    return 19;
  }
  if (!check(!service.request(0U, cuePath), "Zero operation ID was accepted") ||
      !check(service.stop(), "Metadata service did not stop")) {
    return 20;
  }
  const auto stopped = service.waitForEvent(2s);
  if (!check(stopped && stopped->type == GameMetadataEventType::serviceStopped,
      "Metadata service stop event was not published") ||
      !check(service.start(), "Metadata service did not restart") ||
      !check(service.waitForEvent(2s).has_value(), "Restart event was missing") ||
      !check(service.stop(), "Restarted metadata service did not stop")) {
    return 21;
  }

  return 0;
}
