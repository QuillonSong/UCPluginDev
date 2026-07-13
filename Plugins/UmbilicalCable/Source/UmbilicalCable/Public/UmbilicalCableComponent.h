// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UmbilicalCableComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UMBILICALCABLE_API UUmbilicalCableComponent : public UActorComponent
{
	GENERATED_BODY()

	
public:
	// Sets default values for this component's properties
	UUmbilicalCableComponent();
	// 起点目标组件
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	USceneComponent* StartComponent;
	// 终点目标组件
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	USceneComponent* EndComponent;
	// 中心线采样点数量。决定曲线精度和 Mesh 分段数量
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
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
	UMaterialInterface* CableMaterial;
	//启用碰撞
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool EnableCollision = true;
	
	
	
	
	

	//设置端点
	UFUNCTION(BlueprintCallable, Category = UmbilicalCable)
	void SetTargetPoint(USceneComponent* StartPoint, USceneComponent* EndPoint);
	//更新线缆
	UFUNCTION(BlueprintCallable, Category = UmbilicalCable)
	void UpdateCable();
	
private:
	//松弛长度
	float SlackLength;
	//插值点位
	TArray<FVector> LinearPoints;
	//垂链点位
	TArray<FVector> SagPoints;
	
	
	void GenerateCenterline();
	
		
	
protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;
};
