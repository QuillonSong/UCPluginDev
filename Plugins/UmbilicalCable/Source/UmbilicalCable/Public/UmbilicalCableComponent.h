// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "UmbilicalCableComponent.generated.h"

/**
 * 
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UMBILICALCABLE_API UUmbilicalCableComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

protected:
	// 组件初始化时预分配采样点数组，避免每次 UpdateCable 重复分配
	virtual void BeginPlay() override;
	
public:
	// 中心线采样点数量。决定曲线精度和 Mesh 分段数量
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="4", ClampMax="512"))
	int32 PointCount = 32;
	// 线缆截面段数
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="4", ClampMax="16"))
	int32 RadialSegments = 12;
	// 脐带缆实际长度，单位cm（UE默认单位）
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CableLength = 50000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CableRadius = 16.f;
	//松弛度默认0.5；0.3较硬缆线，0.7较软缆线
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float SagFactor = 0.5f;
	//线缆材质
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UMaterialInterface> CableMaterial;
	//启用碰撞
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool EnableCollision = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bIsDrawCenterLine = true;
	
	
	
	//设置端点
	UFUNCTION(BlueprintCallable, Category = UmbilicalCable)
	void SetTargetPoint(USceneComponent* StartPoint, USceneComponent* EndPoint);
	//更新线缆
	UFUNCTION(BlueprintCallable, Category = UmbilicalCable)
	bool UpdateCable();
	UFUNCTION(BlueprintCallable, Category = UmbilicalCable)
	void SetPointCount(int32 NewPointCount);
	
	
private:
	TObjectPtr<USceneComponent> StartComponent;
	TObjectPtr<USceneComponent> EndComponent;
	//松弛长度
	float SlackLength;
	//插值点位
	TArray<FVector> LinearPoints;
	//垂链点位
	TArray<FVector> SagPoints;
	//切线方向存储
	TArray<FVector> Tangents;
	//已构建线缆模型
	bool bIsMeshCreated = false;
	// Mesh顶点缓存
	TArray<FVector> CableVertices;
	// Mesh法线缓存
	TArray<FVector> CableNormals;
	// Mesh UV缓存
	TArray<FVector2D> CableUVs;
	// Mesh三角索引
	TArray<int32> CableTriangles;
	// 顶点颜色，不使用时保持为空
	TArray<FColor> CableColors;
	// 切线，不使用时保持为空
	TArray<FProcMeshTangent> CableMeshTangents;
	
	//调整Poitns数组尺寸
	void ResizePoints();
	//构建中心线
	void GenerateCenterline();
	// 碰撞检测：垂链曲线生成 + LineTrace 碰撞修正
	void DetectCollisions(const FVector& Start, const FVector& End, float SagAmount,
		UWorld* World, int32 LastIdx, TArray<bool>& OutCollisionHit);
	// 平滑计算：碰撞区域邻域加权平滑，碰撞点固定
	void ApplySmoothing(const TArray<bool>& bCollisionHit, UWorld* World, int32 LastIdx);
	// 切线计算：端点单侧差分 + 中间中央差分
	void ComputeTangents(int32 LastIdx);
	//构建模型顶点
	void BuildCableVertices();
	//构建三角索引
	void BuildCableTriangles();
	//构建线缆模型
	void GenerateCable();
	//[DEBUG]
	void DebugDrawCenterline();
	
	
};
