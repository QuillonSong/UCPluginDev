// Fill out your copyright notice in the Description page of Project Settings.

#include "UmbilicalCableComponent.h"


// Sets default values for this component's properties
UUmbilicalCableComponent::UUmbilicalCableComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


void UUmbilicalCableComponent::SetTargetPoint(USceneComponent* StartPoint, USceneComponent* EndPoint)
{
	StartComponent = StartPoint;
	EndComponent = EndPoint;
}

void UUmbilicalCableComponent::UpdateCable()
{
	GenerateCenterline();
}

void UUmbilicalCableComponent::GenerateCenterline()
{
	if (!IsValid(StartComponent) || !IsValid(EndComponent))
	{
		return;
	}

	// 预分配插值数组容量，避免后续索引越界（PointCount 可在运行时通过蓝图调整）
	LinearPoints.SetNum(PointCount);
	SagPoints.SetNum(PointCount);

	// 初始化参数
	const FVector StartPoint = StartComponent->GetComponentLocation();
	const FVector EndPoint = EndComponent->GetComponentLocation();
	SlackLength = CableLength - FVector::Dist(StartPoint, EndPoint);
	const float SagAmount = CableLength * (SlackLength / CableLength) * SagFactor;

	// 缓存World指针：组件未注册或已销毁时 GetWorld() 返回 nullptr
	UWorld* World = GetWorld();

	// 首尾端点直接赋值，不做插值与碰撞检测
	const int32 LastIdx = PointCount - 1;
	LinearPoints[0] = StartPoint;
	SagPoints[0] = StartPoint;
	LinearPoints[LastIdx] = EndPoint;
	SagPoints[LastIdx] = EndPoint;

	// 遍历中间采样点，逐点计算垂链与碰撞
	for (int32 i = 1; i < LastIdx; ++i)
	{
		const float Alpha = static_cast<float>(i) / LastIdx;
		const FVector LinearPoint = FMath::Lerp(StartPoint, EndPoint, Alpha);
		LinearPoints[i] = LinearPoint;
		SagPoints[i] = FVector(
			LinearPoint.X,
			LinearPoint.Y,
			LinearPoint.Z - SagAmount * FMath::Sin(Alpha * UE_PI)
		);
		// 碰撞检测：命中时将垂链点修正至碰撞位置，使线缆贴合障碍物表面
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
				// 沿Z轴抬高线缆半径，避免嵌入碰撞体表面
				SagPoints[i] = FVector(HitResult.Location.X, HitResult.Location.Y, HitResult.Location.Z + CableRadius);
			}
		}
	}
}

// Called when the game starts
void UUmbilicalCableComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...

}


// Called every frame
void UUmbilicalCableComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                             FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}
