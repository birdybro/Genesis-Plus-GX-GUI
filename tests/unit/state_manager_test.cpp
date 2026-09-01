#include "genplusgx/state_manager.h"

#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::vector<std::uint8_t> fakeRawState(std::uint8_t marker)
{
  constexpr std::string_view version = "GENPLUS-GX 1.7.6";
  std::vector<std::uint8_t> state(512U, marker);
  std::ranges::copy(version, state.begin());
  return state;
}

} // namespace

int main()
{
  QTemporaryDir temporaryRoot;
  if (!check(temporaryRoot.isValid(), "Could not create isolated state-manager root")) {
    return 1;
  }
  const auto root = std::filesystem::path{temporaryRoot.path().toStdString()} / "state-data";
  genplusgx::SaveStateManager manager{genplusgx::ApplicationPaths{root}};
  if (!check(manager.initialize(), "Save-state hierarchy initialization failed")) {
    return 2;
  }

  const genplusgx::GameIdentity firstIdentity{
    .sha256 = std::string(64U, '1'),
    .titleSlug = "state-test",
  };
  const genplusgx::GameIdentity secondIdentity{
    .sha256 = std::string(64U, '2'),
    .titleSlug = "other-game",
  };
  const auto fixedTimestamp = std::chrono::system_clock::time_point{
    std::chrono::milliseconds{1'700'000'000'123LL}};
  const auto firstPayload = fakeRawState(0x21U);

  if (!check(manager.saveSlot(firstIdentity, 0U, 0x80U, 42U, firstPayload, fixedTimestamp),
        "Initial wrapped save-state write failed") ||
      !check(manager.statePath(firstIdentity, 0U).filename() == "slot-0.gpgxstate",
        "Save-state slot filename was incorrect")) {
    return 3;
  }
  const auto firstLoad = manager.loadSlot(firstIdentity, 0U, 0x80U);
  if (!check(firstLoad.status && firstLoad.rawPayload == firstPayload,
        "Wrapped state payload round trip failed") ||
      !check(firstLoad.metadata.schemaVersion == 1U && firstLoad.metadata.slot == 0U &&
          firstLoad.metadata.hardware == 0x80U &&
          firstLoad.metadata.emulatedFrameNumber == 42U &&
          firstLoad.metadata.timestamp == fixedTimestamp &&
          firstLoad.metadata.payloadBytes == firstPayload.size(),
        "Wrapped state metadata round trip failed")) {
    return 4;
  }

  if (!check(manager.saveResumeState(
        firstIdentity, 0x80U, 43U, firstPayload, fixedTimestamp),
        "Automatic-resume checkpoint write failed") ||
      !check(manager.resumeStatePath(firstIdentity).filename() ==
          "resume.gpgxstate",
        "Automatic-resume filename was incorrect")) {
    return 15;
  }
  const auto resumeLoad = manager.loadResumeState(firstIdentity, 0x80U);
  if (!check(resumeLoad.status && resumeLoad.rawPayload == firstPayload &&
        resumeLoad.metadata.slot == genplusgx::SaveStateManager::resumeSlot &&
        resumeLoad.metadata.emulatedFrameNumber == 43U,
        "Automatic-resume checkpoint did not round-trip") ||
      !check(manager.loadResumeState(secondIdentity, 0x80U).status.error ==
          genplusgx::SaveStateError::missingState,
        "Missing per-game resume checkpoint was not isolated") ||
      !check(manager.loadStateFile(manager.resumeStatePath(firstIdentity),
          secondIdentity, 0x80U).status.error ==
          genplusgx::SaveStateError::wrongGame,
        "Wrong-game automatic-resume checkpoint was accepted")) {
    return 16;
  }

  auto replacement = fakeRawState(0x7AU);
  replacement[100] = 0xCCU;
  if (!check(manager.saveSlot(firstIdentity, 0U, 0x80U, 99U, replacement, fixedTimestamp),
        "Save-state slot replacement failed")) {
    return 5;
  }
  const auto replacementLoad = manager.loadSlot(firstIdentity, 0U, 0x80U);
  if (!check(replacementLoad.status && replacementLoad.rawPayload == replacement &&
          replacementLoad.metadata.emulatedFrameNumber == 99U,
        "Save-state slot replacement did not become visible atomically") ||
      !check(manager.loadStateFile(
          manager.statePath(firstIdentity, 0U), secondIdentity, 0x80U).status.error ==
          genplusgx::SaveStateError::wrongGame,
        "Wrong-game wrapped state was accepted") ||
      !check(manager.loadSlot(firstIdentity, 0U, 0x40U).status.error ==
          genplusgx::SaveStateError::wrongSystem,
        "Wrong-system wrapped state was accepted") ||
      !check(manager.loadSlot(firstIdentity, 9U, 0x80U).status.error ==
          genplusgx::SaveStateError::missingState,
        "Missing state did not return a typed result")) {
    return 6;
  }

  if (!check(manager.saveSlot(firstIdentity, 10U, 0x80U, 0U, replacement).error ==
          genplusgx::SaveStateError::invalidSlot,
        "Out-of-range state slot was accepted") ||
      !check(manager.loadSlot(firstIdentity, 10U, 0x80U).status.error ==
          genplusgx::SaveStateError::invalidSlot,
        "Out-of-range state slot load was accepted") ||
      !check(manager.saveSlot(firstIdentity, 1U, 0x80U, 0U,
          std::vector<std::uint8_t>(16U, 0U)).error ==
          genplusgx::SaveStateError::invalidPayload,
        "Raw payload without a core signature was accepted")) {
    return 7;
  }
  std::vector<std::uint8_t> oversized(
    genplusgx::SaveStateManager::maximumPayloadBytes + 1U, 0U);
  std::ranges::copy(std::string_view{"GENPLUS-GX 1.7.6"}, oversized.begin());
  if (!check(manager.saveSlot(firstIdentity, 1U, 0x80U, 0U, oversized).error ==
          genplusgx::SaveStateError::invalidPayload,
        "Oversized raw state payload was accepted")) {
    return 8;
  }

  const auto stateFile = manager.statePath(firstIdentity, 0U);
  auto encoded = genplusgx::readFileBounded(
    stateFile, genplusgx::SaveStateManager::maximumPayloadBytes + 128U);
  if (!check(encoded.status && encoded.exists, "Could not read state for corruption test")) {
    return 9;
  }
  encoded.data.back() ^= 0x01U;
  if (!check(genplusgx::writeFileAtomically(
          stateFile, encoded.data, encoded.data.size()),
        "Could not write checksum-corruption fixture") ||
      !check(manager.loadSlot(firstIdentity, 0U, 0x80U).status.error ==
          genplusgx::SaveStateError::checksumMismatch,
        "Corrupt state payload checksum was not rejected")) {
    return 10;
  }

  if (!check(manager.saveSlot(firstIdentity, 0U, 0x80U, 99U, replacement, fixedTimestamp),
        "Could not restore state before schema corruption")) {
    return 11;
  }
  encoded = genplusgx::readFileBounded(stateFile, encoded.data.size());
  encoded.data[8] = 2U;
  if (!check(genplusgx::writeFileAtomically(stateFile, encoded.data, encoded.data.size()),
        "Could not write schema-corruption fixture") ||
      !check(manager.loadSlot(firstIdentity, 0U, 0x80U).status.error ==
          genplusgx::SaveStateError::unsupportedSchema,
        "Unsupported state schema was not rejected")) {
    return 12;
  }

  if (!check(genplusgx::writeFileAtomically(
          stateFile, std::span{encoded.data}.first(50U), encoded.data.size()),
        "Could not write truncated-state fixture") ||
      !check(manager.loadSlot(firstIdentity, 0U, 0x80U).status.error ==
          genplusgx::SaveStateError::corruptState,
        "Truncated state was not rejected")) {
    return 13;
  }

  if (!check(manager.saveSlot(firstIdentity, 0U, 0x80U, 99U, replacement, fixedTimestamp),
        "Could not restore state before deletion") ||
      !check(manager.deleteSlot(firstIdentity, 0U), "State deletion failed") ||
      !check(manager.deleteSlot(firstIdentity, 0U).error ==
          genplusgx::SaveStateError::missingState,
        "Repeated state deletion did not report missing state")) {
    return 14;
  }

  if (!check(manager.deleteResumeState(firstIdentity),
        "Automatic-resume checkpoint deletion failed") ||
      !check(manager.loadResumeState(firstIdentity, 0x80U).status.error ==
          genplusgx::SaveStateError::missingState,
        "Deleted automatic-resume checkpoint remained visible") ||
      !check(manager.deleteResumeState(firstIdentity).error ==
          genplusgx::SaveStateError::missingState,
        "Repeated automatic-resume deletion did not report missing state")) {
    return 17;
  }

  return 0;
}
