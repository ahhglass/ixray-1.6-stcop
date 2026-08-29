#pragma once

#include "../xrSound/Sound.h"
#include "../xrCore/xr_ini.h"
#include "HudSound.h"

class CObject;
struct SHit;
class CEntityAlive;
class CActor;
class CCustomOutfit;
class CHelmet;

struct SHitEffectSettings
{
	bool enable_sounds = true;
	bool enable_cam_effects = true;
	bool enable_actor_sounds = true;
	bool enable_npc_sounds = true;
};

enum EArmorType
{
	eArmorNone = 0,
	eArmorLight,
	eArmorMedium,
	eArmorHeavy,
	eArmorExo,
	eArmorCount
};

enum EHelmetType
{
	eHelmetNone = 0,
	eHelmetGlass,
	eHelmetGasmask,
	eHelmetHelmet,
	eHelmetExo,
	eHelmetCount
};

enum ESoundType
{
	eSoundClank = 0,
	eSoundRustle,
	eSoundJump,
	eSoundDeflectionArmor,
	eSoundDeflectionHelmet,
	eSoundCount
};

struct SProtectionInfo
{
	bool has_protection = false;
	bool is_helmet_hit = false;
	EArmorType armor_type = eArmorNone;
	EHelmetType helmet_type = eHelmetNone;
	
	LPCSTR GetArmorTypeString() const;
	LPCSTR GetHelmetTypeString() const;
};

class COutfitSoundManager
{
public:
	COutfitSoundManager();
	~COutfitSoundManager();

	void LoadFromActorConfig(LPCSTR section);
	void LoadClank(const shared_str& sect) { LoadSoundByType(eSoundClank, sect); }
	void LoadRustle(const shared_str& sect) { LoadSoundByType(eSoundRustle, sect); }
	void LoadJump(const shared_str& sect) { LoadSoundByType(eSoundJump, sect); }
	void LoadDeflectionSounds(const shared_str& sound_path) { LoadSoundByType(eSoundDeflectionArmor, sound_path); }
	void LoadHelmetDeflectionSounds(const shared_str& sound_path) { LoadSoundByType(eSoundDeflectionHelmet, sound_path); }
	void LoadNPCHitSounds();

	void Play(float power, bool hud_view, CObject* owner);
	void PlayJump(bool hud_view, CObject* owner);

	void OnActorHit(SHit* pHit, u16 bone_id);
	void OnNPCHit(CEntityAlive* npc, SHit* pHit, u16 bone_id);

	void Clear();
	void ResetToDefault();
	void ClearDeflectionSounds() { ClearSoundType(eSoundDeflectionArmor); }
	void ClearHelmetDeflectionSounds() { ClearSoundType(eSoundDeflectionHelmet); }
	void ClearNPCHitSounds();

	static void LoadSettings();
	static bool IsHitSoundsEnabled() { return s_settings.enable_sounds; }
	static bool IsCamEffectsEnabled() { return s_settings.enable_cam_effects; }
	static bool IsActorHitSoundsEnabled() { return s_settings.enable_actor_sounds; }
	static bool IsNPCHitSoundsEnabled() { return s_settings.enable_npc_sounds; }

private:
	void LoadSoundByType(ESoundType type, const shared_str& sect);
	void ClearSoundType(ESoundType type);
	void LoadSoundSet(xr_vector<ref_sound>& container, shared_str& sect, const shared_str& new_sect, LPCSTR log_tag);
	bool LoadSoundList(xr_vector<ref_sound>& container, LPCSTR search_mask, LPCSTR log_tag) const;
	void ClearHudSoundType(ESoundType type);
	bool IsLayerSection(LPCSTR sect) const;
	
	void PlaySound(xr_vector<ref_sound>& sounds, float volumeMin, float volumeMax, bool hudView, CObject* owner, const Fvector* pos = nullptr, float power = 1.0f, Fvector2* range = nullptr);
	void PlayHudSound(ESoundType type, bool hudView, CObject* owner, const Fvector* pos = nullptr);
	
	bool ValidateHit(SHit* pHit, bool for_actor) const;
	bool IsValidHitType(ALife::EHitType hit_type) const;
	bool IsHeadBone(u16 bone_id) const;
	bool IsHeadBoneForEntity(CEntityAlive* entity, u16 bone_id) const;
	
	void PlayDeflectionSound(bool hud_view, CObject* owner, const Fvector& pos, bool is_helmet);
	void PlayDeflectionCamEffect(CActor* actor, const SProtectionInfo& protection_info, bool is_head);
	SProtectionInfo GetProtectionInfo(CActor* actor, bool is_head) const;
	
	EArmorType GetArmorType(CCustomOutfit* outfit) const;
	EHelmetType GetHelmetType(CHelmet* helmet) const;
	EArmorType GetNPCArmorType(LPCSTR visual_name) const;
	EHelmetType GetNPCHelmetType(LPCSTR visual_name) const;
	LPCSTR ArmorTypeToString(EArmorType type) const;
	LPCSTR HelmetTypeToString(EHelmetType type) const;
	bool ReadNPCType(LPCSTR visual_name, LPCSTR key, string256& out) const;
	
	xr_vector<ref_sound>* GetNPCSounds(bool is_head, EArmorType armorType, EHelmetType helmetType);
	bool FindEffectorPath(LPCSTR zone, LPCSTR armor_type, string_path& out_path);

private:
	xr_vector<ref_sound>	_sounds[eSoundCount];
	shared_str				_sections[eSoundCount];
	HUD_SOUND_ITEM			_hudSounds[eSoundCount];
	bool					_useHudSound[eSoundCount] = {};
	xr_vector<ref_sound>	_npcSounds[eArmorCount + eHelmetCount];
	
	shared_str				_defaultRustle;
	shared_str				_defaultJump;

	static SHitEffectSettings s_settings;
	static bool s_settings_loaded;
	static CInifile* s_npc_visuals_config;
	static bool s_npc_config_warned;
	
	static xr_map<xr_string, shared_str> s_effector_path_cache;
};
