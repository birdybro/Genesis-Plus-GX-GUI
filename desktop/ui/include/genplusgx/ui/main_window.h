#pragma once

#include <QMainWindow>

class QAction;
class QLabel;
class QMenu;

namespace genplusgx::ui {

class MainWindow final : public QMainWindow {
public:
  explicit MainWindow(QWidget* parent = nullptr);

  void showAboutDialog();

private:
  QAction* addAction(
    QMenu& menu,
    const QString& text,
    const char* objectName,
    const QKeySequence& shortcut = {});
  void buildMenus();
  void buildStatusBar();
  void createCanvas();
  void setGameActionsEnabled(bool enabled);

  QLabel* gameStatus_{nullptr};
  QLabel* systemStatus_{nullptr};
  QLabel* regionStatus_{nullptr};
  QLabel* fpsStatus_{nullptr};
  QLabel* slotStatus_{nullptr};
};

} // namespace genplusgx::ui
