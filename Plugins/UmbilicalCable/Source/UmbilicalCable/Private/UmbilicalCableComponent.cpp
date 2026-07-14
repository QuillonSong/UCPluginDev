// Fill out your copyright notice in the Description page of Project Settings.


#include "UmbilicalCableComponent.h"

void UUmbilicalCableComponent::BeginPlay()
{
	Super::BeginPlay();
	ResizePoints();
}

void UUmbilicalCableComponent::SetTargetPoint(USceneComponent* StartPoint, USceneComponent* EndPoint)
{
	StartComponent = StartPoint;
	EndComponent = EndPoint;
}

bool UUmbilicalCableComponent::UpdateCable()
{
	if (!IsValid(StartComponent) || !IsValid(EndComponent))
	{
		return false;
	}

	// 预检：端点间距超过 CableLength 时 SlackLength 归零，线缆绷直拉紧
	const float RawSlack = CableLength - FVector::Dist(
		StartComponent->GetComponentLocation(), EndComponent->GetComponentLocation());
	if (RawSlack < 0.f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[UmbilicalCable] 线缆长度不足！CableLength=%.1f，端点间距=%.1f，缺少=%.1f——将以直线拉直"),
			CableLength, CableLength + RawSlack, -RawSlack);
	}

	GenerateCenterline();
	if (bIsDrawCenterLine)
		DebugDrawCenterline();
	GenerateCable();
	return true;
}

void UUmbilicalCableComponent::SetPointCount(int32 NewPointCount)
{
	// 约束在 [4, 512] 区间内，与 UPROPERTY Clamp 同步，防止运行时传入极端值
	PointCount = FMath::Clamp(NewPointCount, 4, 512);
	ResizePoints();
	// 采样点数量变更后三角拓扑不再匹配，强制下次走全量重建（修复DSL限制#3）
	bIsMeshCreated = false;
}

void UUmbilicalCableComponent::ResizePoints()
{
	// 预分配中心线与Mesh数组容量，之后 BuildCableVertices 直接下标写入
	const int32 VertexCount = PointCount * RadialSegments;
	LinearPoints.SetNum(PointCount);
	SagPoints.SetNum(PointCount);
	Tangents.SetNum(PointCount);
	CableVertices.SetNum(VertexCount);
	CableNormals.SetNum(VertexCount);
	CableUVs.SetNum(VertexCount);
}

// ============================================================================
// GenerateCenterline — 中心线生成总调度
// ============================================================================
void UUmbilicalCableComponent::GenerateCenterline()
{
	if (!IsValid(StartComponent) || !IsValid(EndComponent))
	{
		return;
	}

	// [INFO]:初始化参数
	const FVector StartPoint = StartComponent->GetComponentLocation();
	const FVector EndPoint = EndComponent->GetComponentLocation();
	// P1: 防御——线缆绷紧时 SlackLength 可能为负，导致 SagAmount < 0 使曲线向上拱起
	SlackLength = FMath::Max(0.f, CableLength - FVector::Dist(StartPoint, EndPoint));
	// CableLength * (SlackLength / CableLength) 数学上等价于 SlackLength
	const float SagAmount = SlackLength * SagFactor;

	// 缓存World指针：组件未注册或已销毁时 GetWorld() 返回 nullptr
	UWorld* World = GetWorld();

	// [INFO]:首尾端点直接赋值，不做插值与碰撞检测
	const int32 LastIdx = PointCount - 1;
	LinearPoints[0] = StartPoint;
	SagPoints[0] = StartPoint;
	LinearPoints[LastIdx] = EndPoint;
	SagPoints[LastIdx] = EndPoint;

	// 1. 碰撞检测：垂链曲线生成 + LineTrace 碰撞修正
	TArray<bool> bCollisionHit;
	DetectCollisions(StartPoint, EndPoint, SagAmount, World, LastIdx, bCollisionHit);

	// 2. 平滑计算：碰撞区域邻域加权平滑（碰撞点自身固定）
	ApplySmoothing(bCollisionHit, World, LastIdx);

	// 3. 切线计算：中心差分 + 端点单侧差分
	ComputeTangents(LastIdx);
}

// ============================================================================
// DetectCollisions — 垂链曲线生成 + 碰撞检测
// ============================================================================
void UUmbilicalCableComponent::DetectCollisions(
	const FVector& Start, const FVector& End, float SagAmount,
	UWorld* World, int32 LastIdx, TArray<bool>& OutCollisionHit)
{
	// 惰性分配碰撞标记数组
	if (EnableCollision && World)
	{
		OutCollisionHit.SetNumZeroed(PointCount);
	}

	for (int32 i = 1; i < LastIdx; ++i)
	{
		const float Alpha = static_cast<float>(i) / LastIdx;
		const FVector LinearPoint = FMath::Lerp(Start, End, Alpha);
		LinearPoints[i] = LinearPoint;
		SagPoints[i] = FVector(
			LinearPoint.X,
			LinearPoint.Y,
			LinearPoint.Z - SagAmount * FMath::Sin(Alpha * UE_PI)
		);

		// 碰撞检测：命中时将垂链点修正至障碍物表面
		if (EnableCollision && World)
		{
			FHitResult HitResult;
			if (World->LineTraceSingleByChannel(
				HitResult,
				LinearPoints[i],
				SagPoints[i],
				ECC_Visibility
			))
			{
				// 沿表面法线偏移线缆半径，避免嵌入（修复DSL限制#1）
				SagPoints[i] = HitResult.Location + HitResult.Normal * CableRadius;
				OutCollisionHit[i] = true;
			}
		}
	}
}

// ============================================================================
// ApplySmoothing — 碰撞区域局部加权平滑
// ============================================================================
void UUmbilicalCableComponent::ApplySmoothing(
	const TArray<bool>& bCollisionHit, UWorld* World, int32 LastIdx)
{
	if (!EnableCollision || !World || bCollisionHit.Num() == 0)
	{
		return;
	}

	// 检查是否存在碰撞命中
	bool bAnyCollision = false;
	for (int32 i = 1; i < LastIdx; ++i)
	{
		if (bCollisionHit[i])
		{
			bAnyCollision = true;
			break;
		}
	}

	if (!bAnyCollision)
	{
		return;
	}

	// 平滑影响半径：至少3个邻点，与采样密度联动缩放
	const int32 SmoothRadius = FMath::Max(3, PointCount / 16);
	constexpr int32 SmoothIterations = 2; // 固定2轮迭代，不依赖 DeltaTime

	// P0: 构建平滑掩码——碰撞命中点 ± SmoothRadius 范围内的非碰撞采样点标记为待平滑
	// 碰撞命中点自身排除，作为固定约束节点不被平滑移动
	TArray<bool> bSmoothMask;
	bSmoothMask.SetNumZeroed(PointCount);
	for (int32 i = 1; i < LastIdx; ++i)
	{
		if (bCollisionHit[i])
		{
			const int32 WinStart = FMath::Max(1, i - SmoothRadius);
			const int32 WinEnd   = FMath::Min(LastIdx - 1, i + SmoothRadius);
			for (int32 w = WinStart; w <= WinEnd; ++w)
			{
				// P0: 碰撞命中点自身不参与平滑，保持障碍物表面约束
				if (!bCollisionHit[w])
				{
					bSmoothMask[w] = true;
				}
			}
		}
	}

	// 迭代局部加权平滑，核权重 [0.25, 0.5, 0.25]
	// 工作缓冲避免同迭代内读写污染
	TArray<FVector> WorkBuffer = SagPoints;
	for (int32 Iter = 0; Iter < SmoothIterations; ++Iter)
	{
		for (int32 i = 1; i < LastIdx; ++i)
		{
			if (bSmoothMask[i])
			{
				WorkBuffer[i] = SagPoints[i - 1] * 0.25f
				              + SagPoints[i]     * 0.5f
				              + SagPoints[i + 1] * 0.25f;
			}
		}
		// 本轮结果写回，供下一轮迭代读取
		for (int32 i = 1; i < LastIdx; ++i)
		{
			if (bSmoothMask[i])
			{
				SagPoints[i] = WorkBuffer[i];
			}
		}
	}
}

// ============================================================================
// ComputeTangents — 中心差分切线计算
// ============================================================================
void UUmbilicalCableComponent::ComputeTangents(int32 LastIdx)
{
	for (int32 i = 0; i < PointCount; ++i)
	{
		if (i == 0)
		{
			Tangents[i] =
				(SagPoints[1] - SagPoints[0]).GetSafeNormal();
		}
		else if (i == LastIdx)
		{
			Tangents[i] =
				(SagPoints[LastIdx] - SagPoints[LastIdx - 1]).GetSafeNormal();
		}
		else
		{
			Tangents[i] =
				(SagPoints[i + 1] - SagPoints[i - 1]).GetSafeNormal();
		}
	}
}


void UUmbilicalCableComponent::BuildCableVertices()
{
	const int32 PointNum = SagPoints.Num();

	if (PointNum < 2 || Tangents.Num() != PointNum)
	{
		return;
	}

	// 内层循环常量提前至外层，避免每顶点重复转型与除法
	const float InvRadialSegments = 1.0f / static_cast<float>(RadialSegments);
	const float InvPointNumMinus1 = 1.0f / static_cast<float>(PointNum - 1);

	// SagPoints 为世界空间坐标，ProceduralMesh 需要组件本地空间——逆变换一次即可
	const FTransform InvTransform = GetComponentTransform().Inverse();

	for (int32 i = 0; i < PointNum; ++i)
	{
		const FVector Center = InvTransform.TransformPosition(SagPoints[i]);
		// 切线已在 GenerateCenterline 中归一化，无需重复 GetSafeNormal
		const FVector Forward = Tangents[i];
		// 防止Forward接近Z轴时叉乘退化
		FVector ReferenceUp = FVector::UpVector;
		if (FMath::Abs(
			FVector::DotProduct(
				Forward,
				ReferenceUp)) > 0.99f)
		{
			ReferenceUp = FVector::ForwardVector;
		}

		// 构造缆线截面的局部坐标系
		const FVector Right =
			FVector::CrossProduct(
				Forward,
				ReferenceUp)
			.GetSafeNormal();

		const FVector Up =
			FVector::CrossProduct(
				Right,
				Forward)
			.GetSafeNormal();

		// U 值在同一截面环内不变，提到内层循环外
		const float U = static_cast<float>(i) * InvPointNumMinus1;

		for (int32 j = 0; j < RadialSegments; ++j)
		{
			const float SegmentAlpha = static_cast<float>(j) * InvRadialSegments;
			const float Angle = SegmentAlpha * UE_TWO_PI;

			const float CosValue = FMath::Cos(Angle);
			const float SinValue = FMath::Sin(Angle);

			// 当前圆周方向
			const FVector Normal =
				Right * CosValue +
				Up * SinValue;

			// 圆环顶点
			const FVector Vertex =
				Center +
				Normal * CableRadius;

			// 扁平化下标：第 i 个截面环的第 j 个顶点
			const int32 VertIdx = i * RadialSegments + j;

			CableVertices[VertIdx] = Vertex;
			CableNormals[VertIdx] = Normal;

			// U沿缆线长度方向，V沿截面旋转方向
			CableUVs[VertIdx] = FVector2D(U, SegmentAlpha);
		}
	}
}

void UUmbilicalCableComponent::BuildCableTriangles()
{
	CableTriangles.Reset();

	const int32 PointNum = SagPoints.Num();

	if (PointNum < 2 || RadialSegments < 3)
	{
		return;
	}

	// 每两个截面环之间生成一圈面
	const int32 QuadCount =
		(PointNum - 1) * RadialSegments;

	// 一个四边形拆成两个三角形
	CableTriangles.Reserve(QuadCount * 6);


	for (int32 i = 0; i < PointNum - 1; ++i)
	{
		for (int32 j = 0; j < RadialSegments; ++j)
		{
			// 当前环顶点
			const int32 Current =
				i * RadialSegments + j;


			// 当前环下一个截面点
			const int32 CurrentNext =
				i * RadialSegments +
				((j + 1) % RadialSegments);


			// 下一个环对应点
			const int32 Next =
				(i + 1) * RadialSegments + j;


			// 下一个环对应下一个点
			const int32 NextNext =
				(i + 1) * RadialSegments +
				((j + 1) % RadialSegments);



			// 三角形1
			CableTriangles.Add(Current);
			CableTriangles.Add(CurrentNext);
			CableTriangles.Add(NextNext);


			// 三角形2
			CableTriangles.Add(Current);
			CableTriangles.Add(NextNext);
			CableTriangles.Add(Next);
		}
	}
}

void UUmbilicalCableComponent::GenerateCable()
{
	// 根据当前SagPoints和Tangents生成顶点、法线、UV
	BuildCableVertices();

	if (bIsMeshCreated)
	{
		// Mesh拓扑未变化，只更新顶点数据
		UpdateMeshSection(
			0,
			CableVertices,
			CableNormals,
			CableUVs,
			CableColors,
			CableMeshTangents
		);
	}
	else
	{
		// 首次生成，需要创建三角索引
		BuildCableTriangles();
		// 首次构建模型
		CreateMeshSection(
			0,
			CableVertices,
			CableTriangles,
			CableNormals,
			CableUVs,
			CableColors,
			CableMeshTangents,
			false
		);

		// 设置材质
		if (CableMaterial)
		{
			SetMaterial(0, CableMaterial);
		}

		bIsMeshCreated = true;
	}
}

void UUmbilicalCableComponent::DebugDrawCenterline()
{
	// 中心线采样点不足，无需绘制
	if (SagPoints.Num() < 2)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	constexpr float DebugDuration = 5.0f;      // 持久时长：事件驱动调用，需跨帧可见
	constexpr float LineThickness = 2.0f;       // 线宽
	const float SphereRadius = CableRadius; // 端点球大小与线缆半径一致，直观对照（运行时成员，非 constexpr）
	constexpr uint8 DepthPriority = 0;          // 世界深度测试，不强制前景

	const FColor CenterlineColor = FColor::Orange;  // 垂链曲线
	const FColor StartColor = FColor::Green;         // 起点
	const FColor EndColor = FColor::Red;             // 终点

	const int32 LastIdx = SagPoints.Num() - 1;

	// 逐段绘制中心线
	for (int32 i = 0; i < LastIdx; ++i)
	{
		DrawDebugLine(
			World,
			SagPoints[i],
			SagPoints[i + 1],
			CenterlineColor,
			false,             // bPersistentLines：false，依赖 Duration 控制生命周期
			DebugDuration,
			DepthPriority,
			LineThickness
		);
	}

	// 起点球标记
	DrawDebugSphere(
		World,
		SagPoints[0],
		SphereRadius,
		8,                   // 分段数，8面足够标识
		StartColor,
		false,
		DebugDuration,
		DepthPriority
	);

	// 终点球标记
	DrawDebugSphere(
		World,
		SagPoints[LastIdx],
		SphereRadius,
		8,
		EndColor,
		false,
		DebugDuration,
		DepthPriority
	);
}
