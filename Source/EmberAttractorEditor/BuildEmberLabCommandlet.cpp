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
#include "NiagaraDataInterfaceExport.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraNodeAssignment.h"
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

static bool FindModuleInput(UNiagaraNodeFunctionCall* Module, const FString& InputName,
	FString& OutName, FNiagaraTypeDefinition& OutType, TArray<FString>* OutSeen = nullptr)
{
	UNiagaraScriptSource* ModuleSource = Module && Module->FunctionScript
		? Cast<UNiagaraScriptSource>(Module->FunctionScript->GetLatestSource()) : nullptr;
	if (!ModuleSource || !ModuleSource->NodeGraph)
	{
		return false;
	}
	// Exact names win; a prefix match tolerates display suffixes such as "Float To Send (Size)".
	bool bFound = false;
	for (const UEdGraphNode* GraphNode : ModuleSource->NodeGraph->Nodes)
	{
		for (const UEdGraphPin* GraphPin : GraphNode->Pins)
		{
			const FString PinName = GraphPin->PinName.ToString();
			if (!PinName.StartsWith(TEXT("Module.")))
			{
				continue;
			}
			const FString Short = PinName.RightChop(7);
			if (OutSeen)
			{
				OutSeen->AddUnique(FString::Printf(TEXT("%s:%s"), *Short,
					*UEdGraphSchema_Niagara::PinToTypeDefinition(GraphPin).GetName()));
			}
			if (Short == InputName || (!bFound && Short.StartsWith(InputName)))
			{
				OutName = Short;
				OutType = UEdGraphSchema_Niagara::PinToTypeDefinition(GraphPin);
				bFound = true;
				if (Short == InputName && !OutSeen)
				{
					return true;
				}
			}
		}
	}
	return bFound;
}

static UEdGraphPin* GetInputOverridePin(UNiagaraNodeFunctionCall* Module, const TCHAR* InputName,
	FNiagaraTypeDefinition& OutType)
{
	FString Name;
	if (!Module || !FindModuleInput(Module, InputName, Name, OutType))
	{
		TArray<FString> Seen;
		FindModuleInput(Module, TEXT("\x01"), Name, OutType, &Seen);
		Error(FString::Printf(TEXT("Module input missing: %s.%s available=[%s]"),
			Module ? *Module->GetFunctionName() : TEXT("null"), InputName, *FString::Join(Seen, TEXT(", "))));
		return nullptr;
	}
	const FNiagaraParameterHandle Alias = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*Name)), Module);
	return &FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Module, Alias, OutType, FGuid(), FGuid());
}

/** Links a module or dynamic-input input to any named parameter (User., Particles., ...). */
static bool LinkInput(UNiagaraNodeFunctionCall* Module, const TCHAR* InputName, const TCHAR* ParameterName)
{
	FNiagaraTypeDefinition Type;
	UEdGraphPin* Pin = GetInputOverridePin(Module, InputName, Type);
	if (!Pin)
	{
		return false;
	}
	const FNiagaraVariableBase Linked(Type, FName(ParameterName));
	TSet<FNiagaraVariableBase> KnownParameters;
	KnownParameters.Add(Linked);
	FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(*Pin, Linked, KnownParameters);
	Note(FString::Printf(TEXT("Linked %s.%s <- %s (%s)"), *Module->GetFunctionName(), InputName, ParameterName, *Type.GetName()));
	return Pin->LinkedTo.Num() == 1;
}

/** Replaces an input with a dynamic-input script and returns the new function node. */
static UNiagaraNodeFunctionCall* SetDynamicInput(UNiagaraNodeFunctionCall* Module, const TCHAR* InputName, const TCHAR* ScriptPath)
{
	FNiagaraTypeDefinition Type;
	UEdGraphPin* Pin = GetInputOverridePin(Module, InputName, Type);
	UNiagaraScript* Script = Pin ? LoadModule(ScriptPath) : nullptr;
	if (!Script)
	{
		return nullptr;
	}
	UNiagaraNodeFunctionCall* DynamicInput = nullptr;
	FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(*Pin, Script, DynamicInput);
	Note(FString::Printf(TEXT("Dynamic input %s.%s <- %s"), *Module->GetFunctionName(), InputName,
		DynamicInput ? *DynamicInput->GetFunctionName() : TEXT("null")));
	return DynamicInput;
}

/** Sets a static-switch pin to the enumerator whose display name contains Include but not Exclude. */
static bool SetStaticSwitch(UNiagaraNodeFunctionCall* Node, const TCHAR* PinPrefix, const TCHAR* Include, const TCHAR* Exclude)
{
	TArray<FString> Seen;
	for (UEdGraphPin* Pin : Node ? Node->Pins : TArray<UEdGraphPin*>())
	{
		if (Pin->Direction != EGPD_Input)
		{
			continue;
		}
		const FNiagaraTypeDefinition Type = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
		const UEnum* Enum = Type.GetEnum();
		Seen.Add(FString::Printf(TEXT("%s=%s"), *Pin->PinName.ToString(), *Pin->DefaultValue));
		if (!Enum || !Pin->PinName.ToString().StartsWith(PinPrefix))
		{
			continue;
		}
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			const FString Display = Enum->GetDisplayNameTextByIndex(Index).ToString();
			const FString Short = Enum->GetNameStringByIndex(Index);
			Seen.Add(FString::Printf(TEXT("  %s='%s'"), *Short, *Display));
			if (Display.Contains(Include) && !Display.Contains(Exclude))
			{
				Pin->DefaultValue = Short;
				Node->MarkNodeRequiresSynchronization(TEXT("EmberLab static switch"), true);
				Note(FString::Printf(TEXT("Static switch %s.%s = %s (%s)"), *Node->GetFunctionName(),
					*Pin->PinName.ToString(), *Short, *Display));
				return true;
			}
		}
	}
	Error(FString::Printf(TEXT("Static switch %s/%s not found on %s pins=[%s]"), PinPrefix, Include,
		Node ? *Node->GetFunctionName() : TEXT("null"), *FString::Join(Seen, TEXT(", "))));
	return false;
}

template <typename ValueType>
static bool SetLocalInput(UNiagaraNodeFunctionCall* Module, UNiagaraScript* Script,
	const FString& UniqueEmitterName, const TCHAR* InputName, const ValueType& Value)
{
	FString Name;
	FNiagaraTypeDefinition Type;
	if (!Module || !Script || !FindModuleInput(Module, InputName, Name, Type) || Type.GetSize() != sizeof(ValueType))
	{
		Error(FString::Printf(TEXT("Local input missing or wrong size: %s.%s"),
			Module ? *Module->GetFunctionName() : TEXT("null"), InputName));
		return false;
	}
	const FNiagaraParameterHandle Alias = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*Name)), Module);
	FNiagaraVariable Parameter(Type, Alias.GetParameterHandleString());
	Parameter = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
		Parameter, *UniqueEmitterName, Script->GetUsage());
	Parameter.SetValue(Value);
	Script->RapidIterationParameters.SetParameterData(Parameter.GetData(), Parameter, true);
	return true;
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
	FNiagaraTypeDefinition Type, int32 IntDefault = 0, float FloatDefault = 0.0f,
	FLinearColor ColorDefault = FLinearColor::White)
{
	FNiagaraVariable Variable(Type, FName(Name));
	if (Type == FNiagaraTypeDefinition::GetColorDef())
	{
		Variable.SetValue(ColorDefault);
	}
	else if (Type == FNiagaraTypeDefinition::GetIntDef())
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
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		// Keep the translated particle-update HLSL beside the build report for review.
		const UNiagaraScript* Update = Handle.GetEmitterData()->UpdateScriptProps.Script;
		FFileHelper::SaveStringToFile(Update->GetVMExecutableData().LastHlslTranslation,
			*(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Hlsl_%s_ParticleUpdate.hlsl"), *System->GetName())));
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

/** bGPU selects the dust look (sprites, no collision); bPortToGPU runs the debris look on the GPU. */
static bool BuildSystem(const TCHAR* AssetName, bool bGPU, int32 Count, bool bDynamicBounds = false, bool bPortToGPU = false)
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
	const bool bGPUSim = bGPU || bPortToGPU;
	Data->SimTarget = bGPUSim ? ENiagaraSimTarget::GPUComputeSim : ENiagaraSimTarget::CPUSim;
	Data->FixedBounds = FBox(FVector(-2000.0, -2000.0, -300.0), FVector(2000.0, 2000.0, 1600.0));
	// The dynamic variant exists only as the "before" case of the measured bounds optimization.
	Data->CalculateBoundsMode = bDynamicBounds
		? ENiagaraEmitterCalculateBoundMode::Dynamic : ENiagaraEmitterCalculateBoundMode::Fixed;
	if (bGPUSim)
	{
		Data->MaxGPUParticlesSpawnPerFrame = 16000000;
	}
	if (bGPU)
	{
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
		AddUserParameter(System, TEXT("User.AttractionEnabled"), FNiagaraTypeDefinition::GetFloatDef(), 0, 1.0f) &&
		AddUserParameter(System, TEXT("User.HorizonRadius"), FNiagaraTypeDefinition::GetFloatDef(), 0, 0.0f) &&
		AddUserParameter(System, TEXT("User.VortexStrength"), FNiagaraTypeDefinition::GetFloatDef(), 0, 0.0f) &&
		AddUserParameter(System, TEXT("User.PullRadius"), FNiagaraTypeDefinition::GetFloatDef(), 0, 4000.0f) &&
		AddUserParameter(System, TEXT("User.CoolSeconds"), FNiagaraTypeDefinition::GetFloatDef(), 0, 30.0f) &&
		AddUserParameter(System, TEXT("User.HotColor"), FNiagaraTypeDefinition::GetColorDef(), 0, 0.0f,
			bGPU ? FLinearColor(1.0f, 0.18f, 0.015f, 0.72f) : FLinearColor(1.4f, 0.55f, 0.12f, 1.0f)) &&
		AddUserParameter(System, TEXT("User.ColdColor"), FNiagaraTypeDefinition::GetColorDef(), 0, 0.0f,
			bGPU ? FLinearColor(0.05f, 0.012f, 0.02f, 0.35f) : FLinearColor(0.08f, 0.1f, 0.14f, 1.0f)) &&
		AddUserParameter(System, TEXT("User.ExportHandler"), FNiagaraTypeDefinition::GetUObjectDef()) &&
		AddUserParameter(System, TEXT("User.CaptureRadius"), FNiagaraTypeDefinition::GetFloatDef(), 0, 0.0f) &&
		AddUserParameter(System, TEXT("User.EscapeRadius"), FNiagaraTypeDefinition::GetFloatDef(), 0, 0.0f) &&
		AddUserParameter(System, TEXT("User.OrbitStrength"), FNiagaraTypeDefinition::GetFloatDef(), 0, 0.0f) &&
		AddUserParameter(System, TEXT("User.OrbitVortex"), FNiagaraTypeDefinition::GetFloatDef(), 0, 0.0f) &&
		AddUserParameter(System, TEXT("User.CapturedTint"), FNiagaraTypeDefinition::GetFloatDef(), 0, 1.0f) &&
		AddUserParameter(System, TEXT("User.FreeTint"), FNiagaraTypeDefinition::GetFloatDef(), 0, 0.0f) &&
		AddUserParameter(System, TEXT("User.CapturedColor"), FNiagaraTypeDefinition::GetColorDef(), 0, 0.0f,
			FLinearColor(0.35f, 1.1f, 1.8f, 1.0f));
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
	UNiagaraNodeFunctionCall* ColorModule = nullptr;
	for (UNiagaraNodeFunctionCall* Node : FunctionNodes)
	{
		if (Node->FunctionScript && Node->FunctionScript->GetPathName().Contains(TEXT("/SpawnRate.")))
		{
			FNiagaraStackGraphUtilities::SetModuleIsEnabled(*Node, false);
			bDisabledSpawnRate = true;
		}
		if (Node->FunctionScript && Node->FunctionScript->GetPathName().Contains(TEXT("/Color.")))
		{
			ColorModule = Node;
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
	const FNiagaraVariable Captured(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Particles.Captured"));
	if (!bGPU)
	{
		FNiagaraStackGraphUtilities::AddParameterModuleToStack({Captured}, *ParticleSpawn, INDEX_NONE, {TEXT("false")});
	}
	UNiagaraScript* UpdateScript = Data->UpdateScriptProps.Script;
	const FString EmitterName = Emitter->GetUniqueEmitterName();

	// Event horizon readback. Particles inside User.HorizonRadius are exported to the
	// gameplay callback and killed by the next module using the export's own result,
	// so every consumed particle is reported exactly once. Particles.Age travels in the
	// Size slot; gameplay converts it into remaining heat.
	UNiagaraNodeFunctionCall* Export = AddModule(TEXT("/Niagara/Modules/ExportParticleData/ExportParticleDataToBlueprint.ExportParticleDataToBlueprint"), ParticleUpdate, 2);
	FNiagaraTypeDefinition ExportType;
	UEdGraphPin* ExportPin = GetInputOverridePin(Export, TEXT("Export Particle Data Interface"), ExportType);
	UNiagaraDataInterface* ExportInterface = nullptr;
	if (ExportPin)
	{
		FString ExportInputName;
		FindModuleInput(Export, TEXT("Export Particle Data Interface"), ExportInputName, ExportType);
		const FNiagaraParameterHandle ExportAlias = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
			FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*ExportInputName)), Export);
		FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(*ExportPin,
			UNiagaraDataInterfaceExport::StaticClass(), ExportAlias.GetParameterHandleString().ToString(), ExportInterface);
	}
	UNiagaraDataInterfaceExport* ExportData = Cast<UNiagaraDataInterfaceExport>(ExportInterface);
	if (!ExportData)
	{
		Error(TEXT("Could not create Export Particle Data interface"));
		return false;
	}
	ExportData->CallbackHandlerParameter.Parameter = FNiagaraVariable(
		FNiagaraTypeDefinition::GetUObjectDef(), TEXT("User.ExportHandler"));
	// GPU writes past this per-frame reservation are dropped; gameplay reports when a frame fills it.
	ExportData->GPUAllocationMode = ENDIExport_GPUAllocationMode::FixedSize;
	ExportData->GPUAllocationFixedSize = 16384;
	UNiagaraNodeFunctionCall* InsideHorizon = SetDynamicInput(Export, TEXT("Condition To Export Data"),
		TEXT("/Niagara/DynamicInputs/Bool/v2/SetBoolByFloatComparison.SetBoolByFloatComparison"));
	UNiagaraNodeFunctionCall* Distance = SetDynamicInput(InsideHorizon, TEXT("A"),
		TEXT("/Niagara/DynamicInputs/Vectors/Position/DistanceBetweenPositions.DistanceBetweenPositions"));
	const bool bExportLinked = InsideHorizon && Distance
		&& SetStaticSwitch(InsideHorizon, TEXT("Comparison"), TEXT("Less"), TEXT("Equal"))
		&& LinkInput(InsideHorizon, TEXT("B"), TEXT("User.HorizonRadius"))
		&& LinkInput(Distance, TEXT("A"), TEXT("Particles.Position"))
		&& LinkInput(Distance, TEXT("B"), TEXT("User.AttractorPosition"))
		&& LinkInput(Export, TEXT("Float To Send"), TEXT("Particles.Age"))
		&& SetLocalInput(Export, UpdateScript, EmitterName, TEXT("Delay Between Data Exports"), 0.0f);
	UNiagaraNodeFunctionCall* Consume = AddModule(TEXT("/Niagara/Modules/Update/Lifetime/KillParticles.KillParticles"), ParticleUpdate, 3);
	if (!bExportLinked || !LinkInput(Consume, TEXT("Kill Particles"),
		TEXT("Output.ExportParticleDataToBlueprint.DataWasExportedThisFrame")))
	{
		Error(TEXT("Failed to build event-horizon export"));
		return false;
	}

	int32 NextModule = 4;
	if (!bGPU)
	{
		// Debris state machine: Drifting -> Captured -> Swallowed (the export and kill above).
		// Captured latches inside User.CaptureRadius and is released only beyond User.EscapeRadius:
		//   Captured = (d < Capture) || (Captured && d < Escape)
		UNiagaraNodeFunctionCall* State = FNiagaraStackGraphUtilities::AddParameterModuleToStack(
			{Captured}, *ParticleUpdate, NextModule++, {TEXT("false")});
		FString StateInput;
		FNiagaraTypeDefinition StateType;
		const TCHAR* StateInputName = FindModuleInput(State, TEXT("Particles.Captured"), StateInput, StateType)
			? TEXT("Particles.Captured") : TEXT("Captured");
		UNiagaraNodeFunctionCall* Latch = SetDynamicInput(State, StateInputName,
			TEXT("/Niagara/DynamicInputs/Bool/Boolean_OrOperation.Boolean_OrOperation"));
		UNiagaraNodeFunctionCall* Enter = Latch ? SetDynamicInput(Latch, TEXT("Bool1"),
			TEXT("/Niagara/DynamicInputs/Bool/v2/SetBoolByFloatComparison.SetBoolByFloatComparison")) : nullptr;
		UNiagaraNodeFunctionCall* Hold = Latch ? SetDynamicInput(Latch, TEXT("Bool2"),
			TEXT("/Niagara/DynamicInputs/Bool/Boolean_AndOperation.Boolean_AndOperation")) : nullptr;
		UNiagaraNodeFunctionCall* Within = Hold ? SetDynamicInput(Hold, TEXT("Bool2"),
			TEXT("/Niagara/DynamicInputs/Bool/v2/SetBoolByFloatComparison.SetBoolByFloatComparison")) : nullptr;
		UNiagaraNodeFunctionCall* EnterDistance = Enter ? SetDynamicInput(Enter, TEXT("A"),
			TEXT("/Niagara/DynamicInputs/Vectors/Position/DistanceBetweenPositions.DistanceBetweenPositions")) : nullptr;
		UNiagaraNodeFunctionCall* WithinDistance = Within ? SetDynamicInput(Within, TEXT("A"),
			TEXT("/Niagara/DynamicInputs/Vectors/Position/DistanceBetweenPositions.DistanceBetweenPositions")) : nullptr;
		const bool bStateLinked = EnterDistance && WithinDistance
			&& SetStaticSwitch(Enter, TEXT("Comparison"), TEXT("Less"), TEXT("Equal"))
			&& SetStaticSwitch(Within, TEXT("Comparison"), TEXT("Less"), TEXT("Equal"))
			&& LinkInput(Enter, TEXT("B"), TEXT("User.CaptureRadius"))
			&& LinkInput(Within, TEXT("B"), TEXT("User.EscapeRadius"))
			&& LinkInput(Hold, TEXT("Bool1"), TEXT("Particles.Captured"))
			&& LinkInput(EnterDistance, TEXT("A"), TEXT("Particles.Position"))
			&& LinkInput(EnterDistance, TEXT("B"), TEXT("User.AttractorPosition"))
			&& LinkInput(WithinDistance, TEXT("A"), TEXT("Particles.Position"))
			&& LinkInput(WithinDistance, TEXT("B"), TEXT("User.AttractorPosition"));
		if (!bStateLinked)
		{
			Error(TEXT("Failed to build debris state machine"));
			return false;
		}
	}

	// Captured debris keep orbiting inward without the player's energy-costing pull:
	//   value = max(Free, Captured ? Bound : Free)
	auto LinkCapturedOrFree = [&](UNiagaraNodeFunctionCall* Module, const TCHAR* InputName, const TCHAR* Free, const TCHAR* Bound)
	{
		if (bGPU)
		{
			return LinkInput(Module, InputName, Free);
		}
		UNiagaraNodeFunctionCall* Max = SetDynamicInput(Module, InputName,
			TEXT("/Niagara/DynamicInputs/Math/Max_Float.Max_Float"));
		UNiagaraNodeFunctionCall* Select = Max ? SetDynamicInput(Max, TEXT("B"),
			TEXT("/Niagara/DynamicInputs/TypeConversions/MakeCustomFloatFromBool.MakeCustomFloatFromBool")) : nullptr;
		return Select
			&& LinkInput(Max, TEXT("A"), Free)
			&& LinkInput(Select, TEXT("Bool"), TEXT("Particles.Captured"))
			&& LinkInput(Select, TEXT("True Float"), Bound)
			&& LinkInput(Select, TEXT("False Float"), Free);
	};

	UNiagaraNodeFunctionCall* Attractor = AddModule(TEXT("/Niagara/Modules/Update/Forces/V2/PointAttractionForce.PointAttractionForce"), ParticleUpdate, NextModule++);
	SetFloatInput(Attractor, UpdateScript, EmitterName, TEXT("Kill Radius"), 0.0f);
	// The pull is a spring (force grows with distance), so its reach is capped by gameplay.
	const bool bRadiusLinked = LinkInput(Attractor, TEXT("Attraction Radius"), TEXT("User.PullRadius"));
	const bool bPositionLinked = BindInput(Attractor, TEXT("Attractor Position"), FNiagaraTypeDefinition::GetPositionDef(), TEXT("User.AttractorPosition"));
	const bool bStrengthLinked = LinkCapturedOrFree(Attractor, TEXT("Attraction Strength"),
		TEXT("User.AttractionStrength"), TEXT("User.OrbitStrength"));
	if (!bRadiusLinked || !bPositionLinked || !bStrengthLinked)
	{
		Error(TEXT("Failed to link attraction controls"));
		return false;
	}

	// Accretion swirl: the third simple rule, driven by gameplay only while the player pulls.
	UNiagaraNodeFunctionCall* Vortex = AddModule(TEXT("/Niagara/Modules/Update/Forces/VortexForce.VortexForce"), ParticleUpdate, NextModule++);
	const bool bVortexLinked = LinkInput(Vortex, TEXT("Vortex Origin"), TEXT("User.AttractorPosition"))
		&& LinkCapturedOrFree(Vortex, TEXT("Vortex Force Amount"), TEXT("User.VortexStrength"), TEXT("User.OrbitVortex"));
	if (!bVortexLinked)
	{
		Error(TEXT("Failed to link vortex controls"));
		return false;
	}
	SetFloatInput(Vortex, UpdateScript, EmitterName, TEXT("Influence Falloff Radius"), 1200.0f);

	UNiagaraNodeFunctionCall* Drag = AddModule(TEXT("/Niagara/Modules/Update/Forces/Drag.Drag"), ParticleUpdate, NextModule++);
	SetFloatInput(Drag, UpdateScript, EmitterName, TEXT("Drag"), 0.7f);

	// Cooling: colour fades from hot to cold over User.CoolSeconds, matching the heat
	// value gameplay derives from the exported age. Captured debris glow User.CapturedColor.
	UNiagaraNodeFunctionCall* FadeOwner = ColorModule;
	const TCHAR* FadeInput = TEXT("Color");
	if (!bGPU && ColorModule)
	{
		UNiagaraNodeFunctionCall* StateColor = SetDynamicInput(ColorModule, TEXT("Color"),
			TEXT("/Niagara/DynamicInputs/LinearColor/LerpLinearColors.LerpLinearColors"));
		UNiagaraNodeFunctionCall* Tint = StateColor ? SetDynamicInput(StateColor, TEXT("LerpFactor"),
			TEXT("/Niagara/DynamicInputs/TypeConversions/MakeCustomFloatFromBool.MakeCustomFloatFromBool")) : nullptr;
		const bool bTintLinked = Tint
			&& LinkInput(StateColor, TEXT("EndColor"), TEXT("User.CapturedColor"))
			&& LinkInput(Tint, TEXT("Bool"), TEXT("Particles.Captured"))
			&& LinkInput(Tint, TEXT("True Float"), TEXT("User.CapturedTint"))
			&& LinkInput(Tint, TEXT("False Float"), TEXT("User.FreeTint"));
		if (!bTintLinked)
		{
			Error(TEXT("Failed to build captured colour"));
			return false;
		}
		FadeOwner = StateColor;
		FadeInput = TEXT("StartColor");
	}
	UNiagaraNodeFunctionCall* Fade = FadeOwner ? SetDynamicInput(FadeOwner, FadeInput,
		TEXT("/Niagara/DynamicInputs/LinearColor/LerpLinearColors.LerpLinearColors")) : nullptr;
	UNiagaraNodeFunctionCall* Clamp = Fade ? SetDynamicInput(Fade, TEXT("LerpFactor"),
		TEXT("/Niagara/DynamicInputs/Clamp/ClampFloat.ClampFloat")) : nullptr;
	UNiagaraNodeFunctionCall* AgeRatio = Clamp ? SetDynamicInput(Clamp, TEXT("Float"),
		TEXT("/Niagara/DynamicInputs/Divide/Divide_Float.Divide_Float")) : nullptr;
	const bool bFadeLinked = AgeRatio
		&& LinkInput(Fade, TEXT("StartColor"), TEXT("User.HotColor"))
		&& LinkInput(Fade, TEXT("EndColor"), TEXT("User.ColdColor"))
		&& LinkInput(AgeRatio, TEXT("A"), TEXT("Particles.Age"))
		&& LinkInput(AgeRatio, TEXT("B"), TEXT("User.CoolSeconds"));
	if (!bFadeLinked)
	{
		Error(TEXT("Failed to build cooling colour"));
		return false;
	}

	if (!bGPU)
	{
		UNiagaraNodeFunctionCall* Collision = AddModule(TEXT("/Niagara/Modules/Collision/Collision.Collision"), ParticleUpdate);
		// GPU collision uses the scene depth buffer; its default kills particles it cannot see,
		// which would make the port simulate a different (shrinking) population.
		UEdGraphPin* KillOffscreen = bPortToGPU && Collision ? Collision->FindPin(TEXT("Kill Offscreen Particles")) : nullptr;
		// GPU depth-buffer collision killed the whole population in testing; the chamber floor
		// is one plane, so the port collides analytically against it instead of tracing the scene.
		if (bPortToGPU && !SetStaticSwitch(Collision, TEXT("GPU Collision Type"), TEXT("Analytical"), TEXT("")))
		{
			return false;
		}
		if (KillOffscreen)
		{
			KillOffscreen->DefaultValue = TEXT("false");
			Collision->MarkNodeRequiresSynchronization(TEXT("EmberLab GPU collision"), true);
			Note(TEXT("Collision.Kill Offscreen Particles = false"));
		}
		else if (bPortToGPU)
		{
			Error(TEXT("GPU port: Kill Offscreen Particles switch not found"));
			return false;
		}
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
	const bool bCPUDynamic = BuildSystem(TEXT("NS_Debris_CPU_DynamicBounds"), false, 500, true);
	// Optimization comparison: the same debris emitter ported to GPU simulation.
	const bool bDebrisGPU = BuildSystem(TEXT("NS_Debris_GPUPort"), false, 500, false, true);
	const bool bMap = bGPU && bCPU && bCPUDynamic && bDebrisGPU && BuildMap();
	Note(FString::Printf(TEXT("RESULT: GPU=%s CPU=%s CPUDynamic=%s DebrisGPUPort=%s MAP=%s"),
		bGPU ? TEXT("PASS") : TEXT("FAIL"), bCPU ? TEXT("PASS") : TEXT("FAIL"),
		bCPUDynamic ? TEXT("PASS") : TEXT("FAIL"), bDebrisGPU ? TEXT("PASS") : TEXT("FAIL"),
		bMap ? TEXT("PASS") : TEXT("FAIL")));
	const FString ReportPath = FPaths::ProjectSavedDir() / TEXT("AssetBuildReport.txt");
	FFileHelper::SaveStringArrayToFile(Report, *ReportPath);
	return bGPU && bCPU && bCPUDynamic && bDebrisGPU && bMap ? 0 : 1;
}
