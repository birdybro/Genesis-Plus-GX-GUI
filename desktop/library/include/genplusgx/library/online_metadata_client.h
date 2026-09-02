#pragma once

#include "genplusgx/library/game_metadata.h"
#include "genplusgx/library/online_metadata_settings.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace genplusgx::library {

using OnlineMetadataCancellation = std::function<bool()>;

struct OnlineHttpResult final {
  OnlineMetadataStatus status;
  int statusCode{0};
  std::string contentType;
  std::vector<std::uint8_t> data;
};

class OnlineHttpTransport {
public:
  virtual ~OnlineHttpTransport() = default;
  [[nodiscard]] virtual OnlineHttpResult get(
    const std::string& url,
    std::size_t maximumBytes) = 0;
};

class QtOnlineHttpTransport final : public OnlineHttpTransport {
public:
  explicit QtOnlineHttpTransport(
    OnlineMetadataCancellation cancellation = {},
    std::vector<std::string> additionalTrustedCaDer = {});
  ~QtOnlineHttpTransport() override;

  QtOnlineHttpTransport(const QtOnlineHttpTransport&) = delete;
  QtOnlineHttpTransport& operator=(const QtOnlineHttpTransport&) = delete;

  [[nodiscard]] OnlineHttpResult get(
    const std::string& url,
    std::size_t maximumBytes) override;

private:
  class Private;
  std::unique_ptr<Private> private_;
};

struct OnlineMetadataLookupResult final {
  OnlineMetadataStatus status;
  OnlineMetadataRecord record;
  std::filesystem::path artworkPath;
  bool fromCache{false};
  bool staleCache{false};
};

[[nodiscard]] OnlineMetadataLookupResult lookupOnlineMetadata(
  const OnlineMetadataSettings& settings,
  const GameMetadata& game,
  const std::filesystem::path& cacheDirectory,
  OnlineHttpTransport& transport,
  const OnlineMetadataCancellation& cancellation = {});

[[nodiscard]] OnlineMetadataDecodeResult decodeLicensedManifestResponse(
  std::span<const std::uint8_t> data,
  const std::string& expectedSha256);
[[nodiscard]] OnlineMetadataDecodeResult decodeRetronianGameResponse(
  std::span<const std::uint8_t> data,
  const std::string& expectedSha256,
  const std::string& preferredLanguage,
  const std::string& preferredRegion);
[[nodiscard]] OnlineMetadataStatus identifyRetronianGame(
  std::span<const std::uint8_t> indexData,
  const std::string& sha256,
  std::string& gameId);

} // namespace genplusgx::library
