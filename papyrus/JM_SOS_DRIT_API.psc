Scriptname JM_SOS_DRIT_API Hidden

; Native compatibility bridge supplied by the JM SOS AE-NG patch.
; Living actors: establishes a real non-NONE SOS assignment and equips it immediately
; when unblocked; otherwise the existing SOS equip event reveals it when armor is stripped.
Bool Function EnsureActorSchlong(Actor akActor, Bool abForceNonNone) Global Native

; Corpse-only compatibility entry point retained for DRIT.
Bool Function EnsureCorpseSchlong(Actor akActor, Bool abForceNonNone) Global Native
