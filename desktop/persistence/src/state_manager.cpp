#include "genplusgx/state_manager.h"

#include <QByteArrayView>
#include <QCryptographicHash>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace genplusgx {
namespace {

constexpr std::array<std::uint8_t, 8> stateMagic{
  'G', 'P', 'G', 'X', 'S', 'T', '0', '1'};
constexpr std::size_t headerBytes = 128U;
constexpr std::size_t gameHashOffset = 48U;
constexpr std::size_t payloadHashOffset = 80U;
constexpr std::size_t coreVersionOffset = 112U;
constexpr std::size_t coreVersionBytes = 16U;
constexpr std::string_view coreStatePrefix = "GENPLUS-GX ";

SaveStateStatus success()
{
  return {};
}

SaveStateStatus failure(SaveStateError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

bool isValidSlot(std::uint32_t slot)
{
  return slot >= SaveStateManager::minimumSlot && slot <= SaveStateManager::maximumSlot;
}

template<typename Value>
void appendLittleEndian(std::vector<std::uint8_t>& output, Value value)
{
  for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
    output.push_back(static_cast<std::uint8_t>(value & static_cast<Value>(0xFFU)));
    value = static_cast<Value>(value >> 8U);
  }
}

template<typename Value>
std::optional<Value> readLittleEndian(
  std::span<const std::uint8_t> input,
  std::size_t offset)
{
  if (offset > input.size() || (input.size() - offset) < sizeof(Value)) {
    return std::nullopt;
  }
  Value value = 0;
  for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
    value |= static_cast<Value>(input[offset + byte]) << (byte * 8U);
  }
  return value;
}

std::optional<std::array<std::uint8_t, 32>> decodeSha256(std::string_view encoded)
{
  if (encoded.size() != 64U) {
    return std::nullopt;
  }
  const auto nibble = [](char character) -> std::optional<std::uint8_t> {
    if (character >= '0' && character <= '9') {
      return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
      return static_cast<std::uint8_t>(character - 'a' + 10);
    }
    return std::nullopt;
  };

  std::array<std::uint8_t, 32> decoded{};
  for (std::size_t index = 0; index < decoded.size(); ++index) {
    const auto high = nibble(encoded[index * 2U]);
    const auto low = nibble(encoded[(index * 2U) + 1U]);
    if (!high || !low) {
      return std::nullopt;
    }
    decoded[index] = static_cast<std::uint8_t>((*high << 4U) | *low);
  }
  return decoded;
}

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data)
{
  QCryptographicHash hash{QCryptographicHash::Sha256};
  hash.addData(QByteArrayView{
    reinterpret_cast<const char*>(data.data()),
    static_cast<qsizetype>(data.size())});
  const auto result = hash.result();
  std::array<std::uint8_t, 32> output{};
  std::memcpy(output.data(), result.constData(), output.size());
  return output;
}

SaveStateStatus persistenceFailure(const PersistenceStatus& status)
{
  return failure(SaveStateError::ioError, status.message);
}

} // namespace

SaveStateManager::SaveStateManager(ApplicationPaths paths)
  : paths_(std::move(paths))
{
}

SaveStateStatus SaveStateManager::initialize() const
{
  const auto initialized = paths_.initialize();
  return initialized ? success() : persistenceFailure(initialized);
}

std::filesystem::path SaveStateManager::gameStateDirectory(
  const GameIdentity& identity) const
{
  return paths_.statesDirectory() / identity.directoryName();
}

std::filesystem::path SaveStateManager::statePath(
  const GameIdentity& identity,
  std::uint32_t slot) const
{
  return gameStateDirectory(identity) / ("slot-" + std::to_string(slot) + ".gpgxstate");
}

std::filesystem::path SaveStateManager::resumeStatePath(
  const GameIdentity& identity) const
{
  return gameStateDirectory(identity) / "resume.gpgxstate";
}

SaveStateStatus SaveStateManager::saveSlot(
  const GameIdentity& identity,
  std::uint32_t slot,
  std::uint32_t hardware,
  std::uint64_t emulatedFrameNumber,
  std::span<const std::uint8_t> rawPayload,
  std::chrono::system_clock::time_point timestamp) const
{
  if (!isValidSlot(slot)) {
    return failure(SaveStateError::invalidSlot, "The save-state slot must be between 0 and 9.");
  }
  return saveFile(statePath(identity, slot), identity, slot, hardware,
    emulatedFrameNumber, rawPayload, timestamp);
}

SaveStateStatus SaveStateManager::saveResumeState(
  const GameIdentity& identity,
  std::uint32_t hardware,
  std::uint64_t emulatedFrameNumber,
  std::span<const std::uint8_t> rawPayload,
  std::chrono::system_clock::time_point timestamp) const
{
  return saveFile(resumeStatePath(identity), identity, resumeSlot, hardware,
    emulatedFrameNumber, rawPayload, timestamp);
}

SaveStateStatus SaveStateManager::saveFile(
  const std::filesystem::path& path,
  const GameIdentity& identity,
  std::uint32_t encodedSlot,
  std::uint32_t hardware,
  std::uint64_t emulatedFrameNumber,
  std::span<const std::uint8_t> rawPayload,
  std::chrono::system_clock::time_point timestamp) const
{
  if (!identity.valid()) {
    return failure(SaveStateError::invalidGameIdentity, "The game identity is invalid.");
  }
  if (rawPayload.size() < coreVersionBytes || rawPayload.size() > maximumPayloadBytes ||
      !std::ranges::equal(coreStatePrefix, rawPayload.first(coreStatePrefix.size()))) {
    return failure(SaveStateError::invalidPayload, "The raw Genesis Plus GX state payload is invalid.");
  }
  const auto gameHash = decodeSha256(identity.sha256);
  if (!gameHash) {
    return failure(SaveStateError::invalidGameIdentity, "The game SHA-256 is invalid.");
  }
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
    timestamp.time_since_epoch()).count();
  if (milliseconds < 0) {
    return failure(SaveStateError::invalidPayload, "The save-state timestamp predates the Unix epoch.");
  }

  const auto payloadHash = sha256(rawPayload);
  std::vector<std::uint8_t> file;
  file.reserve(headerBytes + rawPayload.size());
  file.insert(file.end(), stateMagic.begin(), stateMagic.end());
  appendLittleEndian(file, currentSchemaVersion);
  appendLittleEndian(file, static_cast<std::uint32_t>(headerBytes));
  appendLittleEndian(file, static_cast<std::uint64_t>(milliseconds));
  appendLittleEndian(file, hardware);
  appendLittleEndian(file, encodedSlot);
  appendLittleEndian(file, emulatedFrameNumber);
  appendLittleEndian(file, static_cast<std::uint64_t>(rawPayload.size()));
  file.insert(file.end(), gameHash->begin(), gameHash->end());
  file.insert(file.end(), payloadHash.begin(), payloadHash.end());
  file.insert(file.end(), rawPayload.begin(), rawPayload.begin() + coreVersionBytes);
  if (file.size() != headerBytes) {
    return failure(SaveStateError::invalidPayload, "The save-state header layout is inconsistent.");
  }
  file.insert(file.end(), rawPayload.begin(), rawPayload.end());

  const auto written = writeFileAtomically(
    path, file, headerBytes + maximumPayloadBytes);
  return written ? success() : persistenceFailure(written);
}

SaveStateLoadResult SaveStateManager::loadResumeState(
  const GameIdentity& identity,
  std::uint32_t expectedHardware) const
{
  constexpr auto expectedSlot = resumeSlot;
  return loadFile(
    resumeStatePath(identity), identity, expectedHardware, &expectedSlot);
}

SaveStateLoadResult SaveStateManager::loadSlot(
  const GameIdentity& identity,
  std::uint32_t slot,
  std::uint32_t expectedHardware) const
{
  if (!isValidSlot(slot)) {
    return {
      .status = failure(SaveStateError::invalidSlot, "The save-state slot must be between 0 and 9."),
      .metadata = {},
      .rawPayload = {},
    };
  }
  return loadFile(statePath(identity, slot), identity, expectedHardware, &slot);
}

SaveStateLoadResult SaveStateManager::loadStateFile(
  const std::filesystem::path& path,
  const GameIdentity& expectedIdentity,
  std::uint32_t expectedHardware) const
{
  return loadFile(path, expectedIdentity, expectedHardware, nullptr);
}

SaveStateLoadResult SaveStateManager::loadFile(
  const std::filesystem::path& path,
  const GameIdentity& expectedIdentity,
  std::uint32_t expectedHardware,
  const std::uint32_t* expectedSlot) const
{
  if (!expectedIdentity.valid()) {
    return {
      .status = failure(SaveStateError::invalidGameIdentity, "The expected game identity is invalid."),
      .metadata = {},
      .rawPayload = {},
    };
  }
  const auto expectedHash = decodeSha256(expectedIdentity.sha256);
  if (!expectedHash) {
    return {
      .status = failure(SaveStateError::invalidGameIdentity, "The expected game SHA-256 is invalid."),
      .metadata = {},
      .rawPayload = {},
    };
  }

  const auto loaded = readFileBounded(path, headerBytes + maximumPayloadBytes);
  if (!loaded.status) {
    return {
      .status = loaded.status.error == PersistenceError::dataTooLarge
                  ? failure(SaveStateError::corruptState, loaded.status.message)
                  : persistenceFailure(loaded.status),
      .metadata = {},
      .rawPayload = {},
    };
  }
  if (!loaded.exists) {
    return {
      .status = failure(SaveStateError::missingState, "The save-state file does not exist."),
      .metadata = {},
      .rawPayload = {},
    };
  }
  const std::span<const std::uint8_t> file{loaded.data};
  if (file.size() < headerBytes ||
      !std::ranges::equal(stateMagic, file.first(stateMagic.size()))) {
    return {
      .status = failure(SaveStateError::corruptState, "The save-state magic or header is invalid."),
      .metadata = {},
      .rawPayload = {},
    };
  }

  const auto schema = readLittleEndian<std::uint32_t>(file, 8U);
  const auto encodedHeaderBytes = readLittleEndian<std::uint32_t>(file, 12U);
  const auto timestamp = readLittleEndian<std::uint64_t>(file, 16U);
  const auto hardware = readLittleEndian<std::uint32_t>(file, 24U);
  const auto slot = readLittleEndian<std::uint32_t>(file, 28U);
  const auto frameNumber = readLittleEndian<std::uint64_t>(file, 32U);
  const auto payloadBytes = readLittleEndian<std::uint64_t>(file, 40U);
  if (!schema || !encodedHeaderBytes || !timestamp || !hardware || !slot ||
      !frameNumber || !payloadBytes || *encodedHeaderBytes != headerBytes) {
    return {
      .status = failure(SaveStateError::corruptState, "The save-state header fields are invalid."),
      .metadata = {},
      .rawPayload = {},
    };
  }
  if (*schema != currentSchemaVersion) {
    return {
      .status = failure(SaveStateError::unsupportedSchema, "The save-state schema is not supported."),
      .metadata = {},
      .rawPayload = {},
    };
  }
  const bool knownStateKind = isValidSlot(*slot) || *slot == resumeSlot;
  if (!knownStateKind || (expectedSlot != nullptr && *slot != *expectedSlot)) {
    return {
      .status = failure(SaveStateError::corruptState, "The save-state slot metadata is invalid."),
      .metadata = {},
      .rawPayload = {},
    };
  }
  if (*payloadBytes < coreVersionBytes || *payloadBytes > maximumPayloadBytes ||
      *payloadBytes != (file.size() - headerBytes)) {
    return {
      .status = failure(SaveStateError::corruptState, "The save-state payload length is invalid."),
      .metadata = {},
      .rawPayload = {},
    };
  }
  if (!std::ranges::equal(
        *expectedHash, file.subspan(gameHashOffset, expectedHash->size()))) {
    return {
      .status = failure(SaveStateError::wrongGame, "The save state belongs to a different game."),
      .metadata = {},
      .rawPayload = {},
    };
  }
  if (*hardware != expectedHardware) {
    return {
      .status = failure(SaveStateError::wrongSystem, "The save state belongs to different hardware."),
      .metadata = {},
      .rawPayload = {},
    };
  }
  if (*timestamp > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return {
      .status = failure(SaveStateError::corruptState, "The save-state timestamp is invalid."),
      .metadata = {},
      .rawPayload = {},
    };
  }

  const auto payload = file.subspan(headerBytes, static_cast<std::size_t>(*payloadBytes));
  const auto actualPayloadHash = sha256(payload);
  if (!std::ranges::equal(
        actualPayloadHash, file.subspan(payloadHashOffset, actualPayloadHash.size()))) {
    return {
      .status = failure(SaveStateError::checksumMismatch, "The save-state payload checksum does not match."),
      .metadata = {},
      .rawPayload = {},
    };
  }
  if (!std::ranges::equal(
        file.subspan(coreVersionOffset, coreVersionBytes), payload.first(coreVersionBytes)) ||
      !std::ranges::equal(coreStatePrefix, payload.first(coreStatePrefix.size()))) {
    return {
      .status = failure(SaveStateError::corruptState, "The raw core-state signature is invalid."),
      .metadata = {},
      .rawPayload = {},
    };
  }

  return {
    .status = success(),
    .metadata = {
      .schemaVersion = *schema,
      .slot = *slot,
      .hardware = *hardware,
      .emulatedFrameNumber = *frameNumber,
      .timestamp = std::chrono::system_clock::time_point{
        std::chrono::milliseconds{static_cast<std::int64_t>(*timestamp)}},
      .payloadBytes = static_cast<std::size_t>(*payloadBytes),
    },
    .rawPayload = std::vector<std::uint8_t>{payload.begin(), payload.end()},
  };
}

SaveStateStatus SaveStateManager::deleteSlot(
  const GameIdentity& identity,
  std::uint32_t slot) const
{
  if (!isValidSlot(slot)) {
    return failure(SaveStateError::invalidSlot, "The save-state slot must be between 0 and 9.");
  }

  return deleteFile(identity, statePath(identity, slot));
}

SaveStateStatus SaveStateManager::deleteResumeState(
  const GameIdentity& identity) const
{
  return deleteFile(identity, resumeStatePath(identity));
}

SaveStateStatus SaveStateManager::deleteFile(
  const GameIdentity& identity,
  const std::filesystem::path& path) const
{
  if (!identity.valid()) {
    return failure(SaveStateError::invalidGameIdentity, "The game identity is invalid.");
  }

  std::error_code error;
  const bool removed = std::filesystem::remove(path, error);
  if (error) {
    return failure(SaveStateError::ioError, "Unable to delete the save-state file.");
  }
  if (!removed) {
    return failure(SaveStateError::missingState, "The save-state file does not exist.");
  }
  return success();
}

} // namespace genplusgx
