#include "genplusgx/settings/run_ahead_settings.h"

#include "genplusgx/persistence.h"

#include <QTemporaryDir>

#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main()
{
  using namespace genplusgx;
  using namespace genplusgx::settings;

  QTemporaryDir directory;
  if (!check(directory.isValid(),
        "Could not create a run-ahead settings directory")) {
    return EXIT_FAILURE;
  }
  const auto path = std::filesystem::path{directory.path().toStdString()} /
    "nested" / "run-ahead-settings.json";
  RunAheadSettingsStore store{path};
  const auto missing = store.load();
  const RunAheadConfiguration customized{.enabled = true, .frames = 4U};
  if (!check(missing.status &&
          missing.settings == defaultRunAheadSettings() &&
          !missing.settings.enabled && missing.settings.frames == 1U,
        "Missing run-ahead settings did not use safe disabled defaults") ||
      !check(store.save(customized),
        "Valid run-ahead settings could not be saved")) {
    return EXIT_FAILURE;
  }
  const auto loaded = store.load();
  if (!check(loaded.status && loaded.settings == customized,
        "Run-ahead settings did not round-trip exactly") ||
      !check(!store.save({.enabled = true, .frames = 0U}),
        "A zero-frame run-ahead configuration was persisted") ||
      !check(!store.save({
          .enabled = true,
          .frames = maximumRunAheadFrames + 1U,
        }), "An over-limit run-ahead configuration was persisted") ||
      !check(runAheadSupportedForHardware(0x80U) &&
          runAheadSupportedForHardware(0x40U) &&
          !runAheadSupportedForHardware(0x82U) &&
          !runAheadSupportedForHardware(0x84U) &&
          !runAheadSupportedForHardware(0xffU),
        "The hardware safety gate accepted an unsupported system")) {
    return EXIT_FAILURE;
  }

  CoreInputSettings standardInput;
  CoreInputSettings specializedInput;
  specializedInput.devices.fill(CoreInputDevice::none);
  specializedInput.devices[0] = CoreInputDevice::segaMouse;
  if (!check(runAheadSupportedForInputSettings(standardInput) &&
          !runAheadSupportedForInputSettings(specializedInput),
        "The input safety gate did not isolate unsnapshotted devices")) {
    return EXIT_FAILURE;
  }

  RunAheadDeterminismGuard determinism;
  constexpr std::array<std::uint8_t, 3> firstState{1U, 2U, 3U};
  constexpr std::array<std::uint8_t, 3> changedState{1U, 2U, 4U};
  determinism.reset(true, false);
  if (!check(determinism.pending() && !determinism.verified() &&
          !determinism.faulted() && determinism.failures() == 0U,
        "A fresh determinism guard did not request verification") ||
      !check(determinism.verify(firstState, firstState) ==
          RunAheadVerificationResult::verified && determinism.verified() &&
          !determinism.pending() && !determinism.faulted(),
        "Equal run-ahead states did not verify") ||
      !check(determinism.verify(firstState, changedState) ==
          RunAheadVerificationResult::notPending &&
          determinism.failures() == 0U,
        "A completed guard repeated verification unexpectedly")) {
    return EXIT_FAILURE;
  }
  determinism.reset(true, true);
  if (!check(determinism.verify(firstState, changedState) ==
          RunAheadVerificationResult::mismatch && determinism.faulted() &&
          !determinism.verified() && determinism.failures() == 1U,
        "A state mismatch did not fail closed")) {
    return EXIT_FAILURE;
  }
  determinism.reset(false, true);
  if (!check(!determinism.pending() && !determinism.faulted() &&
          determinism.failures() == 1U,
        "A disabled reset did not preserve only diagnostic failures")) {
    return EXIT_FAILURE;
  }
  determinism.reset(true, false);
  if (!check(determinism.failures() == 0U && determinism.pending(),
        "A new lifecycle did not reset determinism counters")) {
    return EXIT_FAILURE;
  }

  constexpr std::array malformed{
    R"({"schemaVersion":999,"runAhead":{"enabled":true,"frames":1}})",
    R"({"schemaVersion":1,"runAhead":{"enabled":1,"frames":1}})",
    R"({"schemaVersion":1,"runAhead":{"enabled":true,"frames":1.5}})",
    R"({"schemaVersion":1,"runAhead":{"enabled":true,"frames":5}})",
    R"({"schemaVersion":1,"runAhead":{})",
    R"({"schemaVersion":1)",
  };
  for (const std::string_view data : malformed) {
    if (!check(writeFileAtomically(path,
          std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(data.data()), data.size()},
          RunAheadSettingsStore::maximumFileBytes),
          "Could not write a malformed run-ahead fixture")) {
      return EXIT_FAILURE;
    }
    const auto rejected = store.load();
    if (!check(!rejected.status &&
          rejected.status.error == PersistenceError::invalidData &&
          rejected.settings == defaultRunAheadSettings(),
        "Malformed run-ahead settings were not rejected safely")) {
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}
