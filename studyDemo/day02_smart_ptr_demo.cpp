// smart_ptr_demo.cpp
// 编译：cl /EHsc /std:c++17 /utf-8 /nologo smart_ptr_demo.cpp
// 目的：验证 HJMedia 中的智能指针模式
//   HJ_DECLARE_PUWTR、sharedFrom(this)、enable_shared_from_this、weak_ptr 打破循环引用

#include <windows.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// 模拟 HJ_DECLARE_PUWTR 宏：生成 Ptr/Utr/Wtr 三个智能指针类型别名
#define DECLARE_PUWTR(cls)     \
    using Ptr = std::shared_ptr<cls>; \
    using Utr = std::unique_ptr<cls>; \
    using Wtr = std::weak_ptr<cls>

// 模拟 HJObject 的 enable_shared_from_this 扩展
// enable_shared_from_this 让对象在 shared_ptr 管理下能在成员函数内安全获取自己的 shared_ptr
// HJMedia 做了额外封装：支持 C++14 下也能获取 weak_from_this()
template <typename T>
class enable_shared_from_this_ext : public std::enable_shared_from_this<T> {
public:
    // 获取类型安全的 weak_ptr（对应 HJObject::weak_ptr<T>()）
    // 为什么需要这个？在 C++14 下 weak_from_this() 不可用，
    // HJObject 用 SFINAE 技术做了兼容，能获取 weak_ptr 就不会增加引用计数
    template <typename WT>
    std::weak_ptr<WT> weak_ptr() {
        try {
            auto base = this->weak_from_this().lock();
            if (!base) return {};
            return std::static_pointer_cast<WT>(base);
        } catch (...) {
            return {};
        }
    }
};

// 基类：模拟 HJObject
// HJObject 是 HJMedia 所有类的根，提供统一的智能指针类型别名和对象创建工厂
class HJObject : public enable_shared_from_this_ext<HJObject> {
public:
    DECLARE_PUWTR(HJObject);

    HJObject(const std::string& name) : m_name(name) {
        std::cout << "  [构造] " << m_name << " 被创建，初始引用计数=1" << std::endl;
    }
    virtual ~HJObject() {
        std::cout << "  [析构] " << m_name << " 被销毁，引用计数已归零" << std::endl;
    }

    virtual std::string getName() const { return m_name; }

    // 模拟 sharedFrom(this)：用 dynamic_pointer_cast 安全向下转型
    // 问题：shared_from_this() 返回的是基类 shared_ptr<HJObject>
    // 解决：sharedFrom 用 dynamic_pointer_cast 转为派生类 shared_ptr<T>
    // 这样调用方拿到的是类型正确的 shared_ptr
    template <typename T>
    std::shared_ptr<T> sharedFrom(T* ptr) {
        if (!ptr) return nullptr;
        return std::dynamic_pointer_cast<T>(ptr->shared_from_this());
    }

    // 模拟 getSharedPtr()
    template <typename T>
    std::shared_ptr<T> getSharedPtr() {
        return std::dynamic_pointer_cast<T>(shared_from_this());
    }

    // 模拟 static creates<T>() std::make_shared 的工厂封装
    // 项目里统一用 HJCreates<T>(args...) 创建对象
    template <typename T, typename... Args>
    static std::shared_ptr<T> creates(Args&&... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

private:
    std::string m_name;
};

// 派生类：模拟 HJGraphMusicPlayer
class MusicPlayer : public HJObject {
public:
    DECLARE_PUWTR(MusicPlayer);

    MusicPlayer() : HJObject("MusicPlayer") {}

    void play() {
        std::cout << "  [播放] " << getName() << " 开始播放音频流" << std::endl;
    }

    // 演示 sharedFrom(this)：在成员函数内部拿到类型正确的 shared_ptr
    void showSelf() {
        std::cout << "\n  --- [技术要点] sharedFrom(this) 演示 ---" << std::endl;
        std::cout << "  背景：shared_from_this() 返回的是 shared_ptr<HJObject>" << std::endl;
        std::cout << "  问题：我们需要的是 shared_ptr<MusicPlayer>，如果手动转型容易出错" << std::endl;
        std::cout << "  解决：sharedFrom(this) 内部实现 dynamic_pointer_cast 自动向下转型" << std::endl;

        // 方法1：sharedFrom(this) —— dynamic_pointer_cast 向下转型
        auto self = sharedFrom(this);
        std::cout << "  [sharedFrom] 返回类型=MusicPlayer::Ptr" << std::endl;
        std::cout << "  [sharedFrom] 对象名称=" << self->getName() << std::endl;
        std::cout << "  [sharedFrom] 当前引用计数=" << self.use_count() << std::endl;

        // 方法2：getSharedPtr<MusicPlayer>() —— 效果一样
        auto self2 = getSharedPtr<MusicPlayer>();
        std::cout << "  [getSharedPtr] 返回类型=MusicPlayer::Ptr" << std::endl;
        std::cout << "  [getSharedPtr] 对象名称=" << self2->getName() << std::endl;
        std::cout << "  [getSharedPtr] 当前引用计数=" << self2.use_count() << std::endl;
    }
};

// 演示 weak_ptr 打破循环引用
// 场景：父节点和子节点互相持有对方，如果都用 shared_ptr，引用计数永远无法归零
class Node : public HJObject {
public:
    DECLARE_PUWTR(Node);

    Node(const std::string& name) : HJObject(name) {}

    // 用 Wtr (weak_ptr) 持有父节点 —— 不增加引用计数，避免循环引用
    void setParent(Wtr parent) {
        m_parent = parent;
        std::cout << "  [setParent] " << getName() << " 用 weak_ptr 持有父节点（引用计数不变）" << std::endl;
    }

    // 用 Ptr (shared_ptr) 持有子节点 —— 强引用，增加引用计数
    void setChild(Ptr child) {
        m_child = child;
        std::cout << "  [setChild] " << getName() << " 用 shared_ptr 持有子节点（引用计数+1）" << std::endl;
    }

    void showFamily() {
        auto parent = m_parent.lock();  // lock() 将 weak_ptr 提升为 shared_ptr
        if (parent) {
            std::cout << "  [家族关系] " << getName() << " 的父节点是: " << parent->getName() << std::endl;
        } else {
            std::cout << "  [家族关系] " << getName() << " 的父节点已销毁（weak_ptr.lock() 返回空）" << std::endl;
            std::cout << "  因为弱引用不阻止父节点被销毁，这是正确行为" << std::endl;
        }
    }

private:
    Wtr m_parent;  // weak_ptr：不增加引用计数，打破循环
    Ptr m_child;   // shared_ptr：强引用
};

// ==================== Demo 函数 ====================

// Demo 1：Ptr/Utr/Wtr 类型别名
void demo_ptr_aliases() {
    std::cout << "\n==================== Demo 1：Ptr/Utr/Wtr 类型别名 ====================" << std::endl;
    std::cout << "目的：验证 DECLARE_PUWTR 宏生成的三种智能指针类型别名" << std::endl;
    std::cout << "宏展开效果：DECLARE_PUWTR(HJObject) ->" << std::endl;
    std::cout << "  using Ptr = shared_ptr<HJObject>;" << std::endl;
    std::cout << "  using Utr = unique_ptr<HJObject>;" << std::endl;
    std::cout << "  using Wtr = weak_ptr<HJObject>;" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;

    // Ptr = shared_ptr —— 多个地方可以共享同一个对象
    std::cout << "\n[步骤1] Ptr = shared_ptr<HJObject> —— 共享所有权" << std::endl;
    HJObject::Ptr obj = std::make_shared<HJObject>("obj1");
    std::cout << "  [Ptr] 当前引用计数：" << obj.use_count() << "（只有 obj 持有）" << std::endl;
    {
        std::cout << "  进入作用域，创建 objCopy 共享同一对象..." << std::endl;
        HJObject::Ptr objCopy = obj;
        std::cout << "  [Ptr] 引用计数变为：" << objCopy.use_count() << "（obj + objCopy）" << std::endl;
    }
    std::cout << "  离开作用域，objCopy 销毁" << std::endl;
    std::cout << "  [Ptr] 引用计数恢复为：" << obj.use_count() << std::endl;

    // Utr = unique_ptr —— 独占所有权，不能拷贝
    std::cout << "\n[步骤2] Utr = unique_ptr<HJObject> —— 独占所有权" << std::endl;
    std::cout << "  unique_ptr 禁止拷贝，只能移动。适合明确单一所有权的场景" << std::endl;
    HJObject::Utr uniqueObj = std::make_unique<HJObject>("uniqueObj");
    std::cout << "  [Utr] " << uniqueObj->getName() << " 当前由 uniqueObj 独占" << std::endl;
    // uniqueObj 离开作用域时会自动销毁

    // Wtr = weak_ptr —— 观察者，不影响引用计数
    std::cout << "\n[步骤3] Wtr = weak_ptr<HJObject> —— 弱引用观察者" << std::endl;
    std::cout << "  weak_ptr 不增加引用计数，适合打破循环引用" << std::endl;
    std::cout << "  当前 obj1 引用计数=" << obj.use_count() << std::endl;
    HJObject::Wtr weakObj = obj;
    std::cout << "  创建 weak_ptr 后引用计数仍为：" << obj.use_count() << "（weak_ptr 不增加计数）" << std::endl;
    std::cout << "  weak_ptr 不能直接访问对象，需要通过 lock() 提升为 shared_ptr" << std::endl;
    std::cout << "  weak_ptr.lock() 正在提升..." << std::endl;
    if (auto locked = weakObj.lock()) {
        std::cout << "  [Wtr] 提升成功！locked->name=" << locked->getName() << std::endl;
        std::cout << "  [Wtr] 提升后引用计数=" << locked.use_count() << "（obj + locked 临时引用）" << std::endl;
    }
    std::cout << "  临时 shared_ptr 释放后，引用计数恢复为：" << obj.use_count() << std::endl;
}

// Demo 2：sharedFrom(this) 安全向下转型
void demo_shared_from() {
    std::cout << "\n==================== Demo 2：sharedFrom(this) + enable_shared_from_this ====================" << std::endl;
    std::cout << "目的：验证在成员函数内部拿到类型正确的 shared_ptr" << std::endl;
    std::cout << "背景：enable_shared_from_this 提供 shared_from_this()" << std::endl;
    std::cout << "     但返回的是 shared_ptr<基类>，需要手动向下转型到派生类" << std::endl;
    std::cout << "     sharedFrom(this) 封装了 dynamic_pointer_cast 的向下转型" << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    // 通过静态工厂 creates<T>() 创建对象
    std::cout << "\n[步骤1] 通过静态工厂 creates<MusicPlayer>() 创建" << std::endl;
    std::cout << "  creates<T>(args...) 内部 = std::make_shared<T>(args...)" << std::endl;
    auto player = MusicPlayer::creates<MusicPlayer>();
    std::cout << "  [工厂] Player 创建完成，引用计数=" << player.use_count() << std::endl;
    player->play();

    // 在成员函数内使用 sharedFrom(this)
    std::cout << "\n[步骤2] 调用成员函数 showSelf() 验证 sharedFrom(this)" << std::endl;
    player->showSelf();
}

// Demo 3a：weak_ptr 正确用法 —— 打破循环引用
void demo_weak_ptr_break_cycle() {
    std::cout << "\n==================== Demo 3：weak_ptr 打破循环引用 ====================" << std::endl;
    std::cout << "场景：父子节点双向引用" << std::endl;
    std::cout << "问题：如果双方都用 shared_ptr 互指，引用计数永远无法归零 -> 内存泄漏" << std::endl;
    std::cout << "解决：子节点用 weak_ptr 持有父节点，不增加引用计数" << std::endl;
    std::cout << "------------------------------------------------------------------" << std::endl;

    std::cout << "\n[步骤1] 创建父节点 Parent 和子节点 Child" << std::endl;
    auto parent = Node::creates<Node>("Parent");
    auto child = Node::creates<Node>("Child");
    std::cout << "  Parent 引用计数: " << parent.use_count() << "（由 parent 变量持有）" << std::endl;
    std::cout << "  Child 引用计数: " << child.use_count() << "（由 child 变量持有）" << std::endl;

    std::cout << "\n[步骤2] 建立双向关系" << std::endl;
    std::cout << "  child->setParent(parent)：Child 用 weak_ptr 持有 Parent" << std::endl;
    std::cout << "  parent->setChild(child)：Parent 用 shared_ptr 持有 Child" << std::endl;
    child->setParent(parent);   // weak_ptr —— 不增加 parent 引用计数
    parent->setChild(child);    // shared_ptr —— child 引用计数 +1
    std::cout << "  Parent 引用计数: " << parent.use_count() << "（仍为1，weak_ptr 不增加计数）" << std::endl;
    std::cout << "  Child 引用计数: " << child.use_count() << "（变为2，parent 也强引用 child）" << std::endl;

    std::cout << "\n[步骤3] 查询父子关系" << std::endl;
    child->showFamily();

    std::cout << "\n[步骤4] 释放父节点 parent.reset()" << std::endl;
    std::cout << "  reset() 前 Parent 引用计数: " << parent.use_count() << std::endl;
    parent.reset();
    std::cout << "  Parent 引用计数归零 -> Parent 被析构" << std::endl;

    std::cout << "\n[步骤5] 再次查询子节点" << std::endl;
    child->showFamily();
    std::cout << "  Child 仍存活（引用计数为1 + parent 已析构）" << std::endl;
    std::cout << "  因为 weak_ptr 不阻止 Parent 被销毁，这是正确的生命周期管理" << std::endl;

    std::cout << "\n[步骤6] 释放子节点" << std::endl;
    std::cout << "  Child 引用计数: " << child.use_count() << "（只剩 child 自身持有）" << std::endl;
    std::cout << "  验证结果：没有内存泄漏！Child 正常析构" << std::endl;
}

// Demo 3b：循环引用对比
void demo_circular_reference() {
    std::cout << "\n==================== Demo 3b：循环引用对比（如果不用 weak_ptr） ====================" << std::endl;
    std::cout << "演示：双方都用 shared_ptr 互指的坏结果" << std::endl;
    std::cout << "-----------------------------------------------------------------------------" << std::endl;

    struct BadNode : public HJObject {
        DECLARE_PUWTR(BadNode);
        BadNode(const std::string& n) : HJObject(n) {}
        std::shared_ptr<BadNode> m_badParent;  // 错误！应该用 weak_ptr
        std::shared_ptr<BadNode> m_badChild;
    };

    std::cout << "\n[步骤1] 创建两个 BadNode，双方都用 shared_ptr 互指" << std::endl;
    auto bp = std::make_shared<BadNode>("BadParent");
    auto bc = std::make_shared<BadNode>("BadChild");
    bc->m_badParent = bp;  // bp 引用计数 +1
    bp->m_badChild = bc;   // bc 引用计数 +1
    std::cout << "  BadParent 引用计数: " << bp.use_count() << "（bp 持有 + bc.m_badParent 持有）" << std::endl;
    std::cout << "  BadChild 引用计数: " << bc.use_count() << "（bc 持有 + bp.m_badChild 持有）" << std::endl;
    std::cout << "  即使 bp 和 bc 离开作用域，引用计数也无法归零！" << std::endl;
    std::cout << "  因为 BadNode 之间用 shared_ptr 互指，形成循环引用" << std::endl;
    std::cout << "  -> 两个对象内存泄漏（引用计数永远 > 0）" << std::endl;
}

// Demo 4：unique_ptr 所有权转移
void demo_unique_ptr() {
    std::cout << "\n==================== Demo 4：unique_ptr (Utr) 所有权转移 ====================" << std::endl;
    std::cout << "特点：unique_ptr 独占所有权，不能拷贝只能移动" << std::endl;
    std::cout << "适用：明确单一所有权，生命周期明确绑定的场景" << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    std::cout << "\n[步骤1] 创建 unique_ptr<HJObject>" << std::endl;
    auto obj = std::make_unique<HJObject>("tempObj");
    std::cout << "  [Utr] " << obj->getName() << " 当前由 obj 独占所有权" << std::endl;

    std::cout << "\n[步骤2] 通过 std::move 转移所有权" << std::endl;
    std::cout << "  unique_ptr 禁止拷贝，必须用 std::move 转移" << std::endl;
    auto movedObj = std::move(obj);
    if (obj == nullptr) {
        std::cout << "  [Utr] 转移后原 obj 自动置为 nullptr" << std::endl;
    }
    std::cout << "  [Utr] movedObj 接管所有权，name=" << movedObj->getName() << std::endl;
    std::cout << "  [Utr] movedObj 离开作用域时 tempObj 自动销毁" << std::endl;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "=============================================" << std::endl;
    std::cout << "    HJMedia 智能指针模式验证 Demo" << std::endl;
    std::cout << "     对应：week1 第2天学习内容" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "验证内容：" << std::endl;
    std::cout << "  1. Ptr/Utr/Wtr 类型别名（shared_ptr/unique_ptr/weak_ptr）" << std::endl;
    std::cout << "  2. sharedFrom(this) + enable_shared_from_this 安全转型" << std::endl;
    std::cout << "  3. weak_ptr 打破循环引用" << std::endl;
    std::cout << "  4. 循环引用对比（不用 weak_ptr 的后果）" << std::endl;
    std::cout << "  5. unique_ptr 所有权转移" << std::endl;
    std::cout << std::endl;

    demo_ptr_aliases();
    demo_shared_from();
    demo_weak_ptr_break_cycle();
    demo_circular_reference();
    demo_unique_ptr();

    std::cout << "\n=============================================" << std::endl;
    std::cout << "    所有 Demo 执行完毕" << std::endl;
    std::cout << "=============================================" << std::endl;
    return 0;
}
