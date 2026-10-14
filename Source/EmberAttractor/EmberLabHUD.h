#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "EmberLabHUD.generated.h"

UCLASS()
class EMBERATTRACTOR_API AEmberLabHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
