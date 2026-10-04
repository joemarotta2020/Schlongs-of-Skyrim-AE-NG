#include "Schlongification.h"
#include "JM_DRITCompat.h"

namespace SOS {

	inline std::unordered_set<RE::FormID> g_activeActors;

	static bool IsRealGenitalAddon(RE::TESObjectARMO* a_armor) {
		return a_armor && Util::ArmorHasKeyword(a_armor, GenKW) && !Util::ArmorHasKeyword(a_armor, PubKW);
	}

	static void UpdateAssignmentFactions(RE::Actor* a_actor, RE::TESNPC* a_base, RE::TESObjectARMO* a_schlong) {
		if (!a_actor || !a_base)
			return;

		const bool hasRealSchlong = IsRealGenitalAddon(a_schlong);

		if (g_schlongifiedFaction)
			a_actor->AddToFaction(g_schlongifiedFaction, hasRealSchlong ? 0 : -1);

		if (g_SexLabGenderFaction && Util::IsFemale(a_base)) {
			if (hasRealSchlong && g_SexLabForceMaleOnSchlong)
				a_actor->AddToFaction(g_SexLabGenderFaction, 0);
			else
				a_actor->AddToFaction(g_SexLabGenderFaction, 1);
		}
	}

	static bool EquipAssignedAddonIfExposed(RE::Actor* a_actor, const char* a_reason) {
		if (!a_actor || !a_actor->Is3DLoaded())
			return false;

		auto* base = a_actor->GetActorBase();
		if (!base)
			return false;

		if (Util::ActorHasEquippedArmorWithKeyword(a_actor, UndwKW))
			return false;

		const auto slot = RE::BIPED_MODEL::BipedObjectSlot::kModPelvisSecondary;
		auto* wornSlot52 = a_actor->GetWornArmor(slot);

		// A real clothing/armor blocker owns Slot 52 until it is actually removed.
		if (wornSlot52 && !Util::ArmorHasKeyword(wornSlot52, GenKW))
			return false;

		auto* schlong = SchlongLogic::DetermineWinningAddon(a_actor);
		UpdateAssignmentFactions(a_actor, base, schlong);
		if (!schlong)
			return false;

		if (wornSlot52 == schlong) {
			SchlongLogic::ScaleSchlongBones(a_actor);
			return true;
		}

		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager)
			return false;

		const RE::FormID refID = a_actor->GetFormID();
		g_activeActors.insert(refID);

		Util::RemoveSOSItemsFromInventory(a_actor);
		a_actor->AddObjectToContainer(schlong, nullptr, 1, nullptr);
		equipManager->EquipObject(
			a_actor,
			schlong,
			nullptr,
			1,
			nullptr,
			true,
			false,
			true,
			true);

		g_activeActors.erase(refID);
		SchlongLogic::ScaleSchlongBones(a_actor);

		const bool visible = a_actor->GetWornArmor(slot) == schlong;
		if (visible) {
			SKSE::log::info(
				"SOS: restored visible genital addon '{}' for actor '{}' ({:08X}) reason={}",
				schlong->GetName(),
				a_actor->GetName(),
				refID,
				a_reason ? a_reason : "unknown");
		}
		else {
			SKSE::log::warn(
				"SOS: genital restore did not stick for actor '{}' ({:08X}) reason={}",
				a_actor->GetName(),
				refID,
				a_reason ? a_reason : "unknown");
		}

		return visible;
	}

	static void QueueVisibilityStabilization(RE::FormID a_refID, std::uint8_t a_passesRemaining) {
		if (a_refID == 0 || a_passesRemaining == 0)
			return;

		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface)
			return;

		taskInterface->AddTask([a_refID, a_passesRemaining]() {
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_refID);
			if (!actor)
				return;

			if (actor->IsDead()) {
				JMDRITCompat::EnsureCorpseSchlong(actor, true);
				return;
			}

			EquipAssignedAddonIfExposed(actor, "deferred-slot52-stabilization");

			if (a_passesRemaining > 1)
				QueueVisibilityStabilization(a_refID, static_cast<std::uint8_t>(a_passesRemaining - 1));
		});
	}

	static void OnActorActivated(RE::Actor* a_actor) {

		if (!a_actor || !a_actor->Is3DLoaded())
			return;

		auto* base = a_actor->GetActorBase();
		if (!base)
			return;

		auto* schlong = SchlongLogic::DetermineWinningAddon(a_actor);
		UpdateAssignmentFactions(a_actor, base, schlong);
		if (!schlong)
			return;

		// If the actor is currently concealed, keep the authoritative assignment and
		// wait for the blocker to leave.  The equip-event stabilizer will restore the
		// addon after external strip systems (including SexLab) finish their pass.
		if (Util::HasSomethingInSlot52(a_actor) || Util::ActorHasEquippedArmorWithKeyword(a_actor, UndwKW)) {
			SchlongLogic::ScaleSchlongBones(a_actor);
			return;
		}

		EquipAssignedAddonIfExposed(a_actor, "actor-activation");
	}

	//Handles equip/unequip of armor with the Underwear keyword.
	//It hides or restores the Addon without ever touching Slot52 itself.
	static void HandleUnderwearChange(RE::Actor* a_actor, bool is_equipping) {

		if (!a_actor)
			return;

		if (is_equipping) {

			//Nothing to hide if the Slot52 is already empty
			if (!Util::HasSomethingInSlot52(a_actor))
				return;

			//Only removes the physical Addon item, NPC data (rank/addon) stays untouched
			Util::RemoveSOSItemsFromInventory(a_actor);
			return;
		}

		// Underwear removed: defer restoration until the external equipment operation
		// has fully settled.  This prevents SexLab/other strip loops from immediately
		// stripping the addon we just restored.
		QueueVisibilityStabilization(a_actor->GetFormID(), 8);
	}

	static void OnWearChange(RE::Actor* a_actor, const RE::TESEquipEvent* a_event, bool is_equipping) {

		auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_event->baseObject);
		if (!armor)
			return;

		auto* biped = armor->As<RE::BGSBipedObjectForm>();
		if (!biped)
			return;

		// Underwear never wins on a corpse. Delayed outfit refreshes are the source
		// of the late-underwear regression; intercept them at SOS's native equip event
		// and remove/reassert immediately. Living actors keep normal SOS behavior.
		if (Util::ArmorHasKeyword(armor, UndwKW)) {
			if (a_actor->IsDead() && is_equipping) {
				JMDRITCompat::EnsureCorpseSchlong(a_actor, true);
				return;
			}

			HandleUnderwearChange(a_actor, is_equipping);
			return;
		}

		SlotMask mask = static_cast<SlotMask>(biped->GetSlotMask().get());
		if ((mask & SLOT_52) == 0)
			return;

		auto* base = a_actor->GetActorBase();
		if (!base)
			return;

		if (is_equipping) {
			SchlongLogic::ScaleSchlongBones(a_actor);
			return;
		}

		// Keep corpse handling on the DRIT path and never turn a genital removal
		// generated by DRIT itself into a recursive re-enforcement loop.
		if (a_actor->IsDead()) {
			if (!Util::ArmorHasKeyword(armor, GenKW))
				JMDRITCompat::EnsureCorpseSchlong(a_actor, true);
			return;
		}

		// Any living Slot-52 release can be part of a multi-item external strip pass.
		// This includes the genital addon itself: SexLab may strip it directly.  Never
		// try to win that race synchronously inside the unequip event.  Reassert the
		// authoritative assignment over the next few main-thread frames instead.
		QueueVisibilityStabilization(a_actor->GetFormID(), 8);
	}

	RE::BSEventNotifyControl ObjectLoadedHandler::ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) {

		if (!a_event || !a_event->loaded)
			return RE::BSEventNotifyControl::kContinue;

		auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_event->formID);
		if (actor)
			OnActorActivated(actor);

		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl InitScriptHandler::ProcessEvent(const RE::TESInitScriptEvent* a_event, RE::BSTEventSource<RE::TESInitScriptEvent>*) {

		if (!a_event)
			return RE::BSEventNotifyControl::kContinue;

		auto* refr = a_event->objectInitialized.get();
		if (refr) {
			auto* actor = refr->As<RE::Actor>();
			if (actor)
				OnActorActivated(actor);
		}

		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl EquipEventHandler::ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) {

		if (!a_event || !a_event->actor)
			return RE::BSEventNotifyControl::kContinue;

		RE::FormID refID = a_event->actor->GetFormID();

		if (g_activeActors.contains(refID))
			return RE::BSEventNotifyControl::kContinue;

		auto* actor = a_event->actor->As<RE::Actor>();
		if (actor)
			OnWearChange(actor, a_event, a_event->equipped);

		return RE::BSEventNotifyControl::kContinue;
	}

	void RegisterActivationEvents() {
		auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
		if (holder) {
			static ObjectLoadedHandler g_objectLoadedHandler;
			static InitScriptHandler g_initScriptHandler;

			holder->AddEventSink(&g_objectLoadedHandler);
			holder->AddEventSink(&g_initScriptHandler);
			SKSE::log::info("SOS: Activation Events (ObjectLoad + InitScript) Registered");
		}
	}

	void RegisterEquipEvent() {
		auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
		if (holder) {
			static EquipEventHandler g_equipEventHandler;
			holder->AddEventSink(&g_equipEventHandler);
			SKSE::log::info("SOS: EquipEvent registered");
		}
	}
}
