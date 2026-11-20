// ====================================================================
// object_hierarchy.h —— Object 资源管理器的数据模型
//
// 这个类只管理:
//   * 组别名称;
//   * 组别与 ObjectId 的从属关系;
//   * 未分组对象列表。
//
// 它不 include Object, 也不依赖 ImGui / Vulkan。
// 因此资源管理器 UI、SubWindow 和 Object 之间保持低耦合。
// ====================================================================
#pragma once

#include "object_id.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/** 一个对象组 */
struct ObjectGroup {
    // 组 ID (由 ObjectHierarchy 分配)
    std::uint64_t id = 0;

    // 组名称 (可编辑)
    std::string name;

    // 组内对象 ID
    std::vector<ObjectId> objects;
};

/**
 * 对象层级 / 资源管理器模型
 *
 * 对象始终存在于 SubWindow 的 objects_ 列表中;
 * ObjectHierarchy 只保存它们的 ID, 不拥有对象。
 */
class ObjectHierarchy {
public:
    ObjectHierarchy() = default;

    /**
     * 创建组
     * @param name 组名称
     * @return 新组的 ID
     */
    std::uint64_t createGroup(std::string name);

    /** 重命名组 */
    bool renameGroup(std::uint64_t groupId, std::string name);

    /**
     * 删除组
     * 组内对象不会被删除, 而是移回"未分组"。
     */
    bool removeGroup(std::uint64_t groupId);

    // 所有组
    const std::vector<ObjectGroup>& groups() const;

    // 未分组对象 ID
    const std::vector<ObjectId>& ungrouped() const;

    /**
     * 登记一个新对象
     * 默认放入未分组; 重复 ID 会被忽略。
     */
    void addObject(ObjectId id);

    /** 从组和未分组列表中删除对象 */
    bool removeObject(ObjectId id);

    /**
     * 移动对象到组
     * @param groupId 为空表示移动到"未分组"
     * @return true 表示对象存在并移动成功
     */
    bool moveObject(ObjectId id, std::optional<std::uint64_t> groupId);

    /** 查询对象所在组; 未分组返回 nullopt */
    std::optional<std::uint64_t> groupOf(ObjectId id) const;

    /** 对象是否已被登记 */
    bool contains(ObjectId id) const;

    /** 清空层级数据 (不删除对象本身) */
    void clear();

private:
    ObjectGroup* findGroup(std::uint64_t groupId);
    const ObjectGroup* findGroup(std::uint64_t groupId) const;

    std::vector<ObjectGroup> groups_;
    std::vector<ObjectId> ungrouped_;
    std::uint64_t nextGroupId_ = 1;
};
