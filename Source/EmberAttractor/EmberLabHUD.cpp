#include "EmberLabHUD.h"

#include "EmberLabActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

void AEmberLabHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine)
	{
		return;
	}

	AEmberLabActor* Lab = nullptr;
	for (TActorIterator<AEmberLabActor> It(GetWorld()); It; ++It)
	{
		Lab = *It;
		break;
	}
	if (!Lab)
	{
		return;
	}

	const float PanelX = 30.0f;
	const float PanelY = 30.0f;
	const float PanelWidth = 400.0f;
	const float PanelHeight = 333.0f;
	const FLinearColor PanelColor(0.014f, 0.019f, 0.029f, 0.87f);
	const FLinearColor Primary(1.0f, 0.88f, 0.72f, 1.0f);
	const FLinearColor Secondary(0.71f, 0.76f, 0.82f, 1.0f);
	const FLinearColor Accent(1.0f, 0.29f, 0.08f, 1.0f);
	DrawRect(PanelColor, PanelX, PanelY, PanelWidth, PanelHeight);
	DrawRect(Accent, PanelX, PanelY, 5.0f, PanelHeight);

	UFont* Font = GEngine->GetSmallFont();
	DrawText(TEXT("EMBER ATTRACTOR  /  WEEK 01"), Accent, 49.0f, 47.0f, Font, 1.3f);
	DrawText(TEXT("NIAGARA SYSTEM TEST LAB"), Secondary, 49.0f, 76.0f, Font, 0.95f);

	const TCHAR* ModeNames[] = {TEXT("BASELINE"), TEXT("GPU EMBERS"), TEXT("CPU DEBRIS"), TEXT("BOTH SYSTEMS")};
	DrawText(FString::Printf(TEXT("Mode       %s"), ModeNames[FMath::Clamp(Lab->GetMode(), 0, 3)]),
		Primary, 49.0f, 112.0f, Font, 1.13f);
	DrawText(FString::Printf(TEXT("GPU target  %s"), *FString::FormatAsNumber(Lab->GetGPUCount())),
		Secondary, 49.0f, 142.0f, Font, 1.0f);
	DrawText(FString::Printf(TEXT("CPU target  %s"), *FString::FormatAsNumber(Lab->GetCPUCount())),
		Secondary, 49.0f, 166.0f, Font, 1.0f);
	DrawText(FString::Printf(TEXT("Frame avg   %.2f ms  /  %.0f fps"),
		Lab->GetAverageFrameMS(), Lab->GetAverageFrameMS() > 0.0f ? 1000.0f / Lab->GetAverageFrameMS() : 0.0f),
		Primary, 49.0f, 196.0f, Font, 1.0f);

	const FString Attraction = Lab->IsAutoAttractEnabled()
		? TEXT("AUTO") : Lab->IsAttractionActive() ? TEXT("ON") : TEXT("HOLD LEFT MOUSE");
	DrawText(FString::Printf(TEXT("Attraction  %s  /  %.0f"), *Attraction, Lab->GetAttractionStrength()),
		Lab->IsAttractionActive() ? Accent : Secondary, 49.0f, 226.0f, Font, 0.95f);
	DrawText(FString::Printf(TEXT("Repeat     %s"), Lab->IsAutoRepeatEnabled() ? TEXT("ON / 8 SEC") : TEXT("OFF")),
		Secondary, 49.0f, 250.0f, Font, 0.95f);
	if (Lab->IsCapturing())
	{
		DrawText(TEXT("CAPTURING FRAME TIMES"), Accent, 49.0f, 274.0f, Font, 0.95f);
	}
	else if (!Lab->HasGPUAsset() || !Lab->HasCPUAsset())
	{
		DrawText(TEXT("NIAGARA ASSET MISSING — CHECK LOG"), Accent, 49.0f, 274.0f, Font, 0.9f);
	}
	else
	{
		DrawText(TEXT("READY"), Secondary, 49.0f, 274.0f, Font, 0.9f);
	}
	DrawRect(FLinearColor(0.23f, 0.25f, 0.29f, 1.0f), 49.0f, 300.0f, 360.0f, 1.0f);
	DrawText(TEXT("0 BASE  1 GPU  2 CPU  3 BOTH    [ ] COUNT"), Secondary, 49.0f, 311.0f, Font, 0.82f);
	DrawText(TEXT("R RESET    F REPEAT    C CAPTURE"), Secondary, 49.0f, 333.0f, Font, 0.82f);
}
