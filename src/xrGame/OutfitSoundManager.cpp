#include "StdAfx.h"
#include "OutfitSoundManager.h"
#include "../xrEngine/xr_object.h"
#include "Actor.h"
#include "ActorEffector.h"
#include "entity_alive.h"
#include "Hit.h"
#include "Level.h"
#include "CameraEffector.h"
#include "../xrEngine/CameraManager.h"
#include "ActorHelmet.h"
#include "CustomOutfit.h"
#include "InventoryOwner.h"
#include "animation_utils.h"
#include "../Include/xrRender/Kinematics.h"
#include "HudSound.h"

namespace DeflectionConstants
{
	const u16 HEAD_BONE_MIN = 15;
	const u16 HEAD_BONE_MAX = 19;
	const float DEFLECTION_SOUND_VOLUME_MIN = 1.0f;
	const float DEFLECTION_SOUND_VOLUME_MAX = 1.5f;
	const float CLANK_SOUND_VOLUME_MIN = 1.0f;
	const float CLANK_SOUND_VOLUME_MAX = 1.5f;
	const float RUSTLE_SOUND_VOLUME_MIN = 0.8f;
	const float RUSTLE_SOUND_VOLUME_MAX = 1.4f;
	const float JUMP_SOUND_VOLUME_MIN = 1.8f;
	const float JUMP_SOUND_VOLUME_MAX = 2.5f;
	const float LAND_SOUND_VOLUME_MIN = 1.2f;
	const float LAND_SOUND_VOLUME_MAX = 1.8f;
	const float NPC_HIT_SOUND_VOLUME_MIN = 0.9f;
	const float NPC_HIT_SOUND_VOLUME_MAX = 1.1f;
	const float NPC_HIT_SOUND_RANGE_MIN = 1.0f;
	const float NPC_HIT_SOUND_RANGE_MAX = 60.0f;
}

SHitEffectSettings COutfitSoundManager::s_settings;
bool COutfitSoundManager::s_settings_loaded = false;
CInifile* COutfitSoundManager::s_npc_visuals_config = nullptr;
bool COutfitSoundManager::s_npc_config_warned = false;
xr_map<xr_string, shared_str> COutfitSoundManager::s_effector_path_cache;

COutfitSoundManager::COutfitSoundManager()
{}

COutfitSoundManager::~COutfitSoundManager()
{
	Clear();
}

LPCSTR SProtectionInfo::GetArmorTypeString() const
{
	static const LPCSTR names[] = { "none", "light", "medium", "heavy", "exo" };
	return (armor_type < eArmorCount) ? names[armor_type] : "none";
}

LPCSTR SProtectionInfo::GetHelmetTypeString() const
{
	static const LPCSTR names[] = { "none", "glass", "gasmask", "helmet", "exo" };
	return (helmet_type < eHelmetCount) ? names[helmet_type] : "none";
}

void COutfitSoundManager::LoadFromActorConfig(LPCSTR section)
{
	if (!pSettings)
		return;
	if (pSettings->line_exist(section, "step_sound_rustle"))
		LoadRustle(_defaultRustle = pSettings->r_string(section, "step_sound_rustle"));
	if (pSettings->line_exist(section, "sound_jump_equipment"))
		LoadJump(_defaultJump = pSettings->r_string(section, "sound_jump_equipment"));
	if (pSettings->line_exist(section, "sound_crouch_in"))
		LoadCrouchIn(_defaultCrouchIn = pSettings->r_string(section, "sound_crouch_in"));
	if (pSettings->line_exist(section, "sound_crouch_out"))
		LoadCrouchOut(_defaultCrouchOut = pSettings->r_string(section, "sound_crouch_out"));
	if (pSettings->line_exist(section, "sound_crouch_slow_in"))
		LoadCrouchSlowIn(_defaultCrouchSlowIn = pSettings->r_string(section, "sound_crouch_slow_in"));
	if (pSettings->line_exist(section, "sound_crouch_slow_out"))
		LoadCrouchSlowOut(_defaultCrouchSlowOut = pSettings->r_string(section, "sound_crouch_slow_out"));
	if (pSettings->line_exist(section, "sound_lookout"))
		LoadLookout(_defaultLookout = pSettings->r_string(section, "sound_lookout"));
	if (pSettings->line_exist(section, "sound_land"))
		LoadLand(_defaultLand = pSettings->r_string(section, "sound_land"));
	LoadNPCHitSounds();
	LoadSettings();
}

void COutfitSoundManager::LoadSoundByType(ESoundType type, const shared_str& sect)
{
	if (type >= eSoundCount)
		return;

	if (!sect.size())
	{
		ClearSoundType(type);
		return;
	}

	if (_sections[type] == sect)
	{
		if (_useHudSound[type] && !_hudSounds[type].sounds.empty())
			return;
		if (!_useHudSound[type] && !_sounds[type].empty())
			return;
	}

	static const LPCSTR tags[] = {
		"clank", "rustle", "jump",
		"crouch_in", "crouch_out", "crouch_slow_in", "crouch_slow_out", "lookout", "land",
		"deflection", "helmet_deflection"
	};

	if (IsLayerSection(sect.c_str()))
	{
		for (auto& snd : _sounds[type])
			snd.destroy();
		_sounds[type].clear();

		HUD_SOUND_ITEM::DestroySound(_hudSounds[type]);
		HUD_SOUND_ITEM::LoadSound(sect.c_str(), "snd_1_layer", _hudSounds[type], sg_SourceType);

		_sections[type] = sect;
		_useHudSound[type] = true;
		return;
	}

	ClearHudSoundType(type);
	_useHudSound[type] = false;
	LoadSoundSet(_sounds[type], _sections[type], sect, tags[type]);
}

bool COutfitSoundManager::IsLayerSection(LPCSTR sect) const
{
	return pSettings && sect && *sect
		&& pSettings->section_exist(sect)
		&& pSettings->line_exist(sect, "snd_1_layer");
}

void COutfitSoundManager::ClearHudSoundType(ESoundType type)
{
	if (type >= eSoundCount)
		return;

	HUD_SOUND_ITEM::DestroySound(_hudSounds[type]);
	_useHudSound[type] = false;
}

void COutfitSoundManager::ClearSoundType(ESoundType type)
{
	if (type >= eSoundCount)
		return;
	
	for (auto& snd : _sounds[type])
		snd.destroy();
	_sounds[type].clear();
	_sections[type] = nullptr;
	ClearHudSoundType(type);
}

void COutfitSoundManager::Clear()
{
	for (u32 i = 0; i < eSoundCount; ++i)
		ClearSoundType((ESoundType)i);
	
	for (u32 i = 0; i < eArmorCount + eHelmetCount; ++i)
	{
		for (auto& snd : _npcSounds[i])
			snd.destroy();
		_npcSounds[i].clear();
	}
}

void COutfitSoundManager::ResetToDefault()
{
	LoadClank(shared_str(nullptr));
	if (_defaultRustle.size())
		LoadRustle(_defaultRustle);
	if (_defaultJump.size())
		LoadJump(_defaultJump);
	if (_defaultCrouchIn.size())
		LoadCrouchIn(_defaultCrouchIn);
	if (_defaultCrouchOut.size())
		LoadCrouchOut(_defaultCrouchOut);
	if (_defaultCrouchSlowIn.size())
		LoadCrouchSlowIn(_defaultCrouchSlowIn);
	if (_defaultCrouchSlowOut.size())
		LoadCrouchSlowOut(_defaultCrouchSlowOut);
	if (_defaultLookout.size())
		LoadLookout(_defaultLookout);
	if (_defaultLand.size())
		LoadLand(_defaultLand);
}

void COutfitSoundManager::LoadSoundSet(xr_vector<ref_sound>& container, shared_str& sect, const shared_str& new_sect, LPCSTR log_tag)
{
	if (!new_sect.size())
	{
		for (auto& snd : container)
			snd.destroy();
		container.clear();
		sect = nullptr;
		return;
	}
	if (sect == new_sect && !container.empty())
		return;
	
	for (auto& snd : container)
		snd.destroy();
	container.clear();
	sect = new_sect;

	string256 search_mask;
	if (strstr(new_sect.c_str(), "*"))
		xr_strcpy(search_mask, sizeof(search_mask), new_sect.c_str());
	else
		xr_sprintf(search_mask, sizeof(search_mask), "%s*", new_sect.c_str());
	_strlwr(search_mask);

	LoadSoundList(container, search_mask, log_tag);
}

bool COutfitSoundManager::LoadSoundList(xr_vector<ref_sound>& container, LPCSTR search_mask, LPCSTR log_tag) const
{
	FS_FileSet files;
	FS.file_list(files, "$game_sounds$", FS_ListFiles, search_mask);

	if (files.empty())
		return false;

	for (const auto& file : files)
	{
		ref_sound sound;
		sound.create(file.name.c_str(), st_Effect, sg_SourceType);
		if (sound.handle())
			container.push_back(sound);
	}

	return !container.empty();
}

void COutfitSoundManager::PlaySound(xr_vector<ref_sound>& sounds, float volumeMin, float volumeMax, bool hudView, CObject* owner, const Fvector* pos, float power, Fvector2* range)
{
	if (sounds.empty() || owner == nullptr)
		return;

	Fvector sound_pos = pos ? *pos : (hudView ? Fvector().set(0.f, 0.f, 0.f) : owner->Position());
	float volume = Random.randF(volumeMin, volumeMax) * power;
	const u32 index = Random.randI(sounds.size());
	
	if (index < sounds.size())
		sounds[index].play_no_feedback(owner, hudView ? sm_2D : 0, 0, &sound_pos, &volume, nullptr, range);
}

void COutfitSoundManager::PlayHudSound(ESoundType type, bool hudView, CObject* owner, const Fvector* pos)
{
	if (type >= eSoundCount || owner == nullptr || !_useHudSound[type] || _hudSounds[type].sounds.empty())
		return;

	Fvector sound_pos = pos ? *pos : (hudView ? Fvector().set(0.f, 0.f, 0.f) : owner->Position());
	HUD_SOUND_ITEM::PlaySound(_hudSounds[type], sound_pos, owner, hudView);
}

void COutfitSoundManager::Play(float power, bool hud_view, CObject* owner)
{
	if (owner == nullptr)
		return;

	using namespace DeflectionConstants;
	Fvector pos = hud_view ? Fvector().set(0.f, 0.f, 0.f) : owner->Position();

	if (_useHudSound[eSoundClank])
		PlayHudSound(eSoundClank, hud_view, owner, &pos);
	else
		PlaySound(_sounds[eSoundClank], CLANK_SOUND_VOLUME_MIN, CLANK_SOUND_VOLUME_MAX, hud_view, owner, &pos, power);

	if (_useHudSound[eSoundRustle])
		PlayHudSound(eSoundRustle, hud_view, owner, &pos);
	else
		PlaySound(_sounds[eSoundRustle], RUSTLE_SOUND_VOLUME_MIN, RUSTLE_SOUND_VOLUME_MAX, hud_view, owner, &pos, power);
}

void COutfitSoundManager::PlayMotion(ESoundType type, bool hud_view, CObject* owner, float volumeMin, float volumeMax)
{
	if (owner == nullptr || type >= eSoundCount)
		return;

	if (_useHudSound[type])
		PlayHudSound(type, hud_view, owner, nullptr);
	else
		PlaySound(_sounds[type], volumeMin, volumeMax, hud_view, owner, nullptr, 1.0f);
}

void COutfitSoundManager::PlayJump(bool hud_view, CObject* owner)
{
	using namespace DeflectionConstants;
	PlayMotion(eSoundJump, hud_view, owner, JUMP_SOUND_VOLUME_MIN, JUMP_SOUND_VOLUME_MAX);
}

void COutfitSoundManager::PlayLand(bool hud_view, CObject* owner)
{
	using namespace DeflectionConstants;
	PlayMotion(eSoundLand, hud_view, owner, LAND_SOUND_VOLUME_MIN, LAND_SOUND_VOLUME_MAX);
}

void COutfitSoundManager::PlayCrouchIn(bool hud_view, CObject* owner)
{
	using namespace DeflectionConstants;
	PlayMotion(eSoundCrouchIn, hud_view, owner, RUSTLE_SOUND_VOLUME_MIN, RUSTLE_SOUND_VOLUME_MAX);
}

void COutfitSoundManager::PlayCrouchOut(bool hud_view, CObject* owner)
{
	using namespace DeflectionConstants;
	PlayMotion(eSoundCrouchOut, hud_view, owner, RUSTLE_SOUND_VOLUME_MIN, RUSTLE_SOUND_VOLUME_MAX);
}

void COutfitSoundManager::PlayCrouchSlowIn(bool hud_view, CObject* owner)
{
	using namespace DeflectionConstants;
	PlayMotion(eSoundCrouchSlowIn, hud_view, owner, RUSTLE_SOUND_VOLUME_MIN, RUSTLE_SOUND_VOLUME_MAX);
}

void COutfitSoundManager::PlayCrouchSlowOut(bool hud_view, CObject* owner)
{
	using namespace DeflectionConstants;
	PlayMotion(eSoundCrouchSlowOut, hud_view, owner, RUSTLE_SOUND_VOLUME_MIN, RUSTLE_SOUND_VOLUME_MAX);
}

void COutfitSoundManager::PlayLookout(bool hud_view, CObject* owner)
{
	using namespace DeflectionConstants;

	const bool hasLookout = _useHudSound[eSoundLookout] ? !_hudSounds[eSoundLookout].sounds.empty() : !_sounds[eSoundLookout].empty();
	if (hasLookout)
		PlayMotion(eSoundLookout, hud_view, owner, RUSTLE_SOUND_VOLUME_MIN, RUSTLE_SOUND_VOLUME_MAX);
	else
		PlayMotion(eSoundRustle, hud_view, owner, RUSTLE_SOUND_VOLUME_MIN, RUSTLE_SOUND_VOLUME_MAX);
}

void COutfitSoundManager::LoadSettings()
{
	if (s_settings_loaded)
		return;

	if (!pSettings)
	{
		s_settings.enable_sounds = s_settings.enable_cam_effects = s_settings.enable_actor_sounds = s_settings.enable_npc_sounds = true;
		s_settings_loaded = true;
		return;
	}

	LPCSTR section = "deflection_effects";
	if (pSettings->section_exist(section))
	{
		s_settings.enable_sounds = READ_IF_EXISTS(pSettings, r_bool, section, "enable_deflection_sounds", true);
		s_settings.enable_cam_effects = READ_IF_EXISTS(pSettings, r_bool, section, "enable_camera_effects", true);
		s_settings.enable_actor_sounds = READ_IF_EXISTS(pSettings, r_bool, section, "enable_actor_hit_sounds", true);
		s_settings.enable_npc_sounds = READ_IF_EXISTS(pSettings, r_bool, section, "enable_npc_hit_sounds", true);
	}
	else
		s_settings.enable_sounds = s_settings.enable_cam_effects = s_settings.enable_actor_sounds = s_settings.enable_npc_sounds = true;

	if (!s_npc_visuals_config)
	{
		string_path config_path;
		FS.update_path(config_path, "$game_config$", "plugins\\wepl_hit_effect_visuals.ltx");
		if (FS.exist(config_path))
		{
			s_npc_visuals_config = new CInifile(config_path);
			if (s_npc_visuals_config)
				Msg("~ [LoadSettings] Loaded NPC visuals config: %s", config_path);
			else
				Msg("! [LoadSettings] Failed to create NPC visuals config object: %s", config_path);
		}
		else
			Msg("! [LoadSettings] NPC visuals config not found: %s", config_path);
	}

	s_settings_loaded = true;
}

bool COutfitSoundManager::IsValidHitType(ALife::EHitType hit_type) const
{
	return (hit_type == ALife::eHitTypeFireWound || hit_type == ALife::eHitTypeWound || hit_type == ALife::eHitTypeWound_2 || hit_type == ALife::eHitTypeExplosion);
}

bool COutfitSoundManager::IsHeadBone(u16 bone_id) const
{
	using namespace DeflectionConstants;
	return (bone_id >= HEAD_BONE_MIN && bone_id <= HEAD_BONE_MAX);
}

bool COutfitSoundManager::IsHeadBoneForEntity(CEntityAlive* entity, u16 bone_id) const
{
	if (!entity || !entity->Visual() || bone_id == BI_NONE)
		return false;
	
	IKinematics* K = entity->Visual()->dcast_PKinematics();
	if (!K)
		return false;

	const u16 head_bone = K->LL_BoneID("bip01_head");
	const u16 neck_bone = K->LL_BoneID("bip01_neck");
	if (head_bone == BI_NONE && neck_bone == BI_NONE)
	{
		using namespace DeflectionConstants;
		return (bone_id >= HEAD_BONE_MIN && bone_id <= HEAD_BONE_MAX);
	}
	return (neck_bone == bone_id || (head_bone != BI_NONE && find_in_parents(head_bone, bone_id, *K)));
}

void COutfitSoundManager::PlayDeflectionSound(bool hud_view, CObject* owner, const Fvector& pos, bool is_helmet)
{
	using namespace DeflectionConstants;
	ESoundType type = is_helmet ? eSoundDeflectionHelmet : eSoundDeflectionArmor;
	PlaySound(_sounds[type], DEFLECTION_SOUND_VOLUME_MIN, DEFLECTION_SOUND_VOLUME_MAX, hud_view, owner, &pos, 1.0f);
}

EArmorType COutfitSoundManager::GetArmorType(CCustomOutfit* outfit) const
{
	if (!outfit)
		return eArmorNone;
	if (outfit->IsExo || outfit->IsExoProto)
		return eArmorExo;
	if (!outfit->m_ArmorType.size())
		return eArmorNone;

	string256 type_lower;
	xr_strcpy(type_lower, outfit->m_ArmorType.c_str());
	_strlwr(type_lower);
	
	static const LPCSTR types[] = { "none", "light", "medium", "heavy", "exo" };
	for (u32 i = 1; i < eArmorCount; ++i)
		if (!xr_strcmp(type_lower, types[i]))
			return (EArmorType)i;
	
	return eArmorNone;
}

EHelmetType COutfitSoundManager::GetHelmetType(CHelmet* helmet) const
{
	if (!helmet)
		return eHelmetNone;
	if (helmet->GlassPresent)
		return eHelmetGlass;
	if (helmet->IsHudGasMaskAvailable())
		return eHelmetGasmask;
	if (!helmet->m_HelmetType.size())
		return eHelmetNone;

	string256 type_lower;
	xr_strcpy(type_lower, helmet->m_HelmetType.c_str());
	xr_strlwr(type_lower);
	
	static const LPCSTR types[] = { "none", "glass", "gasmask", "helmet", "exo" };
	for (u32 i = 1; i < eHelmetCount; ++i)
		if (!xr_strcmp(type_lower, types[i]))
			return (EHelmetType)i;
	
	return eHelmetNone;
}

EArmorType COutfitSoundManager::GetNPCArmorType(LPCSTR visual_name) const
{
	if (visual_name == nullptr || !*visual_name)
		return eArmorNone;
	
	string256 type_lower;
	if (!ReadNPCType(visual_name, "armorType", type_lower))
		return eArmorNone;
	
	static const LPCSTR types[] = { "none", "light", "medium", "heavy", "exo" };
	for (u32 i = 1; i < eArmorCount; ++i)
		if (!xr_strcmp(type_lower, types[i]))
			return (EArmorType)i;
	
	return eArmorNone;
}

EHelmetType COutfitSoundManager::GetNPCHelmetType(LPCSTR visual_name) const
{
	if (visual_name == nullptr || !*visual_name)
		return eHelmetNone;
	
	string256 type_lower;
	if (!ReadNPCType(visual_name, "helmetType", type_lower))
		return eHelmetNone;
	
	static const LPCSTR types[] = { "none", "glass", "gasmask", "helmet", "exo" };
	for (u32 i = 1; i < eHelmetCount; ++i)
		if (!xr_strcmp(type_lower, types[i]))
			return (EHelmetType)i;
	
	return eHelmetNone;
}

LPCSTR COutfitSoundManager::ArmorTypeToString(EArmorType type) const
{
	static const LPCSTR names[] = { "none", "light", "medium", "heavy", "exo" };
	return (type < eArmorCount) ? names[type] : "none";
}

LPCSTR COutfitSoundManager::HelmetTypeToString(EHelmetType type) const
{
	static const LPCSTR names[] = { "none", "glass", "gasmask", "helmet", "exo" };
	return (type < eHelmetCount) ? names[type] : "none";
}

bool COutfitSoundManager::ReadNPCType(LPCSTR visual_name, LPCSTR key, string256& out) const
{
	if (visual_name == nullptr || !*visual_name || key == nullptr)
		return false;

	CInifile* config = s_npc_visuals_config != nullptr ? s_npc_visuals_config : pSettings;
	if (config == nullptr)
	{
		if (!s_npc_config_warned)
		{
			Msg("! [OutfitSoundManager] NPC visuals config missing");
			s_npc_config_warned = true;
		}
		return false;
	}

	if (!config->section_exist(visual_name) || !config->line_exist(visual_name, key))
		return false;

	LPCSTR value = config->r_string(visual_name, key);
	if (value == nullptr || !*value)
	{
		Msg("! [COutfitSoundManager::ReadNPCType] Empty value for key '%s' in section '%s'", key, visual_name);
		return false;
	}

	xr_strcpy(out, value);
	_strlwr(out);
	return true;
}

xr_vector<ref_sound>* COutfitSoundManager::GetNPCSounds(bool is_head, EArmorType armorType, EHelmetType helmetType)
{
	u32 index = is_head ? (eArmorCount + helmetType) : armorType;
	if (index >= eArmorCount + eHelmetCount || _npcSounds[index].empty())
		return nullptr;
	return &_npcSounds[index];
}

static bool TryFindEffectorInDir(LPCSTR search_dir, LPCSTR armor_type, string_path& out_path)
{
	string_path full_path, test_path;
	FS_FileSet files;

	xr_sprintf(test_path, "%s\\hit_%s_1.anm", search_dir, armor_type);
	FS.update_path(full_path, "$game_anims$", test_path);
	if (FS.exist(full_path))
	{
		xr_strcpy(out_path, test_path);
		return true;
	}

	string256 pattern;
	xr_sprintf(pattern, "%s\\hit_%s_*.anm", search_dir, armor_type);
	_strlwr(pattern);
	FS.file_list(files, "$game_anims$", FS_ListFiles, pattern);

	if (!files.empty())
	{
		FS_FileSetIt file_it = files.begin();
		const u32 random_offset = Random.randI(files.size());
		for (u32 i = 0; i < random_offset && file_it != files.end(); ++i)
			++file_it;
		if (file_it != files.end())
		{
			xr_strcpy(out_path, file_it->name.c_str());
			return true;
		}
	}

	if (strcmp(armor_type, "none") != 0)
	{
		xr_sprintf(test_path, "%s\\hit_none_1.anm", search_dir);
		FS.update_path(full_path, "$game_anims$", test_path);
		if (FS.exist(full_path))
		{
			xr_strcpy(out_path, test_path);
			return true;
		}
	}

	return false;
}

bool COutfitSoundManager::FindEffectorPath(LPCSTR zone, LPCSTR armor_type, string_path& out_path)
{
	string256 temp_key;
	xr_sprintf(temp_key, "%s_%s", zone, armor_type);
	xr_string cache_key = temp_key;

	auto it = s_effector_path_cache.find(cache_key);
	if (it != s_effector_path_cache.end())
	{
		if (it->second.size() > 0)
		{
			xr_strcpy(out_path, it->second.c_str());
			return true;
		}
		return false;
	}

	string_path search_dir;
	static const LPCSTR base_dirs[] = {
		"camera_effects\\wepl\\hit_effect\\%s",
		"camera_effects\\deflection\\hit_effect\\%s"
	};

	for (LPCSTR fmt : base_dirs)
	{
		xr_sprintf(search_dir, fmt, zone);
		if (TryFindEffectorInDir(search_dir, armor_type, out_path))
		{
			s_effector_path_cache[cache_key] = out_path;
			return true;
		}
	}

	s_effector_path_cache[cache_key] = "";
	return false;
}

void COutfitSoundManager::PlayDeflectionCamEffect(CActor* actor, const SProtectionInfo& protection_info, bool is_head)
{
	if (!s_settings.enable_cam_effects || actor == nullptr || !protection_info.has_protection)
		return;

	LPCSTR effector_type = nullptr;
	if (is_head)
	{
		if (protection_info.is_helmet_hit)
			effector_type = protection_info.GetHelmetTypeString();
		else
		{
			LPCSTR armor_type = protection_info.GetArmorTypeString();
			effector_type = (!xr_strcmp(armor_type, "exo")) ? "exo" : "helmet";
		}
	}
	else
		effector_type = protection_info.GetArmorTypeString();

	string_path effector_path;
	if (!FindEffectorPath(is_head ? "head" : "body", effector_type, effector_path))
		return;

	CAnimatorCamEffector* effector = new CAnimatorCamEffector();
	if (effector == nullptr)
	{
		Msg("! [COutfitSoundManager::PlayDeflectionCamEffect] Failed to allocate camera effector");
		return;
	}

	ECamEffectorType effector_id = actor->Cameras().RequestCamEffectorId();
	effector->SetType(effector_id);
	effector->SetCyclic(false);
	effector->SetHudAffect(true);
	effector->Start(effector_path);
	actor->Cameras().AddCamEffector(effector);
}

SProtectionInfo COutfitSoundManager::GetProtectionInfo(CActor* actor, bool is_head) const
{
	SProtectionInfo info;
	if (actor == nullptr)
		return info;

	CCustomOutfit* outfit = actor->GetOutfit();
	CHelmet* helmet = actor->GetHelmet();

	if (is_head)
	{
		if (helmet)
		{
			info.helmet_type = GetHelmetType(helmet);
			if (info.helmet_type != eHelmetNone)
			{
				info.has_protection = true;
				info.is_helmet_hit = true;
			}
		}
		else if (outfit && !outfit->bIsHelmetAvaliable)
		{
			info.armor_type = GetArmorType(outfit);
			if (info.armor_type != eArmorNone)
			{
				info.has_protection = true;
				info.is_helmet_hit = false;
			}
		}
	}
	else if (outfit)
	{
		info.armor_type = GetArmorType(outfit);
		if (info.armor_type != eArmorNone)
		{
			info.has_protection = true;
			info.is_helmet_hit = false;
		}
	}

	return info;
}

bool COutfitSoundManager::ValidateHit(SHit* pHit, bool for_actor) const
{
	if (pHit == nullptr || pHit->power <= 0.0f || !IsValidHitType(pHit->hit_type))
		return false;

	if (for_actor)
	{
		CActor* actor = Actor();
		if (actor == nullptr || !actor->g_Alive())
			return false;
	}
	return true;
}

void COutfitSoundManager::OnActorHit(SHit* pHit, u16 bone_id)
{
	if (!s_settings_loaded)
		LoadSettings();

	if (!s_settings.enable_sounds || !s_settings.enable_actor_sounds || !ValidateHit(pHit, true))
		return;

	CActor* actor = Actor();
	if (actor == nullptr)
		return;

	bool is_head = IsHeadBone(bone_id);
	SProtectionInfo protection_info = GetProtectionInfo(actor, is_head);
	if (!protection_info.has_protection)
		return;

	ESoundType type = protection_info.is_helmet_hit ? eSoundDeflectionHelmet : eSoundDeflectionArmor;
	if (!_sounds[type].empty())
	{
		Fvector pos = actor->Position();
		PlayDeflectionSound(!!actor->HUDview(), actor, pos, protection_info.is_helmet_hit);
	}

	if (s_settings.enable_cam_effects)
		PlayDeflectionCamEffect(actor, protection_info, is_head);
}

void COutfitSoundManager::OnNPCHit(CEntityAlive* npc, SHit* pHit, u16 bone_id)
{
	if (!s_settings_loaded)
		LoadSettings();

	if (!s_settings.enable_sounds || !s_settings.enable_npc_sounds || npc == nullptr || !ValidateHit(pHit, false))
		return;

	CActor* actor = Actor();
	if (actor == nullptr || !actor->g_Alive() || pHit->who != actor || npc->ID() == actor->ID())
		return;

	LPCSTR visual_name = npc->cNameVisual().c_str();
	if (visual_name == nullptr || !*visual_name)
		return;

	bool is_head = IsHeadBoneForEntity(npc, bone_id);
	EArmorType armorType = GetNPCArmorType(visual_name);
	EHelmetType helmetType = GetNPCHelmetType(visual_name);
	
	xr_vector<ref_sound>* sounds = GetNPCSounds(is_head, armorType, helmetType);
	if (!sounds)
		sounds = GetNPCSounds(is_head, eArmorNone, eHelmetNone);
	
	if (!sounds || sounds->empty())
		return;

	using namespace DeflectionConstants;
	Fvector pos = npc->Position();
	Fvector2 range;
	range.set(NPC_HIT_SOUND_RANGE_MIN, NPC_HIT_SOUND_RANGE_MAX);
	PlaySound(*sounds, NPC_HIT_SOUND_VOLUME_MIN, NPC_HIT_SOUND_VOLUME_MAX, false, static_cast<CObject*>(npc), &pos, 1.0f, &range);
}

void COutfitSoundManager::ClearNPCHitSounds()
{
	for (u32 i = 0; i < eArmorCount + eHelmetCount; ++i)
	{
		for (auto& snd : _npcSounds[i])
			snd.destroy();
		_npcSounds[i].clear();
	}
}

void COutfitSoundManager::LoadNPCHitSounds()
{
	static bool s_loaded_once = false;
	if (s_loaded_once)
		return;
	s_loaded_once = true;

	ClearNPCHitSounds();
	if (!pSettings)
	{
		s_loaded_once = false;
		return;
	}

	struct STypeLoad { LPCSTR suffix; u32 index; bool is_head; };
	STypeLoad types[] = {
		{ "hit_none_*", eArmorNone, false },
		{ "hit_light_*", eArmorLight, false },
		{ "hit_medium_*", eArmorMedium, false },
		{ "hit_heavy_*", eArmorHeavy, false },
		{ "hit_exo_*", eArmorExo, false },
		{ "hit_none_*", eHelmetNone, true },
		{ "hit_glass_*", eHelmetGlass, true },
		{ "hit_gasmask_*", eHelmetGasmask, true },
		{ "hit_helmet_*", eHelmetHelmet, true },
		{ "hit_exo_*", eHelmetExo, true }
	};

	for (const auto& type : types)
	{
		string256 search_mask;
		xr_sprintf(search_mask, "wepl\\hit_effect\\%s%s", type.is_head ? "head\\" : "body\\", type.suffix);
		_strlwr(search_mask);
		u32 index = type.is_head ? (eArmorCount + type.index) : type.index;
		LoadSoundList(_npcSounds[index], search_mask, nullptr);
	}
}
