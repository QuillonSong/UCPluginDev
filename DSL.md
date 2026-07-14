# UmbilicalCable 插件设计文档（DSL）

> **版本**：1.2 | **模块类型**：Runtime | **依赖**：Core, CoreUObject, Engine
> 最后更新：2026-07-14

---

## 1. 概述

**UmbilicalCable**（脐带缆/垂链线缆）是一个 UE5 Runtime 插件，提供基于正弦垂链（Sinusoidal Sag）数学模型的三维线缆中心线生成能力。通过指定起止端点和物理参数，自动计算线缆的垂链曲线，并支持可选的环境碰撞检测。

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
| 单一职责 | `GenerateCenterline` 作为调度器，具体 Pass 由独立私有方法实现 |

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
| `CableRadius` | `float` | 16.0 | cm | 线缆半径。碰撞检测命中后沿表面法线偏移此值，避免嵌入 |
| `SagFactor` | `float` | 0.5 | 无 | 松弛度系数。0.3=较硬缆线，0.7=较软缆线 |
| `CableMaterial` | `UMaterialInterface*` | — | — | 线缆材质，配合父类 `SetMaterial` 应用到生成的 Mesh Section |
| `EnableCollision` | `bool` | true | — | 是否对每个中段采样点执行 `LineTraceSingleByChannel` 碰撞检测 |
| `bIsDrawCenterLine` | `bool` | true | — | 是否在 `UpdateCable()` 时绘制调试中心线。调用 `DebugDrawCenterline()` 渲染橙色曲线+端点球，持续 5 秒 |

#### 运行时数据（私有）

| 成员 | 类型 | 说明 |
|------|------|------|
| `LinearPoints` | `TArray<FVector>` | 起止点直线插值采样点。`[0] = StartPoint`, `[N-1] = EndPoint` |
| `SagPoints` | `TArray<FVector>` | 垂链曲线采样点。在 `LinearPoints` 基础上施加 sin 下垂，并经碰撞修正和平滑处理 |
| `Tangents` | `TArray<FVector>` | 每个采样点的切线方向。中央差分计算（端点用单侧差分） |
| `SlackLength` | `float` | 松弛长度 = `max(0, CableLength - 直线距离)`，驱动下垂量计算。已做负值防御 |
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
| `SetPointCount(NewPointCount)` | ✅ | 修改采样点数，内部 `FMath::Clamp(NewPointCount, 4, 512)` 约束有效范围，自动调用 `ResizePoints()` 重建三个点数组 |
| `UpdateCable()` | ✅ | 触发中心线重算 + Mesh 重建。返回 `bool`：`false` 表示 Start/End 组件无效。线缆长度不足时仅输出 Warning 日志，继续以直线绷直生成 |

### 3.3 私有方法

#### 调度器

| 方法 | 说明 |
|------|------|
| `GenerateCenterline()` | **中心线生成总调度**。依次调用：参数初始化 → 端点赋值 → `DetectCollisions()` → `ApplySmoothing()` → `ComputeTangents()` |

#### 碰撞检测

| 方法 | 说明 |
|------|------|
| `DetectCollisions(Start, End, SagAmount, World, LastIdx, OutCollisionHit)` | 遍历中段采样点，生成垂链曲线并执行 `LineTraceSingleByChannel` 碰撞检测。命中时沿 `HitResult.Normal` 偏移 `CableRadius` 修正点位置。输出 `OutCollisionHit` 标记数组供平滑 Pass 使用 |

#### 平滑处理

| 方法 | 说明 |
|------|------|
| `ApplySmoothing(bCollisionHit, World, LastIdx)` | 对碰撞命中区域邻域的非碰撞采样点执行局部加权平滑（三点高斯核 [0.25, 0.5, 0.25]，固定 2 轮迭代）。碰撞命中点自身作为固定约束节点不参与平滑 |

#### 切线计算

| 方法 | 说明 |
|------|------|
| `ComputeTangents(LastIdx)` | 基于 `SagPoints` 计算所有采样点的切线方向。端点使用单侧差分（前向/后向），中间点使用中央差分 |

#### 网格生成与调试

| 方法 | 说明 |
|------|------|
| `ResizePoints()` | 统一调整 6 个数组（`LinearPoints`/`SagPoints`/`Tangents`/`CableVertices`/`CableNormals`/`CableUVs`）为 `PointCount` 或 `PointCount × RadialSegments` 大小。在 `BeginPlay` 和 `SetPointCount` 中被调用。`BuildCableVertices` 直接下标写入，不再自行分配 |
| `BuildCableVertices()` | 基于 `SagPoints`（世界空间） + `Tangents` 构建管状网格顶点。通过 `GetComponentTransform().Inverse()` 将世界空间中心线转换到组件本地空间，确保 Mesh 跟随 Actor 位置。逐采样点构造截面圆环，直接下标写入预分配数组 |
| `BuildCableTriangles()` | 构建三角索引。在相邻截面环之间生成四边形（拆为两个三角形），输出 `CableTriangles`。仅在首次 `GenerateCable()` 时调用（拓扑不变） |
| `GenerateCable()` | Mesh 生成总控。首次调用 `BuildCableVertices` → `BuildCableTriangles` → `CreateMeshSection` + `SetMaterial`；后续仅 `BuildCableVertices` → `UpdateMeshSection` 增量更新顶点 |
| `DebugDrawCenterline()` | 调试可视化。`DrawDebugLine` 逐段绘制中心线（橙色） + `DrawDebugSphere` 标记起止端点（绿/红），持续 5 秒。仅在 `bIsDrawCenterLine == true` 时调用 |

---

## 4. 算法详解：正弦垂链中心线生成

### 4.1 松弛量计算

```
SlackLength = max(0, CableLength - |StartPoint - EndPoint|)
```

- **已修复**：`SlackLength` 使用 `FMath::Max(0.f, ...)` 防御，负值被 clamp 为 0，防止线缆反向拱起。
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

> **公式简化**：`CableLength × (SlackLength / CableLength) × SagFactor` → `SlackLength × SagFactor`，`CableLength` 乘除抵消。

**正弦模型的合理性**：
- 0~π 的 sin 曲线在两端点为0、中间峰值，准确描述垂链下垂的对称形态
- 相比 `cosh` 避免了双曲函数计算开销
- `SagFactor` 提供艺术化调整自由度

### 4.3 端点处理

```cpp
LinearPoints[0]       = SagPoints[0]       = StartPoint;
LinearPoints[N-1]     = SagPoints[N-1]     = EndPoint;
```

端点不做插值（Alpha=0/1，sin=0，无下垂），不执行碰撞检测（末点射线长度为零）。

### 4.4 碰撞检测与修正

由 `DetectCollisions()` 实现：

```
对每个中段采样点：
    从 LinearPoint[i] 向 SagPoint[i] 发射 ECC_Visibility 射线
    命中 → SagPoint[i] = HitResult.Location + HitResult.Normal × CableRadius
         + 标记 OutCollisionHit[i] = true
    未命中 → 保持正弦计算值
```

- 碰撞偏移沿 `HitResult.Normal` 方向（非硬编码 Z 轴），支持斜面/垂直面障碍物
- 碰撞标记数组由函数输出，供后续平滑 Pass 使用

### 4.5 局部加权平滑

由 `ApplySmoothing()` 实现。用于消除碰撞修正导致的曲率突变（碰撞段与自由悬垂段衔接处出现折角）。

**核心约束**：碰撞命中点自身作为固定约束节点，**不参与平滑**。仅对其邻域内的非碰撞采样点做加权平滑。

#### 4.5.1 平滑窗口

碰撞命中点 ± SmoothRadius 范围内的非碰撞采样点参与平滑，端点（i=0, i=N-1）永不参与：

```
SmoothRadius = Max(3, PointCount / 16)
```

#### 4.5.2 平滑掩码构建

```
对每个碰撞命中点 i：
    窗口 [i - SmoothRadius, i + SmoothRadius] 内
    若 !bCollisionHit[w] → bSmoothMask[w] = true  // 仅标记非碰撞点
```

#### 4.5.3 平滑核

采用三点高斯加权核，权重 [0.25, 0.5, 0.25]：

```
Smoothed[i] = SagPoints[i-1] × 0.25 + SagPoints[i] × 0.5 + SagPoints[i+1] × 0.25
```

#### 4.5.4 迭代策略

固定 2 轮迭代，**不依赖 DeltaTime**，单次 `UpdateCable()` 调用内完成。使用工作缓冲（`WorkBuffer`）避免同迭代内读写污染——每轮从 `SagPoints` 读取、写入 `WorkBuffer`，轮末批量写回。

> **设计动因**：选择单次固定迭代而非逐帧 `VInterpTo` 是为了保持 **事件驱动架构**——线缆仅在 `UpdateCable()` 被调用时重算，禁止无意义 Tick。

### 4.6 切线计算

由 `ComputeTangents()` 实现。基于平滑后的 `SagPoints` 逐点计算切线方向（供后续 Mesh 法线/管线朝向使用）：

```
对所有采样点 i（0 ≤ i ≤ N-1）：
    i == 0       → Tangent[i] = (SagPoints[1] - SagPoints[0]).GetSafeNormal()          // 前向差分
    i == N-1     → Tangent[i] = (SagPoints[N-1] - SagPoints[N-2]).GetSafeNormal()      // 后向差分
    其他          → Tangent[i] = (SagPoints[i+1] - SagPoints[i-1]).GetSafeNormal()     // 中央差分
```

### 4.7 数据流

```
BeginPlay → ResizePoints()           // 6 数组预分配（仅一次）
                ├─ LinearPoints / SagPoints / Tangents → SetNum(PointCount)
                └─ CableVertices / CableNormals / CableUVs → SetNum(PointCount × RadialSegments)

SetPointCount(N) → ResizePoints()    // 运行时改 PointCount 自动重分配所有数组

SetTargetPoint() → UpdateCable() → GenerateCenterline()      ← 总调度器
                                      │
                                      ├─ [有效性质检] StartComponent/EndComponent 有效性
                                      ├─ [参数计算] StartPoint, EndPoint, SlackLength, SagAmount
                                      ├─ [端点赋值] idx=0 和 idx=N-1 直接设值
                                      │
                                      ├─ DetectCollisions()                        ← 碰撞检测
                                      │    ├─ Lerp 直线插值 → LinearPoints[i]
                                      │    ├─ Sin 下垂偏移 → SagPoints[i]
                                      │    └─ LineTrace 碰撞 → 法线偏移修正 + 标记 OutCollisionHit[i]
                                      │
                                      ├─ ApplySmoothing(bCollisionHit)             ← 平滑处理
                                      │    ├─ 有碰撞 → 构建平滑掩码（排除碰撞点）
                                      │    │         → 迭代加权平滑(×2)
                                      │    └─ 无碰撞 → 直接返回
                                      │
                                      └─ ComputeTangents(LastIdx)                  ← 切线计算
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

### 4.8 Mesh 管状网格生成

在中心线计算完成后，`GenerateCable()` 基于 `SagPoints` + `Tangents` + `CableRadius` + `RadialSegments` 构建管状（圆柱面）网格。

#### 4.8.1 顶点构建（BuildCableVertices）

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

#### 4.8.2 三角索引（BuildCableTriangles）

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

#### 4.8.3 首次创建 vs 增量更新

`GenerateCable()` 通过 `bIsMeshCreated` 标记区分两种路径：

| 路径 | 条件 | 操作 |
|------|------|------|
| 首次创建 | `bIsMeshCreated == false` | `BuildCableVertices()` → `BuildCableTriangles()` → `CreateMeshSection(0, ...)` → `SetMaterial(0, CableMaterial)` → 标记 `bIsMeshCreated = true` |
| 增量更新 | `bIsMeshCreated == true` | `BuildCableVertices()` → `UpdateMeshSection(0, CableVertices, ...)` |

增量路径仅更新顶点/法线/UV，**不重建三角索引**（拓扑不变：采样点数量和截面段数不变时，三角连接关系不变）。这避免了每帧重新计算索引的开销，适合实时拖动端点或调整 `CableLength`/`SagFactor` 的场景。

**注意**：`SetPointCount()` 变更采样点数量后，`bIsMeshCreated` 已自动重置为 `false`，下次 `GenerateCable()` 将走全量重建路径（修复 DSL 限制#3）。

---

## 5. 已知限制与待办

| 序号 | 类型 | 描述 | 优先级 |
|------|------|------|--------|
| ~~1~~ | ~~限制~~ | ~~碰撞修正仅沿世界Z轴偏移 `CableRadius`，斜面/垂直面会穿模~~ ✅ 已完成：改为 `HitResult.Normal * CableRadius` 法线偏移 | — |
| ~~2~~ | ~~限制~~ | ~~`CableLength` 小于端点间距时 SlackLength 为负，未做 clamp 保护~~ ✅ 已完成：`FMath::Max(0.f, ...)` 防御 | — |
| ~~3~~ | ~~限制~~ | ~~`PointCount` 变更后 `bIsMeshCreated` 未自动重置，三角拓扑与顶点数不匹配~~ ✅ 已完成：`SetPointCount()` 中自动重置 `bIsMeshCreated = false` | — |
| 4 | 待办 | 碰撞通道 `ECC_Visibility` 硬编码，考虑改为可配置属性 `TEnumAsByte<ECollisionChannel>` | 低 |
| 5 | 优化 | Debug 线绘制已移除 ✅ 已完成 | — |
| 6 | 已知 | 碰撞投影为硬投影（点吸附至障碍物表面），多个连续碰撞点可能产生点密度坍缩 | 中 |
| 7 | 已知 | 管体截面 Frame 基于独立 `ReferenceUp` 计算，切线接近竖直时可能翻转 | 低 |
| 8 | 待办 | 可扩展 Parallel Transport Frame 或距离松弛 Pass（在 `GenerateCenterline` 调度器中插入新 Pass 即可） | 低 |

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
| 2026-07-13 | 初始实现：`GenerateCenterline` 垂链计算 + 碰撞检测 |
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
| 2026-07-14 | 修复（DSL#1）：碰撞偏移改用 `HitResult.Normal * CableRadius` 法线方向，替代硬编码Z轴偏移，支持斜面/垂直障碍物 |
| 2026-07-14 | 新增：碰撞段局部加权平滑 — 三点高斯核 [0.25,0.5,0.25] + 固定2轮迭代，消除碰撞修正导致的曲率突变 |
| 2026-07-14 | 修复（DSL#3）：`SetPointCount()` 中自动重置 `bIsMeshCreated = false`，强制下次 `GenerateCable()` 全量重建三角拓扑 |
| 2026-07-14 | 修复（DSL#2）：`SlackLength` 增加 `FMath::Max(0.f, ...)` 负值防御，线缆绷紧时不再反向拱起 |
| 2026-07-14 | 修复：平滑掩码排除碰撞命中点自身，碰撞点作为固定约束节点不参与平滑，消除碰撞区域衔接折角 |
| 2026-07-14 | 重构：`GenerateCenterline` 拆分为调度器 + 三个独立 Pass — `DetectCollisions()` / `ApplySmoothing()` / `ComputeTangents()`，每个 Pass 可独立测试和扩展 |
| 2026-07-14 | 修复：`BuildCableVertices` 增加世界空间→组件本地空间坐标变换（`GetComponentTransform().Inverse()`），修复 BP_Cable 不在场景原点时 Mesh 位置偏移 |
| 2026-07-14 | 新增：`UpdateCable()` 返回 `bool`，组件无效返回 `false`；线缆长度不足时输出 Warning 日志但继续以直线绷直生成 |
| 2026-07-14 | 新增：`PointCount` Clamp [4, 512]、`RadialSegments` Clamp [4, 16]，防止编辑器输入极端值导致冻结 |
| 2026-07-14 | 修正：术语"悬链线/Catenary"→"正弦垂链/Sinusoidal Sag"，准确反映 `sin` 模型本质 |
| 2026-07-14 | 修复：`SetPointCount()` 增加运行时 `FMath::Clamp(NewPointCount, 4, 512)`，同步 UPROPERTY Clamp 约束 |
