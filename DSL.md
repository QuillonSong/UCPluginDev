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
├── Core                        — FVector, TArray, 基础数学
├── CoreUObject                 — UCLASS/USTRUCT/UENUM 反射
├── Engine                      — UWorld (LineTrace), DrawDebugLine
└── ProceduralMeshComponent     — UProceduralMeshComponent (父类), CreateMeshSection, SetMaterial
```

---

## 3. 核心类：UUmbilicalCableComponent

**继承链**：`UProceduralMeshComponent` → `UUmbilicalCableComponent`

**蓝图**：`BlueprintSpawnableComponent`，可直接挂载到任意 Actor。

### 3.1 属性清单

#### 配置属性（公开，编辑器可调）

| 属性 | 类型 | 默认值 | 单位 | 说明 |
|------|------|--------|------|------|
| `StartComponent` | `USceneComponent*` | — | — | 线缆起点绑定的场景组件。运行时通过 `GetComponentLocation()` 获取世界坐标 |
| `EndComponent` | `USceneComponent*` | — | — | 线缆终点绑定的场景组件 |
| `PointCount` | `int32` | 32 | — | 中心线采样点数。控制曲线精度与后续Mesh分段数。**蓝图只读**，运行时通过 `SetPointCount()` 修改以自动重分配数组 |
| `RadialSegments` | `int32` | 12 | — | 线缆截面段数，配合父类 `CreateMeshSection` 构建管状网格 |
| `CableLength` | `float` | 50000.0 | cm | 脐带缆实际物理长度。**必须 ≥ 起止点直线距离**，差值为松弛量来源 |
| `CableRadius` | `float` | 16.0 | cm | 线缆半径。碰撞检测命中后沿Z轴抬高此值，避免嵌入表面 |
| `SagFactor` | `float` | 0.5 | 无 | 松弛度系数。0.3=较硬缆线，0.7=较软缆线 |
| `CableMaterial` | `UMaterialInterface*` | — | — | 线缆材质，配合父类 `SetMaterial` 应用到生成的 Mesh Section |
| `EnableCollision` | `bool` | true | — | 是否对每个中段采样点执行 `LineTraceSingleByChannel` 碰撞检测 |
| `bIsDrawCenterLine` | `bool` | true | — | 是否在 `UpdateCable()` 时绘制调试中心线。调用 `DebugDrawCenterline()` 渲染橙色曲线+端点球，持续 5 秒 |

#### 运行时数据（私有）

| 成员 | 类型 | 说明 |
|------|------|------|
| `LinearPoints` | `TArray<FVector>` | 起止点直线插值采样点。`[0] = StartPoint`, `[N-1] = EndPoint` |
| `SagPoints` | `TArray<FVector>` | 垂链曲线采样点。在 `LinearPoints` 基础上施加 sin 下垂，并经碰撞修正。**对外暴露只读** |
| `Tangents` | `TArray<FVector>` | 每个采样点的切线方向。中央差分计算（端点用单侧差分） |
| `SlackLength` | `float` | 松弛长度 = `CableLength - 直线距离`，驱动下垂量计算 |
| `bIsMeshCreated` | `bool` | Mesh 是否已首次构建。`false` 时走 `CreateMeshSection` 全量创建路径；`true` 时走 `UpdateMeshSection` 增量更新路径 |
| `CableVertices` | `TArray<FVector>` | 管状网格顶点缓存。每帧 `BuildCableVertices()` 重建，容量 = `PointCount × RadialSegments` |
| `CableNormals` | `TArray<FVector>` | 顶点法线缓存。与 `CableVertices` 一一对应，指向截面圆环外法线方向 |
| `CableUVs` | `TArray<FVector2D>` | UV 缓存。U 沿缆线长度方向 [0,1]，V 沿截面旋转方向 [0,1] |
| `CableTriangles` | `TArray<int32>` | 三角索引缓存。仅在首次 `GenerateCable()` 时构建（拓扑不变，无需每帧重建） |
| `CableColors` | `TArray<FColor>` | 顶点颜色。当前保持为空，预留扩展 |
| `CableMeshTangents` | `TArray<FProcMeshTangent>` | 切线数据。当前保持为空，预留扩展 |

### 3.2 公共接口

| 方法 | 蓝图 | 说明 |
|------|------|------|
| `SetTargetPoint(Start, End)` | ✅ | 绑定起始和终止 `SceneComponent`。调用后需手动调用 `UpdateCable()` |
| `SetPointCount(NewPointCount)` | ✅ | 修改采样点数，自动调用 `ResizePoints()` 重建三个点数组 |
| `UpdateCable()` | ✅ | 触发中心线重算 + Mesh 重建。内部依次调用 `GenerateCenterline()` → `GenerateCable()` |

### 3.3 私有方法

| 方法 | 说明 |
|------|------|
| `ResizePoints()` | 统一调整 6 个数组（`LinearPoints`/`SagPoints`/`Tangents`/`CableVertices`/`CableNormals`/`CableUVs`）为 `PointCount` 或 `PointCount × RadialSegments` 大小。在 `BeginPlay` 和 `SetPointCount` 中被调用。`BuildCableVertices` 直接下标写入，不再自行分配 |
| `GenerateCenterline()` | 核心算法。依次执行：有效性质检 → 松弛量计算 → 端点赋值 → 中段遍历（插值 + 垂链 + 碰撞） → 切线计算 |
| `BuildCableVertices()` | 基于 `SagPoints` + `Tangents` 构建管状网格顶点。逐采样点构造截面圆环（`RadialSegments` 等分），直接下标写入预分配的 `CableVertices`/`CableNormals`/`CableUVs` |
| `BuildCableTriangles()` | 构建三角索引。在相邻截面环之间生成四边形（拆为两个三角形），输出 `CableTriangles`。仅在首次 `GenerateCable()` 时调用（拓扑不变） |
| `GenerateCable()` | Mesh 生成总控。首次调用 `BuildCableVertices` → `BuildCableTriangles` → `CreateMeshSection` + `SetMaterial`；后续仅 `BuildCableVertices` → `UpdateMeshSection` 增量更新顶点 |
| `DebugDrawCenterline()` | 调试可视化。`DrawDebugLine` 逐段绘制中心线（橙色） + `DrawDebugSphere` 标记起止端点（绿/红），持续 5 秒。仅在 `bIsDrawCenterLine == true` 时调用 |

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
    SagAmount = SlackLength × SagFactor          // 松弛量 × 松弛系数
    SagPoint[i] = (LinearPoint.X, LinearPoint.Y, LinearPoint.Z - SagAmount × sin(Alpha × π))
```

> **公式简化**（2026-07-14）：`CableLength × (SlackLength / CableLength) × SagFactor` → `SlackLength × SagFactor`，`CableLength` 乘除抵消。

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

### 4.4 切线计算

在中心线生成完成后，基于 `SagPoints` 逐点计算切线方向（供后续 Mesh 法线/管线朝向使用）：

```
对所有采样点 i（0 ≤ i ≤ N-1）：
    i == 0       → Tangent[i] = (SagPoints[1] - SagPoints[0]).GetSafeNormal()        // 前向差分
    i == N-1     → Tangent[i] = (SagPoints[N-1] - SagPoints[N-2]).GetSafeNormal()    // 后向差分
    其他          → Tangent[i] = (SagPoints[i+1] - SagPoints[i-1]).GetSafeNormal()   // 中央差分
```

### 4.5 碰撞检测与修正

```
对每个中段采样点：
    从 LinearPoint[i] 向 SagPoint[i] 发射 ECC_Visibility 射线
    命中 → SagPoint[i] = Hit.Location + (0, 0, CableRadius)
    未命中 → 保持正弦计算值
```

**当前限制**：仅沿世界Z轴偏移 `CableRadius`，仅适配水平地面。斜面/垂直障碍物需改为法线偏移 `HitResult.Normal * CableRadius`。

### 4.6 数据流

```
BeginPlay → ResizePoints()           // 6 数组预分配（仅一次）
                ├─ LinearPoints / SagPoints / Tangents → SetNum(PointCount)
                └─ CableVertices / CableNormals / CableUVs → SetNum(PointCount × RadialSegments)

SetPointCount(N) → ResizePoints()    // 运行时改 PointCount 自动重分配所有数组

SetTargetPoint() → UpdateCable() → GenerateCenterline()
                                      │
                                      ├─ [有效性质检] StartComponent/EndComponent 有效性
                                      ├─ [参数计算] StartPoint, EndPoint, SlackLength, SagAmount
                                      ├─ [端点赋值] idx=0 和 idx=N-1 直接设值
                                      ├─ [中段遍历] i=1 → N-2
                                      │    ├─ Lerp 直线插值
                                      │    ├─ Sin 下垂偏移 → SagPoints[i]
                                      │    └─ LineTrace 碰撞 → 如命中则修正 SagPoints[i]
                                      └─ [切线计算] i=0 → N-1
                                           ├─ i=0: 前向差分
                                           ├─ i=N-1: 后向差分
                                           └─ 其他: 中央差分 → Tangents[i]
                                      │
                                      ├─ [Debug] bIsDrawCenterLine → DebugDrawCenterline()
                                      │    └─ DrawDebugLine 中心线(橙) + DrawDebugSphere 端点(绿/红)，持续 5 秒
                                      │
                                      └→ GenerateCable()
                                           ├─ BuildCableVertices()
                                           │    └─ 逐采样点构造截面圆环，直接下标写入预分配数组
                                           ├─ [首次] BuildCableTriangles()
                                           │    └─ 相邻环四边形 → CableTriangles
                                           ├─ [首次] CreateMeshSection(0, ...) + SetMaterial(0, CableMaterial)
                                           │    └─ 创建新 Mesh Section，标记 bIsMeshCreated = true
                                           └─ [后续] UpdateMeshSection(0, CableVertices, ...)
                                                └─ 仅更新顶点/法线/UV，三角拓扑复用
```

### 4.7 Mesh 管状网格生成

在中心线计算完成后，`GenerateCable()` 基于 `SagPoints` + `Tangents` + `CableRadius` + `RadialSegments` 构建管状（圆柱面）网格。

#### 4.7.1 顶点构建（BuildCableVertices）

对每个采样点 `i`，以 `SagPoints[i]` 为圆心、`Tangents[i]` 为管道方向，构造截面局部坐标系：

```
Forward = Tangents[i].GetSafeNormal()

// 防止 Forward 接近世界 Z 轴时叉乘退化（平行向量叉乘为零向量）
ReferenceUp = (|Forward · UpVector| > 0.99) ? ForwardVector : UpVector

Right  = Cross(Forward, ReferenceUp).GetSafeNormal()   // 截面水平轴
Up     = Cross(Right, Forward).GetSafeNormal()          // 截面垂直轴
```

截面圆环上第 `j` 个顶点（0 ≤ j < RadialSegments）：

```
Angle    = (j / RadialSegments) × 2π
Normal   = Right × cos(Angle) + Up × sin(Angle)    // 径向单位向量
Vertex   = Center + Normal × CableRadius
```

对应的 UV：

```
U = i / (PointCount - 1)      // 沿缆线长度方向 [0,1]
V = j / RadialSegments        // 沿截面旋转方向 [0,1]
```

**退化保护**：若 `Tangents` 数量与 `SagPoints` 不一致，或采样点 < 2，`BuildCableVertices()` 直接返回空。

#### 4.7.2 三角索引（BuildCableTriangles）

在相邻截面环 `i` 与 `i+1` 之间，对每个四边形（由 `(i,j)`, `(i,j+1)`, `(i+1,j+1)`, `(i+1,j)` 组成）拆分为两个三角形：

```
Triangle1: (i,j) → (i,j+1) → (i+1,j+1)
Triangle2: (i,j) → (i+1,j+1) → (i+1,j)
```

索引公式（以 `i * RadialSegments + j` 为基础偏移）：

```
Current      = i * RadialSegments + j
CurrentNext  = i * RadialSegments + (j+1) % RadialSegments
Next         = (i+1) * RadialSegments + j
NextNext     = (i+1) * RadialSegments + (j+1) % RadialSegments
```

总三角形数 = `(PointCount - 1) × RadialSegments × 2`，总索引数 = `(PointCount - 1) × RadialSegments × 6`。

**前提条件**：`PointCount ≥ 2` 且 `RadialSegments ≥ 3`，否则跳过构建。

#### 4.7.3 首次创建 vs 增量更新

`GenerateCable()` 通过 `bIsMeshCreated` 标记区分两种路径：

| 路径 | 条件 | 操作 |
|------|------|------|
| 首次创建 | `bIsMeshCreated == false` | `BuildCableVertices()` → `BuildCableTriangles()` → `CreateMeshSection(0, ...)` → `SetMaterial(0, CableMaterial)` → 标记 `bIsMeshCreated = true` |
| 增量更新 | `bIsMeshCreated == true` | `BuildCableVertices()` → `UpdateMeshSection(0, CableVertices, ...)` |

增量路径仅更新顶点/法线/UV，**不重建三角索引**（拓扑不变：采样点数量和截面段数不变时，三角连接关系不变）。这避免了每帧重新计算索引的开销，适合实时拖动端点或调整 `CableLength`/`SagFactor` 的场景。

**注意**：若 `PointCount` 或 `RadialSegments` 通过 `SetPointCount()` 变更，`ResizePoints()` 仅调整中心线数组尺寸。此时需要外部逻辑重置 `bIsMeshCreated = false` 以触发全量重建（当前版本未自动处理此场景，属已知限制）。

---

## 5. 已知限制与待办

| 序号 | 类型 | 描述 | 优先级 |
|------|------|------|--------|
| 1 | 限制 | 碰撞修正仅沿世界Z轴偏移 `CableRadius`，斜面/垂直面会穿模 | 高 |
| 2 | 限制 | `CableLength` 小于端点间距时 SlackLength 为负，未做 clamp 保护 | 高 |
| 3 | 限制 | `PointCount` 或 `RadialSegments` 通过接口变更后，`bIsMeshCreated` 未自动重置，导致三角拓扑与顶点数不匹配。需在 `SetPointCount()` 或属性 setter 中联动重置 | 高 |
| 4 | 待办 | 碰撞通道 `ECC_Visibility` 硬编码，考虑改为可配置属性 `TEnumAsByte<ECollisionChannel>` | 低 |
| 5 | 优化 | ~~Debug 线绘制已移除~~ ✅ 已完成：`DebugDrawCenterline()` 实现中心线 + 端点球绘制，`bIsDrawCenterLine` 开关控制 | — |
| ~~3~~ | ~~限制~~ | ~~`StartComponent`/`EndComponent` 裸指针 GC 风险~~ ✅ 已完成：已改为 `TObjectPtr<USceneComponent>` | — |
| ~~4~~ | ~~待办~~ | ~~Mesh 生成~~ ✅ 已完成：`BuildCableVertices` + `BuildCableTriangles` + `GenerateCable` 管状网格 | — |
| ~~5~~ | ~~待办~~ | ~~`CableMaterial` 应用~~ ✅ 已完成：`SetMaterial` 在首次 `CreateMeshSection` 时自动应用 | — |
| ~~6~~ | ~~待办~~ | ~~`PointCount` 变更时触发数组自动重分配~~ ✅ 已完成：`SetPointCount()` 封装 `ResizePoints()` 自动重建 | — |
| ~~7~~ | ~~优化~~ | ~~`BuildCableVertices` Add() 重复容量检查~~ ✅ 已完成：`SetNum` 预分配 + 下标直接写入，`ResizePoints` 统一管理 6 数组 | — |
| ~~8~~ | ~~优化~~ | ~~`SetNum` 移至 `BeginPlay`~~ ✅ 已完成 | — |
| ~~9~~ | ~~优化~~ | ~~冗余切线归一化、除法→乘法、U 值外提~~ ✅ 已完成：三项微优化合计省 700+ 次冗余运算/帧 | — |

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
| 2026-07-14 | 新增：Mesh 管状网格生成体系 — `BuildCableVertices()` 截面圆环构建 + `BuildCableTriangles()` 三角索引 + `GenerateCable()` 总控 |
| 2026-07-14 | 新增：`bIsMeshCreated` 标记，支持首次 `CreateMeshSection` 全量创建 / 后续 `UpdateMeshSection` 增量更新双路径 |
| 2026-07-14 | 新增：`CableMaterial` 通过 `SetMaterial` 在首次 Mesh 创建时自动应用 |
| 2026-07-14 | 新增：Mesh 缓存成员 — `CableVertices`, `CableNormals`, `CableUVs`, `CableTriangles`, `CableColors`, `CableMeshTangents` |
| 2026-07-14 | 修复：`StartComponent`/`EndComponent` 改用 `TObjectPtr<USceneComponent>`，消除 GC 悬空风险 |
| 2026-07-14 | 新增：`DebugDrawCenterline()` — 中心线段(橙) + 端点球(绿/红)，`bIsDrawCenterLine` 属性开关 |
| 2026-07-14 | 优化：`BuildCableVertices` — `Add()` → `SetNum()` + 下标写入，`ResizePoints` 统一管理 6 数组定容 |
| 2026-07-14 | 优化：消除冗余 `Tangents.GetSafeNormal()`（切线已在 `GenerateCenterline` 归一化） |
| 2026-07-14 | 优化：`BuildCableVertices` 内层循环 — 除法→乘法（`InvRadialSegments`）、U 值外提 |
| 2026-07-14 | 优化：`SagAmount` 公式简化 `CableLength × (Slack/CableLength) × SagFactor` → `SlackLength × SagFactor` |
| 2026-07-14 | 文档：创建 DSL.md 设计文档 |
| 2026-07-14 | 重构：父类从 `UActorComponent` 切换为 `UProceduralMeshComponent`，为 Mesh 生成提供原生 API 支持 |
| 2026-07-14 | 优化：`SetNum` 数组预分配从 `GenerateCenterline()` 移至 `BeginPlay()`，避免每次重算重复分配 |
| 2026-07-14 | 新增：`Tangents` 切线数组 + 中央差分计算，为后续 Mesh 法线提供数据 |
| 2026-07-14 | 新增：`SetPointCount()` / `ResizePoints()` 封装，`PointCount` 改为蓝图只读，通过接口安全修改 |
