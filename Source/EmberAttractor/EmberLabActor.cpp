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
		TEXT("ember.AttractionStrength"), 350.0f, TEXT("Ember attraction strength."));

	constexpr double AttractorHeight = 200.0;
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
}

void AEmberLabActor::BeginPlay()
{
	Super::BeginPlay();

	UNiagaraSystem* GPUAsset = GPUSystem.LoadSynchronous();
	UNiagaraSystem* CPUAsset = CPUSystem.LoadSynchronous();
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
	if (FParse::Param(FCommandLine::Get(), TEXT("EmberProfile")))
	{
		float WarmupSeconds = 5.0f;
		FParse::Value(FCommandLine::Get(), TEXT("EmberSeconds="), ProfileRequestedDuration);
		FParse::Value(FCommandLine::Get(), TEXT("EmberWarmup="), WarmupSeconds);
		ProfileRequestedDuration = FMath::Clamp(ProfileRequestedDuration, 1.0f, 600.0f);
		bProfilePending = true;
		ProfileWarmupEndSeconds = FPlatformTime::Seconds() + FMath::Clamp(WarmupSeconds, 0.0f, 120.0f);
	}
	bAutoAttract = FParse::Param(FCommandLine::Get(), TEXT("EmberAutoAttract"));

	BeginPlaySeconds = FPlatformTime::Seconds();
	LastTickSeconds = BeginPlaySeconds;
	LastRepeatSeconds = LastTickSeconds;
	SyncConsoleVariables();
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
	SyncConsoleVariables();
	UpdateAttractor();
	GPUComponent->SetVariablePosition(TEXT("User.AttractorPosition"), AttractorPosition);
	CPUComponent->SetVariablePosition(TEXT("User.AttractorPosition"), AttractorPosition);
	const float Enabled = bAttractionActive ? 1.0f : 0.0f;
	GPUComponent->SetVariableFloat(TEXT("User.AttractionEnabled"), Enabled);
	CPUComponent->SetVariableFloat(TEXT("User.AttractionEnabled"), Enabled);
	GPUComponent->SetVariableFloat(TEXT("User.AttractionStrength"), AttractionStrength * Enabled);
	CPUComponent->SetVariableFloat(TEXT("User.AttractionStrength"), AttractionStrength * Enabled);
	AttractorLight->SetVisibility(bAttractionActive);

	if (bAutoRepeat && Now - LastRepeatSeconds >= RepeatIntervalSeconds)
	{
		ApplyNiagaraParameters(true);
		LastRepeatSeconds = Now;
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
	if (Controller->WasInputKeyJustPressed(EKeys::R)) ApplyNiagaraParameters(true);
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

	bAttractionActive = Controller->IsInputKeyDown(EKeys::LeftMouseButton) && Mode != 0;
	if (bAutoAttract)
	{
		const double Elapsed = FPlatformTime::Seconds() - BeginPlaySeconds;
		AttractorPosition.X = 600.0 * FMath::Cos(Elapsed * 0.6);
		AttractorPosition.Y = 450.0 * FMath::Sin(Elapsed * 0.9);
		AttractorMarker->SetWorldLocation(AttractorPosition);
		bAttractionActive = Mode != 0;
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
	const int32 NewGPUCount = FMath::Clamp(CVarEmberGPUCount.GetValueOnGameThread(), 0, 1000000);
	const int32 NewCPUCount = FMath::Clamp(CVarEmberCPUCount.GetValueOnGameThread(), 0, 100000);
	const float NewStrength = FMath::Max(0.0f, CVarEmberAttractionStrength.GetValueOnGameThread());
	const bool bNeedsReset = NewGPUCount != GPUCount || NewCPUCount != CPUCount;
	const bool bChanged = NewMode != Mode || bNeedsReset || NewStrength != AttractionStrength;
	Mode = NewMode;
	GPUCount = NewGPUCount;
	CPUCount = NewCPUCount;
	AttractionStrength = NewStrength;
	if (bChanged)
	{
		ApplyNiagaraParameters(bNeedsReset);
	}
}

void AEmberLabActor::ApplyNiagaraParameters(bool bForceReset)
{
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
