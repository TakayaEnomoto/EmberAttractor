#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiagaraDataInterfaceExport.h"
#include "EmberLabActor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UENUM()
enum class EEmberRoundState : uint8
{
	Playing,
	Won,
	Lost
};

UCLASS()
class EMBERATTRACTOR_API AEmberLabActor : public AActor, public INiagaraParticleCallbackHandler
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

	bool IsGameEnabled() const { return bGameEnabled; }
	EEmberRoundState GetRoundState() const { return RoundState; }
	float GetMass() const { return Mass; }
	float GetTargetMass() const { return TargetMass; }
	float GetEnergy() const { return Energy; }
	bool IsPullLocked() const { return bPullLocked; }
	float GetTimeRemaining() const { return TimeRemaining; }
	float GetHorizonRadius() const { return HorizonRadius; }
	int32 GetConsumedGPU() const { return ConsumedGPU; }
	int32 GetConsumedCPU() const { return ConsumedCPU; }
	int32 GetSaturatedReadbacks() const { return SaturatedReadbacks; }

	/** Event-horizon export from both Niagara systems (GPU data arrives after readback latency). */
	virtual void ReceiveParticleData_Implementation(const TArray<FBasicParticleData>& Data,
		UNiagaraSystem* NiagaraSystem, const FVector& SimulationPositionOffset) override;

private:
	void HandleControls();
	void UpdateAttractor();
	void SyncConsoleVariables();
	void ApplyNiagaraParameters(bool bForceReset);
	void AdjustCount(int32 Direction);
	void StartCapture(bool bShouldExit, float DurationSeconds);
	void RestartRound();
	void UpdateGameplay(float DeltaSeconds);
	void SyncCPUAsset();
	void UpdateDemo(double Now);
	void SetDemoPhase(int32 Phase, double Now);
	bool UpdateDemoAttractor();
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

	/** Same CPU system with dynamic bounds: the "before" case of the bounds optimization. */
	UPROPERTY(EditDefaultsOnly, Category="Ember Lab|Systems")
	TSoftObjectPtr<UNiagaraSystem> CPUSystemDynamicBounds;

	/** Same debris emitter simulated on the GPU: the CPU-to-GPU port comparison. */
	UPROPERTY(EditDefaultsOnly, Category="Ember Lab|Systems")
	TSoftObjectPtr<UNiagaraSystem> DebrisSystemGPUPort;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> DebrisAssetGPUPort;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> GPUAsset;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> CPUAsset;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> CPUAssetDynamicBounds;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HoleMaterial;

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
	double LastStatusLogSeconds = 0.0;
	double LastShotSeconds = 0.0;
	float ShotIntervalSeconds = 0.0f;
	int32 ShotIndex = 0;

	// -EmberDemo: an unattended, scripted showcase used to record the portfolio video.
	bool bDemo = false;
	bool bDemoPulling = false;
	int32 DemoPhase = 0;
	double DemoPhaseStart = 0.0;
	double ProfileWarmupEndSeconds = 0.0;
	float CaptureDurationSeconds = 0.0f;
	float ProfileRequestedDuration = 20.0f;
	int32 CaptureFrameCount = 0;
	int32 PostCaptureExitTicks = 0;
	int32 ScreenshotRequestWindowFrames = 0;
	TArray<double> RecentFrameMS;

	bool bGameEnabled = true;
	bool bDynamicBounds = false;
	EEmberRoundState RoundState = EEmberRoundState::Playing;
	float Mass = 0.0f;
	float TargetMass = 35.0f;
	float Energy = 100.0f;
	bool bPullLocked = false;
	float TimeRemaining = 0.0f;
	float HorizonRadius = 0.0f;
	float CoolSeconds = 35.0f;
	int32 ConsumedGPU = 0;
	int32 ConsumedCPU = 0;
	int32 SaturatedReadbacks = 0;
	float ConsumeFlash = 0.0f;
	TArray<FString> CaptureRows;
};
