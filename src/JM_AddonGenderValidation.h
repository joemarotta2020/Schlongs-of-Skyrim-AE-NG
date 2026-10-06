#pragma once

#include "Actors.h"
#include "Storage.h"

#include <algorithm>
#include <format>
#include <string>

namespace JMAddonGenderValidation {
	inline bool HasExplicitOverride(RE::TESNPC* a_base)
	{
		if (!a_base)
			return false;

		const auto* file = a_base->GetFile(0);
		if (!file)
			return false;

		const std::string baseKey = std::format("{:X}|{}", a_base->GetLocalFormID(), file->GetFilename());
		for (const auto& [key, data] : g_npcOverrides) {
			(void)data;
			const std::string keyStr = key.c_str();
			if (keyStr == baseKey || keyStr.rfind(baseKey + "|", 0) == 0)
				return true;
		}

		return false;
	}

	inline const std::vector<RE::BSFixedString>* GetCurrentGenderGroup(RE::TESNPC* a_base)
	{
		if (!a_base)
			return nullptr;

		const auto& groups = Storage::GetGenderGroups();
		if (a_base->GetSex() == RE::SEX::kMale)
			return &groups.male;
		if (a_base->GetSex() == RE::SEX::kFemale)
			return &groups.female;
		return nullptr;
	}

	inline bool IsAddonAllowedForCurrentGender(RE::TESNPC* a_base, const RE::BSFixedString& a_addonName)
	{
		const auto* group = GetCurrentGenderGroup(a_base);
		if (!group)
			return false;

		return std::find(group->begin(), group->end(), a_addonName) != group->end();
	}

	inline bool ShouldReplaceCachedAddon(RE::TESNPC* a_base, const RE::BSFixedString& a_addonName)
	{
		if (!a_base || a_addonName.empty())
			return false;

		if (HasExplicitOverride(a_base))
			return false;

		return !IsAddonAllowedForCurrentGender(a_base, a_addonName);
	}
}
