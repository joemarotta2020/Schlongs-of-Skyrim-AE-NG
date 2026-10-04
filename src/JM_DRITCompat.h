#pragma once

#include "PCH.h"

namespace JMDRITCompat {
	bool EnsureActorSchlong(RE::Actor* a_actor, bool a_forceNonNone);
	bool EnsureCorpseSchlong(RE::Actor* a_actor, bool a_forceNonNone);
	bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm);
}
