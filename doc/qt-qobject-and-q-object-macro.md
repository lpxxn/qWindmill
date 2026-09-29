# QObject 继承与 Q_OBJECT 宏

## 结论

- **需要 Qt 对象模型能力时继承 `QObject`**：父子对象树（自动析构）、信号槽、事件、线程亲和性（`moveToThread`）、`QPointer`、动态属性等。
- **类本身声明了信号、槽、`Q_PROPERTY`、`Q_INVOKABLE`、`Q_ENUM`，或者需要准确的运行时类型信息（`qobject_cast`、`metaObject()->className()`、`tr()` 翻译上下文）时，加 `Q_OBJECT`**。
- 两者不一定同时出现：
  - 有 `Q_OBJECT` 就必须（直接或间接）继承 `QObject`。
  - 继承 `QObject` 而不写 `Q_OBJECT` 语法上合法，但会少掉一部分功能。Qt 官方建议所有 `QObject` 子类都写上 `Q_OBJECT`。

## 1. 继承 QObject 带来什么

`QObject` 是 Qt 对象模型的基类，继承后可以使用：

| 能力 | 说明 |
|---|---|
| 对象树 | `new Foo(parent)`，父对象析构时自动 delete 子对象 |
| 信号槽连接 | `connect()` 的发送者/接收者必须是 `QObject` |
| 事件系统 | `event()`、`eventFilter()`、`timerEvent()`、`deleteLater()` |
| 线程亲和性 | `moveToThread()`，槽在对象所在线程执行 |
| 弱引用 | `QPointer<T>`，对象销毁后自动变为空指针 |
| 动态属性 | `setProperty()` / `property()` |

代价：`QObject` **不可拷贝**（拷贝构造和赋值被禁用），对象有一定体积，通常在堆上创建、通过指针使用。因此值类型、数据结构（如 `QueryPlan`、协议报文结构体）**不应该**继承 `QObject`。

## 2. Q_OBJECT 宏做了什么

`Q_OBJECT` 在类中声明了以下成员：

```cpp
static const QMetaObject staticMetaObject;
virtual const QMetaObject *metaObject() const;
virtual void *qt_metacast(const char *);
virtual int qt_metacall(QMetaObject::Call, int, void **);
static QString tr(const char *s, ...);
// ...
```

这些成员的**实现**由 **moc**（Meta-Object Compiler）扫描头文件后生成，放在 `moc_xxx.cpp` 中。moc 只处理带 `Q_OBJECT`（或 `Q_GADGET`）的类，生成内容包括：

- 信号的函数体（`signals:` 下只写声明，函数体由 moc 生成）；
- 按名字/索引调用槽和 `Q_INVOKABLE` 方法的分发表；
- 属性、枚举的元信息；
- 该类自己的 `QMetaObject`（类名、父类链等）。

## 3. 必须写 Q_OBJECT 的情况

1. 类中声明了 **`signals:`**，否则信号没有实现，链接报错。
2. 使用 **`Q_PROPERTY` / `Q_INVOKABLE` / `Q_ENUM` / `Q_CLASSINFO`**。
3. 使用**旧式字符串连接** `connect(a, SIGNAL(x()), b, SLOT(y()))`，或 `QMetaObject::invokeMethod(obj, "name")` 按名字调用。
4. 对该类使用 **`qobject_cast<Foo*>`**。没有 `Q_OBJECT` 时 Foo 没有自己的元对象，转换结果不正确（编译器可能报错）。
5. 需要暴露给 **QML**，或需要正确的 `tr()` 翻译上下文。

## 4. 继承 QObject 但不写 Q_OBJECT

```cpp
class Helper : public QObject {   // 没有 Q_OBJECT
public:
    void onData(int);             // 普通成员函数
};
connect(src, &Source::data, helper, &Helper::onData);  // 新式函数指针连接：可以
```

**仍然可用**：对象树、事件、`moveToThread`、作为新式函数指针 `connect` 的接收者（包括连接 lambda）。

**不可用或会出错**：定义自己的信号、`SLOT()` 字符串连接、`invokeMethod` 按名字调用、`qobject_cast<Helper*>`；`metaObject()->className()` 返回的是父类名。

这种写法只适合“纯接收者、只需要对象树或线程亲和性”的简单辅助类。实际项目中建议**一律写上 `Q_OBJECT`**，成本几乎为零，还能避免很多问题。

## 5. 不继承 QObject 也要元信息：Q_GADGET

值类型需要 `Q_PROPERTY`、`Q_ENUM`、`Q_INVOKABLE`，但不需要信号槽、也不想失去可拷贝性时，使用 `Q_GADGET`：

```cpp
struct Point {
    Q_GADGET
    Q_PROPERTY(int x MEMBER x)
public:
    int x = 0;
};
```

只需要注册枚举时，可以用命名空间配合 `Q_NAMESPACE` + `Q_ENUM_NS`。

注意区分 `Q_DECLARE_METATYPE` / `qRegisterMetaType`：它们用于让**某个类型能作为信号槽参数跨线程排队传递**，与 `Q_OBJECT` 解决的不是同一个问题。详见 [qt-metatype-registration.md](qt-metatype-registration.md)。

## 6. 本项目示例：CollectorWorker

```cpp
class CollectorWorker final : public QObject {
    Q_OBJECT
    // ...
};
```

`CollectorWorker` 是 worker：需要 `moveToThread` 到工作线程（依赖 `QObject`），并通过信号把结果发回 UI 线程（依赖 `Q_OBJECT`），因此两者都必须有。

## 7. 常见坑

- **添加或删除 `Q_OBJECT` 后出现 `undefined reference to vtable` 或 `unresolved external ...metaObject`**：moc 没有重新运行。重新运行 CMake（确认开启 `CMAKE_AUTOMOC ON`），或清理后重新构建。
- `Q_OBJECT` 类一般放在 **`.h`** 中。如果放在 `.cpp` 中，需要在文件末尾加 `#include "xxx.moc"`。
- 多重继承时，`QObject` 必须是**第一个**基类，且只能有一个 `QObject` 基类。
- 类模板中不能使用 `Q_OBJECT`，moc 不支持模板。
