#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace genplusgx::test {

enum class SyntheticSegaCdRegion : std::uint8_t {
  usa,
  europe,
  japan,
};

[[nodiscard]] std::vector<std::uint8_t> makeGenesisRamMarkerRom();
[[nodiscard]] std::vector<std::uint8_t> makeGenesisSramWriterRom();
[[nodiscard]] std::vector<std::uint8_t> makeSegaCdBios();
[[nodiscard]] std::vector<std::uint8_t> makeSegaCdDiscImage(
  SyntheticSegaCdRegion region = SyntheticSegaCdRegion::usa);

class TemporaryFixture final {
public:
  TemporaryFixture(std::vector<std::uint8_t> bytes, std::string_view extension);
  ~TemporaryFixture();

  TemporaryFixture(const TemporaryFixture&) = delete;
  TemporaryFixture& operator=(const TemporaryFixture&) = delete;
  TemporaryFixture(TemporaryFixture&&) = delete;
  TemporaryFixture& operator=(TemporaryFixture&&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::test
