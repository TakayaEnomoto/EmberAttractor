#pragma once

#include "Commandlets/Commandlet.h"
#include "BuildEmberLabCommandlet.generated.h"

/** Creates the Niagara systems and the playable demonstration map. */
UCLASS()
class UBuildEmberLabCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
