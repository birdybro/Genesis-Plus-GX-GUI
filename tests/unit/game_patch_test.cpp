#include "genplusgx/game_patch.h"

#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <span>
#include <string_view>
#include <vector>

namespace {

bool check(bool condition, std::string_view message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes)
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

void appendLe32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
  for (unsigned shift = 0U; shift < 32U; shift += 8U) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

void writeLe32(
  std::vector<std::uint8_t>& bytes,
  std::size_t offset,
  std::uint32_t value)
{
  for (unsigned shift = 0U; shift < 32U; shift += 8U) {
    bytes.at(offset++) = static_cast<std::uint8_t>(value >> shift);
  }
}

void refreshPatchCrc(std::vector<std::uint8_t>& patch)
{
  writeLe32(patch, patch.size() - 4U,
    crc32(std::span<const std::uint8_t>{patch}.first(patch.size() - 4U)));
}

void appendVariable(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
  while (true) {
    const auto byte = static_cast<std::uint8_t>(value & 0x7fU);
    value >>= 7U;
    if (value == 0U) {
      bytes.push_back(static_cast<std::uint8_t>(byte | 0x80U));
      return;
    }
    bytes.push_back(byte);
    --value;
  }
}

std::vector<std::uint8_t> makeIps()
{
  return {
    'P', 'A', 'T', 'C', 'H',
    0x00, 0x00, 0x01, 0x00, 0x02, 'X', 'Y',
    0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, '!',
    'E', 'O', 'F', 0x00, 0x00, 0x08,
  };
}

std::vector<std::uint8_t> makeBps(
  std::span<const std::uint8_t> source,
  std::span<const std::uint8_t> target)
{
  std::vector<std::uint8_t> patch{'B', 'P', 'S', '1'};
  appendVariable(patch, source.size());
  appendVariable(patch, target.size());
  appendVariable(patch, 4U);
  patch.insert(patch.end(), {'t', 'e', 's', 't'});
  appendVariable(patch, ((3U - 1U) << 2U) | 0U); // SourceRead abc
  appendVariable(patch, ((3U - 1U) << 2U) | 1U); // TargetRead XYZ
  patch.insert(patch.end(), {'X', 'Y', 'Z'});
  appendVariable(patch, ((3U - 1U) << 2U) | 3U); // TargetCopy XYZ
  appendVariable(patch, 3U << 1U);
  appendVariable(patch, ((3U - 1U) << 2U) | 2U); // SourceCopy abc
  appendVariable(patch, 3U << 1U);
  appendLe32(patch, crc32(source));
  appendLe32(patch, crc32(target));
  appendLe32(patch, crc32(patch));
  return patch;
}

std::vector<std::uint8_t> makeUps(
  std::span<const std::uint8_t> source,
  std::span<const std::uint8_t> target)
{
  std::vector<std::uint8_t> patch{'U', 'P', 'S', '1'};
  appendVariable(patch, source.size());
  appendVariable(patch, target.size());
  const auto size = std::max(source.size(), target.size());
  std::size_t offset = 0U;
  std::size_t relativeBase = 0U;
  while (offset < size) {
    const auto sourceByte = offset < source.size() ? source[offset] : 0U;
    const auto targetByte = offset < target.size() ? target[offset] : 0U;
    if (sourceByte == targetByte) {
      ++offset;
      continue;
    }
    appendVariable(patch, offset - relativeBase);
    do {
      const auto left = offset < source.size() ? source[offset] : 0U;
      const auto right = offset < target.size() ? target[offset] : 0U;
      const auto difference = static_cast<std::uint8_t>(left ^ right);
      if (difference == 0U) {
        break;
      }
      patch.push_back(difference);
      ++offset;
    } while (offset < size);
    patch.push_back(0U);
    relativeBase = offset + 1U;
    ++offset;
  }
  appendLe32(patch, crc32(source));
  appendLe32(patch, crc32(target));
  appendLe32(patch, crc32(patch));
  return patch;
}

bool writeBytes(
  const std::filesystem::path& path,
  std::span<const std::uint8_t> bytes)
{
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(output);
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path)
{
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

int main()
{
  using namespace genplusgx;
  bool passed = true;

  const std::vector<std::uint8_t> ipsSource{'a', 'b', 'c', 'd', 'e'};
  const auto ipsPatch = makeIps();
  const auto ips = applyGamePatch(ipsSource, ipsPatch, "test.IPS");
  passed &= check(ips.status && ips.format == GamePatchFormat::ips &&
      ips.data == std::vector<std::uint8_t>({'a', 'X', 'Y', 'd', '!', '!', '!', 0U}),
    "IPS literal, RLE, growth, and final-size records apply");
  auto corruptIps = ipsPatch;
  corruptIps.pop_back();
  corruptIps.pop_back();
  passed &= check(!applyGamePatch(ipsSource, corruptIps, "bad.ips").status,
    "truncated IPS final size is rejected");
  auto zeroRle = std::vector<std::uint8_t>{'P','A','T','C','H',
    0,0,1, 0,0, 0,0,'x', 'E','O','F'};
  passed &= check(!applyGamePatch(ipsSource, zeroRle, "bad.ips").status,
    "zero-length IPS RLE is rejected");
  const std::vector<std::uint8_t> emptyIps{
    'P','A','T','C','H','E','O','F',0,0,0};
  passed &= check(!applyGamePatch(ipsSource, emptyIps, "bad.ips").status,
    "an IPS patch producing an empty game is rejected");

  const std::vector<std::uint8_t> bpsSource{'a', 'b', 'c', 'a', 'b', 'c'};
  const std::vector<std::uint8_t> bpsTarget{
    'a', 'b', 'c', 'X', 'Y', 'Z', 'X', 'Y', 'Z', 'a', 'b', 'c'};
  const auto bpsPatch = makeBps(bpsSource, bpsTarget);
  const auto bps = applyGamePatch(bpsSource, bpsPatch, "test.bps");
  passed &= check(bps.status && bps.data == bpsTarget,
    "BPS source-read, target-read, target-copy, and source-copy actions apply");
  auto wrongBpsSource = bpsSource;
  wrongBpsSource[0] = 'z';
  passed &= check(applyGamePatch(wrongBpsSource, bpsPatch, "test.bps").status.error ==
      GamePatchError::sourceMismatch,
    "BPS source checksum mismatch is explicit");
  auto corruptBps = bpsPatch;
  corruptBps.back() ^= 0x01U;
  passed &= check(applyGamePatch(bpsSource, corruptBps, "test.bps").status.error ==
      GamePatchError::checksumMismatch,
    "BPS patch checksum corruption is rejected");
  auto wrongTargetBps = bpsPatch;
  wrongTargetBps[wrongTargetBps.size() - 8U] ^= 0x40U;
  refreshPatchCrc(wrongTargetBps);
  passed &= check(applyGamePatch(
      bpsSource, wrongTargetBps, "test.bps").status.error ==
        GamePatchError::checksumMismatch,
    "BPS output checksum corruption is rejected after action execution");
  auto invalidCopyBps = bpsPatch;
  invalidCopyBps[11U] = 0x8bU;
  refreshPatchCrc(invalidCopyBps);
  passed &= check(applyGamePatch(
      bpsSource, invalidCopyBps, "test.bps").status.error ==
        GamePatchError::invalidPatch,
    "BPS cannot target-copy from unwritten output");
  std::vector<std::uint8_t> invalidVariableBps{'B','P','S','1'};
  invalidVariableBps.insert(invalidVariableBps.end(), 10U, 0U);
  invalidVariableBps.insert(invalidVariableBps.end(), 12U, 0U);
  refreshPatchCrc(invalidVariableBps);
  passed &= check(applyGamePatch(
      bpsSource, invalidVariableBps, "test.bps").status.error ==
        GamePatchError::invalidPatch,
    "overflowing or unterminated BPS variable integers are rejected");

  const std::vector<std::uint8_t> upsSource{'a', 'b', 'c', 'd'};
  const std::vector<std::uint8_t> upsTarget{'a', 'X', 'c', 'Y', 'Z'};
  const auto upsPatch = makeUps(upsSource, upsTarget);
  const auto upsForward = applyGamePatch(upsSource, upsPatch, "test.ups");
  const auto upsReverse = applyGamePatch(upsTarget, upsPatch, "test.ups");
  passed &= check(upsForward.status && upsForward.data == upsTarget &&
      upsReverse.status && upsReverse.data == upsSource,
    "UPS applies in both documented directions including size changes");
  auto wrongUps = upsSource;
  wrongUps[2] = 'q';
  passed &= check(applyGamePatch(wrongUps, upsPatch, "test.ups").status.error ==
      GamePatchError::sourceMismatch,
    "UPS rejects content matching neither direction");
  auto wrongTargetUps = upsPatch;
  wrongTargetUps[wrongTargetUps.size() - 8U] ^= 0x20U;
  refreshPatchCrc(wrongTargetUps);
  passed &= check(applyGamePatch(
      upsSource, wrongTargetUps, "test.ups").status.error ==
        GamePatchError::checksumMismatch,
    "UPS output checksum corruption is rejected after XOR execution");
  auto invalidOffsetUps = upsPatch;
  invalidOffsetUps[6U] = 0xffU;
  refreshPatchCrc(invalidOffsetUps);
  passed &= check(applyGamePatch(
      upsSource, invalidOffsetUps, "test.ups").status.error ==
        GamePatchError::invalidPatch,
    "UPS relative offsets cannot escape the declared files");
  auto unterminatedUps = upsPatch;
  unterminatedUps.erase(unterminatedUps.end() - 13);
  refreshPatchCrc(unterminatedUps);
  passed &= check(applyGamePatch(
      upsSource, unterminatedUps, "test.ups").status.error ==
        GamePatchError::invalidPatch,
    "UPS XOR records require an in-stream terminator");

  passed &= check(!applyGamePatch(ipsSource, ipsPatch, "test.xdelta").status &&
      hasSupportedGamePatchExtension("patch.BPS") &&
      supportedGamePatchExtensions().size() == 3U,
    "only advertised case-insensitive formats are accepted");

  std::mt19937 random{0x47505883U};
  for (std::size_t iteration = 0U; iteration < 512U; ++iteration) {
    auto mutated = iteration % 3U == 0U ? ipsPatch
      : (iteration % 3U == 1U ? bpsPatch : upsPatch);
    const auto mutations = 1U + random() % 4U;
    for (std::size_t mutation = 0U; mutation < mutations; ++mutation) {
      mutated[random() % mutated.size()] ^= static_cast<std::uint8_t>(random());
    }
    const auto extension = iteration % 3U == 0U ? "fuzz.ips"
      : (iteration % 3U == 1U ? "fuzz.bps" : "fuzz.ups");
    const auto result = applyGamePatch(ipsSource, mutated, extension);
    passed &= check(!result.status || result.data.size() <= maximumPatchedGameBytes,
      "bounded mutation corpus cannot produce oversized output");
  }

  QTemporaryDir temporary;
  passed &= check(temporary.isValid(), "temporary patch directory is available");
  const auto root = std::filesystem::path{temporary.path().toStdString()};
  const auto sourcePath = root / "Test Game.md";
  const auto patchPath = root / "Test Game.ips";
  passed &= check(writeBytes(sourcePath, ipsSource) && writeBytes(patchPath, ipsPatch),
    "file patch fixtures were written");
  const auto discovered = discoverGamePatchSidecar(sourcePath);
  passed &= check(discovered.status && discovered.path == patchPath,
    "single same-stem sidecar is discovered");
  const auto firstFile = applyGamePatchFile(sourcePath, patchPath, root / "cache");
  const auto secondFile = applyGamePatchFile(sourcePath, patchPath, root / "cache");
  passed &= check(firstFile.status && secondFile.status &&
      firstFile.path == secondFile.path && readBytes(firstFile.path) == ips.data &&
      readBytes(sourcePath) == ipsSource,
    "file patching is non-destructive and reuses exact cached output");
  const std::vector<std::uint8_t> collision(ips.data.size(), 0x5aU);
  passed &= check(writeBytes(firstFile.path, collision),
    "cache collision fixture was written");
  const auto collisionFile = applyGamePatchFile(
    sourcePath, patchPath, root / "cache");
  passed &= check(collisionFile.status && collisionFile.path != firstFile.path &&
      readBytes(collisionFile.path) == ips.data,
    "a mismatched cache entry receives a collision-safe alternate path");
  const auto secondSidecar = root / "Test Game.md.BPS";
  passed &= check(writeBytes(secondSidecar, bpsPatch) &&
      discoverGamePatchSidecar(sourcePath).status.error ==
        GamePatchError::ambiguousSidecar,
    "multiple sidecars require explicit user selection");

  return passed ? 0 : 1;
}
