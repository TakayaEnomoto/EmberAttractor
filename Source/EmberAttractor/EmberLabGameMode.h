#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "EmberLabGameMode.generated.h"

UCLASS()
class EMBERATTRACTOR_API AEmberLabGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AEmberLabGameMode();
	virtual void StartPlay() override;
};
