#include "main_window.h"

#include "collector_worker.h"
#include "query_plan.h"
#include "ui_main_window.h" // AUTOUIC 根据 main_window.ui 自动生成；不要手工编辑生成文件。

#include <QCloseEvent>
#include <QHeaderView>
#include <QThread>
#include <QTreeWidgetItem>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this); // .ui 负责布局和控件的初始值；行为在 connectUi() 中连接。
    ui->resultsTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->resultsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->resultsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->resultsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->resultsTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    connectUi();
}

MainWindow::~MainWindow()
{
    // 主窗口被关闭时，先请求工作线程停止再等待退出。
    // QPointer 会在 QObject 被销毁后自动置空，避免悬空指针。
    if (worker_)
        worker_->requestCancel();
    if (thread_)
        thread_->wait();
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (worker_)
        worker_->requestCancel();
    QMainWindow::closeEvent(event);
}

void MainWindow::connectUi()
{
    connect(ui->queryButton, &QPushButton::clicked, this, &MainWindow::startQuery);
    connect(ui->cancelButton, &QPushButton::clicked, this, &MainWindow::cancelQuery);
    connect(ui->addressesEdit, &QLineEdit::returnPressed, this, &MainWindow::startQuery);
}

CollectorConfig MainWindow::configFromUi() const
{
    CollectorConfig config;
    config.host = ui->hostEdit->text().trimmed();
    config.port = quint16(ui->portSpin->value());
    config.timeoutMs = ui->timeoutSpin->value();
    config.requestGapMs = ui->gapSpin->value();
    config.retries = ui->retriesSpin->value();
    config.retryDelayMs = ui->retryDelaySpin->value();
    return config;
}

void MainWindow::startQuery()
{
    if (thread_) // 同一时间只有一个采集任务，避免网关被并发请求冲击。
        return;
    const CollectorConfig config = configFromUi();
    if (config.host.isEmpty()) {
        ui->statusLabel->setText(QStringLiteral("请输入 TCP 服务地址。"));
        return;
    }
    QString error;
    const auto plan = makeQueryPlan(ui->addressesEdit->text(), &error);
    if (!plan) {
        ui->statusLabel->setText(error);
        return;
    }

    ui->resultsTree->clear();
    ui->logEdit->clear();
    ui->queryButton->setEnabled(false);
    ui->cancelButton->setEnabled(true);
    ui->statusLabel->setText(QStringLiteral("正在读取 %1 台设备，涉及 %2 个批次……")
                                 .arg(plan->requestedAddresses.size()).arg(plan->blocks.size()));

    // QObject 的线程归属由 moveToThread 决定。run() 在工作线程内执行，
    // QTcpSocket 也在该线程内创建；工作线程只发送信号，不直接修改任何控件。
    auto *thread = new QThread(this);
    auto *worker = new CollectorWorker(config, *plan);
    thread_ = thread;
    worker_ = worker;
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &CollectorWorker::run);
    connect(worker, &CollectorWorker::logLine, this, &MainWindow::appendLog);
    connect(worker, &CollectorWorker::resultReady, this, &MainWindow::showReadings);
    connect(worker, &CollectorWorker::failed, this, [this](const QString &message) {
        appendLog(QStringLiteral("查询失败：%1").arg(message));
        ui->statusLabel->setText(message);
        ui->tabs->setCurrentWidget(ui->logsTab);
    });
    // 这里使用 DirectConnection：即使主窗口析构时 GUI 线程正等待线程结束，
    // 工作线程也能直接调用 thread->quit()，不会因为排队到 GUI 线程而死锁。
    connect(worker, &CollectorWorker::finished, thread, &QThread::quit, Qt::DirectConnection);
    connect(worker, &CollectorWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this] {
        worker_ = nullptr;
        thread_ = nullptr;
        ui->queryButton->setEnabled(true);
        ui->cancelButton->setEnabled(false);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::cancelQuery()
{
    if (worker_) {
        worker_->requestCancel(); // 原子标记；工作线程轮询时会停止。
        ui->statusLabel->setText(QStringLiteral("正在取消……"));
    }
}

void MainWindow::appendLog(const QString &line)
{
    ui->logEdit->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz  ")) + line);
}

void MainWindow::showReadings(const QList<DeviceReading> &readings)
{
    // QTreeWidgetItem 默认归父项所有；父项再归 QTreeWidget 所有。
    // 无需手动 delete 子项，clear() 会负责释放它们。
    for (const auto &reading : readings) {
        auto *deviceItem = new QTreeWidgetItem(ui->resultsTree);
        deviceItem->setText(0, QString::number(reading.device.address));
        deviceItem->setText(1, reading.device.name);
        deviceItem->setText(2, QStringLiteral("起点 0x%1")
                                   .arg(reading.device.startAddress, 4, 16, QChar('0')));
        deviceItem->setText(3, reading.collectedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        for (const auto &measurement : reading.measurements) {
            auto *item = new QTreeWidgetItem(deviceItem);
            item->setText(1, measurement.label);
            item->setText(2, measurement.key);
            item->setText(3, measurement.value.toString());
            item->setText(4, measurement.unit);
        }
        deviceItem->setExpanded(true);
    }
    ui->tabs->setCurrentWidget(ui->resultsTab);
    ui->statusLabel->setText(QStringLiteral("查询完成：%1 台设备，每台 %2 项测量数据。")
                                 .arg(readings.size())
                                 .arg(readings.isEmpty() ? 0 : readings.first().measurements.size()));
}
