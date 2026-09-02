#pragma once

#include "genplusgx/input_snapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace genplusgx::netplay {

inline constexpr std::uint16_t protocolVersion = 1U;
inline constexpr std::size_t authenticationTagBytes = 32U;
inline constexpr std::size_t nonceBytes = 32U;
inline constexpr std::uint16_t defaultPort = 55'455U;
inline constexpr std::uint32_t maximumInputDelayFrames = 8U;
inline constexpr std::uint32_t maximumRollbackFrames = 12U;

enum class NetplayRole : std::uint8_t {
  host,
  guest,
};

enum class NetplaySessionState {
  disconnected,
  listening,
  connecting,
  authenticating,
  connected,
};

enum class NetplaySessionError {
  none,
  invalidConfiguration,
  listenFailed,
  connectionFailed,
  timeout,
  authenticationFailed,
  incompatiblePeer,
  protocolViolation,
  transportFailure,
};

struct NetplaySessionDescriptor final {
  std::string gameSha256;
  std::string settingsSha256;
  std::string coreVersion;

  [[nodiscard]] bool valid() const noexcept;
  friend bool operator==(
    const NetplaySessionDescriptor&,
    const NetplaySessionDescriptor&) = default;
};

struct NetplayConfiguration final {
  NetplayRole role{NetplayRole::host};
  std::size_t localPlayer{0U};
  std::size_t remotePlayer{1U};
  std::uint32_t inputDelayFrames{2U};
  std::uint32_t rollbackFrames{8U};

  [[nodiscard]] bool valid() const noexcept;
};

struct NetplayInputFrame final {
  std::uint64_t frameNumber{0U};
  InputDeviceState state;

  friend bool operator==(const NetplayInputFrame&, const NetplayInputFrame&) =
    default;
};

enum class NetplayBridgeError {
  none,
  queueFull,
  invalidFrame,
};

struct NetplayBridgeStatus final {
  NetplayBridgeError error{NetplayBridgeError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == NetplayBridgeError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct NetplayBridgeMetrics final {
  std::size_t outgoingDepth{0U};
  std::size_t outgoingCapacity{0U};
  std::uint64_t submittedFrames{0U};
  std::uint64_t consumedFrames{0U};
  std::uint64_t rejectedFrames{0U};
};

} // namespace genplusgx::netplay
