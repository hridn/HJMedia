// raii_demo.cpp
// 编译：cl /EHsc /std:c++17 /utf-8 /nologo raii_demo.cpp
// 目的：验证 HJMedia 中的 RAII 模式
//   ScopeGuard/HJOnceToken 作用域守卫、AUTO_LOCK 自动锁、异常安全

#include <windows.h>
#include <iostream>
#include <functional>
#include <mutex>
#include <thread>
#include <chrono>

// ==================== 模拟 HJOnceToken ====================
// 作用域守卫（ScopeGuard）：构造函数执行 onConstructed，析构函数执行 onDestructed
// C++ 保证：无论函数正常 return、break、continue、还是抛异常，
// 局部对象的析构函数一定会执行，这就是 RAII 的核心保证
//
// 在 HJMedia 中的典型用法：
//   HJOnceToken token(nullptr, [&] { setIsBusy(false); });
//   函数退出时自动清理 busy 状态
class ScopeGuard {
public:
    using Task = std::function<void(void)>;

    // 带构造回调的版本 —— 对应 HJOnceToken(onConstructed, onDestructed)
    template <typename FUNC>
    ScopeGuard(const FUNC& onConstructed, Task onDestructed = nullptr) {
        onConstructed();
        m_onDestructed = std::move(onDestructed);
        std::cout << "  [ScopeGuard-构造] 构造回调已执行" << std::endl;
    }

    // 只有析构回调的版本 —— 对应 HJOnceToken(nullptr, onDestructed)
    // 最常用的模式：只注册清理回调
    ScopeGuard(std::nullptr_t, Task onDestructed = nullptr) {
        m_onDestructed = std::move(onDestructed);
        std::cout << "  [ScopeGuard-注册] 已注册析构回调，等待作用域结束" << std::endl;
    }

    ~ScopeGuard() {
        if (m_onDestructed) {
            m_onDestructed();
            std::cout << "  [ScopeGuard-清理] 析构回调已执行" << std::endl;
        }
    }

    // 禁止拷贝和移动（同 HJOnceToken）
    ScopeGuard() = delete;
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    Task m_onDestructed;
};

// ==================== 模拟 HJ_AUTO_LOCK ====================
// 即 std::lock_guard<std::recursive_mutex>
// 优点：构造时 lock，析构时 unlock，不可能忘记释放锁
#define AUTO_LOCK(mtx) std::lock_guard<std::recursive_mutex> _lk_##__LINE__(mtx)

// ==================== 模拟 HJ_AUTOU_LOCK ====================
// 即 std::unique_lock<std::recursive_mutex>
// 比 lock_guard 更灵活，可以提前 unlock()，但仍然保证析构时释放
#define AUTOU_LOCK(mtx) std::unique_lock<std::recursive_mutex> _ulk_##__LINE__(mtx)

// ==================== 模拟 proRun() 模式 ====================
// 演示 HJMedia 中 proRun() 的 RAII 惯用法：
// 进入函数时设置 isBusy = true，退出时自动恢复 isBusy = false
class TaskRunner {
public:
    TaskRunner(const std::string& name) : m_name(name), m_isBusy(false) {}

    // 模拟 proRun() —— 核心演示
    int proRun() {
        std::cout << "\n  --- [proRun] " << m_name << " 开始执行 ---" << std::endl;

        // 关键行：ScopeGuard 确保退出时自动清理
        // 等价于 HJMedia 中的：
        //   HJOnceToken token(nullptr, [&] { setIsBusy(false); });
        std::cout << "  [proRun] 注册 ScopeGuard 析构回调（setIsBusy(false)）" << std::endl;
        ScopeGuard guard(nullptr, [this] {
            setIsBusy(false);
        });

        // 设置忙状态
        setIsBusy(true);
        std::cout << "  [proRun] 当前 isBusy=" << isBusy() << "（正在处理中）" << std::endl;

        // 【重点】以下多条退出路径都能正确清理：
        // 路径1：正常 return（当前激活）
        // 路径2：提前 return（取消注释下面这行验证）
        // 路径3：抛异常（取消注释下面这行验证）
        // if (true) return -1;              // 测试提前 return
        // throw std::runtime_error("错误"); // 测试异常时清理

        std::cout << "  [proRun] " << m_name << " 正常处理完毕" << std::endl;
        return 0;
        // 函数结束 -> guard 析构 -> onDestructed 执行 -> setIsBusy(false)
    }

    bool isBusy() const { return m_isBusy; }

private:
    void setIsBusy(bool busy) {
        m_isBusy = busy;
        std::cout << "  [setIsBusy] " << m_name << " isBusy 设为 " << (busy ? "true" : "false") << std::endl;
    }

    std::string m_name;
    bool m_isBusy;
};

// ==================== 演示 RAII 自动锁 ====================
// 多个线程同时操作时，锁自动管理，无需手动 unlock
class Counter {
public:
    Counter() : m_count(0) {}

    // ++ 操作，进入时自动 lock，离开时自动 unlock
    void increment() {
        // lock_guard：构造时 lock，函数结束时析构自动 unlock
        std::cout << "  [increment] 正在获取锁..." << std::endl;
        {
            AUTO_LOCK(m_mutex);
            std::cout << "  [increment] 已获得锁，执行 m_count++" << std::endl;
            m_count++;
            std::cout << "  [increment] count=" << m_count << std::endl;
        }  // 离开这个作用域时 lock_guard 自动 unlock
        std::cout << "  [increment] 锁已自动释放" << std::endl;
    }

    // -- 操作，同上
    void decrement() {
        AUTO_LOCK(m_mutex);
        m_count--;
        std::cout << "  [decrement] count=" << m_count << std::endl;
    }

    // unique_lock 比 lock_guard 灵活，可以提前 unlock
    void batchAdd(int n) {
        AUTOU_LOCK(m_mutex);
        std::cout << "  [batchAdd] 已获得锁（unique_lock），开始批量操作" << std::endl;
        for (int i = 0; i < n; ++i) {
            m_count++;
        }
        std::cout << "  [batchAdd] 批量完成，count=" << m_count << std::endl;
        // unique_lock 在这里也可以手动 unlock()，但不手动也会在析构时自动解锁
        std::cout << "  [batchAdd] 函数结束，unique_lock 自动 unlock" << std::endl;
    }

    int getCount() const { return m_count; }

private:
    int m_count;
    mutable std::recursive_mutex m_mutex;  // 可重入互斥锁，同一线程可多次 lock
};

// ==================== 演示异常安全 ====================
// 即使操作中途抛异常，资源也能被正确释放
class FileHandle {
public:
    FileHandle(const std::string& name) : m_name(name) {
        std::cout << "  [FileHandle] 打开文件: " << m_name << std::endl;
        m_isOpen = true;
    }

    ~FileHandle() {
        if (m_isOpen) {
            std::cout << "  [FileHandle] 【RAII自动关闭】关闭文件: " << m_name << std::endl;
            m_isOpen = false;
        }
    }

    // 禁止拷贝
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    // 支持移动（所有权转移）
    FileHandle(FileHandle&& other) noexcept
        : m_name(std::move(other.m_name)), m_isOpen(other.m_isOpen) {
        other.m_isOpen = false;
    }

    void write(const std::string& data) {
        if (m_isOpen) {
            std::cout << "  [write] 写入 " << m_name << ": " << data << std::endl;
        }
    }

private:
    std::string m_name;
    bool m_isOpen = false;
};

// 模拟一个会抛出异常的操作
void processWithException() {
    std::cout << "\n  --- [processWithException] 开始处理 ---" << std::endl;
    FileHandle file("test.txt");
    file.write("hello");

    // 模拟处理过程中发生异常
    std::cout << "  [process] 操作过程中出现错误，即将抛异常..." << std::endl;
    std::cout << "  [process] 注意：异常发生后 FileHandle 是否会自动关闭？" << std::endl;
    throw std::runtime_error("文件写入失败！");
    // 即使 throw，file 的析构函数仍然会被调用，自动关闭文件
    // 这就是 C++ RAII 的核心优势
}

// ==================== main ====================
int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "=============================================" << std::endl;
    std::cout << "    HJMedia RAII 模式验证 Demo" << std::endl;
    std::cout << "     对应：week1 第2天学习内容" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "验证内容：" << std::endl;
    std::cout << "  1. ScopeGuard 作用域守卫（对应 HJOnceToken）" << std::endl;
    std::cout << "  2. RAII 自动锁（lock_guard / unique_lock）" << std::endl;
    std::cout << "  3. 异常安全（即使抛异常，RAII 资源也自动释放）" << std::endl;
    std::cout << std::endl;

    // Demo 1：ScopeGuard 自动清理
    std::cout << "\n==================== Demo 1：ScopeGuard 作用域守卫 ====================" << std::endl;
    std::cout << "场景：proRun() 函数开始时设置 isBusy=true，结束时必须恢复 isBusy=false" << std::endl;
    std::cout << "问题：函数有多个 return 时容易忘记，或者中途抛异常导致状态没恢复" << std::endl;
    std::cout << "解决：ScopeGuard（对应 HJOnceToken）利用 RAII 在析构时自动清理" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;
    {
        TaskRunner runner("Task1");
        runner.proRun();
        std::cout << "\n  [main] 验证：proRun 结束后 isBusy=" << runner.isBusy() << "（已自动恢复为 false）" << std::endl;
        std::cout << "  [main] 不需要手动调用 setIsBusy(false)，RAII 自动完成了清理" << std::endl;
    }

    // Demo 2：Lock_guard 自动解锁
    std::cout << "\n==================== Demo 2：RAII 锁（lock_guard / unique_lock） ====================" << std::endl;
    std::cout << "场景：多线程环境需要保证同一时刻只有一个线程修改数据" << std::endl;
    std::cout << "问题：lock() 后忘记 unlock() 会导致死锁" << std::endl;
    std::cout << "解决：lock_guard 构造时 lock，析构时自动 unlock" << std::endl;
    std::cout << "---------------------------------------------------------------------------" << std::endl;
    {
        Counter counter;
        counter.increment();
        counter.increment();
        counter.decrement();

        std::cout << "\n  [main] batchAdd 使用 unique_lock，比 lock_guard 更灵活" << std::endl;
        counter.batchAdd(5);
    }

    // Demo 3：异常安全
    std::cout << "\n==================== Demo 3：异常安全 ====================" << std::endl;
    std::cout << "场景：函数执行到一半抛出异常，已经打开的资源需要被关闭" << std::endl;
    std::cout << "问题：try-catch 只能捕获异常，不会自动释放资源" << std::endl;
    std::cout << "解决：RAII 对象的析构函数在异常传播途中也会执行" << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    try {
        processWithException();
    } catch (const std::exception& e) {
        std::cout << "\n  [catch] 捕获到异常: " << e.what() << std::endl;
        std::cout << "  [catch] 尽管函数异常退出，FileHandle 已在析构时自动关闭文件" << std::endl;
        std::cout << "  [catch] 验证结果：RAII 保证资源不会被泄漏" << std::endl;
    }

    std::cout << "\n=============================================" << std::endl;
    std::cout << "    所有 Demo 执行完毕" << std::endl;
    std::cout << "=============================================" << std::endl;
    return 0;
}
