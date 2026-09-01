#include "genplusgx/state_manager.h"

#include <QByteArray>
#include <QBuffer>
#include <QImage>
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

std::vector<std::uint8_t> fakePng()
{
  const auto bytes = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  return {
    reinterpret_cast<const std::uint8_t*>(bytes.constData()),
    reinterpret_cast<const std::uint8_t*>(bytes.constData()) +
      static_cast<std::size_t>(bytes.size())};
}

std::vector<std::uint8_t> oversizedPng()
{
  QImage image{1025, 1, QImage::Format_RGB32};
  image.fill(Qt::black);
  QByteArray bytes;
  QBuffer buffer{&bytes};
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
    return {};
  }
  return {
    reinterpret_cast<const std::uint8_t*>(bytes.constData()),
    reinterpret_cast<const std::uint8_t*>(bytes.constData()) +
      static_cast<std::size_t>(bytes.size())};
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
      !check(firstLoad.metadata.schemaVersion ==
          genplusgx::SaveStateManager::currentSchemaVersion &&
          firstLoad.metadata.slot == 0U &&
          firstLoad.metadata.hardware == 0x80U &&
          firstLoad.metadata.emulatedFrameNumber == 42U &&
          firstLoad.metadata.timestamp == fixedTimestamp &&
          firstLoad.metadata.payloadBytes == firstPayload.size() &&
          firstLoad.metadata.name.empty() &&
          firstLoad.metadata.thumbnailPng.empty(),
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
  const genplusgx::SaveStatePresentation presentation{
    .name = "Before the final boss",
    .thumbnailPng = fakePng(),
  };
  if (!check(manager.saveSlot(
        firstIdentity, 0U, 0x80U, 99U, replacement, presentation, fixedTimestamp),
        "Save-state slot replacement failed")) {
    return 5;
  }
  const auto replacementLoad = manager.loadSlot(firstIdentity, 0U, 0x80U);
  if (!check(replacementLoad.status && replacementLoad.rawPayload == replacement &&
          replacementLoad.metadata.emulatedFrameNumber == 99U &&
          replacementLoad.metadata.name == presentation.name &&
          replacementLoad.metadata.thumbnailPng == presentation.thumbnailPng,
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

  const auto exportedState = root / "manual-export.gpgxstate";
  if (!check(manager.exportSlot(
        firstIdentity, 0U, 0x80U, exportedState),
        "Validated state export failed") ||
      !check(manager.importSlot(
        exportedState, firstIdentity, 2U, 0x80U),
        "Validated state import failed")) {
    return 18;
  }
  const auto imported = manager.loadSlot(firstIdentity, 2U, 0x80U);
  if (!check(imported.status && imported.rawPayload == replacement &&
        imported.metadata.slot == 2U &&
        imported.metadata.name == presentation.name &&
        imported.metadata.thumbnailPng == presentation.thumbnailPng,
      "Imported state did not preserve payload and presentation") ||
      !check(manager.renameSlot(
        firstIdentity, 2U, 0x80U, "After the final boss"),
        "State rename failed")) {
    return 19;
  }
  const auto renamed = manager.loadSlot(firstIdentity, 2U, 0x80U);
  const auto protectedPayload = fakeRawState(0x44U);
  if (!check(renamed.status && renamed.metadata.name == "After the final boss" &&
        renamed.metadata.thumbnailPng == presentation.thumbnailPng &&
        renamed.metadata.timestamp == fixedTimestamp,
      "State rename did not preserve thumbnail/timestamp") ||
      !check(manager.saveSlot(
        secondIdentity, 2U, 0x80U, 5U, protectedPayload, fixedTimestamp),
        "Could not create protected destination state") ||
      !check(manager.importSlot(
        exportedState, secondIdentity, 2U, 0x80U).error ==
          genplusgx::SaveStateError::wrongGame,
        "Wrong-game manual state import was accepted") ||
      !check(manager.loadSlot(secondIdentity, 2U, 0x80U).rawPayload ==
          protectedPayload,
        "Rejected wrong-game import changed its destination slot") ||
      !check(manager.exportSlot(firstIdentity, 9U, 0x80U,
          root / "missing.gpgxstate").error ==
          genplusgx::SaveStateError::missingState,
        "Missing slot export did not return a typed error")) {
    return 20;
  }

  auto invalidPresentation = presentation;
  invalidPresentation.name.assign(
    genplusgx::SaveStateManager::maximumDisplayNameBytes + 1U, 'x');
  if (!check(manager.saveSlot(firstIdentity, 1U, 0x80U, 0U, replacement,
          invalidPresentation).error == genplusgx::SaveStateError::invalidPayload,
        "Oversized state name was accepted")) {
    return 21;
  }
  invalidPresentation = presentation;
  invalidPresentation.thumbnailPng = {1U, 2U, 3U};
  if (!check(manager.saveSlot(firstIdentity, 1U, 0x80U, 0U, replacement,
          invalidPresentation).error == genplusgx::SaveStateError::invalidPayload,
        "Non-PNG state thumbnail was accepted")) {
    return 22;
  }
  invalidPresentation = presentation;
  invalidPresentation.name = std::string{"\xC3\x28", 2U};
  if (!check(manager.saveSlot(firstIdentity, 1U, 0x80U, 0U, replacement,
          invalidPresentation).error == genplusgx::SaveStateError::invalidPayload,
        "Invalid UTF-8 state name was accepted")) {
    return 28;
  }
  invalidPresentation = presentation;
  invalidPresentation.thumbnailPng = {
    0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU, 0U, 0U, 0U, 0U};
  const auto oversizedThumbnail = oversizedPng();
  if (!check(manager.saveSlot(firstIdentity, 1U, 0x80U, 0U, replacement,
          invalidPresentation).error == genplusgx::SaveStateError::invalidPayload,
        "Corrupt PNG state thumbnail was accepted") ||
      !check(!oversizedThumbnail.empty(),
        "Could not create oversized PNG fixture") ||
      !check([&] {
          invalidPresentation.thumbnailPng = oversizedThumbnail;
          return manager.saveSlot(firstIdentity, 1U, 0x80U, 0U, replacement,
            invalidPresentation).error == genplusgx::SaveStateError::invalidPayload;
        }(),
        "Oversized-dimension state thumbnail was accepted") ||
      !check(manager.renameSlot(firstIdentity, 2U, 0x80U,
          std::string{"\xC3\x28", 2U}).error ==
          genplusgx::SaveStateError::invalidPayload,
        "Invalid UTF-8 state rename was accepted") ||
      !check(manager.exportSlot(firstIdentity, 2U, 0x80U,
          "relative.gpgxstate").error == genplusgx::SaveStateError::ioError,
        "Relative state export destination was accepted")) {
    return 29;
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
    stateFile, genplusgx::SaveStateManager::maximumFileBytes);
  if (!check(encoded.status && encoded.exists, "Could not read state for corruption test")) {
    return 9;
  }
  const std::array<std::size_t, 11> mutationOffsets{
    0U, 8U, 12U, 24U, 40U, 48U, 80U, 112U, 128U, 136U, 184U};
  for (std::size_t index = 0U; index < mutationOffsets.size(); ++index) {
    auto mutation = encoded.data;
    mutation[mutationOffsets[index]] ^= 0x01U;
    const auto mutationPath = root /
      ("state-mutation-" + std::to_string(index) + ".gpgxstate");
    if (!check(genplusgx::writeFileAtomically(mutationPath, mutation,
          genplusgx::SaveStateManager::maximumFileBytes),
          "Could not write bounded state mutation") ||
        !check(!manager.loadStateFile(
          mutationPath, firstIdentity, 0x80U).status,
          "Corrupt state mutation was accepted")) {
      return 30;
    }
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

  if (!check(manager.saveSlot(firstIdentity, 0U, 0x80U, 99U, replacement,
        presentation, fixedTimestamp),
        "Could not restore state before schema corruption")) {
    return 11;
  }
  encoded = genplusgx::readFileBounded(stateFile, encoded.data.size());
  encoded.data[8] = 99U;
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

  if (!check(manager.saveSlot(firstIdentity, 0U, 0x80U, 99U, replacement,
        presentation, fixedTimestamp),
        "Could not restore state before presentation corruption")) {
    return 23;
  }
  encoded = genplusgx::readFileBounded(
    stateFile, genplusgx::SaveStateManager::maximumFileBytes);
  encoded.data[176U + 8U] ^= 0x01U;
  if (!check(genplusgx::writeFileAtomically(
          stateFile, encoded.data, genplusgx::SaveStateManager::maximumFileBytes),
        "Could not write presentation-corruption fixture") ||
      !check(manager.loadSlot(firstIdentity, 0U, 0x80U).status.error ==
          genplusgx::SaveStateError::checksumMismatch,
        "Corrupt presentation checksum was not rejected")) {
    return 24;
  }

  if (!check(manager.saveSlot(firstIdentity, 0U, 0x80U, 99U, replacement,
        presentation, fixedTimestamp),
        "Could not restore state before legacy-schema test")) {
    return 25;
  }
  encoded = genplusgx::readFileBounded(
    stateFile, genplusgx::SaveStateManager::maximumFileBytes);
  const auto presentationBytes =
    8U + presentation.name.size() + presentation.thumbnailPng.size();
  std::vector<std::uint8_t> legacy{
    encoded.data.begin(), encoded.data.begin() + 128};
  legacy[8] = static_cast<std::uint8_t>(
    genplusgx::SaveStateManager::legacySchemaVersion);
  legacy[9] = 0U;
  legacy[10] = 0U;
  legacy[11] = 0U;
  legacy[12] = 128U;
  legacy[13] = 0U;
  legacy[14] = 0U;
  legacy[15] = 0U;
  legacy.insert(legacy.end(),
    encoded.data.begin() + static_cast<std::ptrdiff_t>(176U + presentationBytes),
    encoded.data.end());
  const auto legacyPath = root / "legacy-schema-1.gpgxstate";
  if (!check(genplusgx::writeFileAtomically(
        legacyPath, legacy, genplusgx::SaveStateManager::maximumFileBytes),
        "Could not write legacy state fixture")) {
    return 26;
  }
  const auto legacyLoad = manager.loadStateFile(
    legacyPath, firstIdentity, 0x80U);
  if (!check(legacyLoad.status && legacyLoad.rawPayload == replacement &&
        legacyLoad.metadata.schemaVersion ==
          genplusgx::SaveStateManager::legacySchemaVersion &&
        legacyLoad.metadata.name.empty() &&
        legacyLoad.metadata.thumbnailPng.empty(),
      "Schema-1 state did not remain backward compatible")) {
    return 27;
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
