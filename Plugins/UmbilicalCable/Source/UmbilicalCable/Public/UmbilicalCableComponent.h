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
	// 起点目标组件
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<USceneComponent> StartComponent;
	// 终点目标组件
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<USceneComponent> EndComponent;
	// 中心线采样点数量。决定曲线精度和 Mesh 分段数量
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 PointCount = 32;
	// 线缆截面段数
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 RadialSegments = 12;
	// 脐带缆实际长度，单位cm（UE默认单位）
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CableLength = 50000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CableRadius = 16.f;
	//松弛度默认0.5；0.3较硬缆线，0.7较软缆线
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SagFactor = 0.5f;
	//线缆材质
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UMaterialInterface> CableMaterial;
	//启用碰撞
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool EnableCollision = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsDrawCenterLine = true;
	
	
	
	//设置端点
	UFUNCTION(BlueprintCallable, Category = UmbilicalCable)
	void SetTargetPoint(USceneComponent* StartPoint, USceneComponent* EndPoint);
	//更新线缆
	UFUNCTION(BlueprintCallable, Category = UmbilicalCable)
	void UpdateCable();
	UFUNCTION(BlueprintCallable, Category = UmbilicalCable)
	void SetPointCount(int32 NewPointCount);
	
	
private:
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
	//构建模型顶点
	void BuildCableVertices();
	//构建三角索引
	void BuildCableTriangles();
	//构建线缆模型
	void GenerateCable();
	//[DEBUG]
	void DebugDrawCenterline();
	
	
};
