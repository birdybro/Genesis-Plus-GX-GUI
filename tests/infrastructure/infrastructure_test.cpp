#include "genplusgx/version.h"

#include <iostream>
#include <string_view>

int main()
{
  constexpr std::string_view version{GENPLUSGX_VERSION};
  constexpr std::string_view applicationName{GENPLUSGX_APP_NAME};
  constexpr std::string_view applicationId{GENPLUSGX_APP_ID};
  constexpr std::string_view commit{GENPLUSGX_GIT_COMMIT};

  if (version.empty() || applicationName.empty() || applicationId.empty() || commit.empty()) {
    std::cerr << "Configured build metadata contains an empty value\n";
    return 1;
  }

  if (!applicationId.starts_with("org.")) {
    std::cerr << "Application identifier is not a reverse-domain name\n";
    return 2;
  }

  static_assert(__cplusplus >= 202002L,
    "The test target must be compiled as C++20 or newer");

  std::cout << applicationName << ' ' << version << " (" << commit << ")\n";
  return 0;
}
