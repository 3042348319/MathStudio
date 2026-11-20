// ====================================================================
// plugin_api.h —— MathStudio 动态插件 C ABI
//
// 设计目标
// --------------------------------------------------------------------
// 普通 C++ Plugin 对象不能安全地跨 DLL 边界传递, 原因是:
//   * std::string / std::vector / std::unique_ptr 的 ABI 与 CRT 有关;
//   * Plugin 的虚函数表依赖主程序和 DLL 完全相同的编译器设置;
//   * 插件 DLL 不应该链接整个 MathStudio 可执行文件。
//
// 因此动态插件使用稳定的纯 C ABI:
//   * DLL 只导出两个 C 函数;
//   * 宿主向插件传一个 MsObjectApi 函数表;
//   * 插件通过函数指针读写 Object, 不直接持有 Object*;
//   * 插件状态由插件自己分配, 通过 destroy 回调释放。
//
// DLL 必须导出:
//   MathStudioPluginInfo* mathstudio_plugin_info(void);
//
// 头文件只依赖 C 标准库, 可以被任意编译器编译。
// ====================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define MS_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define MS_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** 三维向量; 与主程序内部 glm::vec3 逐字段对应 */
typedef struct MsVec3 {
    float x;
    float y;
    float z;
} MsVec3;

/**
 * 宿主暴露给插件的 Object 操作表。
 *
 * 插件不能直接解引用 object, 只能通过函数指针访问。
 * 这样即使主程序和 DLL 使用不同的 C++ 运行时, 也不会
 * 在 std::string / vector 的边界上崩溃。
 */
typedef struct MsObjectApi {
    /** 不透明句柄; 插件只能原样传回下面的函数 */
    void* object;

    /** 读取 Object 世界坐标 */
    MsVec3 (*get_position)(void* object);

    /** 设置 Object 世界坐标 */
    void (*set_position)(void* object, MsVec3 position);

    /** 读取 Object RGBA 颜色 */
    MsVec3 (*get_color)(void* object);

    /** 设置 Object RGB 颜色 (alpha 由宿主保留) */
    void (*set_color)(void* object, MsVec3 color);
} MsObjectApi;

/**
 * 动态插件生成的一个绘制实例。
 *
 * 每个实例使用“宿主 Object 自己的 Mesh”, 通过 transform 变换出
 * 多个实体 (例如轨迹点、环、重复方块)。
 * transform 是列主序 4x4 矩阵, 与 glm::mat4 内存布局一致。
 */
typedef struct MsRenderInstance {
    float transform[16];
    float color[4];
    int transparent;
} MsRenderInstance;

/**
 * 插件实例。
 *
 * 插件作者通常不需要手工填写这个结构:
 * 可以使用 plugin_api.h 里的辅助宏/示例实现。
 * 所有函数指针都可以为 NULL, 宿主会跳过对应回调。
 */
typedef struct MsPlugin {
    /** 插件私有状态, 由插件自己 new/delete */
    void* instance;

    /** 插件类型名; 必须与 .math 中保存的 typeId 一致 */
    const char* (*type_name)(void* instance);

    /** 释放 instance 和 MsPlugin 自身 */
    void (*destroy)(void* instance);

    /**
     * 释放整个 MsPlugin (包括 instance)。
     *
     * 如果提供这个回调, 宿主优先调用它, 这样 delete 一定发生在
     * 创建插件的同一个 DLL/CRT 内, 避免跨 CRT 释放。
     */
    void (*destroy_plugin)(MsPlugin* plugin);

    /** 挂载到 Object 时调用; 适合记录初始位置 */
    void (*on_attach)(void* instance, const MsObjectApi* api);

    /** 每帧更新 */
    void (*update)(void* instance, const MsObjectApi* api,
                   float delta_seconds);

    /** 时间轴定位; 使用绝对时间, 不是 delta */
    void (*set_time)(void* instance, const MsObjectApi* api,
                     float absolute_time);

    /**
     * 生成绘制实例。
     *
     * 插件把内部数组的地址写到 *out 并返回数量。
     * 宿主只在本帧内读取这块内存, 下一次调用前插件可以复用。
     * 返回 0 表示不接管绘制, 宿主会绘制 Object 的默认 Mesh。
     *
     * 实例的 Mesh 固定为宿主 Object 的 mesh; 动态插件可以
     * 通过不同 transform 生成多个实体。
     */
    size_t (*collect_instances)(void* instance,
                                const MsObjectApi* api,
                                const MsRenderInstance** out);

    /**
     * 返回动态参数数量。
     *
     * 宿主会用 parameter_name / parameter_get / parameter_set
     * 自动生成参数控件, 不需要插件链接 ImGui。
     */
    size_t (*parameter_count)(void* instance);

    /** 参数显示名; 生命周期至少到下一次 collect/update */
    const char* (*parameter_name)(void* instance, size_t index);

    /** 读取参数当前值 */
    float (*parameter_get)(void* instance, size_t index);

    /** 设置参数值; 插件负责 clamp */
    void (*parameter_set)(void* instance, size_t index, float value);

    /**
     * 保存状态。
     *
     * 调用约定:
     *   1. 宿主先用 buffer=nullptr, capacity=0 查询所需字节数;
     *   2. 插件返回所需字节数;
     *   3. 宿主分配 buffer 后再次调用;
     *   4. 插件把数据写进 buffer, 返回实际写入字节数。
     */
    size_t (*save)(void* instance, void* buffer, size_t capacity);

    /** 从 save 写出的数据恢复状态 */
    int (*load)(void* instance, const void* data, size_t size);
} MsPlugin;

/** DLL 入口: 返回一个新建的插件实例 */
typedef MsPlugin* (*MsCreatePluginFn)(void);

/** DLL 类型名导出函数; 宿主扫描 DLL 时先读它, 不必创建立即销毁 */
typedef const char* (*MsPluginTypeFn)(void);

#define MS_PLUGIN_CREATE_SYMBOL "mathstudio_create_plugin"
#define MS_PLUGIN_TYPE_SYMBOL "mathstudio_plugin_type"

#ifdef __cplusplus
}  // extern "C"
#endif
