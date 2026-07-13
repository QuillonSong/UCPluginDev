# UmbilicalCable 插件设计文档（DSL）

> **版本**：1.0 | **模块类型**：Runtime | **依赖**：Core, CoreUObject, Engine
> 最后更新：2026-07-14

---

## 1. 概述

**UmbilicalCable**（脐带缆/垂链线缆）是一个 UE5 Runtime 插件，提供基于悬链线（Catenary）数学模型的三维线缆中心线生成能力。通过指定起止端点和物理参数，自动计算线缆的垂链曲线，并支持可选的环境碰撞检测。

### 核心用途

- 水下/深海脐带缆可视化
- 输电线/缆索物理模拟
- 软管、绳索的实时形变

### 设计原则

| 原则 | 说明 |
|------|------|
| 事件驱动 | 线缆仅在 `UpdateCable()` 被调用时重新计算，禁止无意义 Tick |
| 低耦合 | 单组件即插即用，不依赖外部 Manager/Subsystem |
| 蓝图友好 | 核心属性和方法均暴露给蓝图 |

---

## 2. 模块结构

```
Plugins/UmbilicalCable/
├── UmbilicalCable.uplugin          # 插件描述文件
├── Resources/
│   └── Icon128.png                 # 编辑器图标
├── Source/UmbilicalCable/
│   ├── UmbilicalCable.Build.cs     # 构建规则
│   ├── Public/
│   │   ├── UmbilicalCable.h        # 模块接口
│   │   └── UmbilicalCableComponent.h   # 核心组件
│   └── Private/
│       ├── UmbilicalCable.cpp
│       └── UmbilicalCableComponent.cpp
└── DSL.md                          # 本文档
```

### 依赖关系

```
UmbilicalCable (Runtime)
├── Core           — FVector, TArray, 基础数学
├── CoreUObject    — UCLASS/USTRUCT/UENUM 反射
└── Engine         — UActorComponent, UWorld (LineTrace), DrawDebugLine
```

---

## 3. 核心类：UUmbilicalCableComponent

**继承链**：`UActorComponent` → `UUmbilicalCableComponent`

**蓝图**：`BlueprintSpawnableComponent`，可直接挂载到任意 Actor。

### 3.1 属性清单

#### 配置属性（公开，编辑器可调）

| 属性 | 类型 | 默认值 | 单位 | 说明 |
|------|------|--------|------|------|
| `StartComponent` | `USceneComponent*` | — | — | 线缆起点绑定的场景组件。运行时通过 `GetComponentLocation()` 获取世界坐标 |
| `EndComponent` | `USceneComponent*` | — | — | 线缆终点绑定的场景组件 |
| `PointCount` | `int32` | 32 | — | 中心线采样点数。控制曲线精度与后续Mesh分段数。**修改后需调用 `UpdateCable()` 重建数组** |
| `RadialSegments` | `int32` | 12 | — | 线缆截面段数（预留，供后续 Mesh 生成使用） |
| `CableLength` | `float` | 50000.0 | cm | 脐带缆实际物理长度。**必须 ≥ 起止点直线距离**，差值为松弛量来源 |
| `CableRadius` | `float` | 16.0 | cm | 线缆半径。碰撞检测命中后沿Z轴抬高此值，避免嵌入表面 |
| `SagFactor` | `float` | 0.5 | 无 | 松弛度系数。0.3=较硬缆线，0.7=较软缆线 |
| `CableMaterial` | `UMaterialInterface*` | — | — | 线缆材质（预留，供后续Mesh生成使用） |
| `EnableCollision` | `bool` | true | — | 是否对每个中段采样点执行 `LineTraceSingleByChannel` 碰撞检测 |

#### 运行时数据（私有）

| 成员 | 类型 | 说明 |
|------|------|------|
| `LinearPoints` | `TArray<FVector>` | 起止点直线插值采样点。`[0] = StartPoint`, `[N-1] = EndPoint` |
| `SagPoints` | `TArray<FVector>` | 垂链曲线采样点。在 `LinearPoints` 基础上施加 sin 下垂，并经碰撞修正。**对外暴露只读** |
| `SlackLength` | `float` | 松弛长度 = `CableLength - 直线距离`，驱动下垂量计算 |
| `PipePoints` | `TArray<FVector>` | 预留：最终管道路径点 |

### 3.2 公共接口

| 方法 | 蓝图 | 说明 |
|------|------|------|
| `SetTargetPoint(Start, End)` | ✅ | 绑定起始和终止 `SceneComponent`。调用后需手动调用 `UpdateCable()` |
| `UpdateCable()` | ✅ | 触发中心线重算。内部调用 `GenerateCenterline()` |

### 3.3 私有方法

| 方法 | 说明 |
|------|------|
| `GenerateCenterline()` | 核心算法。依次执行：有效性质检 → 数组预分配 → 松弛量计算 → 端点赋值 → 中段遍历（插值 + 垂链 + 碰撞） |

---

## 4. 算法详解：悬链线中心线生成

### 4.1 松弛量计算

```
SlackLength = CableLength - |StartPoint - EndPoint|
```

- 若 `CableLength < 直线距离`，`SlackLength` 为负 → 线缆被"拉平"甚至反向隆起。**调用方需保证 `CableLength` 不小于端点间距。**
- `SlackLength = 0` 时线缆完全绷直，无下垂。

### 4.2 垂链下垂模型

采用简化的正弦悬垂模型（非严格物理悬链线方程 `cosh`，工程近似）：

```
对于每个采样点 i（1 ≤ i ≤ N-2）：
    Alpha = i / (N-1)                           // 归一化进度 [0, 1]
    LinearPoint = Lerp(Start, End, Alpha)       // 直线插值
    SagZ = SagAmount × sin(Alpha × π)           // 下垂偏移量
    Sag = (CableLength - |Start-End|) / CableLength × SagFactor × CableLength
    SagPoint[i] = (LinearPoint.X, LinearPoint.Y, LinearPoint.Z - SagZ)
```

**正弦模型的合理性**：
- 0~π 的 sin 曲线在两端点为0、中间峰值，准确描述悬链下垂的对称形态
- 相比 `cosh` 避免了双曲函数计算开销
- `SagFactor` 提供艺术化调整自由度

### 4.3 端点处理

```cpp
LinearPoints[0]       = SagPoints[0]       = StartPoint;
LinearPoints[N-1]     = SagPoints[N-1]     = EndPoint;
```

端点不做插值（Alpha=0/1，sin=0，无下垂），不执行碰撞检测（末点射线长度为零）。

### 4.4 碰撞检测与修正

```
对每个中段采样点：
    从 LinearPoint[i] 向 SagPoint[i] 发射 ECC_Visibility 射线
    命中 → SagPoint[i] = Hit.Location + (0, 0, CableRadius)
    未命中 → 保持正弦计算值
```

**当前限制**：仅沿世界Z轴偏移 `CableRadius`，仅适配水平地面。斜面/垂直障碍物需改为法线偏移 `HitResult.Normal * CableRadius`。

### 4.5 数据流

```
SetTargetPoint() → UpdateCable() → GenerateCenterline()
                                         │
                                         ├─ [有效性质检] StartComponent/EndComponent 有效性
                                         ├─ [数组预分配] LinearPoints/SagPoints.SetNum(PointCount)
                                         ├─ [参数计算] StartPoint, EndPoint, SlackLength, SagAmount
                                         ├─ [端点赋值] idx=0 和 idx=N-1 直接设值
                                         ├─ [中段遍历] i=1 → N-2
                                         │    ├─ Lerp 直线插值
                                         │    ├─ Sin 下垂偏移 → SagPoints[i]
                                         │    └─ LineTrace 碰撞 → 如命中则修正 SagPoints[i]
                                         └─ [返回] SagPoints 可供外部消费
```

---

## 5. 已知限制与待办

| 序号 | 类型 | 描述 | 优先级 |
|------|------|------|--------|
| 1 | 限制 | 碰撞修正仅沿世界Z轴偏移 `CableRadius`，斜面/垂直面会穿模 | 高 |
| 2 | 限制 | `CableLength` 小于端点间距时 SlackLength 为负，未做 clamp 保护 | 高 |
| 3 | 限制 | `StartComponent`/`EndComponent` 使用裸指针，存在GC悬空风险。应改为 `TObjectPtr` | 中 |
| 4 | 待办 | Mesh 生成：基于 `SagPoints` + `RadialSegments` + `CableRadius` 构建管状网格 | 中 |
| 5 | 待办 | `CableMaterial` 应用到生成的 Mesh | 低 |
| 6 | 待办 | `PointCount` 变更时触发数组自动重分配，避免调用方忘记 `UpdateCable()` | 低 |
| 7 | 待办 | 碰撞通道 `ECC_Visibility` 硬编码，考虑改为可配置属性 `TEnumAsByte<ECollisionChannel>` | 低 |
| 8 | 优化 | 若 `GenerateCenterline` 在 Tick 中高频调用，将 `SetNum` 移到 `BeginPlay` 或仅在 `PointCount` 变更时执行 | 低 |
| 9 | 优化 | Debug 线绘制已移除，后续可添加 `bDrawDebug` 开关恢复 `ENABLE_DRAW_DEBUG` 块 | 低 |

---

## 6. 使用约定

### 蓝图调用流程

```
BeginPlay → SetTargetPoint(Start, End) → UpdateCable() → [读取 SagPoints]
                                                             │
                                                    Mesh生成 / 物理 / 渲染
```

### 编辑器配置

1. 将组件挂载到 Actor
2. 设置 `StartComponent` / `EndComponent` 指向场景中的 SceneComponent
3. 调整 `CableLength`、`SagFactor`、`CableRadius` 至期望效果
4. 调用 `UpdateCable()` 观察 `SagPoints` 输出

---

## 7. 版本历史

| 日期 | 变更 |
|------|------|
| 2026-07-13 | 初始实现：`GenerateCenterline` 悬链计算 + 碰撞检测 |
| 2026-07-13 | 修复：数组预分配 `SetNum` 防越界 |
| 2026-07-13 | 修复：`LineTraceSingleByChannel` 增加 `FHitResult` 捕获 |
| 2026-07-13 | 修复：`GetWorld()` 空指针保护 |
| 2026-07-13 | 修复：碰撞命中后将 `SagPoints[i]` 修正为 `HitResult.Location + CableRadius` |
| 2026-07-13 | 重构：端点（i=0, i=N-1）提出循环，单独赋值 |
| 2026-07-14 | 文档：创建 DSL.md 设计文档 |
