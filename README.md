# UmbilicalCable — 脐带缆插件

UE5 Runtime 插件，基于悬链线（Catenary）数学模型的三维线缆中心线生成，支持可选的环境碰撞检测与管状 Mesh 生成。

## 快速开始

```
BeginPlay → SetTargetPoint(Start, End) → UpdateCable()
```

## 核心特性

- **悬链线模型**：正弦近似，`SagFactor` 可调节硬度
- **碰撞检测**：`LineTraceSingleByChannel` + 法线偏移，支持斜面/垂直面
- **局部加权平滑**：碰撞点固定约束，邻域非碰撞点迭代平滑
- **管状 Mesh**：基于 `UProceduralMeshComponent`，支持增量更新
- **事件驱动**：仅在 `UpdateCable()` 调用时计算，无 Tick 开销
- **蓝图友好**：所有公共接口暴露给蓝图

## 架构

```
GenerateCenterline()             ← 总调度器
├── DetectCollisions()           ← 垂链曲线 + LineTrace 碰撞
├── ApplySmoothing()             ← 碰撞区域局部加权平滑
└── ComputeTangents()            ← 中心差分切线计算
         ↓
GenerateCable()                  ← Mesh 构建
├── BuildCableVertices()         ← 截面圆环顶点
└── BuildCableTriangles()        ← 三角索引
```

## 完整文档

详见 [DSL.md](DSL.md)

## 版本

v1.1 — 2026-07-14
