#pragma once

#include <QObject>

#include <functional>
#include <memory>
#include <string>

namespace genplusgx::achievements {

struct CredentialResult final {
  bool succeeded{false};
  bool found{false};
  std::string token;
  std::string detail;
};

using CredentialCallback = std::function<void(CredentialResult)>;

class CredentialStore final : public QObject {
  Q_OBJECT

public:
  explicit CredentialStore(QObject* parent = nullptr);
  ~CredentialStore() override;

  void readToken(const std::string& username, CredentialCallback callback);
  void writeToken(
    const std::string& username,
    std::string token,
    CredentialCallback callback);
  void deleteToken(const std::string& username, CredentialCallback callback);

  [[nodiscard]] static std::string keyForUsername(const std::string& username);

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::achievements
