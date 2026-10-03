Scriptname JM_SOS_DRIT_API Hidden

; Native compatibility bridge supplied by the JM SOS AE-NG DRIT patch.
; Corpse-only: strips SOS_Underwear, ensures a transient runtime assignment,
; and equips a real genital addon when Slot 52 is not occupied by surviving armor.
Bool Function EnsureCorpseSchlong(Actor akActor, Bool abForceNonNone) Global Native
