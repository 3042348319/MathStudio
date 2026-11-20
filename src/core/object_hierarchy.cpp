// ====================================================================
// object_hierarchy.cpp —— 资源管理器层级模型实现
// ====================================================================

#include "object_hierarchy.h"

#include <algorithm>
#include <utility>

ObjectGroup* ObjectHierarchy::findGroup(
    std::uint64_t groupId) {
    for (ObjectGroup& group : groups_) {
        if (group.id == groupId) {
            return &group;
        }
    }
    return nullptr;
}

const ObjectGroup* ObjectHierarchy::findGroup(
    std::uint64_t groupId) const {
    for (const ObjectGroup& group : groups_) {
        if (group.id == groupId) {
            return &group;
        }
    }
    return nullptr;
}

std::uint64_t ObjectHierarchy::createGroup(std::string name) {
    ObjectGroup group;
    group.id = nextGroupId_++;
    group.name = std::move(name);
    groups_.push_back(std::move(group));
    return groups_.back().id;
}

bool ObjectHierarchy::renameGroup(std::uint64_t groupId,
                                  std::string name) {
    ObjectGroup* group = findGroup(groupId);
    if (group == nullptr) {
        return false;
    }
    group->name = std::move(name);
    return true;
}

bool ObjectHierarchy::removeGroup(std::uint64_t groupId) {
    ObjectGroup* group = findGroup(groupId);
    if (group == nullptr) {
        return false;
    }

    // 组内对象不删除, 移回未分组。
    for (ObjectId id : group->objects) {
        ungrouped_.push_back(id);
    }

    groups_.erase(
        std::remove_if(groups_.begin(), groups_.end(),
                       [groupId](const ObjectGroup& value) {
                           return value.id == groupId;
                       }),
        groups_.end());
    return true;
}

const std::vector<ObjectGroup>& ObjectHierarchy::groups() const {
    return groups_;
}

const std::vector<ObjectId>& ObjectHierarchy::ungrouped() const {
    return ungrouped_;
}

void ObjectHierarchy::addObject(ObjectId id) {
    if (contains(id)) {
        return;
    }
    ungrouped_.push_back(id);
}

bool ObjectHierarchy::removeObject(ObjectId id) {
    bool removed = false;

    const auto ungroupedIt =
        std::find(ungrouped_.begin(), ungrouped_.end(), id);
    if (ungroupedIt != ungrouped_.end()) {
        ungrouped_.erase(ungroupedIt);
        removed = true;
    }

    for (ObjectGroup& group : groups_) {
        const auto it = std::find(group.objects.begin(),
                                  group.objects.end(), id);
        if (it != group.objects.end()) {
            group.objects.erase(it);
            removed = true;
        }
    }

    return removed;
}

bool ObjectHierarchy::moveObject(
    ObjectId id, std::optional<std::uint64_t> groupId) {
    if (!contains(id)) {
        return false;
    }

    // 先从原位置移除。
    removeObject(id);

    if (!groupId.has_value()) {
        ungrouped_.push_back(id);
        return true;
    }

    ObjectGroup* group = findGroup(*groupId);
    if (group == nullptr) {
        ungrouped_.push_back(id);
        return true;
    }

    group->objects.push_back(id);
    return true;
}

std::optional<std::uint64_t> ObjectHierarchy::groupOf(
    ObjectId id) const {
    for (const ObjectGroup& group : groups_) {
        if (std::find(group.objects.begin(), group.objects.end(), id) !=
            group.objects.end()) {
            return group.id;
        }
    }
    return std::nullopt;
}

bool ObjectHierarchy::contains(ObjectId id) const {
    if (std::find(ungrouped_.begin(), ungrouped_.end(), id) !=
        ungrouped_.end()) {
        return true;
    }

    for (const ObjectGroup& group : groups_) {
        if (std::find(group.objects.begin(), group.objects.end(), id) !=
            group.objects.end()) {
            return true;
        }
    }
    return false;
}

void ObjectHierarchy::clear() {
    groups_.clear();
    ungrouped_.clear();
    nextGroupId_ = 1;
}
