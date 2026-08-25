#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace genplusgx::test {

[[nodiscard]] std::vector<std::uint8_t> makeGenesisRamMarkerRom();
[[nodiscard]] std::vector<std::uint8_t> makeGenesisSramWriterRom();

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
