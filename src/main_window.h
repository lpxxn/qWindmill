#pragma once

#include "models.h"

#include <QMainWindow>
#include <QPointer>

class CollectorWorker;
class QCloseEvent;
class QThread;

namespace Ui { class MainWindow; }

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void connectUi();
    void startQuery();
    void cancelQuery();
    void showReadings(const QList<DeviceReading> &readings);
    void appendLog(const QString &line);
    CollectorConfig configFromUi() const;

    Ui::MainWindow *ui;
    QPointer<QThread> thread_;
    QPointer<CollectorWorker> worker_;
};
