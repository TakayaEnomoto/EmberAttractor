#include "EmberLabHUD.h"

#include "EmberLabActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

namespace
{
	void DrawMeter(AHUD& HUD, float X, float Y, float Width, float Fraction, const FLinearColor& Fill)
	{
		HUD.DrawRect(FLinearColor(0.08f, 0.09f, 0.11f, 1.0f), X, Y, Width, 8.0f);
		HUD.DrawRect(Fill, X, Y, Width * FMath::Clamp(Fraction, 0.0f, 1.0f), 8.0f);
	}
}

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
	const float PanelHeight = Lab->IsGameEnabled() ? 479.0f : 333.0f;
	const FLinearColor PanelColor(0.014f, 0.019f, 0.029f, 0.87f);
	const FLinearColor Primary(1.0f, 0.88f, 0.72f, 1.0f);
	const FLinearColor Secondary(0.71f, 0.76f, 0.82f, 1.0f);
	const FLinearColor Accent(1.0f, 0.29f, 0.08f, 1.0f);
	const FLinearColor EnergyColor(0.35f, 0.75f, 1.0f, 1.0f);
	const FLinearColor CapturedColor(0.45f, 0.95f, 1.0f, 1.0f);
	DrawRect(PanelColor, PanelX, PanelY, PanelWidth, PanelHeight);
	DrawRect(Accent, PanelX, PanelY, 5.0f, PanelHeight);

	UFont* Font = GEngine->GetSmallFont();
	DrawText(TEXT("EMBER ATTRACTOR  /  BLACK HOLE"), Accent, 49.0f, 47.0f, Font, 1.3f);
	DrawText(Lab->IsGameEnabled() ? TEXT("REACH CRITICAL MASS BEFORE THE MATTER COOLS")
		: TEXT("LAB SANDBOX  /  CONSTANT WORKLOAD"), Secondary, 49.0f, 76.0f, Font, 0.95f);

	const TCHAR* ModeNames[] = {TEXT("BASELINE"), TEXT("GPU DUST"), TEXT("CPU DEBRIS"), TEXT("BOTH SYSTEMS")};
	DrawText(FString::Printf(TEXT("Mode       %s"), ModeNames[FMath::Clamp(Lab->GetMode(), 0, 3)]),
		Primary, 49.0f, 112.0f, Font, 1.13f);
	DrawText(FString::Printf(TEXT("GPU target  %s"), *FString::FormatAsNumber(Lab->GetGPUCount())),
		Secondary, 49.0f, 142.0f, Font, 1.0f);
	DrawText(FString::Printf(TEXT("CPU target  %s"), *FString::FormatAsNumber(Lab->GetCPUCount())),
		Secondary, 49.0f, 166.0f, Font, 1.0f);
	DrawText(FString::Printf(TEXT("Frame avg   %.2f ms  /  %.0f fps"),
		Lab->GetAverageFrameMS(), Lab->GetAverageFrameMS() > 0.0f ? 1000.0f / Lab->GetAverageFrameMS() : 0.0f),
		Primary, 49.0f, 196.0f, Font, 1.0f);

	float Y = 226.0f;
	if (Lab->IsGameEnabled())
	{
		DrawText(FString::Printf(TEXT("Mass        %.1f / %.0f"), Lab->GetMass(), Lab->GetTargetMass()),
			Primary, 49.0f, Y, Font, 1.0f);
		DrawMeter(*this, 49.0f, Y + 22.0f, 360.0f, Lab->GetMass() / Lab->GetTargetMass(), Accent);
		Y += 38.0f;
		DrawText(FString::Printf(TEXT("Energy      %.0f%s"), Lab->GetEnergy(),
			Lab->IsPullLocked() ? TEXT("  /  RECHARGING") : TEXT("")),
			Lab->IsPullLocked() ? Accent : Secondary, 49.0f, Y, Font, 1.0f);
		DrawMeter(*this, 49.0f, Y + 22.0f, 360.0f, Lab->GetEnergy() / 100.0f,
			Lab->IsPullLocked() ? Accent : EnergyColor);
		Y += 38.0f;
		DrawText(FString::Printf(TEXT("Time        %.1f s     Horizon %.0f cm"),
			Lab->GetTimeRemaining(), Lab->GetHorizonRadius()), Secondary, 49.0f, Y, Font, 1.0f);
		Y += 24.0f;
		DrawText(FString::Printf(TEXT("Consumed    GPU %s  /  CPU %s%s"),
			*FString::FormatAsNumber(Lab->GetConsumedGPU()), *FString::FormatAsNumber(Lab->GetConsumedCPU()),
			Lab->GetSaturatedReadbacks() > 0 ? *FString::Printf(TEXT("  /  %d full readbacks"), Lab->GetSaturatedReadbacks()) : TEXT("")),
			Secondary, 49.0f, Y, Font, 0.95f);
		Y += 24.0f;
		DrawText(TEXT("Debris      orange drifting  /  cyan captured"), CapturedColor, 49.0f, Y, Font, 0.95f);
		Y += 28.0f;
	}

	const FString Attraction = Lab->IsAutoAttractEnabled()
		? TEXT("AUTO") : Lab->IsAttractionActive() ? TEXT("ON") : TEXT("HOLD LEFT MOUSE");
	DrawText(FString::Printf(TEXT("Pull        %s  /  %.0f"), *Attraction, Lab->GetAttractionStrength()),
		Lab->IsAttractionActive() ? Accent : Secondary, 49.0f, Y, Font, 0.95f);
	Y += 24.0f;
	if (Lab->IsCapturing())
	{
		DrawText(TEXT("CAPTURING FRAME TIMES"), Accent, 49.0f, Y, Font, 0.95f);
	}
	else if (!Lab->HasGPUAsset() || !Lab->HasCPUAsset())
	{
		DrawText(TEXT("NIAGARA ASSET MISSING — CHECK LOG"), Accent, 49.0f, Y, Font, 0.9f);
	}
	else if (!Lab->IsGameEnabled())
	{
		DrawText(FString::Printf(TEXT("Repeat     %s"), Lab->IsAutoRepeatEnabled() ? TEXT("ON / 8 SEC") : TEXT("OFF")),
			Secondary, 49.0f, Y, Font, 0.95f);
	}
	Y += 26.0f;
	DrawRect(FLinearColor(0.23f, 0.25f, 0.29f, 1.0f), 49.0f, Y, 360.0f, 1.0f);
	DrawText(TEXT("0 BASE  1 GPU  2 CPU  3 BOTH    [ ] COUNT"), Secondary, 49.0f, Y + 11.0f, Font, 0.82f);
	DrawText(TEXT("R RESTART    F REPEAT (LAB)    C CAPTURE"), Secondary, 49.0f, Y + 33.0f, Font, 0.82f);

	if (Lab->IsGameEnabled() && Lab->GetRoundState() != EEmberRoundState::Playing)
	{
		const bool bWon = Lab->GetRoundState() == EEmberRoundState::Won;
		const FString Title = bWon ? TEXT("CRITICAL MASS REACHED") : TEXT("THE MATTER WENT COLD");
		const FString Detail = FString::Printf(TEXT("Mass %.1f / %.0f   -   press R to play again"),
			Lab->GetMass(), Lab->GetTargetMass());
		const float CenterX = Canvas->ClipX * 0.5f;
		const float CenterY = Canvas->ClipY * 0.42f;
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f), CenterX - 330.0f, CenterY - 50.0f, 660.0f, 110.0f);
		float TitleWidth = 0.0f;
		float TitleHeight = 0.0f;
		GetTextSize(Title, TitleWidth, TitleHeight, Font, 2.4f);
		DrawText(Title, bWon ? Primary : Accent, CenterX - TitleWidth * 0.5f, CenterY - 36.0f, Font, 2.4f);
		float DetailWidth = 0.0f;
		float DetailHeight = 0.0f;
		GetTextSize(Detail, DetailWidth, DetailHeight, Font, 1.1f);
		DrawText(Detail, Secondary, CenterX - DetailWidth * 0.5f, CenterY + 22.0f, Font, 1.1f);
	}
}
