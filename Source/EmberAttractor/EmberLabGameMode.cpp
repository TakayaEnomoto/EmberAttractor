#include "EmberLabGameMode.h"

#include "EmberLabActor.h"
#include "EmberLabHUD.h"
#include "EngineUtils.h"
#include "Engine/World.h"

AEmberLabGameMode::AEmberLabGameMode()
{
	HUDClass = AEmberLabHUD::StaticClass();
	DefaultPawnClass = nullptr;
}

void AEmberLabGameMode::StartPlay()
{
	Super::StartPlay();

	for (TActorIterator<AEmberLabActor> It(GetWorld()); It; ++It)
	{
		return;
	}
	GetWorld()->SpawnActor<AEmberLabActor>(AEmberLabActor::StaticClass(), FTransform::Identity);
}
