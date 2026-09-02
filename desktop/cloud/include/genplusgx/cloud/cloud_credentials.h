#pragma once

#include <QObject>

#include <functional>
#include <memory>
#include <string>

namespace genplusgx::cloud {

struct CredentialResult final {
  bool succeeded{false};
  bool found{false};
  std::string password;
  std::string detail;
};

using CredentialCallback = std::function<void(CredentialResult)>;

class CredentialStore final : public QObject {
  Q_OBJECT

public:
  explicit CredentialStore(QObject* parent = nullptr);
  ~CredentialStore() override;

  void readPassword(const std::string& endpoint, const std::string& username,
    CredentialCallback callback);
  void writePassword(const std::string& endpoint, const std::string& username,
    std::string password, CredentialCallback callback);
  void deletePassword(const std::string& endpoint, const std::string& username,
    CredentialCallback callback);

  [[nodiscard]] static std::string keyForAccount(
    const std::string& endpoint, const std::string& username);

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::cloud
