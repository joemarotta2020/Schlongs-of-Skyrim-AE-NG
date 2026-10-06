#pragma once

#include <algorithm>

namespace JMAddonGenderValidation {
	template <class Range, class Name>
	bool ContainsAddon(const Range& a_group, const Name& a_name)
	{
		return std::find(a_group.begin(), a_group.end(), a_name) != a_group.end();
	}

	template <class Range, class Name>
	bool ShouldReplaceCachedAddon(bool a_hasExplicitOverride, const Range& a_genderGroup, const Name& a_addonName)
	{
		return !a_hasExplicitOverride && !ContainsAddon(a_genderGroup, a_addonName);
	}
}
