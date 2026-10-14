#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EmberLabActor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS()
class EMBERATTRACTOR_API AEmberLabActor : public AActor
{
	GENERATED_BODY()

public:
	AEmberLabActor();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	int32 GetMode() const { return Mode; }
	int32 GetGPUCount() const { return GPUCount; }
	int32 GetCPUCount() const { return CPUCount; }
	float GetAttractionStrength() const { return AttractionStrength; }
	bool IsAttractionActive() const { return bAttractionActive; }
	bool IsAutoRepeatEnabled() const { return bAutoRepeat; }
	bool IsAutoAttractEnabled() const { return bAutoAttract; }
	bool IsCapturing() const { return bCaptureActive; }
	bool HasGPUAsset() const { return bHasGPUAsset; }
	bool HasCPUAsset() const { return bHasCPUAsset; }
	FVector GetAttractorPosition() const { return AttractorPosition; }
	float GetAverageFrameMS() const;

private:
	void HandleControls();
	void UpdateAttractor();
	void SyncConsoleVariables();
	void ApplyNiagaraParameters(bool bForceReset);
	void AdjustCount(int32 Direction);
	void StartCapture(bool bShouldExit, float DurationSeconds);
	void FinishCapture();

	UPROPERTY(VisibleAnywhere, Category="Ember Lab")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="Ember Lab")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category="Ember Lab")
	TObjectPtr<UNiagaraComponent> GPUComponent;

	UPROPERTY(VisibleAnywhere, Category="Ember Lab")
	TObjectPtr<UNiagaraComponent> CPUComponent;

	UPROPERTY(VisibleAnywhere, Category="Ember Lab")
	TObjectPtr<UStaticMeshComponent> Floor;

	UPROPERTY(VisibleAnywhere, Category="Ember Lab")
	TObjectPtr<UStaticMeshComponent> BackWall;

	UPROPERTY(VisibleAnywhere, Category="Ember Lab")
	TObjectPtr<UStaticMeshComponent> AttractorMarker;

	UPROPERTY(VisibleAnywhere, Category="Ember Lab")
	TObjectPtr<UPointLightComponent> AttractorLight;

	UPROPERTY(VisibleAnywhere, Category="Ember Lab")
	TObjectPtr<UDirectionalLightComponent> RoomLight;

	UPROPERTY(EditDefaultsOnly, Category="Ember Lab|Systems")
	TSoftObjectPtr<UNiagaraSystem> GPUSystem;

	UPROPERTY(EditDefaultsOnly, Category="Ember Lab|Systems")
	TSoftObjectPtr<UNiagaraSystem> CPUSystem;

	FVector AttractorPosition = FVector(0.0, 0.0, 200.0);
	int32 Mode = -1;
	int32 GPUCount = -1;
	int32 CPUCount = -1;
	float AttractionStrength = -1.0f;
	bool bAttractionActive = false;
	bool bAutoRepeat = false;
	bool bAutoAttract = false;
	bool bHasGPUAsset = false;
	bool bHasCPUAsset = false;
	bool bCaptureActive = false;
	bool bNativeCSVActive = false;
	bool bExitAfterCapture = false;
	bool bProfilePending = false;
	double LastTickSeconds = 0.0;
	double BeginPlaySeconds = 0.0;
	double LastRepeatSeconds = 0.0;
	double CaptureStartSeconds = 0.0;
	double ProfileWarmupEndSeconds = 0.0;
	float CaptureDurationSeconds = 0.0f;
	float ProfileRequestedDuration = 20.0f;
	int32 CaptureFrameCount = 0;
	int32 PostCaptureExitTicks = 0;
	int32 ScreenshotRequestWindowFrames = 0;
	TArray<double> RecentFrameMS;
	TArray<FString> CaptureRows;
};
