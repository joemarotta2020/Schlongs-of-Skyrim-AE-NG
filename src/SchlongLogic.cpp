#include "SchlongLogic.h"
#include "JM_AddonGenderValidation.h"

//Scale bones
static void SetNodeScaleImpl(RE::NiAVObject* a_root, const char* a_nodeName, float a_scale) {

	if (auto node = a_root->GetObjectByName(a_nodeName))
		node->local.scale = a_scale;
}

//Function to make the penis look good
inline static float Rescale(float a_scale, float a_factor) {

	return 1.0f + (a_scale - 1.0f) * a_factor;
}

static uint8_t GenerateGaussianRank(int target, std::mt19937& rng) {

	std::normal_distribution<float> dist(static_cast<float>(target), g_SOSGaussianDeviation);

	int rank = static_cast<int>(std::round(dist(rng)));

	return static_cast<uint8_t>(std::clamp(rank, 1, 20));
}

static std::uint8_t GenerateRandomRank(int a_targetSize) {

	if (a_targetSize == -1) {

		static std::uniform_int_distribution<int> dist(1, 20);
		return static_cast<std::uint8_t>(dist(g_rng));
	}
	return GenerateGaussianRank(a_targetSize, g_rng);
}

namespace SchlongLogic {

	void ScaleSchlongBones(RE::Actor* a_actor) {
		if (!a_actor || !a_actor->Is3DLoaded())
			return;

		RE::FormID actorID = a_actor->GetFormID();

		SKSE::GetTaskInterface()->AddTask([actorID]() {
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
			if (!actor || !actor->Is3DLoaded())
				return;

			auto* root = actor->Get3D();
			if (!root)
				return;

			auto* base = actor->GetActorBase();
			if (!base)
				return;

			auto* npcData = Storage::GetNPCData(base->GetFormID());
			const auto* addonData = npcData ? Storage::GetAddonBoneData(npcData->addonName) : nullptr;

			float sizeFactor = npcData ? (static_cast<float>(npcData->rank) / 20.0f) : 1.0f;

			for (std::size_t i = 0; i < sNiNodes.size(); ++i) {
				float targetScale = 1.0f;

				if (addonData)
					targetScale = Rescale(addonData->bones[i], sizeFactor);

				SetNodeScaleImpl(root, sNiNodes[i], targetScale);
			}
			});
	}

	static void GetCompatibleCandidates(RE::TESNPC* a_base, std::vector<AddonCandidate>& outCandidates) {

		outCandidates.clear();

		if (!a_base)
			return;

		RE::TESRace* race = a_base->GetRace();
		if (!race)
			return;

		RE::BSFixedString raceName = race->GetFormEditorID();
		if (raceName.empty())
			raceName = std::format("0x{:08X}", race->GetFormID()).c_str();

		int sex = a_base->GetSex();
		const auto& genderGroups = Storage::GetGenderGroups();
		const std::vector<RE::BSFixedString>* targetGroup = nullptr;

		if (sex == RE::SEX::kMale)
			targetGroup = &genderGroups.male;
		else if (sex == RE::SEX::kFemale)
			targetGroup = &genderGroups.female;

		if (!targetGroup || targetGroup->empty())
			return;

		const auto& allAddons = Storage::GetAddons();
		outCandidates.reserve(targetGroup->size());

		for (const RE::BSFixedString& addonName : *targetGroup) {
			auto itAddon = allAddons.find(addonName);
			if (itAddon == allAddons.end())
				continue;

			const auto& raceMap = itAddon->second.compatibleRaces;
			auto itRace = raceMap.find(raceName);

			if (itRace != raceMap.end()) {
				const auto& raceData = itRace->second;
				if (raceData.Enabled) {
					outCandidates.emplace_back(addonName, raceData.Chance, raceData.Rank);
				}
			}
		}
	}

	static RE::TESObjectARMO* ResolveAddonArmor(const RE::BSFixedString& a_addonName) {

		if (a_addonName.empty())
			return nullptr;

		std::string editorID = "SOS_Addon_";
		editorID += a_addonName.c_str();
		editorID += "_Genitals";
		return RE::TESForm::LookupByEditorID<RE::TESObjectARMO>(editorID);
	}

	static bool IsResolvableAddonArmor(RE::TESObjectARMO* a_armor) {

		return a_armor != nullptr;
	}

	RE::TESObjectARMO* ResolveCachedAddon(RE::FormID a_baseID) {

		auto* data = Storage::GetNPCData(a_baseID);
		if (!data || Storage::IsNoneAddonName(data->addonName))
			return nullptr;

		auto* armor = ResolveAddonArmor(data->addonName);

		if (!IsResolvableAddonArmor(armor)) {
			SKSE::log::warn("SOS: Cached addon '{}' cannot be resolved for NPC Base FormID {:08X}. Clearing cached assignment.", data->addonName, a_baseID);
			Storage::ClearNPCData(a_baseID);
			return nullptr;
		}

		return armor;
	}

	RE::TESObjectARMO* DetermineWinningAddon(RE::Actor* a_actor) {

		if (!a_actor)
			return nullptr;

		auto* npcBase = a_actor->GetActorBase();
		if (!npcBase)
			return nullptr;

		const RE::FormID baseID = npcBase->GetFormID();
		bool migratedStaleNone = false;
		bool repairedCrossGender = false;

		// A cached non-NONE assignment is authoritative if its addon still resolves and
		// still belongs to the actor's current SOS gender group. Manual MCM/JSON overrides
		// remain authoritative even when they intentionally cross that gender boundary.
		// Historically, SOS also serialized random NONE as an empty addon name. That made
		// old NONE results permanent even after probabilities/configuration were changed.
		if (Storage::HasNPCData(baseID)) {
			auto* cached = Storage::GetNPCData(baseID);

			if (cached && Storage::IsNoneAddonName(cached->addonName)) {
				if (npcBase->HasKeywordString(NPCKW) || Storage::HasExplicitNoneOverride(baseID))
					return nullptr;

				SKSE::log::info(
					"SOS: migrating stale cached NONE-like assignment for actor '{}' base {:08X}; rerolling from current compatible addons",
					a_actor->GetName(),
					baseID);
				Storage::ClearNPCData(baseID);
				migratedStaleNone = true;
			}
			else if (cached && JMAddonGenderValidation::ShouldReplaceCachedAddon(npcBase, cached->addonName)) {
				SKSE::log::warn(
					"SOS: rejecting cached cross-gender addon '{}' for actor '{}' base {:08X}; rerolling from current gender group",
					cached->addonName,
					a_actor->GetName(),
					baseID);
				Storage::ClearNPCData(baseID);
				repairedCrossGender = true;
			}
			else {
				if (auto* cachedArmor = ResolveCachedAddon(baseID))
					return cachedArmor;

				// ResolveCachedAddon clears missing/invalid cached addon data. Fall through
				// and rebuild the assignment from the current configuration immediately.
				SKSE::log::info(
					"SOS: rebuilding invalid cached addon for actor '{}' base {:08X}",
					a_actor->GetName(),
					baseID);
			}
		}

		// SOS_NoneDefault remains an intentional hard opt-out.
		if (npcBase->HasKeywordString(NPCKW)) {
			Storage::SetNPCAddonData(baseID, "", 1);
			return nullptr;
		}

		std::vector<AddonCandidate> candidates;
		GetCompatibleCandidates(npcBase, candidates);

		if (candidates.empty())
			return nullptr;

		struct ResolvedCandidate {
			AddonCandidate candidate;
			RE::TESObjectARMO* armor{ nullptr };
		};

		std::vector<ResolvedCandidate> resolved;
		resolved.reserve(candidates.size());
		std::uint32_t totalWeight = 0;

		for (const auto& candidate : candidates) {
			auto* armor = ResolveAddonArmor(candidate.name);
			if (!IsResolvableAddonArmor(armor)) {
				SKSE::log::warn(
					"SOS: skipping enabled addon '{}' for actor '{}' because its addon armor cannot be resolved",
					candidate.name,
					a_actor->GetName());
				continue;
			}

			resolved.push_back({ candidate, armor });
			totalWeight += candidate.probability;
		}

		if (resolved.empty()) {
			SKSE::log::warn(
				"SOS: no valid compatible genital addons remain for actor '{}' base {:08X}",
				a_actor->GetName(),
				baseID);
			return nullptr;
		}

		std::size_t selectedIndex = 0;

		// NON-NONE invariant: once at least one enabled, race-compatible genital addon
		// exists, Chance values are weights between those addons. NONE is not part of
		// the random draw. This makes 100+100 exactly 50/50 and also prevents an
		// accidental NONE gap when enabled weights total less than 100.
		if (totalWeight > 0) {
			std::uniform_int_distribution<std::uint32_t> dist(1, totalWeight);
			const auto roll = dist(g_rng);
			std::uint32_t accum = 0;

			for (std::size_t i = 0; i < resolved.size(); ++i) {
				accum += resolved[i].candidate.probability;
				if (roll <= accum) {
					selectedIndex = i;
					break;
				}
			}
		}
		else {
			// If every enabled compatible addon is configured at zero, still honor the
			// non-NONE invariant by choosing uniformly rather than manufacturing NONE.
			std::uniform_int_distribution<std::size_t> dist(0, resolved.size() - 1);
			selectedIndex = dist(g_rng);
		}

		const auto& selected = resolved[selectedIndex];
		const std::uint8_t generatedRank = GenerateRandomRank(selected.candidate.targetRank);
		Storage::SetNPCAddonData(baseID, selected.candidate.name, generatedRank);

		if (migratedStaleNone) {
			SKSE::log::info(
				"SOS: stale NONE repaired for actor '{}' base {:08X} -> addon '{}'",
				a_actor->GetName(),
				baseID,
				selected.candidate.name);
		}

		if (repairedCrossGender) {
			SKSE::log::info(
				"SOS: corrected cross-gender assignment for actor '{}' base {:08X} -> addon '{}'",
				a_actor->GetName(),
				baseID,
				selected.candidate.name);
		}

		return selected.armor;
	}

	static void ScaleSchlongBonesAE(RE::StaticFunctionTag*, RE::Actor* a_actor, float) {

		SchlongLogic::ScaleSchlongBones(a_actor);
	}

	static RE::TESObjectARMO* GetWinningAddon(RE::StaticFunctionTag*, RE::Actor* a_actor) {

		return SchlongLogic::DetermineWinningAddon(a_actor);
	}

	static std::vector<RE::BSFixedString> GetCompatibleAddonNames(RE::StaticFunctionTag*, RE::Actor* a_actor) {

		std::vector<RE::BSFixedString> names;

		if (!a_actor)
			return names;

		auto* base = a_actor->GetActorBase();
		if (!base)
			return names;

		names.push_back("None");

		std::vector<AddonCandidate> candidates;
		GetCompatibleCandidates(base, candidates);

		names.reserve(candidates.size() + 1);

		for (const auto& c : candidates) {
			names.push_back(c.name);
		}

		return names;
	}

	static std::vector<RE::BSFixedString> GetAllAddonNamesForGender(RE::StaticFunctionTag*, RE::Actor* a_actor) {

		std::vector<RE::BSFixedString> result;
		result.emplace_back("None");

		if (!a_actor)
			return result;

		auto* base = a_actor->GetActorBase();
		if (!base)
			return result;

		const auto& allAddons = Storage::GetAddons();
		const auto& genderGroups = Storage::GetGenderGroups();

		bool actorIsMale = (base->GetSex() == RE::SEX::kMale);
		const auto& targetGenderList = actorIsMale ? genderGroups.male : genderGroups.female;

		for (const auto& name : targetGenderList) {

			if (allAddons.contains(name))
				result.emplace_back(name);
		}

		return result;
	
	}

	static float GetGaussianDeviation(RE::StaticFunctionTag*) {
		
		return g_SOSGaussianDeviation;
	}

	static void SetGaussianDeviation(RE::StaticFunctionTag*, float a_value) {

		if (a_value < 0)
			return;

		g_SOSGaussianDeviation = a_value;

		g_JSONDefaults["GaussianDeviation"] = a_value;

		CSimpleIniA ini;
		ini.SetUnicode();

		if (ini.LoadFile(INIPath) >= 0) {
			ini.SetDoubleValue("JSON", "GaussianDeviation", static_cast<double>(a_value));
			ini.SaveFile(INIPath);
			SKSE::log::info("SOS: Gaussian Deviation updated to {} via MCM.", a_value);
		}
	}

	bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm) {

		if (!a_vm)
			return false;

		a_vm->RegisterFunction("GetWinningAddon", SosPapyrusScript, GetWinningAddon);
		a_vm->RegisterFunction("ScaleSchlongBonesAE", SosPapyrusScript, ScaleSchlongBonesAE);
		a_vm->RegisterFunction("GetCompatibleAddonNames", SosPapyrusScript, GetCompatibleAddonNames);
		a_vm->RegisterFunction("GetAllAddonNamesForGender", SosPapyrusScript, GetAllAddonNamesForGender);
		a_vm->RegisterFunction("GetGaussianDeviation", SosPapyrusScript, GetGaussianDeviation);
		a_vm->RegisterFunction("SetGaussianDeviation", SosPapyrusScript, SetGaussianDeviation);

		return true;
	}
}
