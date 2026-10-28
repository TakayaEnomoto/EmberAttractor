#include "EmberLabActor.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "RenderTimer.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	TAutoConsoleVariable<int32> CVarEmberMode(
		TEXT("ember.Mode"), 3, TEXT("0: baseline, 1: GPU embers, 2: CPU debris, 3: both."));
	TAutoConsoleVariable<int32> CVarEmberGPUCount(
		TEXT("ember.GPUCount"), 100000, TEXT("Target GPU ember count."));
	TAutoConsoleVariable<int32> CVarEmberCPUCount(
		TEXT("ember.CPUCount"), 500, TEXT("Target CPU debris count."));
	TAutoConsoleVariable<float> CVarEmberAttractionStrength(
		TEXT("ember.AttractionStrength"), 4.0f, TEXT("Base black-hole pull: spring stiffness in 1/s^2 inside the pull radius."));
	TAutoConsoleVariable<int32> CVarEmberGame(
		TEXT("ember.Game"), 1, TEXT("1: black-hole round (horizon, energy, timer); 0: lab sandbox with a constant workload."));
	TAutoConsoleVariable<float> CVarEmberRoundSeconds(
		TEXT("ember.RoundSeconds"), 40.0f, TEXT("Round length in seconds."));
	TAutoConsoleVariable<float> CVarEmberTargetMass(
		TEXT("ember.TargetMass"), 35.0f, TEXT("Critical mass needed to win; the released matter is worth 100 when fully hot."));
	TAutoConsoleVariable<float> CVarEmberCoolSeconds(
		TEXT("ember.CoolSeconds"), 35.0f, TEXT("Seconds for released matter to cool to zero value."));
	TAutoConsoleVariable<int32> CVarEmberCPUDynamicBounds(
		TEXT("ember.CPUDynamicBounds"), 0, TEXT("1: CPU debris uses dynamic bounds (optimization 'before' case); 0: fixed bounds."));
	TAutoConsoleVariable<int32> CVarEmberDebrisOnGPU(
		TEXT("ember.DebrisOnGPU"), 0, TEXT("1: debris runs as the GPU port (optimization comparison); 0: CPU simulation."));

	constexpr double AttractorHeight = 200.0;
	constexpr float MaxEnergy = 100.0f;
	constexpr float EnergyDrainPerSecond = 30.0f;
	constexpr float EnergyRegenPerSecond = 14.0f;
	constexpr float EnergyUnlockThreshold = 25.0f;
	constexpr float HorizonBaseRadius = 45.0f;
	constexpr float HorizonRadiusPerMass = 3.5f;
	constexpr float MassForDoublePull = 30.0f;
	constexpr float PullBaseRadius = 350.0f;
	constexpr float PullRadiusPerHorizon = 2.0f;
	constexpr float LabPullRadius = 4000.0f;
	constexpr float VortexAcceleration = 120.0f;
	constexpr int32 GPUExportReservation = 16384;
	constexpr double RepeatIntervalSeconds = 8.0;
	constexpr int32 GPUCountPresets[] = {10000, 25000, 50000, 100000, 200000};
	constexpr int32 CPUCountPresets[] = {500, 1000, 2000, 3500, 5000};

	int32 StepPreset(int32 Current, int32 Direction, const int32* Presets, int32 PresetCount)
	{
		if (Direction > 0)
		{
			for (int32 Index = 0; Index < PresetCount; ++Index)
			{
				if (Presets[Index] > Current)
				{
				return Presets[Index];
				}
			}
			return Presets[PresetCount - 1];
		}

		for (int32 Index = PresetCount - 1; Index >= 0; --Index)
		{
			if (Presets[Index] < Current)
			{
			return Presets[Index];
			}
		}
		return Presets[0];
	}
}

AEmberLabActor::AEmberLabActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("LabCamera"));
	Camera->SetupAttachment(SceneRoot);
	Camera->SetRelativeLocation(FVector(0.0, -1800.0, 1400.0));
	Camera->SetRelativeRotation((FVector(0.0, 0.0, 150.0) - FVector(0.0, -1800.0, 1400.0)).Rotation());
	Camera->SetFieldOfView(65.0f);

	GPUComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("GPUEmbers"));
	GPUComponent->SetupAttachment(SceneRoot);
	GPUComponent->SetRelativeLocation(FVector(0.0, 0.0, AttractorHeight));
	GPUComponent->SetAutoActivate(false);

	CPUComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("CPUDebris"));
	CPUComponent->SetupAttachment(SceneRoot);
	CPUComponent->SetRelativeLocation(FVector(0.0, 0.0, AttractorHeight));
	CPUComponent->SetAutoActivate(false);

	Floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
	Floor->SetupAttachment(SceneRoot);
	Floor->SetRelativeLocation(FVector(0.0, 0.0, -12.0));
	Floor->SetRelativeScale3D(FVector(40.0, 40.0, 0.2));
	Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Floor->SetCollisionResponseToAllChannels(ECR_Block);

	BackWall = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackWall"));
	BackWall->SetupAttachment(SceneRoot);
	BackWall->SetRelativeLocation(FVector(0.0, 2000.0, 1000.0));
	BackWall->SetRelativeScale3D(FVector(40.0, 0.2, 20.0));
	BackWall->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	AttractorMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AttractorMarker"));
	AttractorMarker->SetupAttachment(SceneRoot);
	AttractorMarker->SetRelativeLocation(AttractorPosition);
	AttractorMarker->SetRelativeScale3D(FVector(0.18));
	AttractorMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	AttractorLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("AttractorLight"));
	AttractorLight->SetupAttachment(AttractorMarker);
	AttractorLight->SetLightColor(FLinearColor(1.0f, 0.22f, 0.04f));
	AttractorLight->SetIntensity(20000.0f);
	AttractorLight->SetAttenuationRadius(500.0f);

	RoomLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("RoomLight"));
	RoomLight->SetupAttachment(SceneRoot);
	RoomLight->SetRelativeRotation(FRotator(-48.0, -32.0, 0.0));
	RoomLight->SetLightColor(FLinearColor(0.50f, 0.66f, 1.0f));
	RoomLight->SetIntensity(5.0f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (CubeMesh.Succeeded())
	{
		Floor->SetStaticMesh(CubeMesh.Object);
		BackWall->SetStaticMesh(CubeMesh.Object);
	}
	if (SphereMesh.Succeeded())
	{
		AttractorMarker->SetStaticMesh(SphereMesh.Object);
	}

	GPUSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/EmberLab/NS_Embers_GPU.NS_Embers_GPU")));
	CPUSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/EmberLab/NS_Debris_CPU.NS_Debris_CPU")));
	CPUSystemDynamicBounds = TSoftObjectPtr<UNiagaraSystem>(
		FSoftObjectPath(TEXT("/Game/EmberLab/NS_Debris_CPU_DynamicBounds.NS_Debris_CPU_DynamicBounds")));
	DebrisSystemGPUPort = TSoftObjectPtr<UNiagaraSystem>(
		FSoftObjectPath(TEXT("/Game/EmberLab/NS_Debris_GPUPort.NS_Debris_GPUPort")));
}

void AEmberLabActor::BeginPlay()
{
	Super::BeginPlay();

	GPUAsset = GPUSystem.LoadSynchronous();
	CPUAsset = CPUSystem.LoadSynchronous();
	CPUAssetDynamicBounds = CPUSystemDynamicBounds.LoadSynchronous();
	DebrisAssetGPUPort = DebrisSystemGPUPort.LoadSynchronous();
	bHasGPUAsset = GPUAsset != nullptr;
	bHasCPUAsset = CPUAsset != nullptr;
	if (GPUAsset)
	{
		GPUComponent->SetAsset(GPUAsset);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Ember Lab: missing /Game/EmberLab/NS_Embers_GPU"));
	}
	if (CPUAsset)
	{
		CPUComponent->SetAsset(CPUAsset);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Ember Lab: missing /Game/EmberLab/NS_Debris_CPU"));
	}

	if (UMaterialInterface* MarkerMaterial = AttractorMarker->GetMaterial(0))
	{
		HoleMaterial = UMaterialInstanceDynamic::Create(MarkerMaterial, this);
		HoleMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.0f, 0.0f, 0.0f));
		AttractorMarker->SetMaterial(0, HoleMaterial);
	}

	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		Controller->SetViewTarget(this);
		Controller->SetShowMouseCursor(true);
	}

	int32 RequestedMode = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("EmberMode="), RequestedMode))
	{
		CVarEmberMode->Set(FMath::Clamp(RequestedMode, 0, 3), ECVF_SetByCommandline);
	}
	int32 RequestedCount = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("EmberGPUCount="), RequestedCount))
	{
		CVarEmberGPUCount->Set(RequestedCount, ECVF_SetByCommandline);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("EmberCPUCount="), RequestedCount))
	{
		CVarEmberCPUCount->Set(RequestedCount, ECVF_SetByCommandline);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("EmberDynamicBounds")))
	{
		CVarEmberCPUDynamicBounds->Set(1, ECVF_SetByCommandline);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("EmberDebrisOnGPU")))
	{
		CVarEmberDebrisOnGPU->Set(1, ECVF_SetByCommandline);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("EmberProfile")))
	{
		float WarmupSeconds = 5.0f;
		FParse::Value(FCommandLine::Get(), TEXT("EmberSeconds="), ProfileRequestedDuration);
		FParse::Value(FCommandLine::Get(), TEXT("EmberWarmup="), WarmupSeconds);
		ProfileRequestedDuration = FMath::Clamp(ProfileRequestedDuration, 1.0f, 600.0f);
		bProfilePending = true;
		// Scaling captures need a constant particle population, so the round is off.
		CVarEmberGame->Set(0, ECVF_SetByCommandline);
		ProfileWarmupEndSeconds = FPlatformTime::Seconds() + FMath::Clamp(WarmupSeconds, 0.0f, 120.0f);
	}
	bAutoAttract = FParse::Param(FCommandLine::Get(), TEXT("EmberAutoAttract"));
	FParse::Value(FCommandLine::Get(), TEXT("EmberShotEvery="), ShotIntervalSeconds);
	bDemo = FParse::Param(FCommandLine::Get(), TEXT("EmberDemo"));

	BeginPlaySeconds = FPlatformTime::Seconds();
	DemoPhaseStart = BeginPlaySeconds;
	LastTickSeconds = BeginPlaySeconds;
	LastRepeatSeconds = LastTickSeconds;
	SyncCPUAsset();
	SyncConsoleVariables();
	RestartRound();
}

void AEmberLabActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (PostCaptureExitTicks > 0 && --PostCaptureExitTicks == 0)
	{
		FPlatformMisc::RequestExit(false);
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const double FrameMS = (Now - LastTickSeconds) * 1000.0;
	LastTickSeconds = Now;
	if (FrameMS > 0.0)
	{
		RecentFrameMS.Add(FrameMS);
		if (RecentFrameMS.Num() > 180)
		{
			RecentFrameMS.RemoveAt(0, 1, EAllowShrinking::No);
		}
	}

	HandleControls();
	if (bDemo)
	{
		UpdateDemo(Now);
	}
	SyncCPUAsset();
	SyncConsoleVariables();
	UpdateAttractor();
	UpdateGameplay(DeltaSeconds);

	// Gameplay -> simulation: mass grows the horizon and the pull; energy gates the pull.
	const float Enabled = bAttractionActive ? 1.0f : 0.0f;
	const float MassScale = 1.0f + Mass / MassForDoublePull;
	const float Strength = AttractionStrength * MassScale * Enabled;
	for (UNiagaraComponent* Component : {GPUComponent.Get(), CPUComponent.Get()})
	{
		Component->SetVariablePosition(TEXT("User.AttractorPosition"), AttractorPosition);
		Component->SetVariableFloat(TEXT("User.AttractionEnabled"), Enabled);
		Component->SetVariableFloat(TEXT("User.AttractionStrength"), Strength);
		Component->SetVariableFloat(TEXT("User.VortexStrength"), VortexAcceleration * MassScale * Enabled);
		Component->SetVariableFloat(TEXT("User.PullRadius"),
			bGameEnabled ? PullBaseRadius + PullRadiusPerHorizon * HorizonRadius : LabPullRadius);
		Component->SetVariableFloat(TEXT("User.HorizonRadius"), HorizonRadius);
		Component->SetVariableFloat(TEXT("User.CoolSeconds"), CoolSeconds);
	}
	const float MarkerRadius = FMath::Max(HorizonRadius, 18.0f);
	AttractorMarker->SetRelativeScale3D(FVector(MarkerRadius / 50.0f));
	AttractorLight->SetVisibility(bAttractionActive || bGameEnabled);
	AttractorLight->SetIntensity(bAttractionActive ? 20000.0f : 4000.0f);
	AttractorLight->SetAttenuationRadius(500.0f + 4.0f * HorizonRadius);

	if (bAutoRepeat && !bGameEnabled && Now - LastRepeatSeconds >= RepeatIntervalSeconds)
	{
		ApplyNiagaraParameters(true);
		LastRepeatSeconds = Now;
	}

	if (ShotIntervalSeconds > 0.0f && Now - LastShotSeconds >= ShotIntervalSeconds)
	{
		// Developer aid for checking visuals in unattended runs.
		LastShotSeconds = Now;
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling"), TEXT("Shots"),
			FString::Printf(TEXT("Shot_%03d.png"), ShotIndex++)), true, false);
	}

	if (bProfilePending && Now >= ProfileWarmupEndSeconds)
	{
		bProfilePending = false;
		StartCapture(true, ProfileRequestedDuration);
	}

	if (bCaptureActive)
	{
		if (FrameMS > 0.0)
		{
			++CaptureFrameCount;
			const int32 bScreenshotRequestWindow = ScreenshotRequestWindowFrames > 0 ? 1 : 0;
			ScreenshotRequestWindowFrames = FMath::Max(0, ScreenshotRequestWindowFrames - 1);
			const FString GameThreadMS = GGameThreadTime > 0
				? FString::Printf(TEXT("%.4f"), FPlatformTime::ToMilliseconds(GGameThreadTime)) : FString();
			const FString RenderThreadMS = GRenderThreadTime > 0
				? FString::Printf(TEXT("%.4f"), FPlatformTime::ToMilliseconds(GRenderThreadTime)) : FString();
			CaptureRows.Add(FString::Printf(TEXT("%.4f,%.4f,%.3f,%s,%s,%d,%d,%d,%.3f,%d,%d"),
				Now - CaptureStartSeconds, FrameMS, 1000.0 / FrameMS,
				*GameThreadMS, *RenderThreadMS, Mode, GPUCount, CPUCount,
				AttractionStrength, bAttractionActive ? 1 : 0, bScreenshotRequestWindow));
		}
		const bool bDurationReached = CaptureDurationSeconds > 0.0f
			&& Now - CaptureStartSeconds >= CaptureDurationSeconds;
		const bool bPerfTestSettled = !bExitAfterCapture || CaptureFrameCount >= 302;
		const bool bWatchdogReached = bExitAfterCapture && Now - CaptureStartSeconds >= 90.0;
		if (bDurationReached && (bPerfTestSettled || bWatchdogReached))
		{
			if (bWatchdogReached && !bPerfTestSettled)
			{
				UE_LOG(LogTemp, Warning, TEXT("Ember Lab: profile watchdog ended capture before 300-frame particle perf test completed."));
			}
			FinishCapture();
		}
	}
}

void AEmberLabActor::HandleControls()
{
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (!Controller)
	{
		return;
	}

	if (Controller->WasInputKeyJustPressed(EKeys::Zero)) CVarEmberMode->Set(0, ECVF_SetByConsole);
	if (Controller->WasInputKeyJustPressed(EKeys::One)) CVarEmberMode->Set(1, ECVF_SetByConsole);
	if (Controller->WasInputKeyJustPressed(EKeys::Two)) CVarEmberMode->Set(2, ECVF_SetByConsole);
	if (Controller->WasInputKeyJustPressed(EKeys::Three)) CVarEmberMode->Set(3, ECVF_SetByConsole);
	if (Controller->WasInputKeyJustPressed(EKeys::LeftBracket)) AdjustCount(-1);
	if (Controller->WasInputKeyJustPressed(EKeys::RightBracket)) AdjustCount(1);
	if (Controller->WasInputKeyJustPressed(EKeys::R)) RestartRound();
	if (Controller->WasInputKeyJustPressed(EKeys::F))
	{
		bAutoRepeat = !bAutoRepeat;
		LastRepeatSeconds = FPlatformTime::Seconds();
	}
	if (Controller->WasInputKeyJustPressed(EKeys::C) && !bProfilePending && PostCaptureExitTicks == 0)
	{
		if (bCaptureActive)
		{
			FinishCapture();
		}
		else
		{
			StartCapture(false, 0.0f);
		}
	}
}

void AEmberLabActor::UpdateAttractor()
{
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (!Controller)
	{
		bAttractionActive = false;
		return;
	}

	if (UpdateDemoAttractor())
	{
		return;
	}
	const bool bRoundAllowsPull = !bGameEnabled || (RoundState == EEmberRoundState::Playing && !bPullLocked);
	bAttractionActive = Controller->IsInputKeyDown(EKeys::LeftMouseButton) && Mode != 0 && bRoundAllowsPull;
	if (bAutoAttract)
	{
		const double Elapsed = FPlatformTime::Seconds() - BeginPlaySeconds;
		AttractorPosition.X = 600.0 * FMath::Cos(Elapsed * 0.6);
		AttractorPosition.Y = 450.0 * FMath::Sin(Elapsed * 0.9);
		AttractorMarker->SetWorldLocation(AttractorPosition);
		bAttractionActive = Mode != 0 && bRoundAllowsPull;
		return;
	}
	FVector RayOrigin;
	FVector RayDirection;
	if (Controller->DeprojectMousePositionToWorld(RayOrigin, RayDirection) && !FMath::IsNearlyZero(RayDirection.Z))
	{
		const double Distance = (AttractorHeight - RayOrigin.Z) / RayDirection.Z;
		if (Distance > 0.0)
		{
			const FVector Intersection = RayOrigin + RayDirection * Distance;
			AttractorPosition.X = FMath::Clamp(Intersection.X, -1800.0, 1800.0);
			AttractorPosition.Y = FMath::Clamp(Intersection.Y, -1800.0, 1800.0);
			AttractorMarker->SetWorldLocation(AttractorPosition);
		}
	}
}

void AEmberLabActor::SyncConsoleVariables()
{
	const int32 NewMode = FMath::Clamp(CVarEmberMode.GetValueOnGameThread(), 0, 3);
	const int32 NewGPUCount = FMath::Clamp(CVarEmberGPUCount.GetValueOnGameThread(), 0, 16000000);
	const int32 NewCPUCount = FMath::Clamp(CVarEmberCPUCount.GetValueOnGameThread(), 0, 100000);
	const float NewStrength = FMath::Max(0.0f, CVarEmberAttractionStrength.GetValueOnGameThread());
	const bool bNewGame = CVarEmberGame.GetValueOnGameThread() != 0;
	const bool bNeedsReset = NewGPUCount != GPUCount || NewCPUCount != CPUCount;
	const bool bRoundChanged = NewMode != Mode || bNeedsReset || bNewGame != bGameEnabled;
	const bool bChanged = bRoundChanged || NewStrength != AttractionStrength;
	const bool bFirstSync = Mode < 0;
	Mode = NewMode;
	GPUCount = NewGPUCount;
	CPUCount = NewCPUCount;
	AttractionStrength = NewStrength;
	bGameEnabled = bNewGame;
	TargetMass = FMath::Max(1.0f, CVarEmberTargetMass.GetValueOnGameThread());
	CoolSeconds = FMath::Max(1.0f, CVarEmberCoolSeconds.GetValueOnGameThread());
	if (bRoundChanged && !bFirstSync)
	{
		// The matter supply changed, so the old round's mass and timer are meaningless.
		RestartRound();
	}
	else if (bChanged)
	{
		ApplyNiagaraParameters(bNeedsReset);
	}
}

namespace EmberDemo
{
	// Phase order of the scripted showcase; each phase logs its start for video editing.
	enum : int32 { Warmup, WinRound, WinHold, ColdRound, ColdHold, GPUMillion, GPUFourMillion, CPUDebris, DebrisPort, Done };
}

void AEmberLabActor::SetDemoPhase(int32 Phase, double Now)
{
	DemoPhase = Phase;
	DemoPhaseStart = Now;
	UE_LOG(LogTemp, Display, TEXT("Ember Demo: phase %d at %.3f"), Phase, Now - BeginPlaySeconds);
	auto Console = [this](const TCHAR* Command) { GEngine->Exec(GetWorld(), Command); };
	switch (Phase)
	{
	case EmberDemo::WinRound:
		RestartRound();
		break;
	case EmberDemo::ColdRound:
		// A short, passive round so the matter visibly goes cold before critical mass.
		CVarEmberCoolSeconds->Set(10.0f, ECVF_SetByConsole);
		CVarEmberRoundSeconds->Set(13.0f, ECVF_SetByConsole);
		CoolSeconds = 10.0f;
		RestartRound();
		break;
	case EmberDemo::GPUMillion:
		CVarEmberCoolSeconds->Set(35.0f, ECVF_SetByConsole);
		CVarEmberRoundSeconds->Set(40.0f, ECVF_SetByConsole);
		CVarEmberGame->Set(0, ECVF_SetByConsole);
		CVarEmberMode->Set(1, ECVF_SetByConsole);
		CVarEmberGPUCount->Set(1000000, ECVF_SetByConsole);
		// A stronger pull than the game default so the scale shots visibly swirl.
		CVarEmberAttractionStrength->Set(14.0f, ECVF_SetByConsole);
		bAutoAttract = true;
		Console(TEXT("stat fps"));
		Console(TEXT("stat unit"));
		Console(TEXT("fx.Niagara.Debug.Hud Enabled=1 OverviewEnabled=1 OverviewLocation=1250,40"));
		break;
	case EmberDemo::GPUFourMillion:
		CVarEmberGPUCount->Set(4000000, ECVF_SetByConsole);
		break;
	case EmberDemo::CPUDebris:
		CVarEmberMode->Set(2, ECVF_SetByConsole);
		CVarEmberCPUCount->Set(5000, ECVF_SetByConsole);
		break;
	case EmberDemo::DebrisPort:
		CVarEmberDebrisOnGPU->Set(1, ECVF_SetByConsole);
		CVarEmberCPUCount->Set(20000, ECVF_SetByConsole);
		break;
	case EmberDemo::Done:
		FPlatformMisc::RequestExit(false);
		break;
	default:
		break;
	}
}

void AEmberLabActor::UpdateDemo(double Now)
{
	const double Elapsed = Now - DemoPhaseStart;
	const bool bRoundOver = RoundState != EEmberRoundState::Playing;
	switch (DemoPhase)
	{
	case EmberDemo::Warmup:         if (Elapsed > 5.0) SetDemoPhase(EmberDemo::WinRound, Now); break;
	case EmberDemo::WinRound:       if (bRoundOver || Elapsed > 40.0) SetDemoPhase(EmberDemo::WinHold, Now); break;
	case EmberDemo::WinHold:        if (Elapsed > 4.0) SetDemoPhase(EmberDemo::ColdRound, Now); break;
	case EmberDemo::ColdRound:      if (bRoundOver || Elapsed > 15.0) SetDemoPhase(EmberDemo::ColdHold, Now); break;
	case EmberDemo::ColdHold:       if (Elapsed > 3.0) SetDemoPhase(EmberDemo::GPUMillion, Now); break;
	case EmberDemo::GPUMillion:     if (Elapsed > 9.0) SetDemoPhase(EmberDemo::GPUFourMillion, Now); break;
	case EmberDemo::GPUFourMillion: if (Elapsed > 9.0) SetDemoPhase(EmberDemo::CPUDebris, Now); break;
	case EmberDemo::CPUDebris:      if (Elapsed > 9.0) SetDemoPhase(EmberDemo::DebrisPort, Now); break;
	case EmberDemo::DebrisPort:     if (Elapsed > 9.0) SetDemoPhase(EmberDemo::Done, Now); break;
	default: break;
	}
}

bool AEmberLabActor::UpdateDemoAttractor()
{
	if (!bDemo || DemoPhase > EmberDemo::ColdHold)
	{
		return false;
	}
	// Sweep an outward spiral over the debris field. In the winning round the pull is used
	// in bursts (stop near empty, resume at half a tank); the cold round never pulls.
	const double Elapsed = FPlatformTime::Seconds() - DemoPhaseStart;
	const double Radius = 120.0 + 520.0 * FMath::Min(Elapsed / 24.0, 1.0);
	const double Angle = Elapsed * 1.15;
	AttractorPosition.X = Radius * FMath::Cos(Angle);
	AttractorPosition.Y = Radius * FMath::Sin(Angle);
	AttractorMarker->SetWorldLocation(AttractorPosition);
	if (DemoPhase == EmberDemo::WinRound && RoundState == EEmberRoundState::Playing && !bPullLocked)
	{
		bDemoPulling = bDemoPulling ? Energy > 8.0f : Energy >= 55.0f;
	}
	else
	{
		bDemoPulling = false;
	}
	bAttractionActive = bDemoPulling;
	return true;
}

void AEmberLabActor::SyncCPUAsset()
{
	const bool bWantGPUPort = CVarEmberDebrisOnGPU.GetValueOnGameThread() != 0 && DebrisAssetGPUPort;
	const bool bWantDynamic = !bWantGPUPort && CVarEmberCPUDynamicBounds.GetValueOnGameThread() != 0 && CPUAssetDynamicBounds;
	UNiagaraSystem* Desired = bWantGPUPort ? DebrisAssetGPUPort.Get()
		: bWantDynamic ? CPUAssetDynamicBounds.Get() : CPUAsset.Get();
	if (!Desired || CPUComponent->GetAsset() == Desired)
	{
		return;
	}
	bDynamicBounds = bWantDynamic;
	const bool bWasActive = CPUComponent->IsActive();
	CPUComponent->DeactivateImmediate();
	CPUComponent->SetAsset(Desired);
	UE_LOG(LogTemp, Display, TEXT("Ember Lab: CPU debris uses %s"), *Desired->GetName());
	if (bWasActive)
	{
		ApplyNiagaraParameters(true);
	}
}

void AEmberLabActor::RestartRound()
{
	RoundState = EEmberRoundState::Playing;
	Mass = 0.0f;
	Energy = MaxEnergy;
	bPullLocked = false;
	TimeRemaining = FMath::Max(5.0f, CVarEmberRoundSeconds.GetValueOnGameThread());
	HorizonRadius = bGameEnabled ? HorizonBaseRadius : 0.0f;
	ConsumedGPU = 0;
	ConsumedCPU = 0;
	SaturatedReadbacks = 0;
	ApplyNiagaraParameters(true);
}

void AEmberLabActor::UpdateGameplay(float DeltaSeconds)
{
	if (!bGameEnabled)
	{
		HorizonRadius = 0.0f;
		return;
	}
	if (RoundState == EEmberRoundState::Playing)
	{
		if (bAttractionActive)
		{
			Energy = FMath::Max(0.0f, Energy - EnergyDrainPerSecond * DeltaSeconds);
			if (Energy <= 0.0f)
			{
				bPullLocked = true;
			}
		}
		else
		{
			Energy = FMath::Min(MaxEnergy, Energy + EnergyRegenPerSecond * DeltaSeconds);
			if (bPullLocked && Energy >= EnergyUnlockThreshold)
			{
				bPullLocked = false;
			}
		}
		TimeRemaining = FMath::Max(0.0f, TimeRemaining - DeltaSeconds);
		if (Mass >= TargetMass)
		{
			RoundState = EEmberRoundState::Won;
		}
		else if (TimeRemaining <= 0.0f)
		{
			RoundState = EEmberRoundState::Lost;
		}
	}
	HorizonRadius = HorizonBaseRadius + HorizonRadiusPerMass * Mass;

	const double Now = FPlatformTime::Seconds();
	if (Now - LastStatusLogSeconds >= 5.0)
	{
		LastStatusLogSeconds = Now;
		UE_LOG(LogTemp, Display, TEXT("Ember Lab: round=%d mass=%.2f/%.0f energy=%.0f time=%.1f horizon=%.0f consumed GPU=%d CPU=%d saturated=%d"),
			static_cast<int32>(RoundState), Mass, TargetMass, Energy, TimeRemaining, HorizonRadius,
			ConsumedGPU, ConsumedCPU, SaturatedReadbacks);
	}
}

void AEmberLabActor::ReceiveParticleData_Implementation(const TArray<FBasicParticleData>& Data,
	UNiagaraSystem* NiagaraSystem, const FVector& SimulationPositionOffset)
{
	// "GPU" here means the dust system; the debris GPU port still counts as debris.
	const bool bFromGPU = NiagaraSystem != nullptr && NiagaraSystem == GPUAsset;
	if (bFromGPU)
	{
		ConsumedGPU += Data.Num();
		if (Data.Num() >= GPUExportReservation)
		{
			++SaturatedReadbacks;
		}
	}
	else
	{
		ConsumedCPU += Data.Num();
	}
	if (!bGameEnabled || RoundState != EEmberRoundState::Playing)
	{
		return;
	}

	// All released matter is worth 100 mass when hot; each system's share is spread over
	// its particle count so the round plays the same at every count preset.
	const bool bBoth = (Mode & 3) == 3;
	const float Share = bBoth ? (bFromGPU ? 60.0f : 40.0f) : 100.0f;
	const int32 Count = bFromGPU ? GPUCount : CPUCount;
	if (Count <= 0)
	{
		return;
	}
	const float ValuePerParticle = Share / Count;
	float Gained = 0.0f;
	for (const FBasicParticleData& Particle : Data)
	{
		// Size carries Particles.Age; matter cools linearly to zero value.
		Gained += ValuePerParticle * FMath::Clamp(1.0f - Particle.Size / CoolSeconds, 0.0f, 1.0f);
	}
	Mass += Gained;
}

void AEmberLabActor::ApplyNiagaraParameters(bool bForceReset)
{
	GPUComponent->SetVariableObject(TEXT("User.ExportHandler"), this);
	CPUComponent->SetVariableObject(TEXT("User.ExportHandler"), this);
	GPUComponent->SetVariableFloat(TEXT("User.HorizonRadius"), HorizonRadius);
	CPUComponent->SetVariableFloat(TEXT("User.HorizonRadius"), HorizonRadius);
	GPUComponent->SetVariableInt(TEXT("User.ParticleCount"), GPUCount);
	CPUComponent->SetVariableInt(TEXT("User.ParticleCount"), CPUCount);
	const float AppliedStrength = bAttractionActive ? AttractionStrength : 0.0f;
	GPUComponent->SetVariableFloat(TEXT("User.AttractionStrength"), AppliedStrength);
	CPUComponent->SetVariableFloat(TEXT("User.AttractionStrength"), AppliedStrength);
	GPUComponent->SetVariablePosition(TEXT("User.AttractorPosition"), AttractorPosition);
	CPUComponent->SetVariablePosition(TEXT("User.AttractorPosition"), AttractorPosition);

	const bool bGPUWanted = bHasGPUAsset && (Mode & 1) != 0;
	const bool bCPUWanted = bHasCPUAsset && (Mode & 2) != 0;
	if (bGPUWanted)
	{
		if (!GPUComponent->IsActive()) GPUComponent->Activate(true);
		else if (bForceReset) GPUComponent->ReinitializeSystem();
	}
	else if (GPUComponent->IsActive())
	{
		GPUComponent->DeactivateImmediate();
	}
	if (bCPUWanted)
	{
		if (!CPUComponent->IsActive()) CPUComponent->Activate(true);
		else if (bForceReset) CPUComponent->ReinitializeSystem();
	}
	else if (CPUComponent->IsActive())
	{
		CPUComponent->DeactivateImmediate();
	}
}

void AEmberLabActor::AdjustCount(int32 Direction)
{
	const int32 CurrentMode = FMath::Clamp(CVarEmberMode.GetValueOnGameThread(), 0, 3);
	if ((CurrentMode & 1) != 0)
	{
		CVarEmberGPUCount->Set(StepPreset(CVarEmberGPUCount.GetValueOnGameThread(), Direction,
			GPUCountPresets, UE_ARRAY_COUNT(GPUCountPresets)), ECVF_SetByConsole);
	}
	if ((CurrentMode & 2) != 0)
	{
		CVarEmberCPUCount->Set(StepPreset(CVarEmberCPUCount.GetValueOnGameThread(), Direction,
			CPUCountPresets, UE_ARRAY_COUNT(CPUCountPresets)), ECVF_SetByConsole);
	}
}

void AEmberLabActor::StartCapture(bool bShouldExit, float DurationSeconds)
{
	bCaptureActive = true;
	bExitAfterCapture = bShouldExit;
	CaptureDurationSeconds = DurationSeconds;
	CaptureStartSeconds = FPlatformTime::Seconds();
	CaptureFrameCount = 0;
	CaptureRows.Reset();
	CaptureRows.Add(TEXT("elapsed_s,frame_wall_ms,fps,game_thread_ms,render_thread_ms,mode,gpu_target_count,cpu_target_count,attraction_strength,attraction_enabled,screenshot_request_window"));
	ScreenshotRequestWindowFrames = 3;
	bNativeCSVActive = false;
	if (IConsoleVariable* DetailedStats = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.DetailedCSVStats")))
	{
		DetailedStats->Set(1, ECVF_SetByConsole);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Ember Lab: fx.DetailedCSVStats is unavailable; per-system CSV columns may be absent."));
	}
	if (IConsoleVariable* BlockOnEnd = IConsoleManager::Get().FindConsoleVariable(TEXT("csv.BlockOnCaptureEnd")))
	{
		BlockOnEnd->Set(1, ECVF_SetByConsole);
	}
	if (GEngine)
	{
		GEngine->Exec(GetWorld(), TEXT("NiagaraDebugHud 3"));
		bNativeCSVActive = GEngine->Exec(GetWorld(), TEXT("CsvProfile START"));
		if (bNativeCSVActive)
		{
			UE_LOG(LogTemp, Display, TEXT("Ember Lab: Unreal CSV profiler started; output under %sCSV/"), *FPaths::ProfilingDir());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Ember Lab: Unreal CSV profiler did not accept CsvProfile START."));
		}
	}
	const FString ScreenshotFolder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling"));
	IFileManager::Get().MakeDirectory(*ScreenshotFolder, true);
	const FString ScreenshotPath = FPaths::Combine(ScreenshotFolder,
		FString::Printf(TEXT("EmberLab_%s_mode%d.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")), Mode));
	FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
	if (bShouldExit && GEngine)
	{
		GEngine->Exec(GetWorld(), TEXT("fx.ParticlePerfStats.RunTest 300 1 1 1"));
	}
	UE_LOG(LogTemp, Display, TEXT("Ember Lab: frame-time capture started (mode %d)."), Mode);
}

void AEmberLabActor::FinishCapture()
{
	if (!bCaptureActive)
	{
		return;
	}
	bCaptureActive = false;
	if (bNativeCSVActive && GEngine)
	{
		GEngine->Exec(GetWorld(), TEXT("CsvProfile STOP"));
		bNativeCSVActive = false;
		UE_LOG(LogTemp, Display, TEXT("Ember Lab: Unreal CSV profiler stop requested; output under %sCSV/"), *FPaths::ProfilingDir());
	}
	const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling"));
	IFileManager::Get().MakeDirectory(*Folder, true);
	const FString Filename = FString::Printf(TEXT("EmberLab_%s_mode%d.csv"),
		*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")), Mode);
	const FString Path = FPaths::Combine(Folder, Filename);
	const FString Contents = FString::Join(CaptureRows, TEXT("\n")) + TEXT("\n");
	const bool bSaved = FFileHelper::SaveStringToFile(Contents, *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (bSaved)
	{
		UE_LOG(LogTemp, Display, TEXT("Ember Lab: saved %d frame-time samples to %s"),
			CaptureRows.Num() - 1, *Path);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Ember Lab: could not save frame-time capture to %s"), *Path);
	}
	CaptureRows.Reset();
	if (bExitAfterCapture && bSaved)
	{
		PostCaptureExitTicks = 3;
	}
	else
	{
		bExitAfterCapture = false;
	}
}

float AEmberLabActor::GetAverageFrameMS() const
{
	if (RecentFrameMS.IsEmpty())
	{
		return 0.0f;
	}
	double Sum = 0.0;
	for (const double Sample : RecentFrameMS)
	{
		Sum += Sample;
	}
	return static_cast<float>(Sum / RecentFrameMS.Num());
}
