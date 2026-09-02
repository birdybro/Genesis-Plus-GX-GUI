#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace genplusgx::updates {

enum class Error : std::uint8_t {
  none,
  invalidRequest,
  invalidSettings,
  signatureInvalid,
  invalidManifest,
  rollbackDetected,
  unsupportedPlatform,
  network,
  redirectRejected,
  responseTooLarge,
  cancelled,
  queueFull,
  notRunning,
  io,
  hashMismatch,
  threadFailure,
};

struct Status final {
  Error error{Error::none};
  std::string message;
  [[nodiscard]] bool ok() const noexcept { return error == Error::none; }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
};

struct SemanticVersion final {
  std::uint32_t major{0U};
  std::uint32_t minor{0U};
  std::uint32_t patch{0U};
  [[nodiscard]] auto operator<=>(const SemanticVersion&) const = default;
  [[nodiscard]] std::string toString() const;
};

[[nodiscard]] std::optional<SemanticVersion> parseSemanticVersion(
  std::string_view text) noexcept;

struct Asset final {
  std::string platform;
  std::string architecture;
  std::string format;
  std::string fileName;
  std::string url;
  std::string sha256;
  std::uint64_t size{0U};
  [[nodiscard]] bool operator==(const Asset&) const = default;
};

struct Manifest final {
  std::uint32_t schemaVersion{0U};
  SemanticVersion version;
  std::string publishedAt;
  std::string releasePage;
  std::string keyId;
  std::vector<Asset> assets;
};

struct Trust final {
  std::string publicKeyHex;
  std::string keyId;
  std::string manifestUrl;
  std::string signatureUrl;
  std::string repositoryUrl;
  std::vector<std::string> allowedHosts;
  bool allowNonDefaultHttpsPorts{false};
};

[[nodiscard]] Trust productionTrust();
[[nodiscard]] std::string currentPlatform();
[[nodiscard]] std::string currentArchitecture();

struct CheckResult final {
  Status status;
  Manifest manifest;
  std::optional<Asset> asset;
  bool updateAvailable{false};
};

struct DownloadResult final {
  Status status;
  std::filesystem::path path;
  Asset asset;
};

} // namespace genplusgx::updates
