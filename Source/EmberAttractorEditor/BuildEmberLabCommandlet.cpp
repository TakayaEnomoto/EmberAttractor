#include "BuildEmberLabCommandlet.h"

#include "EmberLabActor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraCommon.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BuildEmberLabCommandlet)

namespace EmberLabBuilder
{
static constexpr TCHAR AssetFolder[] = TEXT("/Game/EmberLab");
static TArray<FString> Report;
static UMaterial* DebrisMaterial = nullptr;

static void Note(const FString& Message)
{
	Report.Add(Message);
	UE_LOG(LogTemp, Display, TEXT("EmberLab: %s"), *Message);
}

static void Error(const FString& Message)
{
	Report.Add(TEXT("ERROR: ") + Message);
	UE_LOG(LogTemp, Error, TEXT("EmberLab: %s"), *Message);
}

static UNiagaraScript* LoadModule(const TCHAR* ObjectPath)
{
	UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, ObjectPath);
	if (!Script)
	{
		Error(FString::Printf(TEXT("Missing Niagara module %s"), ObjectPath));
	}
	return Script;
}

static UNiagaraNodeFunctionCall* AddModule(const TCHAR* Path, UNiagaraNodeOutput* Output, int32 Index = INDEX_NONE)
{
	if (!Output)
	{
		Error(FString::Printf(TEXT("No output node for module %s"), Path));
		return nullptr;
	}
	UNiagaraScript* Script = LoadModule(Path);
	return Script ? FNiagaraStackGraphUtilities::AddScriptModuleToStack(Script, *Output, Index) : nullptr;
}

static bool BindInput(UNiagaraNodeFunctionCall* Module, const TCHAR* InputName,
	FNiagaraTypeDefinition Type, const TCHAR* UserName)
{
	if (!Module)
	{
		return false;
	}
	UNiagaraScriptSource* ModuleSource = Module->FunctionScript
		? Cast<UNiagaraScriptSource>(Module->FunctionScript->GetLatestSource()) : nullptr;
	const FName ExpectedName(*FString::Printf(TEXT("Module.%s"), InputName));
	bool bInputExists = false;
	FNiagaraTypeDefinition ActualType;
	TArray<FString> SeenInputs;
	if (ModuleSource && ModuleSource->NodeGraph)
	{
		for (const UEdGraphNode* GraphNode : ModuleSource->NodeGraph->Nodes)
		{
			for (const UEdGraphPin* GraphPin : GraphNode->Pins)
			{
				if (GraphPin->PinName.ToString().StartsWith(TEXT("Module.")))
				{
					SeenInputs.AddUnique(FString::Printf(TEXT("%s:%s"), *GraphPin->PinName.ToString(),
						*UEdGraphSchema_Niagara::PinToTypeDefinition(GraphPin).GetName()));
				}
				if (GraphPin->PinName == ExpectedName)
				{
					bInputExists = true;
					ActualType = UEdGraphSchema_Niagara::PinToTypeDefinition(GraphPin);
					break;
				}
			}
			if (bInputExists)
			{
				break;
			}
		}
	}
	if (!bInputExists || ActualType != Type)
	{
		Note(FString::Printf(TEXT("Module %s script=%s source=%s nodes=%d pins=[%s]"),
			*Module->GetFunctionName(), Module->FunctionScript ? *Module->FunctionScript->GetPathName() : TEXT("null"),
			ModuleSource ? *ModuleSource->GetClass()->GetName() : TEXT("null"),
			ModuleSource && ModuleSource->NodeGraph ? ModuleSource->NodeGraph->Nodes.Num() : -1,
			*FString::Join(SeenInputs, TEXT(", "))));
		Error(FString::Printf(TEXT("Module input missing or wrong type: %s.%s expected=%s actual=%s"),
			*Module->GetFunctionName(), InputName, *Type.GetName(),
			bInputExists ? *ActualType.GetName() : TEXT("missing")));
		return false;
	}
	const FNiagaraParameterHandle ModuleHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(FName(InputName));
	const FNiagaraParameterHandle Alias = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(ModuleHandle, Module);
	UEdGraphPin& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*Module, Alias, Type, FGuid(), FGuid());
	const FNiagaraVariableBase UserVar(Type, FName(UserName));
	TSet<FNiagaraVariableBase> KnownParameters;
	KnownParameters.Add(UserVar);
	FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin, UserVar, KnownParameters);
	Note(FString::Printf(TEXT("Bound %s.%s <- %s"), *Module->GetFunctionName(), InputName, UserName));
	return Pin.LinkedTo.Num() == 1;
}

static void SetFloatInput(UNiagaraNodeFunctionCall* Module, UNiagaraScript* Script,
	const FString& UniqueEmitterName, const TCHAR* InputName, float Value)
{
	if (!Module || !Script)
	{
		return;
	}
	const FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(FName(InputName));
	const FNiagaraParameterHandle Alias = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, Module);
	FNiagaraVariable Parameter(FNiagaraTypeDefinition::GetFloatDef(), Alias.GetParameterHandleString());
	Parameter = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
		Parameter, *UniqueEmitterName, Script->GetUsage());
	Parameter.SetValue(Value);
	Script->RapidIterationParameters.SetParameterData(Parameter.GetData(), Parameter, true);
}

static UNiagaraNodeOutput* OutputFor(FVersionedNiagaraEmitterData* Data, ENiagaraScriptUsage Usage)
{
	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Data->GraphSource);
	if (!Source || !Source->NodeGraph)
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
	{
		if (UNiagaraNodeOutput* Output = Cast<UNiagaraNodeOutput>(Node))
		{
			if (Output->GetUsage() == Usage)
			{
				return Output;
			}
		}
	}
	return nullptr;
}

static bool AddUserParameter(UNiagaraSystem* System, const TCHAR* Name,
	FNiagaraTypeDefinition Type, int32 IntDefault = 0, float FloatDefault = 0.0f)
{
	FNiagaraVariable Variable(Type, FName(Name));
	if (Type == FNiagaraTypeDefinition::GetIntDef())
	{
		Variable.SetValue(IntDefault);
	}
	else if (Type == FNiagaraTypeDefinition::GetFloatDef())
	{
		Variable.SetValue(FloatDefault);
	}
	if (!System->GetExposedParameters().AddParameter(Variable))
	{
		Error(FString::Printf(TEXT("Could not add user parameter %s"), Name));
		return false;
	}
	if (Variable.IsDataAllocated())
	{
		System->GetExposedParameters().SetParameterData(Variable.GetData(), Variable);
	}
	return true;
}

static bool SaveSystem(UNiagaraSystem* System)
{
	System->RequestCompile(true);
	System->WaitForCompilationComplete(true, false);
	bool bAnyCompileError = false;
	auto LogScriptStatus = [&](const TCHAR* Label, UNiagaraScript* Script)
	{
		if (!Script)
		{
			Error(FString::Printf(TEXT("%s script is null"), Label));
			bAnyCompileError = true;
			return;
		}
		bAnyCompileError |= Script->GetLastCompileStatus() == ENiagaraScriptCompileStatus::NCS_Error;
		Note(FString::Printf(TEXT("%s status=%d error=%s"), Label,
			static_cast<int32>(Script->GetLastCompileStatus()), *Script->GetVMExecutableData().ErrorMsg));
		for (const FNiagaraCompileEvent& Event : Script->GetVMExecutableData().LastCompileEvents)
		{
			Note(FString::Printf(TEXT("%s compile event: %s"), Label, *Event.Message));
		}
	};
	LogScriptStatus(TEXT("system spawn"), System->GetSystemSpawnScript());
	LogScriptStatus(TEXT("system update"), System->GetSystemUpdateScript());
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		LogScriptStatus(TEXT("emitter spawn"), Data->EmitterSpawnScriptProps.Script);
		LogScriptStatus(TEXT("emitter update"), Data->EmitterUpdateScriptProps.Script);
		LogScriptStatus(TEXT("particle spawn"), Data->SpawnScriptProps.Script);
		LogScriptStatus(TEXT("particle update"), Data->UpdateScriptProps.Script);
		if (Data->SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			LogScriptStatus(TEXT("gpu compute"), Data->GetGPUComputeScript());
		}
	}
	const bool bOutstanding = System->HasOutstandingCompilationRequests(true);
	const bool bRuntimeReady = !FApp::CanEverRender() || System->IsReadyToRun();
	if (bOutstanding || bAnyCompileError || !bRuntimeReady)
	{
		Error(FString::Printf(TEXT("System not ready after compile: %s outstanding=%d compileErrors=%d canRender=%d"),
			*System->GetPathName(), bOutstanding ? 1 : 0, bAnyCompileError ? 1 : 0, FApp::CanEverRender() ? 1 : 0));
		return false;
	}
	if (!FApp::CanEverRender())
	{
		Note(TEXT("NullRHI: scripts compiled; runtime readiness requires a rendering RHI"));
	}
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		const ENiagaraScriptCompileStatus Status = Data->SimTarget == ENiagaraSimTarget::GPUComputeSim
			? Data->GetGPUComputeScript()->GetLastCompileStatus()
			: Data->UpdateScriptProps.Script->GetLastCompileStatus();
		Note(FString::Printf(TEXT("Emitter %s: target=%s compile=%d renderers=%d"),
			*Handle.GetName().ToString(), Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"),
			static_cast<int32>(Status), Data->GetRenderers().Num()));
		if (Status == ENiagaraScriptCompileStatus::NCS_Error)
		{
			Error(TEXT("Emitter compile error"));
			return false;
		}
	}
	UPackage* Package = System->GetOutermost();
	Package->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bSaved = UPackage::SavePackage(Package, System, *Filename, Args);
	if (!bSaved)
	{
		Error(FString::Printf(TEXT("Failed to save %s"), *Filename));
	}
	return bSaved;
}

static bool BuildSystem(const TCHAR* AssetName, bool bGPU, int32 Count)
{
	const FString PackageName = FString::Printf(TEXT("%s/%s"), AssetFolder, AssetName);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		Error(TEXT("Could not create system package"));
		return false;
	}
	Package->FullyLoad();
	if (UNiagaraSystem* Previous = FindObject<UNiagaraSystem>(Package, AssetName))
	{
		Previous->WaitForCompilationComplete(true, false);
		Previous->ClearFlags(RF_Public | RF_Standalone);
		Previous->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	}
	UNiagaraSystem* System = NewObject<UNiagaraSystem>(Package, FName(AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!System)
	{
		Error(TEXT("Could not create system"));
		return false;
	}
	UNiagaraSystemFactoryNew::InitializeSystem(System, true);
	// A transient flag survives duplication into the system and prevents the emitter
	// from being serialized. The temporary outer is sufficient for the seed object.
	UNiagaraEmitter* BaseEmitter = NewObject<UNiagaraEmitter>(GetTransientPackage(),
		bGPU ? TEXT("EmbersGPU") : TEXT("DebrisCPU"), RF_Transactional);
	UNiagaraEmitterFactoryNew::InitializeEmitter(BaseEmitter, true);
	BaseEmitter->bIsInheritable = false;
	const FGuid Version = BaseEmitter->GetExposedVersion().VersionGuid;
	FNiagaraEditorUtilities::AddEmitterToSystem(*System, *BaseEmitter, Version);
	if (System->GetEmitterHandles().Num() != 1)
	{
		Error(TEXT("System did not receive one emitter"));
		return false;
	}

	const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[0];
	FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	if (!Data || !Emitter)
	{
		Error(TEXT("Emitter copy invalid"));
		return false;
	}
	Data->SimTarget = bGPU ? ENiagaraSimTarget::GPUComputeSim : ENiagaraSimTarget::CPUSim;
	Data->FixedBounds = FBox(FVector(-2000.0, -2000.0, -300.0), FVector(2000.0, 2000.0, 1600.0));
	Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
	if (bGPU)
	{
		Data->MaxGPUParticlesSpawnPerFrame = 1000000;
		for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
			{
				Sprite->Material = LoadObject<UMaterialInterface>(nullptr,
					TEXT("/Niagara/DefaultAssets/DefaultSpriteMaterial.DefaultSpriteMaterial"));
			}
		}
	}
	else
	{
		const TArray<UNiagaraRendererProperties*> Existing = Data->GetRenderers();
		for (UNiagaraRendererProperties* Renderer : Existing)
		{
			Emitter->RemoveRenderer(Renderer, Handle.GetInstance().Version);
		}
		UNiagaraMeshRendererProperties* MeshRenderer = NewObject<UNiagaraMeshRendererProperties>(Emitter, TEXT("DebrisMeshRenderer"));
		FNiagaraMeshRendererMeshProperties Mesh;
		Mesh.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		Mesh.Scale = FVector(0.08, 0.08, 0.08);
		// InitBindings creates an empty slot zero; particles select slot zero by default.
		MeshRenderer->Meshes.Reset();
		MeshRenderer->Meshes.Add(Mesh);
		MeshRenderer->bOverrideMaterials = true;
		FNiagaraMeshMaterialOverride MaterialOverride;
		MaterialOverride.ExplicitMat = DebrisMaterial;
		MeshRenderer->OverrideMaterials.Add(MaterialOverride);
		Emitter->AddRenderer(MeshRenderer, Handle.GetInstance().Version);
	}

	const bool bParams =
		AddUserParameter(System, TEXT("User.ParticleCount"), FNiagaraTypeDefinition::GetIntDef(), Count) &&
		AddUserParameter(System, TEXT("User.AttractorPosition"), FNiagaraTypeDefinition::GetPositionDef()) &&
		AddUserParameter(System, TEXT("User.AttractionStrength"), FNiagaraTypeDefinition::GetFloatDef(), 0, 400.0f) &&
		AddUserParameter(System, TEXT("User.AttractionEnabled"), FNiagaraTypeDefinition::GetFloatDef(), 0, 1.0f);
	if (!bParams)
	{
		return false;
	}

	UNiagaraNodeOutput* EmitterUpdate = OutputFor(Data, ENiagaraScriptUsage::EmitterUpdateScript);
	UNiagaraNodeOutput* ParticleSpawn = OutputFor(Data, ENiagaraScriptUsage::ParticleSpawnScript);
	UNiagaraNodeOutput* ParticleUpdate = OutputFor(Data, ENiagaraScriptUsage::ParticleUpdateScript);
	if (!EmitterUpdate || !ParticleSpawn || !ParticleUpdate)
	{
		Error(TEXT("Emitter graph missing an output"));
		return false;
	}
	UNiagaraScriptSource* Source = CastChecked<UNiagaraScriptSource>(Data->GraphSource);
	TArray<UNiagaraNodeFunctionCall*> FunctionNodes;
	Source->NodeGraph->GetNodesOfClass(FunctionNodes);
	bool bDisabledSpawnRate = false;
	bool bSetOneLoop = false;
	for (UNiagaraNodeFunctionCall* Node : FunctionNodes)
	{
		if (Node->FunctionScript && Node->FunctionScript->GetPathName().Contains(TEXT("/SpawnRate.")))
		{
			FNiagaraStackGraphUtilities::SetModuleIsEnabled(*Node, false);
			bDisabledSpawnRate = true;
		}
		if (Node->FunctionScript && Node->FunctionScript->GetPathName().Contains(TEXT("/Color.")))
		{
			FNiagaraStackGraphUtilities::SetModuleIsEnabled(*Node, false);
		}
		if (Node->FunctionScript && Node->FunctionScript->GetPathName().Contains(TEXT("/AddVelocity.")))
		{
			FNiagaraStackGraphUtilities::SetModuleIsEnabled(*Node, false);
		}
		if (Node->FunctionScript && Node->FunctionScript->GetPathName().Contains(TEXT("/EmitterState.")))
		{
			// Keep the single burst simulating for the whole test session. The factory's
			// short loop duration otherwise stops the emitter before profiling begins.
			SetFloatInput(Node, Data->EmitterUpdateScriptProps.Script,
				Emitter->GetUniqueEmitterName(), TEXT("Loop Duration"), 600.0f);
			UEdGraphPin* LoopBehavior = nullptr;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin->Direction == EGPD_Input && Pin->PinName == TEXT("Life Cycle Mode"))
				{
					const UEnum* LifeEnum = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin).GetEnum();
					if (LifeEnum)
					{
						Pin->DefaultValue = LifeEnum->GetNameStringByValue(1); // Self, not the looping system.
						Node->MarkNodeRequiresSynchronization(TEXT("EmberLab self lifecycle"), true);
					}
				}
				if (Pin->Direction == EGPD_Input && Pin->PinName == TEXT("Loop Behavior"))
				{
					LoopBehavior = Pin;
				}
			}
			if (LoopBehavior)
			{
				const UEnum* LoopEnum = UEdGraphSchema_Niagara::PinToTypeDefinition(LoopBehavior).GetEnum();
				const FString OnceName = LoopEnum ? LoopEnum->GetNameStringByValue(1) : FString();
				if (!OnceName.IsEmpty())
				{
					Note(FString::Printf(TEXT("Emitter loop: %s -> %s"), *LoopBehavior->DefaultValue, *OnceName));
					LoopBehavior->DefaultValue = OnceName;
					Node->MarkNodeRequiresSynchronization(TEXT("EmberLab single burst"), true);
					bSetOneLoop = true;
				}
			}
		}
	}
	if (!bDisabledSpawnRate || !bSetOneLoop)
	{
		Error(TEXT("Could not configure default emitter rate and one-shot loop"));
		return false;
	}
	UNiagaraNodeFunctionCall* Burst = AddModule(TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"), EmitterUpdate);
	if (!BindInput(Burst, TEXT("Spawn Count"), FNiagaraTypeDefinition::GetIntDef(), TEXT("User.ParticleCount")))
	{
		Error(TEXT("Failed to link burst count"));
		return false;
	}
	UNiagaraNodeFunctionCall* Sphere = AddModule(TEXT("/Niagara/Modules/Spawn/Location/SphereLocation.SphereLocation"), ParticleSpawn);
	SetFloatInput(Sphere, Data->SpawnScriptProps.Script, Emitter->GetUniqueEmitterName(), TEXT("Sphere Radius"), 700.0f);
	const FNiagaraParameterHandle ScaleHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		FNiagaraParameterHandle::CreateModuleParameterHandle(TEXT("Non Uniform Scale")), Sphere);
	FNiagaraVariable SpawnScale(FNiagaraTypeDefinition::GetVec3Def(), ScaleHandle.GetParameterHandleString());
	SpawnScale = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
		SpawnScale, *Emitter->GetUniqueEmitterName(), Data->SpawnScriptProps.Script->GetUsage());
	SpawnScale.SetValue(FVector3f(1.0f, 1.0f, 0.18f));
	Data->SpawnScriptProps.Script->RapidIterationParameters.SetParameterData(SpawnScale.GetData(), SpawnScale, true);
	FNiagaraVariable Lifetime(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particles.Lifetime"));
	FNiagaraStackGraphUtilities::AddParameterModuleToStack({Lifetime}, *ParticleSpawn, INDEX_NONE, {TEXT("600.0")});
	FNiagaraVariable Color(FNiagaraTypeDefinition::GetColorDef(), TEXT("Particles.Color"));
	if (bGPU)
	{
		FNiagaraVariable SpriteSize(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Particles.SpriteSize"));
		FNiagaraStackGraphUtilities::AddParameterModuleToStack(
			{SpriteSize}, *ParticleSpawn, INDEX_NONE, {TEXT("(X=2.0,Y=2.0)")});
	}
	const FString ColorValue = bGPU
		? TEXT("(R=1.0,G=0.18,B=0.015,A=0.72)")
		: TEXT("(R=0.12,G=0.6,B=1.4,A=1.0)");
	FNiagaraStackGraphUtilities::AddParameterModuleToStack({Color}, *ParticleSpawn, INDEX_NONE, {ColorValue});
	UNiagaraNodeFunctionCall* Attractor = AddModule(TEXT("/Niagara/Modules/Update/Forces/V2/PointAttractionForce.PointAttractionForce"), ParticleUpdate, 2);
	SetFloatInput(Attractor, Data->UpdateScriptProps.Script, Emitter->GetUniqueEmitterName(), TEXT("Attraction Radius"), 4000.0f);
	SetFloatInput(Attractor, Data->UpdateScriptProps.Script, Emitter->GetUniqueEmitterName(), TEXT("Kill Radius"), 0.0f);
	const bool bPositionLinked = BindInput(Attractor, TEXT("Attractor Position"), FNiagaraTypeDefinition::GetPositionDef(), TEXT("User.AttractorPosition"));
	const bool bStrengthLinked = BindInput(Attractor, TEXT("Attraction Strength"), FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.AttractionStrength"));
	if (!bPositionLinked || !bStrengthLinked)
	{
		Error(TEXT("Failed to link attraction controls"));
		return false;
	}
	UNiagaraNodeFunctionCall* Drag = AddModule(TEXT("/Niagara/Modules/Update/Forces/Drag.Drag"), ParticleUpdate, 3);
	SetFloatInput(Drag, Data->UpdateScriptProps.Script, Emitter->GetUniqueEmitterName(), TEXT("Drag"), 0.7f);
	// Week 1 uses two simple GPU rules: attraction and drag. Keeping idle embers
	// in view makes the input response and render workload easier to inspect.
	if (!bGPU)
	{
		AddModule(TEXT("/Niagara/Modules/Collision/Collision.Collision"), ParticleUpdate);
	}
	Source->NodeGraph->NotifyGraphChanged();
	FAssetRegistryModule::AssetCreated(System);
	Note(FString::Printf(TEXT("Created %s with %d burst particles and %s renderer"), *PackageName, Count,
		bGPU ? TEXT("GPU sprite") : TEXT("CPU mesh")));
	return SaveSystem(System);
}

static bool BuildMap()
{
	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
	if (!World)
	{
		Error(TEXT("Could not create blank editor map"));
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.Name = TEXT("EmberLab");
	AEmberLabActor* Actor = World->SpawnActor<AEmberLabActor>(AEmberLabActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	if (!Actor)
	{
		Error(TEXT("Could not spawn EmberLab actor"));
		return false;
	}
	const bool bSaved = UEditorLoadingAndSavingUtils::SaveMap(World, TEXT("/Game/EmberLab/L_EmberLab"));
	if (!bSaved)
	{
		Error(TEXT("Could not save EmberLab map"));
	}
	else
	{
		Note(TEXT("Created /Game/EmberLab/L_EmberLab with AEmberLabActor"));
	}
	return bSaved;
}
}

int32 UBuildEmberLabCommandlet::Main(const FString& Params)
{
	using namespace EmberLabBuilder;
	UPackage* MaterialPackage = CreatePackage(TEXT("/Game/EmberLab/M_Debris"));
	MaterialPackage->FullyLoad();
	DebrisMaterial = FindObject<UMaterial>(MaterialPackage, TEXT("M_Debris"));
	if (!DebrisMaterial)
	{
		DebrisMaterial = NewObject<UMaterial>(MaterialPackage, TEXT("M_Debris"), RF_Public | RF_Standalone);
		DebrisMaterial->SetShadingModel(MSM_Unlit);
		DebrisMaterial->bUsedWithNiagaraMeshParticles = true;
		UMaterialExpressionParticleColor* ParticleColor = NewObject<UMaterialExpressionParticleColor>(DebrisMaterial);
		DebrisMaterial->GetExpressionCollection().AddExpression(ParticleColor);
		DebrisMaterial->GetEditorOnlyData()->EmissiveColor.Connect(0, ParticleColor);
		DebrisMaterial->PostEditChange();
		FAssetRegistryModule::AssetCreated(DebrisMaterial);
	}
	FSavePackageArgs MaterialSaveArgs;
	MaterialSaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString MaterialFilename = FPackageName::LongPackageNameToFilename(MaterialPackage->GetName(), FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(MaterialPackage, DebrisMaterial, *MaterialFilename, MaterialSaveArgs))
	{
		EmberLabBuilder::Error(TEXT("Failed to save debris material"));
		return 1;
	}
	const bool bGPU = BuildSystem(TEXT("NS_Embers_GPU"), true, 100000);
	const bool bCPU = BuildSystem(TEXT("NS_Debris_CPU"), false, 500);
	const bool bMap = bGPU && bCPU && BuildMap();
	Note(FString::Printf(TEXT("RESULT: GPU=%s CPU=%s MAP=%s"),
		bGPU ? TEXT("PASS") : TEXT("FAIL"), bCPU ? TEXT("PASS") : TEXT("FAIL"), bMap ? TEXT("PASS") : TEXT("FAIL")));
	const FString ReportPath = FPaths::ProjectSavedDir() / TEXT("AssetBuildReport.txt");
	FFileHelper::SaveStringArrayToFile(Report, *ReportPath);
	return bGPU && bCPU && bMap ? 0 : 1;
}
