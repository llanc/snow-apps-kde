#pragma once

#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>

class QWidget;
class QWindow;

namespace adqt::widgets::detail {
// Observe both Qt surface creation and native attachment. A QWindow and its WId
// can survive replacement of the NSWindow hosting its view.
class WindowSurfaceObserver final : public QObject {
 public:
  WindowSurfaceObserver(QWidget* widget, std::function<void()> changed, QObject* parent,
                        std::function<void()> retiring = {});
  WindowSurfaceObserver(QWindow* surface, std::function<void()> changed, QObject* parent,
                        std::function<void()> retiring = {});
  ~WindowSurfaceObserver() override;
  void synchronize();

 protected:
  bool eventFilter(QObject* object, QEvent* event) override;

 private:
  void observeSurface();
  QPointer<QWidget> widget_;
  QPointer<QWindow> surface_;
  std::function<void()> changed_;
  std::function<void()> retiring_;
  bool widgetSource_ = true;
#ifdef Q_OS_MACOS
  void* nativeObservation_ = nullptr;
  quintptr nativeView_ = 0;
#endif
  std::shared_ptr<bool> synchronizing_ = std::make_shared<bool>(false);
};
}  // namespace adqt::widgets::detail
