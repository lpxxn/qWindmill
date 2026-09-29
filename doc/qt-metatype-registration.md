# Qt 元类型注册：`qRegisterMetaType` 与 `Q_DECLARE_METATYPE`

本文说明 `src/main.cpp` 中这一行代码的作用、原理，以及它在项目中的使用位置：

```cpp
qRegisterMetaType<QList<DeviceReading>>("QList<DeviceReading>");
```

## 1. 它是做什么的

这行代码在运行时向 Qt 的元类型系统登记 `QList<DeviceReading>` 类型。登记之后，这个类型才能作为信号参数**跨线程传递**。

在本项目中，后台采集线程（`CollectorWorker`）把设备读数通过信号发给 GUI 线程（`MainWindow`）显示，依赖的就是这次注册。

## 2. 为什么跨线程需要注册

Qt 信号槽的连接方式取决于发送方和接收方所在的线程：

| 情况 | 连接方式 | 参数处理 |
| --- | --- | --- |
| 同一线程 | `Qt::DirectConnection` | 直接调用槽函数，参数按引用传递，不需要注册 |
| 不同线程 | `Qt::QueuedConnection` | 参数被拷贝后放入接收线程的事件队列，需要注册 |

排队连接的流程：

1. 发信号时，Qt 把参数**拷贝**一份，打包成事件，投递到接收对象所在线程的事件队列；
2. 接收线程的事件循环取出事件，调用槽函数，然后销毁这份拷贝。

Qt 需要通过元类型系统拿到该类型的构造、拷贝和析构方法才能完成这些操作。未注册时编译不会报错，但运行时参数无法传递，控制台会输出警告，**槽函数不会被调用**：

```
QObject::connect: Cannot queue arguments of type 'QList<DeviceReading>'
(Make sure 'QList<DeviceReading>' is registered using qRegisterMetaType().)
```

`QString`、`int` 等 Qt 内置类型已经注册过，所以 `logLine(QString)`、`failed(QString)` 这些信号跨线程时不需要额外处理。只有自定义类型及包含它的容器需要注册。

## 3. 项目中的使用位置

### 3.1 类型声明：`src/models.h`

```cpp
struct DeviceReading { ... };

Q_DECLARE_METATYPE(QList<DeviceReading>)
```

### 3.2 注册：`src/main.cpp`

```cpp
qRegisterMetaType<QList<DeviceReading>>("QList<DeviceReading>");
MainWindow window;
```

注册要在第一次发出信号之前完成，所以放在 `main()` 里、创建窗口之前。

### 3.3 信号声明：`src/collector_worker.h`

```cpp
signals:
    void resultReady(const QList<DeviceReading> &readings);
```

### 3.4 数据产生：`src/collector_worker.cpp`

`CollectorWorker::run()` 在工作线程中通过 TCP 读取寄存器，调用 `decodeBlock()`（`src/protocol.cpp`）解码为 `DeviceReading`，汇总成 `QList<DeviceReading>` 后 `emit resultReady(...)`。

### 3.5 跨线程连接：`src/main_window.cpp`

```cpp
auto *thread = new QThread(this);
auto *worker = new CollectorWorker(config, *plan);
worker->moveToThread(thread);
connect(thread, &QThread::started, worker, &CollectorWorker::run);
connect(worker, &CollectorWorker::resultReady, this, &MainWindow::showReadings);
```

`worker` 通过 `moveToThread` 移到工作线程，`MainWindow` 在 GUI 线程，因此这条连接自动变成排队连接。

### 3.6 接收显示：`MainWindow::showReadings`

在 GUI 线程执行，把读数填入 `QTreeWidget`。控件只能在 GUI 线程操作，所以“工作线程采集并发信号、GUI 线程更新界面”的设计离不开排队连接，也就离不开元类型注册。

## 4. `Q_DECLARE_METATYPE` 与 `qRegisterMetaType` 的区别

| | `Q_DECLARE_METATYPE(T)` | `qRegisterMetaType<T>()` |
| --- | --- | --- |
| 生效时机 | 编译期 | 运行期 |
| 参数 | C++ 类型（不是字符串） | 模板参数是类型，可选一个字符串名字 |
| 作用 | 特化 `QMetaTypeId<T>`，让 `QVariant`、`qMetaTypeId<T>()` 等可用；并用 `#T` 把类型文字转成字符串，作为类型的**正式名字** | 在运行时把类型真正登记到元类型系统；如果传了字符串，同时登记这个名字 |
| 位置 | 头文件、全局作用域 | 通常在 `main()` 或初始化代码中 |

本项目中，`Q_DECLARE_METATYPE(QList<DeviceReading>)` 确定的正式名字是 `"QList<DeviceReading>"`。

### 宏参数中含逗号的问题

`Q_DECLARE_METATYPE` 是宏，类型中若有逗号（如 `QMap<int, QString>`）会被拆成两个宏参数导致编译错误。需要先定义别名：

```cpp
using ReadingMap = QMap<int, QString>;
Q_DECLARE_METATYPE(ReadingMap)
```

此时正式名字是 `"ReadingMap"`。

## 5. 字符串参数是否必须与类型一致

**不必须。** `qRegisterMetaType` 的字符串参数是可选的：

| 写法 | 效果 |
| --- | --- |
| `qRegisterMetaType<QList<DeviceReading>>()` | 使用 `Q_DECLARE_METATYPE` 给出的正式名字，**推荐** |
| `qRegisterMetaType<QList<DeviceReading>>("QList<DeviceReading>")` | 与正式名字相同，效果等同于不传（本项目当前写法） |
| `qRegisterMetaType<QList<DeviceReading>>("ReadingList")` | 登记为**别名**（类似 typedef），两个名字都指向同一个类型 ID |

需要注意：

- 字符串要与**信号/槽签名中实际写出的类型名**一致，而不一定与 C++ 真实类型名一致；
- 名字必须是规范化形式：不带多余空格，不带 `const` 和 `&`。Qt 6 的 debug 版本对不规范的名字会触发断言。

### 名字什么时候才重要

只有**按名字查找类型**的场景才会用到这个字符串：

- 老式字符串连接：`connect(obj, SIGNAL(...), obj2, SLOT(...))`；
- `QMetaObject::invokeMethod` 按方法名调用、`Q_ARG`；
- `QMetaType::fromName("...")`；
- QML、属性系统等通过名字识别类型的地方。

使用函数指针写法 `connect(worker, &CollectorWorker::resultReady, ...)` 时，Qt 在编译期直接拿到类型 ID，不经过名字查找，字符串写成什么都不影响结果。

> 补充：Qt 6 使用函数指针写法连接时，多数情况下会自动注册参数类型。显式注册仍是常见的稳妥做法，在 Qt 5 下则是必需的。

## 6. 别名的使用示例

### 6.1 定义并注册

```cpp
// models.h
using ReadingList = QList<DeviceReading>;
Q_DECLARE_METATYPE(QList<DeviceReading>)

// main.cpp
qRegisterMetaType<QList<DeviceReading>>("ReadingList");
```

### 6.2 信号和槽签名使用 typedef

```cpp
// collector_worker.h
signals:
    void resultReady(const ReadingList &readings);

// main_window.h
public slots:
    void showReadings(const ReadingList &readings);
```

moc 按源码中**字面写出的文字**记录参数类型名，所以记下的是 `"ReadingList"`。

### 6.3 老式字符串连接

```cpp
connect(worker, SIGNAL(resultReady(ReadingList)),
        this,   SLOT(showReadings(ReadingList)));
```

跨线程排队时，Qt 用 `"ReadingList"` 查找类型来拷贝参数。**如果没有注册这个别名，会报 `Cannot queue arguments of type 'ReadingList'`**，这是别名的典型用途。

### 6.4 按名字调用与查找

```cpp
// 按方法名跨线程调用
QMetaObject::invokeMethod(window, "showReadings", Qt::QueuedConnection,
                          Q_ARG(ReadingList, readings));

// 按名字查类型（Qt 6）
QMetaType t = QMetaType::fromName("ReadingList");
qDebug() << t.id() << (t == QMetaType::fromType<QList<DeviceReading>>()); // true
```

`Q_ARG(ReadingList, ...)` 同样把 `ReadingList` 转成字符串去匹配方法签名，因此也依赖别名注册。

## 7. 对本项目的建议

项目全部使用函数指针写法连接信号槽，不需要别名。`main.cpp` 中的注册可以简化为：

```cpp
qRegisterMetaType<QList<DeviceReading>>();
```

效果与当前写法相同，也避免了手写名字出错。只有将来改用 `SIGNAL/SLOT` 宏、`invokeMethod` 按名字调用，或在 QML 中使用该类型时，才需要考虑注册别名。
