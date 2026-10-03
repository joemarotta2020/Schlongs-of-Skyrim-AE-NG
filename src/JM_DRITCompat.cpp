#include "JM_DRITCompat.h"

#include "SchlongLogic.h"
#include "Storage.h"
#include "util.h"

namespace {
	constexpr const char* kPapyrusScript = "JM_SOS_DRIT_API";

	struct ForcedCandidate {
		RE::BSFixedString name;
		RE::TESObjectARMO* armor{ nullptr };
		std::uint8_t chance{ 0 };
		std::int8_t targetRank{ 10 };
	};

	std::mt19937& RNG()
	{
		static std::mt19937 rng(std::random_device{}());
		return rng;
	}

	std::uint8_t GenerateRank(std::int8_t a_targetRank)
	{
		if (a_targetRank < 0) {
			std::uniform_int_distribution<int> dist(1, 20);
			return static_cast<std::uint8_t>(dist(RNG()));
		}

		std::normal_distribution<float> dist(static_cast<float>(a_targetRank), g_SOSGaussianDeviation);
		const int rank = static_cast<int>(std::round(dist(RNG())));
		return static_cast<std::uint8_t>(std::clamp(rank, 1, 20));
	}

	RE::TESObjectARMO* ResolveAddonArmor(const RE::BSFixedString& a_addonName)
	{
		if (a_addonName.empty())
			return nullptr;

		std::string editorID = "SOS_Addon_";
		editorID += a_addonName.c_str();
		editorID += "_Genitals";
		return RE::TESForm::LookupByEditorID<RE::TESObjectARMO>(editorID);
	}

	bool IsRealGenitalAddon(RE::TESObjectARMO* a_armor)
	{
		return a_armor &&
			Util::ArmorHasKeyword(a_armor, GenKW) &&
			!Util::ArmorHasKeyword(a_armor, PubKW);
	}

	void StripCorpseUnderwear(RE::Actor* a_actor)
	{
		if (!a_actor || !a_actor->IsDead())
			return;

		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		auto inventory = a_actor->GetInventory();

		for (const auto& [form, data] : inventory) {
			if (!form)
				continue;

			auto* armor = form->As<RE::TESObjectARMO>();
			if (!armor || !Util::ArmorHasKeyword(armor, UndwKW))
				continue;

			const auto& [count, entry] = data;
			if (count <= 0)
				continue;

			if (equipManager && entry && entry->IsWorn())
				equipManager->UnequipObject(a_actor, armor);

			// DRIT corpses must never reacquire SOS underwear through a delayed outfit
			// refresh. Remove every copy rather than merely hiding/unequipping it.
			a_actor->RemoveItem(
				armor,
				count,
				RE::ITEM_REMOVE_REASON::kRemove,
				nullptr,
				nullptr);
		}
	}

	RE::TESObjectARMO* SelectForcedNonNoneAddon(RE::Actor* a_actor)
	{
		if (!a_actor)
			return nullptr;

		auto* base = a_actor->GetActorBase();
		if (!base)
			return nullptr;

		auto* race = base->GetRace();
		if (!race)
			return nullptr;

		RE::BSFixedString raceName = race->GetFormEditorID();
		if (raceName.empty())
			raceName = std::format("0x{:08X}", race->GetFormID()).c_str();

		const auto& genderGroups = Storage::GetGenderGroups();
		const std::vector<RE::BSFixedString>* genderGroup = nullptr;

		if (base->GetSex() == RE::SEX::kMale)
			genderGroup = &genderGroups.male;
		else if (base->GetSex() == RE::SEX::kFemale)
			genderGroup = &genderGroups.female;

		if (!genderGroup || genderGroup->empty())
			return nullptr;

		const auto& addons = Storage::GetAddons();
		std::vector<ForcedCandidate> candidates;
		candidates.reserve(genderGroup->size());

		std::uint32_t totalPositiveWeight = 0;

		for (const auto& addonName : *genderGroup) {
			auto addonIt = addons.find(addonName);
			if (addonIt == addons.end())
				continue;

			auto raceIt = addonIt->second.compatibleRaces.find(raceName);
			if (raceIt == addonIt->second.compatibleRaces.end() || !raceIt->second.Enabled)
				continue;

			auto* armor = ResolveAddonArmor(addonName);
			if (!IsRealGenitalAddon(armor))
				continue;

			ForcedCandidate candidate{
				addonName,
				armor,
				raceIt->second.Chance,
				raceIt->second.Rank
			};
			candidates.push_back(candidate);
			totalPositiveWeight += candidate.chance;
		}

		if (candidates.empty())
			return nullptr;

		std::size_t selectedIndex = 0;

		if (totalPositiveWeight > 0) {
			std::uniform_int_distribution<std::uint32_t> dist(1, totalPositiveWeight);
			const auto roll = dist(RNG());
			std::uint32_t running = 0;

			for (std::size_t i = 0; i < candidates.size(); ++i) {
				running += candidates[i].chance;
				if (roll <= running) {
					selectedIndex = i;
					break;
				}
			}
		}
		else {
			// Hard corpse invariant: if compatible genital addons are enabled but all
			// have zero chance, choose uniformly instead of leaving an exposed NONE.
			std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
			selectedIndex = dist(RNG());
		}

		const auto& selected = candidates[selectedIndex];
		Storage::SetNPCAddonData(
			base->GetFormID(),
			selected.name,
			GenerateRank(selected.targetRank));

		SKSE::log::info(
			"JM DRIT: forced transient non-NONE addon '{}' for dead actor '{}' ({:08X})",
			selected.name,
			a_actor->GetName(),
			a_actor->GetFormID());

		return selected.armor;
	}

	void RefreshCorpse3D(RE::Actor* a_actor, RE::TESObjectARMO* a_schlong)
	{
		if (!a_actor || !a_schlong)
			return;

		const auto slot = RE::BIPED_MODEL::BipedObjectSlot::kModPelvisSecondary;
		auto* wornSlot52 = a_actor->GetWornArmor(slot);
		const bool wornExpected = wornSlot52 == a_schlong;
		const bool loaded3D = a_actor->Is3DLoaded();
		bool refreshQueued = false;

		// EquipObject can update inventory/equipment state before the corpse's existing
		// NiNode has attached the ArmorAddon. Force one corpse-only 3D reset so the
		// rendered state catches up with the authoritative SOS assignment.
		if (loaded3D) {
			a_actor->DoReset3D(false);
			refreshQueued = true;
		}

		SKSE::log::info(
			"JM DRIT: corpse enforce actor='{}' ({:08X}) addon='{}' ({:08X}) worn={} 3D={} reset3D={}",
			a_actor->GetName(),
			a_actor->GetFormID(),
			a_schlong->GetName(),
			a_schlong->GetFormID(),
			wornExpected,
			loaded3D,
			refreshQueued);
	}

	void ApplySOSFactions(RE::Actor* a_actor, RE::TESNPC* a_base)
	{
		if (!a_actor || !a_base)
			return;

		if (g_schlongifiedFaction)
			a_actor->AddToFaction(g_schlongifiedFaction, 0);

		if (g_SexLabGenderFaction && Util::IsFemale(a_base)) {
			if (g_SexLabForceMaleOnSchlong)
				a_actor->AddToFaction(g_SexLabGenderFaction, 0);
			else
				a_actor->AddToFaction(g_SexLabGenderFaction, 1);
		}
	}

	bool EnsureCorpseSchlongImpl(RE::Actor* a_actor, bool a_forceNonNone)
	{
		if (!a_actor || !a_actor->IsDead())
			return false;

		auto* base = a_actor->GetActorBase();
		if (!base)
			return false;

		// First eliminate the late-underwear path completely. This is intentionally
		// corpse-only and never changes living-NPC underwear behavior.
		StripCorpseUnderwear(a_actor);

		// Preserve SOS's existing assignment/draw when it succeeds. If the actor died
		// before activation or a female legitimately rolled NONE, force a real,
		// race-compatible genital addon when requested.
		auto* schlong = SchlongLogic::DetermineWinningAddon(a_actor);
		if ((!IsRealGenitalAddon(schlong)) && a_forceNonNone)
			schlong = SelectForcedNonNoneAddon(a_actor);

		if (!IsRealGenitalAddon(schlong)) {
			SKSE::log::warn(
				"JM DRIT: no compatible genital addon available for dead actor '{}' ({:08X})",
				a_actor->GetName(),
				a_actor->GetFormID());
			return false;
		}

		ApplySOSFactions(a_actor, base);

		// Respect legitimate surviving armor that occupies Slot 52. Its unequip event
		// will restore the already-cached addon later. DRIT should not silently strip
		// intact armor just to force visual exposure.
		auto* slot52Armor = a_actor->GetWornArmor(RE::BIPED_MODEL::BipedObjectSlot::kModPelvisSecondary);
		if (slot52Armor && !Util::ArmorHasKeyword(slot52Armor, GenKW)) {
			SKSE::log::debug(
				"JM DRIT: addon assignment ensured for '{}', but surviving Slot 52 armor '{}' remains equipped",
				a_actor->GetName(),
				slot52Armor->GetName());
			SchlongLogic::ScaleSchlongBones(a_actor);
			return true;
		}

		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager)
			return false;

		// Remove stale/pube/genital inventory instances, then add exactly one current
		// addon and force the immediate native equip path used by SOS AE-NG itself.
		Util::RemoveSOSItemsFromInventory(a_actor);
		a_actor->AddObjectToContainer(schlong, nullptr, 1, nullptr);
		equipManager->EquipObject(
			a_actor,
			schlong,
			nullptr,
			1,
			nullptr,
			true,   // preventUnequip
			false,  // playSound
			true,   // immediate
			true);  // applyNow

		RefreshCorpse3D(a_actor, schlong);
		SchlongLogic::ScaleSchlongBones(a_actor);
		return true;
	}

	bool EnsureCorpseSchlongPapyrus(RE::StaticFunctionTag*, RE::Actor* a_actor, bool a_forceNonNone)
	{
		return EnsureCorpseSchlongImpl(a_actor, a_forceNonNone);
	}
}

namespace JMDRITCompat {
	bool EnsureCorpseSchlong(RE::Actor* a_actor, bool a_forceNonNone)
	{
		return EnsureCorpseSchlongImpl(a_actor, a_forceNonNone);
	}

	bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm)
			return false;

		a_vm->RegisterFunction("EnsureCorpseSchlong", kPapyrusScript, EnsureCorpseSchlongPapyrus);
		return true;
	}
}
