#include "StdAfx.h"
#include "UIInteractionMarkers.h"

#include <algorithm>

#include "../Actor.h"
#include "../Level.h"
#include "../GameObject.h"
#include "../inventory_item.h"
#include "../InventoryBox.h"
#include "../InventoryOwner.h"
#include "../UsableScriptObject.h"
#include "../entity_alive.h"
#include "../ai/monsters/basemonster/base_monster.h"
#include "../ai/stalker/ai_stalker.h"
#include "../PhysicObject.h"
#include "../ZoneCampfire.h"
#include "../space_restrictor.h"
#include "../AnomalyZone.h"
#include "../GametaskManager.h"
#include "../GameTask.h"
#include "../ai_space.h"
#include "../alife_simulator.h"
#include "../alife_object_registry.h"
#include "../xrServerEntities/xrServer_Objects_ALife_Monsters.h"
#include "../Include/xrRender/Kinematics.h"
#include "../xrEngine/GameMtlLib.h"
#include "../xrEngine/xr_level_controller.h"
#include "../xrEngine/string_table.h"
#include "../xrCore/_color.h"
#include "../xrEngine/CameraBase.h"
#include "../xrEngine/xr_collide_form.h"
#include "UIGameCustom.h"
#include "ui/UITalkWnd.h"
#include "ui/UITradeWnd.h"
#include "ui/UIPdaWnd.h"
#include "ui/UIActorMenu.h"
#include "ui/UIInventoryWnd.h"
#include "../../xrUI/UIXmlInit.h"
#include "../../xrUI/UITextureMaster.h"
#include "../../xrUI/Widgets/UIStaticItem.h"
#include "../../xrUI/ui_base.h"
#include "../../xrCore/FormatParsers/XML/xrXMLParser.h"
#include "../ScriptsSubsystems/StoryID/StoryIDManager.h"
#include "ui/UIGameTutorial.h"
#include "../../xrCore/LocatorAPI_defs.h"
#include "../../xrCore/LocatorAPI.h"

extern ENGINE_API xr_atomic_bool g_bRendering;

Fvector2 World2Ui(Fvector pos, bool hud, bool allow_offscreen);
extern CUIGameCustom* CurrentGameUI();
extern CUISequencer* g_tutorial;

CInteractionMarkerManager* g_pInteractionMarkerManager = nullptr;

CInteractionMarkerManager::~CInteractionMarkerManager()
{
	HUD_SOUND_ITEM::DestroySound(m_focus_snd);
}

namespace
{
	LPCSTR ClassSectionName(EWSUIClass cls)
	{
		switch (cls)
		{
		case EWSUIClass::Item: return "wsui_class_item";
		case EWSUIClass::Npc: return "wsui_class_npc";
		case EWSUIClass::Body: return "wsui_class_body";
		case EWSUIClass::Stash: return "wsui_class_stash";
		case EWSUIClass::Usable: return "wsui_class_usable";
		case EWSUIClass::Door: return "wsui_class_door";
		case EWSUIClass::Campfire: return "wsui_class_campfire";
		case EWSUIClass::Zone: return "wsui_class_zone";
		default: return nullptr;
		}
	}

	LPCSTR ClassName(EWSUIClass cls)
	{
		switch (cls)
		{
		case EWSUIClass::Item: return "item";
		case EWSUIClass::Npc: return "npc";
		case EWSUIClass::Body: return "body";
		case EWSUIClass::Stash: return "stash";
		case EWSUIClass::Usable: return "usable";
		case EWSUIClass::Door: return "door";
		case EWSUIClass::Campfire: return "campfire";
		case EWSUIClass::Zone: return "zone";
		default: return "none";
		}
	}

	LPCSTR TranslateIfExists(LPCSTR string_id, LPCSTR fallback)
	{
		if (!string_id || !string_id[0])
			return fallback;

		LPCSTR translated = g_pStringTable->translate(string_id).c_str();
		if (translated && translated[0] && xr_strcmp(translated, string_id) != 0)
			return translated;

		return fallback;
	}

	LPCSTR WsuiString(LPCSTR id)
	{
		return g_pStringTable->translate(id).c_str();
	}

	LPCSTR ItemDisplayName(CInventoryItem* item)
	{
		if (!item)
			return nullptr;

		if (LPCSTR name = item->NameItem())
		{
			if (name[0])
				return name;
		}

		if (LPCSTR short_name = item->NameShort())
		{
			if (short_name[0])
				return short_name;
		}

		return nullptr;
	}

	Fvector ParseVector3(LPCSTR value)
	{
		Fvector result = {};
		if (!value || !value[0])
			return result;

		int count = sscanf(value, "%f,%f,%f", &result.x, &result.y, &result.z);
		if (count < 3)
			result.set(0.f, 0.f, 0.f);
		return result;
	}

	float EaseOutElastic(float t)
	{
		if (t <= 0.f)
			return 0.f;
		if (t >= 1.f)
			return 1.f;

		const float p = 0.3f;
		return powf(2.f, -10.f * t) * sinf((t - p / 4.f) * (2.f * PI) / p) + 1.f;
	}
}

bool CInteractionMarkerManager::IsEnabled()
{
	return g_pInteractionMarkerManager && g_pInteractionMarkerManager->m_enabled;
}

bool CInteractionMarkerManager::ShouldSuppressVanilla()
{
	return IsEnabled() && g_pInteractionMarkerManager->m_suppress_vanilla;
}

bool CInteractionMarkerManager::ShouldSuppressTutorialUi()
{
	if (!IsEnabled() || !g_pInteractionMarkerManager || !g_pInteractionMarkerManager->m_suppress_tutorial_ui)
		return false;

	LPCSTR tutorial_name = g_pInteractionMarkerManager->GetActiveTutorialName();
	return tutorial_name && g_pInteractionMarkerManager->HasTutorialPromptMapping(tutorial_name);
}

bool CInteractionMarkerManager::ShouldSuppressNpcName(float distance)
{
	if (!ShouldSuppressVanilla() || !g_pInteractionMarkerManager)
		return false;

	const u32 idx = static_cast<u32>(EWSUIClass::Npc);
	if (idx >= static_cast<u32>(EWSUIClass::Count))
		return false;

	const SWSUIClassDef& def = g_pInteractionMarkerManager->m_classes[idx];
	if (!def.enabled)
		return false;

	return distance <= def.show_distance;
}

void CInteractionMarkerManager::Load()
{
	m_enabled = false;
	m_suppress_vanilla = true;
	m_scan_radius = 5.f;
	m_prompt_distance = 4.f;
	m_max_markers = 10;
	m_dot_size = 12.f;
	m_lerp_speed = 0.15f;
	m_scan_interval_ms = 150;
	m_markers.clear();
	m_los_rr_order.clear();
	m_los_rr_cursor = 0;
	m_los_rr_order_size = 0;
	m_los_cam_valid = false;
	m_scan_motion_valid = false;
	m_bones_by_section.clear();
	m_pos_adj_by_section.clear();
	m_door_visuals_y.clear();
	m_npc_roles_by_section.clear();
	m_usable_textures_by_section.clear();
	m_zone_textures_by_name.clear();
	m_zone_prompts_by_name.clear();
	m_tutorial_prompts_by_name.clear();
	m_quest_scheme_stories.clear();
	m_breakable_box_visuals.clear();
	m_bone_priority.clear();
	m_focus_id = 0xffff;
	HUD_SOUND_ITEM::DestroySound(m_focus_snd);
	m_focus_sound_loaded = false;
	m_enable_focus_sound = false;
	m_markers_cfg = {};
	m_prompt_cfg = {};
	m_wheel_focus_id = 0xffff;
	m_engine_focus_id = 0xffff;
	m_dik_icons.clear();

	if (!pSettings->section_exist("wsui"))
		return;

	m_enabled = READ_IF_EXISTS(pSettings, r_bool, "wsui", "enabled", false);
	if (!m_enabled)
		return;

	m_suppress_vanilla = READ_IF_EXISTS(pSettings, r_bool, "wsui", "suppress_vanilla", true);
	m_scan_radius = READ_IF_EXISTS(pSettings, r_float, "wsui", "scan_radius", 5.f);
	m_prompt_distance = READ_IF_EXISTS(pSettings, r_float, "wsui", "prompt_distance", 4.f);
	m_max_markers = READ_IF_EXISTS(pSettings, r_u32, "wsui", "max_markers", 10);
	m_dot_size = READ_IF_EXISTS(pSettings, r_float, "wsui", "dot_size", 12.f);
	m_lerp_speed = READ_IF_EXISTS(pSettings, r_float, "wsui", "lerp_speed", 0.15f);
	m_scan_interval_ms = READ_IF_EXISTS(pSettings, r_u32, "wsui", "scan_interval_ms", 150);
	m_ui_xml = READ_IF_EXISTS(pSettings, r_string, "wsui", "ui_xml", nullptr);
	if (!m_ui_xml.size())
		m_ui_xml = READ_IF_EXISTS(pSettings, r_string, "wsui", "prompt_ui_xml", "ui_wsui.xml");
	m_hide_mute_stalkers = READ_IF_EXISTS(pSettings, r_bool, "wsui", "hide_mute_stalkers", true);
	m_enable_task_icons = READ_IF_EXISTS(pSettings, r_bool, "wsui", "enable_task_icons", true);
	m_suppress_tutorial_ui = READ_IF_EXISTS(pSettings, r_bool, "wsui", "suppress_tutorial_ui", true);
	m_enable_quest_scheme_scan = READ_IF_EXISTS(pSettings, r_bool, "wsui", "enable_quest_scheme_scan", true);
	m_enable_focus_sound = READ_IF_EXISTS(pSettings, r_bool, "wsui", "enable_focus_sound", false);
	LoadFocusSound();
	LoadDikIcons();

	LoadClassDefs();
	LoadBonePriority();
	LoadLookupSection("bones_by_section", false);
	LoadLookupSection("pos_adj_by_section", true);
	LoadFloatLookupSection("door_visuals", m_door_visuals_y);
	LoadTextureLookupSection("npc_roles_by_section", m_npc_roles_by_section);
	LoadTextureLookupSection("usable_textures_by_section", m_usable_textures_by_section);
	LoadTextureLookupSection("zone_textures_by_name", m_zone_textures_by_name);
	LoadTextureLookupSection("zone_prompts_by_name", m_zone_prompts_by_name);
	LoadTextureLookupSection("tutorial_prompts_by_name", m_tutorial_prompts_by_name);
	LoadWsuiXml();

	if (m_enable_quest_scheme_scan)
		BuildQuestSchemeIndex();

	if (pSettings->section_exist("breakable_box_visuals"))
	{
		const CInifile::Sect& sect = pSettings->r_section("breakable_box_visuals");
		for (const auto& line : sect.Data)
			m_breakable_box_visuals.insert(line.first);
	}
}

void CInteractionMarkerManager::LoadDikIcons()
{
	m_dik_icons.clear();
	if (!pSettings->section_exist("wsui_dik_icons"))
		return;

	const CInifile::Sect& sect = pSettings->r_section("wsui_dik_icons");
	for (const auto& line : sect.Data)
	{
		int dik = keyname_to_dik(*line.first);
		if (!dik)
			dik = atoi(*line.first);
		if (dik > 0)
			m_dik_icons[dik] = line.second;
	}
}

void CInteractionMarkerManager::LoadClassDefs()
{
	for (u32 i = 1; i < static_cast<u32>(EWSUIClass::Count); ++i)
	{
		const EWSUIClass cls = static_cast<EWSUIClass>(i);
		LPCSTR section = ClassSectionName(cls);
		SWSUIClassDef& def = m_classes[i];

		def.enabled = true;
		def.texture = nullptr;
		def.active_texture = nullptr;
		def.bone = nullptr;
		def.show_distance = m_scan_radius;

		if (!section || !pSettings->section_exist(section))
			continue;

		def.enabled = READ_IF_EXISTS(pSettings, r_bool, section, "enabled", true);
		def.texture = READ_IF_EXISTS(pSettings, r_string, section, "texture", nullptr);
		def.active_texture = READ_IF_EXISTS(pSettings, r_string, section, "active_texture", def.texture);
		def.bone = READ_IF_EXISTS(pSettings, r_string, section, "bone", nullptr);
		def.show_distance = READ_IF_EXISTS(pSettings, r_float, section, "show_distance", m_scan_radius);
	}
}

void CInteractionMarkerManager::LoadFloatLookupSection(LPCSTR section_name, xr_map<shared_str, float>& out)
{
	out.clear();
	if (!section_name || !pSettings->section_exist(section_name))
		return;

	const CInifile::Sect& sect = pSettings->r_section(section_name);
	for (const auto& line : sect.Data)
		out[line.first] = (float)atof(*line.second);
}

void CInteractionMarkerManager::LoadTextureLookupSection(LPCSTR section_name, xr_map<shared_str, shared_str>& out)
{
	out.clear();
	if (!section_name || !pSettings->section_exist(section_name))
		return;

	const CInifile::Sect& sect = pSettings->r_section(section_name);
	for (const auto& line : sect.Data)
		out[line.first] = line.second;
}

void CInteractionMarkerManager::LoadLookupSection(LPCSTR section_name, bool is_pos_adj)
{
	if (!section_name || !pSettings->section_exist(section_name))
		return;

	const CInifile::Sect& sect = pSettings->r_section(section_name);
	for (const auto& line : sect.Data)
	{
		if (is_pos_adj)
			m_pos_adj_by_section[line.first] = ParseVector3(*line.second);
		else
			m_bones_by_section[line.first] = line.second;
	}
}

void CInteractionMarkerManager::LoadBonePriority()
{
	m_bone_priority.clear();

	static const LPCSTR default_bones[] = {
		"wpn_body", "bip01_spine1", "spine_1", "door_right", "door_left",
		"lock", "door", "link", "joint", "joint1", "patch", "bone01", nullptr
	};

	if (pSettings->section_exist("bones"))
	{
		const CInifile::Sect& sect = pSettings->r_section("bones");
		for (const auto& line : sect.Data)
			m_bone_priority.push_back(line.first);
	}

	if (m_bone_priority.empty())
	{
		for (u32 i = 0; default_bones[i]; ++i)
			m_bone_priority.emplace_back(default_bones[i]);
	}
}

namespace
{
	bool IsQuestSchemeActiveValue(LPCSTR active)
	{
		if (!active || !active[0])
			return false;

		return !strncmp(active, "ph_button", 9) ||
			!strncmp(active, "ph_code", 7) ||
			!strncmp(active, "ph_idle", 7);
	}

	void ExtractStoryIdFromFilename(LPCSTR filename, string64& out)
	{
		out[0] = 0;
		if (!filename || !filename[0])
			return;

		const char* slash = strrchr(filename, '\\');
		if (!slash)
			slash = strrchr(filename, '/');
		const char* base = slash ? slash + 1 : filename;

		const char* dot = strrchr(base, '.');
		size_t len = dot ? size_t(dot - base) : xr_strlen(base);
		if (len >= sizeof(string64))
			len = sizeof(string64) - 1;

		strncpy_s(out, sizeof(string64), base, len);
		out[len] = 0;
	}
}

void CInteractionMarkerManager::CollectQuestSchemesInDir(LPCSTR dir_path)
{
	if (!dir_path || !dir_path[0])
		return;

	FS_FileSet listing;
	FS.file_list(listing, dir_path, FS_ListFiles | FS_ListFolders | FS_RootOnly, "*");

	for (const FS_File& entry : listing)
	{
		string_path child_path;
		xr_strconcat(child_path, dir_path, "/", entry.name.c_str());

		if (entry.attrib & FS_File::flSubDir)
		{
			CollectQuestSchemesInDir(child_path);
			continue;
		}

		if (!strstr(entry.name.c_str(), ".ltx"))
			continue;

		CInifile script_ini(child_path, TRUE, TRUE, FALSE);
		if (!script_ini.section_exist("logic") || !script_ini.line_exist("logic", "active"))
			continue;

		LPCSTR active = script_ini.r_string("logic", "active");
		if (!IsQuestSchemeActiveValue(active))
			continue;

		string64 story_id = {};
		ExtractStoryIdFromFilename(entry.name.c_str(), story_id);
		if (story_id[0])
			m_quest_scheme_stories.insert(story_id);
	}
}

void CInteractionMarkerManager::BuildQuestSchemeIndex()
{
	m_quest_scheme_stories.clear();

	string_path scripts_root;
	FS.update_path(scripts_root, _game_config_, "scripts\\");
	CollectQuestSchemesInDir(scripts_root);
}

bool CInteractionMarkerManager::IsQuestSchemeStory(LPCSTR story_id) const
{
	return story_id && story_id[0] && m_quest_scheme_stories.find(story_id) != m_quest_scheme_stories.end();
}

shared_str CInteractionMarkerManager::ResolveStoryId(CGameObject* obj) const
{
	if (!obj)
		return nullptr;

	if (LPCSTR story_id = CScriptStoryIDManager::GetInstance().GetID(obj->ID()))
	{
		if (story_id[0])
			return story_id;
	}

	return obj->cName();
}

bool CInteractionMarkerManager::IsQuestSchemeObject(CGameObject* obj) const
{
	if (!obj || !m_enable_quest_scheme_scan)
		return false;

	if (!obj->cast_physics_object())
		return false;

	shared_str story_id = ResolveStoryId(obj);
	return story_id.size() && IsQuestSchemeStory(story_id.c_str());
}

LPCSTR CInteractionMarkerManager::GetActiveTutorialName() const
{
	if (!g_tutorial || !g_tutorial->IsActive())
		return nullptr;

	if (!g_tutorial->m_name || !g_tutorial->m_name[0])
		return nullptr;

	return g_tutorial->m_name;
}

bool CInteractionMarkerManager::HasTutorialPromptMapping(LPCSTR tutorial_name) const
{
	if (!tutorial_name || !tutorial_name[0])
		return false;

	if (m_tutorial_prompts_by_name.find(tutorial_name) != m_tutorial_prompts_by_name.end())
		return true;

	if (m_zone_prompts_by_name.find(tutorial_name) != m_zone_prompts_by_name.end())
		return true;

	return false;
}

LPCSTR CInteractionMarkerManager::ResolveTutorialPrompt(LPCSTR tutorial_name) const
{
	if (!tutorial_name || !tutorial_name[0])
		return nullptr;

	auto it = m_tutorial_prompts_by_name.find(tutorial_name);
	if (it != m_tutorial_prompts_by_name.end())
		return it->second.c_str();

	auto zone_it = m_zone_prompts_by_name.find(tutorial_name);
	if (zone_it != m_zone_prompts_by_name.end())
		return zone_it->second.c_str();

	return nullptr;
}

float CInteractionMarkerManager::UiScale() const
{
	return m_prompt_cfg.ui_scale > 0.f ? m_prompt_cfg.ui_scale : 1.f;
}

float CInteractionMarkerManager::AspectScaleX() const
{
	const float base = (Device.TargetWidth * UI_BASE_HEIGHT) / (Device.TargetHeight * UI_BASE_WIDTH);
	return base * m_prompt_cfg.aspect_correction;
}

bool CInteractionMarkerManager::IsBreakableBox(CGameObject* obj) const
{
	if (!obj)
		return false;

	return m_breakable_box_visuals.find(obj->cNameVisual()) != m_breakable_box_visuals.end();
}

bool CInteractionMarkerManager::IsExplosiveObject(CGameObject* obj) const
{
	if (!obj)
		return false;

	if (obj->cast_explosive() || obj->cast_explosive_rocket())
		return true;

	LPCSTR sect = obj->cNameSect().c_str();
	if (!sect || !sect[0])
		return false;

	if (strstr(sect, "ied_") || strstr(sect, "mine_") || strstr(sect, "explosive_"))
		return true;

	return false;
}

bool CInteractionMarkerManager::MatchSquadOrLeader(u16 object_id, u16 compare_id) const
{
	if (object_id == compare_id)
		return true;

	if (!ai().get_alife() || compare_id == u16(-1))
		return false;

	CSE_ALifeDynamicObject* se_obj = ai().alife().objects().object(compare_id, true);
	if (!se_obj)
		return false;

	CSE_ALifeOnlineOfflineGroup* squad = se_obj->cast_online_offline_group();
	if (!squad)
		return false;

	return object_id == squad->commander_id();
}

bool CInteractionMarkerManager::IsTaskTarget(u16 object_id, bool& is_storyline) const
{
	is_storyline = false;

	if (!m_enable_task_icons || !Level().GameTaskManager())
		return false;

	CGameTaskManager* tm = Level().GameTaskManager();

	if (CGameTask* story_task = tm->ActiveTask(eTaskTypeStoryline))
	{
		if (story_task->HasActiveMapTarget())
		{
			const SGameTaskObjective& objective = story_task->Objective(story_task->ActiveObjectiveIdx());
			if (MatchSquadOrLeader(object_id, objective.m_map_object_id))
			{
				is_storyline = true;
				return true;
			}
		}
	}

	if (CGameTask* add_task = tm->ActiveTask(eTaskTypeAdditional))
	{
		if (add_task->HasActiveMapTarget())
		{
			const SGameTaskObjective& objective = add_task->Objective(add_task->ActiveObjectiveIdx());
			if (MatchSquadOrLeader(object_id, objective.m_map_object_id))
			{
				is_storyline = false;
				return true;
			}
		}
	}

	return false;
}

bool CInteractionMarkerManager::IsSquadLeaderNpc(CAI_Stalker* stalker) const
{
	if (!stalker)
		return false;

	LPCSTR sect = stalker->cNameSect().c_str();
	if (sect && strstr(sect, "leader"))
		return true;

	return false;
}

bool CInteractionMarkerManager::HasValidBone(CGameObject* obj, LPCSTR bone_name) const
{
	if (!bone_name || !bone_name[0] || xr_stricmp(bone_name, "nil") == 0)
		return false;

	if (IKinematics* kinematics = PKinematics(obj->Visual()))
		return kinematics->LL_BoneID(bone_name) != BI_NONE;

	return false;
}

LPCSTR CInteractionMarkerManager::ResolveBoneName(CGameObject* obj, EWSUIClass cls, const SWSUIClassDef& def) const
{
	if (!obj)
		return nullptr;

	// Ground items / zones: center marker is more reliable than bones
	if (cls == EWSUIClass::Item || cls == EWSUIClass::Campfire || cls == EWSUIClass::Zone)
		return nullptr;

	shared_str section = obj->cNameSect();
	auto bone_it = m_bones_by_section.find(section);
	if (bone_it != m_bones_by_section.end())
	{
		if (HasValidBone(obj, bone_it->second.c_str()))
			return bone_it->second.c_str();
	}

	if (def.bone.size() && HasValidBone(obj, def.bone.c_str()))
		return def.bone.c_str();

	if (IKinematics* kinematics = PKinematics(obj->Visual()))
	{
		for (const shared_str& bone : m_bone_priority)
		{
			if (kinematics->LL_BoneID(bone.c_str()) != BI_NONE)
				return bone.c_str();
		}
	}

	return nullptr;
}

bool CInteractionMarkerManager::IsDoorObject(CGameObject* obj) const
{
	if (!obj)
		return false;

	if (CPhysicObject* physic = obj->cast_physics_object())
	{
		Fvector closed, open;
		if (physic->get_door_vectors(closed, open))
			return true;
	}

	if (IKinematics* kinematics = PKinematics(obj->Visual()))
	{
		static const LPCSTR door_bones[] = { "door_right", "door_left", "lock", "door", nullptr };
		for (u32 i = 0; door_bones[i]; ++i)
		{
			if (kinematics->LL_BoneID(door_bones[i]) != BI_NONE)
				return true;
		}
	}

	return false;
}

bool CInteractionMarkerManager::HasUsableTip(CGameObject* obj) const
{
	if (!obj)
		return false;

	if (CUsableScriptObject* usable = obj->cast_usable_script_object())
		return usable->tip_text() && usable->tip_text()[0];

	return false;
}

bool CInteractionMarkerManager::IsKnownZone(CGameObject* obj) const
{
	if (!obj)
		return false;

	if (!obj->cast_restrictor())
		return false;

	shared_str name = obj->cName();
	if (m_zone_textures_by_name.find(name) != m_zone_textures_by_name.end())
		return true;
	if (m_zone_prompts_by_name.find(name) != m_zone_prompts_by_name.end())
		return true;

	if (!xr_strcmp(obj->cNameSect(), "camp_zone"))
		return true;

	LPCSTR name_cstr = name.c_str();
	if (name_cstr && strstr(name_cstr, "_sr_sleep"))
		return true;

	return false;
}

LPCSTR CInteractionMarkerManager::ResolveZonePrompt(CGameObject* obj) const
{
	if (!obj)
		return nullptr;

	auto it = m_zone_prompts_by_name.find(obj->cName());
	if (it != m_zone_prompts_by_name.end())
		return it->second.c_str();

	if (!xr_strcmp(obj->cNameSect(), "camp_zone") || strstr(obj->cName().c_str(), "_sr_sleep"))
		return "sleep_zone_tip";

	return nullptr;
}

LPCSTR CInteractionMarkerManager::ResolveDoorBone(CGameObject* obj) const
{
	if (!obj)
		return "nil";

	if (IKinematics* kinematics = PKinematics(obj->Visual()))
	{
		static const LPCSTR door_bones[] = { "door_right", "door_left", "lock", "door", nullptr };
		for (u32 i = 0; door_bones[i]; ++i)
		{
			if (kinematics->LL_BoneID(door_bones[i]) != BI_NONE)
				return door_bones[i];
		}
	}

	return "nil";
}

float CInteractionMarkerManager::GetDoorVisualYOffset(CGameObject* obj) const
{
	if (!obj)
		return 0.25f;

	auto it = m_door_visuals_y.find(obj->cNameVisual());
	if (it != m_door_visuals_y.end())
		return it->second;

	return 0.25f;
}

shared_str CInteractionMarkerManager::ResolveActiveTexture(CGameObject* obj, EWSUIClass cls, const SWSUIClassDef& def, bool focused) const
{
	if (m_markers_cfg.features.special_icons_always && obj)
	{
		if (cls == EWSUIClass::Item && IsExplosiveObject(obj))
			return "ui_wsui_marker_explosive";

		if (cls == EWSUIClass::Usable)
		{
			if (IsBreakableBox(obj))
				return "ui_wsui_marker_breakable";
			if (IsExplosiveObject(obj))
				return "ui_wsui_marker_explosive";
		}
	}

	if (!focused)
		return def.texture;

	if (!obj)
		return def.active_texture.size() ? def.active_texture : def.texture;

	const u16 object_id = obj->ID();

	if (m_enable_task_icons)
	{
		bool is_storyline = false;
		if (IsTaskTarget(object_id, is_storyline))
			return is_storyline ? "ui_wsui_marker_task_pri" : "ui_wsui_marker_task_sec";
	}

	shared_str section = obj->cNameSect();

	if (cls == EWSUIClass::Npc)
	{
		if (CAI_Stalker* stalker = obj->cast_stalker())
		{
			if (IsSquadLeaderNpc(stalker))
				return "ui_wsui_marker_leader";
		}

		auto role_it = m_npc_roles_by_section.find(section);
		if (role_it != m_npc_roles_by_section.end())
			return role_it->second;

		LPCSTR sect = section.c_str();
		if (strstr(sect, "trader") || strstr(sect, "barman") || strstr(sect, "merchant"))
			return "ui_wsui_marker_trade";
		if (strstr(sect, "mechanic") || strstr(sect, "_tech"))
			return "ui_wsui_marker_mech";
		if (strstr(sect, "medic"))
			return "ui_wsui_marker_medic";
		if (strstr(sect, "guide") || strstr(sect, "lesnik"))
			return "ui_wsui_marker_guide";
		if (strstr(sect, "leader"))
			return "ui_wsui_marker_leader";
	}
	else if (cls == EWSUIClass::Body)
	{
		if (obj->cast_base_monster() && !obj->cast_stalker())
			return "ui_wsui_marker_butcher";
	}
	else if (cls == EWSUIClass::Item)
	{
		if (IsExplosiveObject(obj))
			return "ui_wsui_marker_explosive";
	}
	else if (cls == EWSUIClass::Usable)
	{
		if (IsBreakableBox(obj))
			return "ui_wsui_marker_breakable";

		if (IsExplosiveObject(obj))
			return "ui_wsui_marker_explosive";

		if (IsQuestSchemeObject(obj))
			return "ui_wsui_marker_dotactive";

		auto tex_it = m_usable_textures_by_section.find(section);
		if (tex_it != m_usable_textures_by_section.end())
			return tex_it->second;

		LPCSTR sect = section.c_str();
		if (strstr(sect, "tiski") || strstr(sect, "workshop"))
			return "ui_wsui_marker_mech";
	}
	else if (cls == EWSUIClass::Zone)
	{
		auto tex_it = m_zone_textures_by_name.find(obj->cName());
		if (tex_it != m_zone_textures_by_name.end())
			return tex_it->second;

		LPCSTR name = obj->cName().c_str();
		if (name && strstr(name, "_heli_"))
			return "ui_wsui_marker_dotactive";
		if (!xr_strcmp(obj->cNameSect(), "camp_zone") || (name && strstr(name, "_sr_sleep")))
			return "ui_wsui_marker_talk";
	}

	return def.active_texture.size() ? def.active_texture : def.texture;
}

EWSUIClass CInteractionMarkerManager::ClassifyObject(CGameObject* obj) const
{
	if (!obj || obj->getDestroy() || !obj->getVisible())
		return EWSUIClass::None;

	if (CActor* actor = obj->cast_actor())
	{
		if (actor == Actor())
			return EWSUIClass::None;
	}

	if (obj->cast_inventory_box())
		return EWSUIClass::Stash;

	if (CAI_Stalker* stalker = obj->cast_stalker())
	{
		if (CEntityAlive* alive = stalker->cast_entity_alive())
		{
			if (alive->g_Alive())
			{
				return EWSUIClass::Npc;
			}
			else
			{
				return EWSUIClass::Body;
			}
		}
	}

	if (CBaseMonster* monster = obj->cast_base_monster())
	{
		if (CEntityAlive* alive = monster->cast_entity_alive())
		{
			if (!alive->g_Alive())
				return EWSUIClass::Body;
		}
	}

	if (CInventoryItem* item = obj->cast_inventory_item())
	{
		if (item->Useful() && item->CanTake())
			return EWSUIClass::Item;
	}

	if (IsDoorObject(obj))
		return EWSUIClass::Door;

	if (obj->cast_zone_campfire())
		return EWSUIClass::Campfire;

	if (IsKnownZone(obj))
		return EWSUIClass::Zone;

	if (IsQuestSchemeObject(obj))
		return EWSUIClass::Usable;

	if (IsBreakableBox(obj))
		return EWSUIClass::Usable;

	if (HasUsableTip(obj))
		return EWSUIClass::Usable;

	return EWSUIClass::None;
}

bool CInteractionMarkerManager::ResolveMarkerDef(CGameObject* obj, EWSUIClass cls, SWSUIClassDef& out_def, Fvector& out_offset) const
{
	const u32 idx = static_cast<u32>(cls);
	if (idx >= static_cast<u32>(EWSUIClass::Count))
		return false;

	out_def = m_classes[idx];
	if (!out_def.enabled || !out_def.texture.size())
		return false;

	shared_str section = obj->cNameSect();
	auto bone_it = m_bones_by_section.find(section);
	if (bone_it != m_bones_by_section.end())
		out_def.bone = bone_it->second;

	auto pos_it = m_pos_adj_by_section.find(section);
	out_offset = (pos_it != m_pos_adj_by_section.end()) ? pos_it->second : Fvector().set(0.f, 0.f, 0.f);

	if (cls == EWSUIClass::Door)
	{
		out_def.bone = ResolveDoorBone(obj);
		out_offset.y = GetDoorVisualYOffset(obj);
	}

	return true;
}

bool CInteractionMarkerManager::GetMarkerWorldPos(CGameObject* obj, EWSUIClass cls, LPCSTR bone_name, const Fvector& offset, Fvector& out) const
{
	if (bone_name && bone_name[0] && xr_stricmp(bone_name, "nil") != 0)
	{
		if (IKinematics* kinematics = PKinematics(obj->Visual()))
		{
			const u16 bone_id = kinematics->LL_BoneID(bone_name);
			if (bone_id != BI_NONE)
			{
				Fmatrix matrix;
				matrix.mul_43(obj->XFORM(), kinematics->LL_GetTransform(bone_id));
				out = matrix.c;
				out.add(offset);
				return true;
			}
		}
	}

	obj->Center(out);

	if (cls == EWSUIClass::Zone || cls == EWSUIClass::Campfire)
	{
		Fvector to_viewer;
		to_viewer.sub(Device.vCameraPosition, out);
		const float dist = to_viewer.magnitude();
		if (dist > 0.01f)
		{
			to_viewer.mul(1.f / dist);
			float pull = 0.5f;
			if (cls == EWSUIClass::Zone)
			{
				if (CSpaceRestrictor* restrictor = obj->cast_restrictor())
					pull = std::min(restrictor->Radius() * 0.45f, dist * 0.85f);
			}
			else if (CAnomalyZone* zone = obj->cast_anomaly_zone())
				pull = std::min(zone->Radius() * 0.45f, dist * 0.85f);

			out.mad(out, to_viewer, pull);
		}
	}

	out.add(offset);
	return true;
}

Fvector2 CInteractionMarkerManager::WorldToScreen(const Fvector& world_pos, bool allow_offscreen) const
{
	return ::World2Ui(world_pos, false, allow_offscreen);
}

void CInteractionMarkerManager::RefreshMarkerClassifyCache(SInteractionMarker& marker, CGameObject* obj, EWSUIClass cls)
{
	marker.cls = cls;
	if (!ResolveMarkerDef(obj, cls, marker.cached_def, marker.cached_offset))
	{
		marker.classify_valid = false;
		return;
	}

	if (LPCSTR bone = ResolveBoneName(obj, cls, marker.cached_def))
		marker.cached_bone = bone;
	else
		marker.cached_bone = nullptr;

	marker.classify_valid = true;
	marker.los_stale = true;
}

void CInteractionMarkerManager::RebuildLosRoundRobinOrder()
{
	m_los_rr_order.clear();
	m_los_rr_order.reserve(m_markers.size());

	for (const auto& [id, marker] : m_markers)
	{
		VERIFY(id == marker.object_id);
		m_los_rr_order.push_back(id);
	}

	if (m_los_rr_order.size() != m_los_rr_order_size)
	{
		m_los_rr_cursor = 0;
		m_los_rr_order_size = m_los_rr_order.size();
	}
}

void CInteractionMarkerManager::MarkLosCachesStale()
{
	for (auto& [id, marker] : m_markers)
	{
		VERIFY(id == marker.object_id);
		marker.los_stale = true;
	}
}

void CInteractionMarkerManager::Scan(CActor* actor)
{
	if (!actor)
		return;

	xr_set<u16> found_ids;
	const Fvector& origin = actor->Position();

	static xr_vector<ISpatialShared> spatial_results;
	spatial_results.clear();
	spatial_results.reserve(64);

	const u64 spatial_mask =
		u64(ESPATIAL_TYPE::ITEM) |
		u64(ESPATIAL_TYPE::STALKER) |
		u64(ESPATIAL_TYPE::STALKER_DEAD) |
		u64(ESPATIAL_TYPE::INV_BOX) |
		u64(ESPATIAL_TYPE::AI) |
		u64(ESPATIAL_TYPE::MONSTER) |
		u64(ESPATIAL_TYPE::MONSTER_DEAD) |
		u64(ESPATIAL_TYPE::PHYSIC_OBJECT) |
		u64(ESPATIAL_TYPE::ANOMALY_ZONE) |
		u64(ESPATIAL_TYPE::SPACE_RESTRICTOR) |
		u64(ESPATIAL_TYPE::CAMP_ZONE);

	g_SpatialSpace->q_sphere(spatial_results, 0, ESPATIAL_TYPE(spatial_mask), origin, m_scan_radius);

	for (const ISpatialShared& spatial : spatial_results)
	{
		if (!spatial.get())
			continue;

		CObject* object = spatial->dcast_CObject();
		if (!object)
			continue;

		CGameObject* game_object = object->cast_game_object();
		if (!game_object)
			continue;

		const float distance = origin.distance_to(game_object->Position());
		const EWSUIClass cls = ClassifyObject(game_object);
		if (cls == EWSUIClass::None)
			continue;

		if (cls == EWSUIClass::Npc && m_hide_mute_stalkers)
		{
			if (CInventoryOwner* owner = game_object->cast_inventory_owner())
			{
				if (!owner->IsTalkEnabled())
					continue;
			}
		}

		SWSUIClassDef def;
		Fvector offset;
		if (!ResolveMarkerDef(game_object, cls, def, offset))
			continue;

		if (distance > def.show_distance)
			continue;

		const u16 id = game_object->ID();
		found_ids.insert(id);

		const bool is_new = m_markers.find(id) == m_markers.end();
		SInteractionMarker& marker = m_markers[id];
		marker.object_id = id;
		RefreshMarkerClassifyCache(marker, game_object, cls);
		if (!marker.classify_valid)
		{
			m_markers.erase(id);
			found_ids.erase(id);
			continue;
		}

		if (is_new)
		{
			marker.screen_pos.set(-1.f, -1.f);
			marker.target_screen_pos.set(-1.f, -1.f);
			marker.los_has_result = false;
			marker.los_stale = true;
		}
		marker.visible = true;
	}

	for (auto it = m_markers.begin(); it != m_markers.end();)
	{
		if (found_ids.find(it->first) == found_ids.end())
			it = m_markers.erase(it);
		else
			++it;
	}

	RebuildLosRoundRobinOrder();
}

void CInteractionMarkerManager::UpdateMarkerPositions(CActor* actor)
{
	m_engine_focus_id = 0xffff;
	m_focus_id = 0xffff;

	CGameObject* focus_object = actor ? actor->ObjectWeLookingAt() : nullptr;
	if (focus_object)
		m_engine_focus_id = focus_object->ID();

	if (m_markers_cfg.features.wheel_cycle_pickups && m_wheel_focus_id != 0xffff)
	{
		auto wheel_it = m_markers.find(m_wheel_focus_id);
		if (wheel_it != m_markers.end() && wheel_it->second.cls == EWSUIClass::Item)
			m_focus_id = m_wheel_focus_id;
		else
			m_wheel_focus_id = 0xffff;
	}

	if (m_focus_id == 0xffff)
		m_focus_id = m_engine_focus_id;

	const u32 now = Device.dwTimeGlobal;
	const Fvector& cam_pos = Device.vCameraPosition;
	const Fvector& cam_dir = Device.vCameraDirection;

	bool invalidate_los = false;
	if (!m_los_cam_valid)
	{
		invalidate_los = true;
		m_los_cam_valid = true;
	}
	else
	{
		if (m_los_cam_pos.distance_to_sqr(cam_pos) > WSUI_LOS_CAM_POS_EPS * WSUI_LOS_CAM_POS_EPS)
			invalidate_los = true;
		else if (1.f - m_los_cam_dir.dotproduct(cam_dir) > WSUI_LOS_CAM_DIR_EPS)
			invalidate_los = true;
	}

	if (invalidate_los)
	{
		MarkLosCachesStale();
		m_los_cam_pos = cam_pos;
		m_los_cam_dir = cam_dir;
	}

	xr_set<u16> los_check_ids;
	if (!m_los_rr_order.empty())
	{
		const u32 order_size = m_los_rr_order.size();
		const u32 checks = std::min(WSUI_LOS_CHECKS_PER_FRAME, order_size);
		for (u32 i = 0; i < checks; ++i)
		{
			const u32 idx = (m_los_rr_cursor + i) % order_size;
			los_check_ids.insert(m_los_rr_order[idx]);
		}
		m_los_rr_cursor = (m_los_rr_cursor + checks) % order_size;
	}

	const float lerp_factor = clampr(m_lerp_speed * Device.fTimeDelta * 60.f, 0.f, 1.f);

	for (auto& [id, marker] : m_markers)
	{
		marker.visible = false;
		marker.reachable = false;
		marker.distance = 0.f;

		if (!marker.classify_valid || marker.cls == EWSUIClass::None)
			continue;

		CObject* object = Level().Objects.net_Find(id);
		CGameObject* game_object = object ? object->cast_game_object() : nullptr;
		if (!game_object)
			continue;

		const EWSUIClass cls = marker.cls;
		SWSUIClassDef def = marker.cached_def;
		Fvector offset = marker.cached_offset;
		LPCSTR bone_name = marker.cached_bone.size() ? marker.cached_bone.c_str() : nullptr;

		if (cls == EWSUIClass::Door)
		{
			def.bone = ResolveDoorBone(game_object);
			offset.y = GetDoorVisualYOffset(game_object);
			if (def.bone.size())
				bone_name = def.bone.c_str();
		}

		Fvector world_pos;
		if (!GetMarkerWorldPos(game_object, cls, bone_name, offset, world_pos))
			continue;

		const float distance = actor->Position().distance_to(world_pos);
		marker.distance = distance;
		if (distance > def.show_distance)
			continue;

		const bool is_focus = id == m_focus_id;
		bool inside_volume = false;
		if (cls == EWSUIClass::Zone)
		{
			if (CSpaceRestrictor* restrictor = game_object->cast_restrictor())
			{
				Fsphere probe;
				probe.P = actor->Position();
				probe.R = 0.25f;
				inside_volume = restrictor->inside(probe);
			}
		}
		else if (cls == EWSUIClass::Campfire)
		{
			if (CAnomalyZone* zone = game_object->cast_anomaly_zone())
				inside_volume = actor->Position().distance_to(zone->Position()) <= zone->Radius();
		}

		if (inside_volume)
		{
			marker.reachable = true;
		}
		else if (is_focus)
		{
			marker.reachable = HasLineOfSight(actor, game_object, world_pos);
			marker.los_reachable = marker.reachable;
			marker.los_has_result = true;
			marker.los_stale = false;
			marker.los_check_time = now;
		}
		else
		{
			const bool stale = !marker.los_has_result || marker.los_stale || (now - marker.los_check_time >= WSUI_LOS_CACHE_TTL_MS);
			if (stale && los_check_ids.find(id) != los_check_ids.end())
			{
				marker.los_reachable = HasLineOfSight(actor, game_object, world_pos);
				marker.los_has_result = true;
				marker.los_stale = false;
				marker.los_check_time = now;
			}

			marker.reachable = marker.los_has_result ? marker.los_reachable : false;
			if (!marker.reachable)
				continue;
		}

		Fvector2 target = WorldToScreen(world_pos, is_focus || inside_volume);
		if (target.x < 0.f)
			continue;

		marker.target_screen_pos = target;

		if (marker.screen_pos.x < 0.f || marker.screen_pos.y < 0.f)
			marker.screen_pos = target;
		else
		{
			marker.screen_pos.x = _lerp(marker.screen_pos.x, target.x, lerp_factor);
			marker.screen_pos.y = _lerp(marker.screen_pos.y, target.y, lerp_factor);
		}

		marker.visible = true;
	}

	ApplyMarkerLimit(actor);
}

void CInteractionMarkerManager::ApplyMarkerLimit(CActor* actor)
{
	if (!actor || m_max_markers == 0)
		return;

	xr_vector<std::pair<float, u16>> ranked;
	ranked.reserve(m_markers.size());

	for (const auto& [id, marker] : m_markers)
	{
		if (!marker.visible || !marker.reachable)
			continue;
		ranked.emplace_back(GetMarkerSortScore(id, marker), id);
	}

	if (ranked.size() <= m_max_markers)
		return;

	std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b)
	{
		if (a.first != b.first)
			return a.first > b.first;
		return a.second < b.second;
	});

	xr_set<u16> keep_ids;
	for (u32 i = 0; i < m_max_markers; ++i)
		keep_ids.insert(ranked[i].second);

	if (m_focus_id != 0xffff)
		keep_ids.insert(m_focus_id);

	for (auto& [id, marker] : m_markers)
	{
		if (!marker.visible)
			continue;

		if (keep_ids.find(id) == keep_ids.end())
			marker.visible = false;
	}
}

float CInteractionMarkerManager::GetMarkerSortScore(u16 id, const SInteractionMarker& marker) const
{
	float score = 1000000.f - marker.distance;

	if (m_enable_task_icons)
	{
		bool is_storyline = false;
		if (IsTaskTarget(id, is_storyline))
			score += m_markers_cfg.marker_priority.task;
	}

	if (id == m_focus_id)
		score += 5000.f;

	switch (marker.cls)
	{
	case EWSUIClass::Npc: score += m_markers_cfg.marker_priority.npc; break;
	case EWSUIClass::Stash: score += 2000.f; break;
	case EWSUIClass::Usable: score += 1500.f; break;
	case EWSUIClass::Door: score += 1000.f; break;
	default: break;
	}

	return score;
}

bool CInteractionMarkerManager::ShouldHideUI() const
{
	CUIGameCustom* ui = CurrentGameUI();
	if (!ui)
		return false;

	if (!ui->GameIndicatorsShown())
		return true;

	if (ui->HasShownDialogs())
		return true;

	if (ui->TalkMenu && ui->TalkMenu->IsActiveTalkUi())
		return true;

	if (ui->TradeWnd() && ui->TradeWnd()->IsShown())
		return true;

	return false;
}

bool CInteractionMarkerManager::IsPdaOpen() const
{
	CUIGameCustom* ui = CurrentGameUI();
	return ui && ui->PdaMenu() && ui->PdaMenu()->IsShown();
}

bool CInteractionMarkerManager::IsInventoryOpen() const
{
	CUIGameCustom* ui = CurrentGameUI();
	if (!ui)
		return false;

	if (ui->ActorMenu() && ui->ActorMenu()->IsShown())
		return true;

	if (ui->InventoryWnd() && ui->InventoryWnd()->IsShown())
		return true;

	return false;
}

bool CInteractionMarkerManager::ShouldSuspendScan() const
{
	if (ShouldHideUI())
		return true;

	if (IsPdaOpen())
		return true;

	if (IsInventoryOpen())
		return true;

	return false;
}

bool CInteractionMarkerManager::ShouldSuspendMarkLoop() const
{
	if (ShouldHideUI())
		return true;

	if (IsInventoryOpen())
		return true;

	return false;
}

bool CInteractionMarkerManager::HasLineOfSight(CActor* actor, CGameObject* obj, const Fvector& world_pos) const
{
	if (!actor || !obj)
		return false;

	if (CGameObject* look_at = actor->ObjectWeLookingAt())
	{
		if (look_at == obj || look_at->ID() == obj->ID())
			return true;
	}

	const Fvector& from = Device.vCameraPosition;
	Fvector dir;
	dir.sub(world_pos, from);
	const float range = dir.magnitude();
	if (range < 0.25f)
		return true;

	dir.div(range);

	static collide::rq_results ray_results;
	ray_results.r_clear();

	collide::ray_defs RD(from, dir, range, CDB::OPT_CULL, collide::rqtBoth);

	struct SLoSData
	{
		CGameObject* target = nullptr;
		CObject* actor_obj = nullptr;
		bool blocked = false;
	} los_data;

	los_data.target = obj;
	los_data.actor_obj = actor->cast_game_object();

	Level().ObjectSpace.RayQuery(ray_results, RD, [](collide::rq_result& result, LPVOID params) -> bool
	{
		SLoSData& data = *static_cast<SLoSData*>(params);

		if (result.O)
		{
			if (result.O == data.target || result.O == data.actor_obj)
				return false;

			if (CGameObject* go = result.O->cast_game_object())
			{
				if (go->cast_inventory_item())
					return true;

				if (CEntity* entity = go->cast_entity())
				{
					if (!entity->g_Alive())
						return true;
				}
			}

			if ((result.O->SpatialComponent->type & ESPATIAL_TYPE::OBSTACLE) != ESPATIAL_TYPE::NONE)
			{
				data.blocked = true;
				return false;
			}

			data.blocked = true;
			return false;
		}

		CDB::TRI& tri = Level().ObjectSpace.GetStaticTris()[result.element];
		if (GMLib.GetMaterialByIdx(tri.material)->Flags.is(SGameMtl::flPassable))
			return true;

		data.blocked = true;
		return false;
	}, &los_data, nullptr, obj);

	if (!los_data.blocked)
		return true;

	for (const collide::rq_result& result : ray_results.r_results())
	{
		if (result.O == obj)
			return true;
	}

	return false;
}

float CInteractionMarkerManager::SquareHeight(float width_ui) const
{
	return width_ui * AspectScaleX();
}

void CInteractionMarkerManager::UpdateTutorialPromptFade()
{
	LPCSTR tutorial_name = GetActiveTutorialName();
	const bool want = tutorial_name && HasTutorialPromptMapping(tutorial_name);

	if (want != m_tutorial_target_visible)
	{
		m_tutorial_target_visible = want;
		m_tutorial_fade_start_time = Device.dwTimeGlobal;
		m_tutorial_fade_start_alpha = m_tutorial_prompt_alpha;
	}

	const u32 fade_dur = want ? m_prompt_cfg.fade_animation.fade_in_ms : m_prompt_cfg.fade_animation.fade_out_ms;
	const float target = want ? 1.f : 0.f;

	if (!fade_dur)
	{
		m_tutorial_prompt_alpha = target;
		return;
	}

	float t = float(Device.dwTimeGlobal - m_tutorial_fade_start_time) / float(fade_dur);
	t = clampr(t, 0.f, 1.f);
	m_tutorial_prompt_alpha = m_tutorial_fade_start_alpha + (target - m_tutorial_fade_start_alpha) * t;

	if (!want && m_tutorial_prompt_alpha <= 0.01f)
		m_tutorial_prompt_alpha = 0.f;
}

float CInteractionMarkerManager::GetDotDistanceScale(float distance, float show_distance) const
{
	if (!m_markers_cfg.distance_fade.enable_scale || show_distance <= 0.f)
		return 1.f;

	const float t = clampr(1.f - distance / show_distance, 0.f, 1.f);
	return _lerp(m_markers_cfg.distance_fade.min_scale, m_markers_cfg.distance_fade.max_scale, t);
}

float CInteractionMarkerManager::GetDotDistanceAlpha(float distance, float show_distance) const
{
	if (!m_markers_cfg.distance_fade.enable_alpha || show_distance <= 0.f)
		return 1.f;

	const float t = clampr(1.f - distance / show_distance, 0.f, 1.f);
	return _lerp(m_markers_cfg.distance_fade.min_alpha, 1.f, t);
}

void CInteractionMarkerManager::OnMouseWheel(int direction)
{
	if (!m_enabled || !m_markers_cfg.features.wheel_cycle_pickups || !g_actor || !g_actor->g_Alive())
		return;

	if (ShouldHideUI() || ShouldSuspendMarkLoop())
		return;

	xr_vector<std::pair<float, u16>> items;
	items.reserve(m_markers.size());

	for (const auto& [id, marker] : m_markers)
	{
		if (marker.cls != EWSUIClass::Item || !marker.visible || !marker.reachable)
			continue;
		items.emplace_back(marker.distance, id);
	}

	if (items.size() < 2)
		return;

	std::sort(items.begin(), items.end(), [](const auto& a, const auto& b)
	{
		if (a.first != b.first)
			return a.first < b.first;
		return a.second < b.second;
	});

	u16 current = m_wheel_focus_id;
	if (current == 0xffff)
		current = m_engine_focus_id;

	int index = -1;
	for (u32 i = 0; i < items.size(); ++i)
	{
		if (items[i].second == current)
		{
			index = (int)i;
			break;
		}
	}

	if (index < 0)
		index = (direction > 0) ? 0 : (int)items.size() - 1;
	else if (direction > 0)
		index = (index + 1) % (int)items.size();
	else
		index = (index - 1 + (int)items.size()) % (int)items.size();

	m_wheel_focus_id = items[index].second;
}

bool CInteractionMarkerManager::IsScanEnvironmentMoving(CActor* actor) const
{
	if (!actor)
		return true;

	if (!m_scan_motion_valid)
		return true;

	const auto& scan = m_markers_cfg.scan;
	const float actor_eps = scan.actor_pos_eps > 0.f ? scan.actor_pos_eps : WSUI_LOS_CAM_POS_EPS;
	const float cam_eps = scan.camera_dir_eps > 0.f ? scan.camera_dir_eps : WSUI_LOS_CAM_DIR_EPS;

	if (m_scan_actor_pos.distance_to_sqr(actor->Position()) > actor_eps * actor_eps)
		return true;

	if (1.f - m_scan_cam_dir.dotproduct(Device.vCameraDirection) > cam_eps)
		return true;

	return false;
}

u32 CInteractionMarkerManager::GetEffectiveScanIntervalMs(CActor* actor) const
{
	const auto& scan = m_markers_cfg.scan;
	if (!scan.adaptive || !actor || IsScanEnvironmentMoving(actor))
		return m_scan_interval_ms;

	if (scan.idle_interval_ms > m_scan_interval_ms)
		return scan.idle_interval_ms;

	return m_scan_interval_ms;
}

void CInteractionMarkerManager::Update()
{
	if (!m_enabled || !g_actor || !g_actor->g_Alive())
		return;

	if (ShouldHideUI())
	{
		m_markers.clear();
		m_focus_id = 0xffff;
		m_engine_focus_id = 0xffff;
		m_wheel_focus_id = 0xffff;
		m_prev_focus_id = 0xffff;
		m_popin_focus_id = 0xffff;
		m_prompt_focus_id = 0xffff;
		m_prompt_alpha = 0.f;
		m_prompt_target_visible = false;
		m_scan_motion_valid = false;
		return;
	}

	const u32 now = Device.dwTimeGlobal;
	const bool environment_moving = IsScanEnvironmentMoving(g_actor);
	const u32 scan_interval = GetEffectiveScanIntervalMs(g_actor);
	if (!ShouldSuspendScan() && (environment_moving || now - m_last_scan_time >= scan_interval))
	{
		Scan(g_actor);
		m_last_scan_time = now;
		m_scan_actor_pos = g_actor->Position();
		m_scan_cam_dir = Device.vCameraDirection;
		m_scan_motion_valid = true;
	}

	if (!ShouldSuspendMarkLoop())
	{
		UpdateMarkerPositions(g_actor);
		UpdateAnimations(g_actor);
	}
}

u32 CInteractionMarkerManager::ColorWithAlpha(u32 color, float alpha) const
{
	const u32 a = u32(clampr(alpha, 0.f, 1.f) * 255.f);
	return (color & 0x00ffffff) | (a << 24);
}

bool CInteractionMarkerManager::WantsPromptVisible(CActor* actor) const
{
	if (!actor || m_focus_id == 0xffff)
		return false;

	auto it = m_markers.find(m_focus_id);
	if (it == m_markers.end())
		return false;

	const SInteractionMarker& marker = it->second;
	if (!marker.visible || !marker.reachable || marker.distance > m_prompt_distance)
		return false;

	if (m_suppress_tutorial_ui && (marker.cls == EWSUIClass::Zone || marker.cls == EWSUIClass::Campfire))
	{
		if (LPCSTR tutorial_name = GetActiveTutorialName())
		{
			if (HasTutorialPromptMapping(tutorial_name))
				return false;
		}
	}

	string512 action_text;
	return BuildPromptText(actor, marker, action_text);
}

void CInteractionMarkerManager::UpdatePromptFade(CActor* actor)
{
	const bool want = WantsPromptVisible(actor);

	if (want != m_prompt_target_visible)
	{
		m_prompt_target_visible = want;
		m_prompt_fade_start_time = Device.dwTimeGlobal;
		m_prompt_fade_start_alpha = m_prompt_alpha;
		if (want)
			m_prompt_focus_id = m_focus_id;
	}

	if (want)
		m_prompt_focus_id = m_focus_id;

	const u32 fade_dur = want ? m_prompt_cfg.fade_animation.fade_in_ms : m_prompt_cfg.fade_animation.fade_out_ms;
	const float target = want ? 1.f : 0.f;

	if (!fade_dur)
	{
		m_prompt_alpha = target;
		if (!want)
			m_prompt_focus_id = 0xffff;
		return;
	}

	float t = float(Device.dwTimeGlobal - m_prompt_fade_start_time) / float(fade_dur);
	t = clampr(t, 0.f, 1.f);
	m_prompt_alpha = m_prompt_fade_start_alpha + (target - m_prompt_fade_start_alpha) * t;

	if (!want && m_prompt_alpha <= 0.01f)
	{
		m_prompt_alpha = 0.f;
		m_prompt_focus_id = 0xffff;
	}
}

float CInteractionMarkerManager::GetFocusPopinScale() const
{
	if (!m_markers_cfg.popin_duration_ms || m_focus_id == 0xffff || m_popin_focus_id != m_focus_id)
		return 1.f;

	const float elapsed = float(Device.dwTimeGlobal - m_popin_start_time);
	const float t = elapsed / float(m_markers_cfg.popin_duration_ms);
	if (t >= 1.f)
		return 1.f;

	const float min_scale = 0.55f;
	return min_scale + (1.f - min_scale) * EaseOutElastic(t);
}

void CInteractionMarkerManager::LoadWsuiXml()
{
	m_markers_cfg = {};
	m_prompt_cfg = {};

	if (!m_ui_xml.size())
		return;

	CUIXml xml;
	if (!xml.Load(CONFIG_PATH, UI_PATH, m_ui_xml.c_str()))
		return;

	LoadWsuiMarkers(xml);
	LoadWsuiPrompt(xml);
}

void CInteractionMarkerManager::LoadWsuiMarkers(CUIXml& xml)
{
	LoadMarkersScan(xml);
	LoadMarkersFeatures(xml);
	LoadMarkersDistanceFade(xml);
	LoadMarkersPriority(xml);
	LoadMarkersPopinAnimation(xml);
}

void CInteractionMarkerManager::LoadMarkersScan(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:scan";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& scan = m_markers_cfg.scan;
	scan.adaptive = xml.ReadAttribInt(path, 0, "adaptive", 0) != 0;
	scan.idle_interval_ms = xml.ReadAttribInt(path, 0, "idle_interval_ms", 0);
	scan.actor_pos_eps = xml.ReadAttribFlt(path, 0, "actor_pos_eps", 0.f);
	scan.camera_dir_eps = xml.ReadAttribFlt(path, 0, "camera_dir_eps", 0.f);
}

void CInteractionMarkerManager::LoadMarkersFeatures(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:features";
	if (!xml.NavigateToNode(path, 0))
		return;

	m_markers_cfg.features.hide_dots = xml.ReadAttribInt(path, 0, "hide_dots", 0) != 0;
	m_markers_cfg.features.wheel_cycle_pickups = xml.ReadAttribInt(path, 0, "wheel_cycle_pickups", 0) != 0;
	m_markers_cfg.features.special_icons_always = xml.ReadAttribInt(path, 0, "special_icons_always", 0) != 0;
}

void CInteractionMarkerManager::LoadMarkersDistanceFade(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:distance_fade";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& cfg = m_markers_cfg.distance_fade;
	cfg.enable_scale = xml.ReadAttribInt(path, 0, "enable_scale", 0) != 0;
	cfg.enable_alpha = xml.ReadAttribInt(path, 0, "enable_alpha", 0) != 0;
	cfg.min_scale = xml.ReadAttribFlt(path, 0, "min_scale", 0.f);
	cfg.max_scale = xml.ReadAttribFlt(path, 0, "max_scale", 0.f);
	cfg.min_alpha = xml.ReadAttribFlt(path, 0, "min_alpha", 0.f);
}

void CInteractionMarkerManager::LoadMarkersPriority(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:marker_priority";
	if (!xml.NavigateToNode(path, 0))
		return;

	m_markers_cfg.marker_priority.task = xml.ReadAttribFlt(path, 0, "task", 0.f);
	m_markers_cfg.marker_priority.npc = xml.ReadAttribFlt(path, 0, "npc", 0.f);
}

void CInteractionMarkerManager::LoadMarkersPopinAnimation(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:popin_animation";
	if (!xml.NavigateToNode(path, 0))
		return;

	m_markers_cfg.popin_duration_ms = xml.ReadAttribInt(path, 0, "duration_ms", 0);
}

void CInteractionMarkerManager::LoadWsuiPrompt(CUIXml& xml)
{
	LoadPromptLayout(xml);
	LoadPromptFeatures(xml);
	LoadPromptFadeAnimation(xml);
	LoadPromptMainPanel(xml);
	LoadPromptItemStackCount(xml);
	LoadPromptItemCondition(xml);
	LoadPromptItemCard(xml);
	LoadPromptTutorialScreen(xml);
}

void CInteractionMarkerManager::LoadPromptLayout(CUIXml& xml)
{
	const LPCSTR root = "wsui_prompt";
	if (!xml.NavigateToNode(root, 0))
		return;

	m_prompt_cfg.ui_scale = xml.ReadAttribFlt(root, 0, "ui_scale", 0.f);
	m_prompt_cfg.aspect_correction = xml.ReadAttribFlt(root, 0, "aspect_correction", 0.f);
	m_prompt_cfg.font_scale_w = xml.ReadAttribFlt(root, 0, "font_scale_w", 0.f);
	m_prompt_cfg.font_scale_h = xml.ReadAttribFlt(root, 0, "font_scale_h", 0.f);
	m_prompt_cfg.text_pad = xml.ReadAttribFlt(root, 0, "text_pad", 0.f);

	if (xml.NavigateToNode("wsui_prompt:anchor", 0))
	{
		m_prompt_cfg.anchor_x = xml.ReadAttribFlt("wsui_prompt:anchor", 0, "x", 0.f);
		m_prompt_cfg.anchor_y = xml.ReadAttribFlt("wsui_prompt:anchor", 0, "y", 0.f);
	}
}

void CInteractionMarkerManager::LoadPromptFeatures(CUIXml& xml)
{
	const LPCSTR path = "wsui_prompt:features";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& f = m_prompt_cfg.features;
	f.item_stack_count = xml.ReadAttribInt(path, 0, "item_stack_count", 0) != 0;
	f.item_condition = xml.ReadAttribInt(path, 0, "item_condition", 0) != 0;
	f.item_card = xml.ReadAttribInt(path, 0, "item_card", 0) != 0;
	f.keybind = xml.ReadAttribInt(path, 0, "keybind", 0) != 0;
	f.fixed_screen = xml.ReadAttribInt(path, 0, "fixed_screen", 0) != 0;
	f.fixed_x = xml.ReadAttribFlt(path, 0, "fixed_x", 0.f);
	f.fixed_y = xml.ReadAttribFlt(path, 0, "fixed_y", 0.f);
}

void CInteractionMarkerManager::LoadPromptFadeAnimation(CUIXml& xml)
{
	const LPCSTR path = "wsui_prompt:fade_animation";
	if (!xml.NavigateToNode(path, 0))
		return;

	m_prompt_cfg.fade_animation.fade_in_ms = xml.ReadAttribInt(path, 0, "fade_in_ms", 0);
	m_prompt_cfg.fade_animation.fade_out_ms = xml.ReadAttribInt(path, 0, "fade_out_ms", 0);
}

void CInteractionMarkerManager::LoadTextLabel(CUIXml& xml, LPCSTR path, SWSUITextLabel& out)
{
	if (!xml.NavigateToNode(path, 0))
		return;

	out.x = xml.ReadAttribFlt(path, 0, "x", 0.f);
	out.y = xml.ReadAttribFlt(path, 0, "y", 0.f);
	if (LPCSTR font_name = xml.ReadAttrib(path, 0, "font", nullptr))
		out.font_name = font_name;

	u32 color = 0;
	CGameFont* loaded_font = nullptr;
	if (CUIXmlInit::InitFont(xml, path, 0, color, loaded_font) && loaded_font)
	{
		out.font = loaded_font;
		out.color = color;
	}
}

void CInteractionMarkerManager::LoadBackground(CUIXml& xml, LPCSTR path, SWSUIBackground& out, bool read_enable)
{
	if (!xml.NavigateToNode(path, 0))
		return;

	if (read_enable)
		out.enabled = xml.ReadAttribInt(path, 0, "enable", 0) != 0;
	else
		out.enabled = true;

	if (LPCSTR tex = xml.ReadAttrib(path, 0, "texture", nullptr))
		out.texture = tex;
	out.height = xml.ReadAttribFlt(path, 0, "height", 0.f);
	out.width = xml.ReadAttribFlt(path, 0, "width", 0.f);
	out.pad = xml.ReadAttribFlt(path, 0, "pad", 0.f);

	const int r = xml.ReadAttribInt(path, 0, "r", 255);
	const int g = xml.ReadAttribInt(path, 0, "g", 255);
	const int b = xml.ReadAttribInt(path, 0, "b", 255);
	const int a = xml.ReadAttribInt(path, 0, "a", 255);
	if (r != 255 || g != 255 || b != 255 || a != 255)
		out.color = color_rgba(r, g, b, a);
}

void CInteractionMarkerManager::LoadPromptMainPanel(CUIXml& xml)
{
	LoadBackground(xml, "wsui_prompt:main_panel:background", m_prompt_cfg.main_panel.background);

	if (xml.NavigateToNode("wsui_prompt:main_panel:keybind", 0))
	{
		const LPCSTR path = "wsui_prompt:main_panel:keybind";
		auto& kb = m_prompt_cfg.main_panel.keybind;
		kb.width = xml.ReadAttribFlt(path, 0, "width", 0.f);
		kb.height = xml.ReadAttribFlt(path, 0, "height", 0.f);
		kb.icon_width = xml.ReadAttribFlt(path, 0, "icon_width", 0.f);
		kb.icon_height = xml.ReadAttribFlt(path, 0, "icon_height", 0.f);
		if (LPCSTR tex = xml.ReadAttrib(path, 0, "texture", nullptr))
			kb.texture = tex;
		if (LPCSTR tex = xml.ReadAttrib(path, 0, "pressed_texture", nullptr))
			kb.pressed_texture = tex;
		LoadTextLabel(xml, "wsui_prompt:main_panel:keybind:label", kb.label);
	}

	auto& at = m_prompt_cfg.main_panel.action_text;
	LoadTextLabel(xml, "wsui_prompt:main_panel:action_text:verb", at.verb);
	LoadTextLabel(xml, "wsui_prompt:main_panel:action_text:object_name", at.object_name);
	LoadTextLabel(xml, "wsui_prompt:main_panel:action_text:full_line", at.full_line);
}

void CInteractionMarkerManager::LoadPromptItemStackCount(CUIXml& xml)
{
	const LPCSTR path = "wsui_prompt:item_stack_count";
	if (!xml.NavigateToNode(path, 0))
		return;

	m_prompt_cfg.item_stack_count.group_distance = xml.ReadAttribFlt(path, 0, "group_distance", 0.f);
}

void CInteractionMarkerManager::LoadPromptItemCondition(CUIXml& xml)
{
	const LPCSTR path = "wsui_prompt:item_condition";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& cond = m_prompt_cfg.item_condition;
	cond.x = xml.ReadAttribFlt(path, 0, "x", 0.f);
	cond.y = xml.ReadAttribFlt(path, 0, "y", 0.f);
	cond.line_spacing = xml.ReadAttribFlt(path, 0, "line_spacing", 0.f);
	LoadTextLabel(xml, "wsui_prompt:item_condition:label", cond.label);
	LoadBackground(xml, "wsui_prompt:item_condition:background", cond.background, true);

	const LPCSTR grad_path = "wsui_prompt:item_condition:color_gradient";
	if (xml.NavigateToNode(grad_path, 0))
	{
		const int min_r = xml.ReadAttribInt(grad_path, 0, "min_r", 0);
		const int min_g = xml.ReadAttribInt(grad_path, 0, "min_g", 0);
		const int min_b = xml.ReadAttribInt(grad_path, 0, "min_b", 0);
		const int min_a = xml.ReadAttribInt(grad_path, 0, "min_a", 0);
		const int max_r = xml.ReadAttribInt(grad_path, 0, "max_r", 0);
		const int max_g = xml.ReadAttribInt(grad_path, 0, "max_g", 0);
		const int max_b = xml.ReadAttribInt(grad_path, 0, "max_b", 0);
		const int max_a = xml.ReadAttribInt(grad_path, 0, "max_a", 0);
		cond.color_min = color_rgba(min_r, min_g, min_b, min_a);
		cond.color_max = color_rgba(max_r, max_g, max_b, max_a);
	}
}

void CInteractionMarkerManager::LoadPromptItemCard(CUIXml& xml)
{
	const LPCSTR path = "wsui_prompt:item_card";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& card = m_prompt_cfg.item_card;
	card.x = xml.ReadAttribFlt(path, 0, "x", 0.f);
	card.y = xml.ReadAttribFlt(path, 0, "y", 0.f);
	LoadBackground(xml, "wsui_prompt:item_card:background", card.background);
	LoadItemCardMetric(xml, "wsui_prompt:item_card:weight_metric", card.weight);
	LoadItemCardMetric(xml, "wsui_prompt:item_card:value_metric", card.value);
}

void CInteractionMarkerManager::LoadPromptTutorialScreen(CUIXml& xml)
{
	const LPCSTR path = "wsui_prompt:tutorial_screen";
	if (!xml.NavigateToNode(path, 0))
		return;

	m_prompt_cfg.tutorial_x = xml.ReadAttribFlt(path, 0, "x", 0.f);
	m_prompt_cfg.tutorial_y = xml.ReadAttribFlt(path, 0, "y", 0.f);
}

void CInteractionMarkerManager::LoadItemCardMetric(CUIXml& xml, LPCSTR path, SWSUIItemCardMetric& out) const
{
	if (!xml.NavigateToNode(path, 0))
		return;

	out.x = xml.ReadAttribFlt(path, 0, "x", out.x);
	out.y = xml.ReadAttribFlt(path, 0, "y", out.y);
	out.icon_x = xml.ReadAttribFlt(path, 0, "icon_x", out.icon_x);
	out.icon_y = xml.ReadAttribFlt(path, 0, "icon_y", out.icon_y);
	out.icon_w = xml.ReadAttribFlt(path, 0, "icon_w", out.icon_w);
	out.icon_h = xml.ReadAttribFlt(path, 0, "icon_h", out.icon_h);
	out.text_x = xml.ReadAttribFlt(path, 0, "text_x", out.text_x);
	out.text_y = xml.ReadAttribFlt(path, 0, "text_y", out.text_y);
	if (LPCSTR tex = xml.ReadAttrib(path, 0, "icon", nullptr))
		out.icon = tex;

	if (LPCSTR font_name = xml.ReadAttrib(path, 0, "font", nullptr))
		out.font_name = font_name;

	u32 color = out.color;
	CGameFont* loaded_font = nullptr;
	if (CUIXmlInit::InitFont(xml, path, 0, color, loaded_font) && loaded_font)
	{
		out.font = loaded_font;
		out.color = color;
	}
	else if (g_FontManager && g_FontManager->pFontSystem)
		out.font = g_FontManager->pFontSystem;
}

void CInteractionMarkerManager::LoadFocusSound()
{
	HUD_SOUND_ITEM::DestroySound(m_focus_snd);
	m_focus_sound_loaded = false;

	if (!m_enable_focus_sound)
		return;

	if (!pSettings->section_exist("wsui_sounds"))
		return;

	if (!pSettings->line_exist("wsui_sounds", "snd_focus"))
		return;

	HUD_SOUND_ITEM::LoadSound("wsui_sounds", "snd_focus", m_focus_snd, SOUND_TYPE_IDLE);
	m_focus_sound_loaded = !m_focus_snd.sounds.empty();
}

void CInteractionMarkerManager::PlayFocusSound() const
{
	if (!m_focus_sound_loaded)
		return;

	HUD_SOUND_ITEM::PlaySound(const_cast<HUD_SOUND_ITEM&>(m_focus_snd), Fvector().set(0.f, 0.f, 0.f), nullptr, true);
}

void CInteractionMarkerManager::UpdateFocusSound(CActor* actor)
{
	if (!m_enable_focus_sound || !m_focus_sound_loaded || !actor || ShouldHideUI())
		return;

	if (m_focus_id == 0xffff || m_focus_id == m_prev_focus_id)
		return;

	auto it = m_markers.find(m_focus_id);
	if (it == m_markers.end() || !it->second.visible || !it->second.reachable)
		return;

	if (it->second.distance > m_prompt_distance)
		return;

	string512 action_text;
	if (!BuildPromptText(actor, it->second, action_text))
		return;

	PlayFocusSound();
}

void CInteractionMarkerManager::UpdateAnimations(CActor* actor)
{
	if (m_focus_id != m_prev_focus_id)
		UpdateFocusSound(actor);

	if (m_focus_id != m_prev_focus_id)
	{
		if (m_focus_id != 0xffff && m_markers_cfg.popin_duration_ms > 0)
		{
			m_popin_focus_id = m_focus_id;
			m_popin_start_time = Device.dwTimeGlobal;
		}
		m_prev_focus_id = m_focus_id;
	}

	UpdatePromptFade(actor);
	UpdateTutorialPromptFade();
}

bool CInteractionMarkerManager::DrawTextureMarker(const shared_str& texture_id, float cx, float cy, float w, float h, u32 color, bool keep_square, float angle) const
{
	if (!texture_id.size())
		return false;

	if (keep_square)
		h = SquareHeight(w);

	if (!CUITextureMaster::ItemExist(texture_id))
		return false;

	CUIStaticItem item;
	if (!CUITextureMaster::InitTexture(texture_id, &item, "hud\\cursor", false))
		return false;

	item.SetTextureColor(color);
	item.SetSize(Fvector2().set(w, h));
	item.SetPos(cx - w * 0.5f, cy - h * 0.5f);
	if (angle != 0.f)
		item.Render(angle);
	else
		item.Render();
	return true;
}

u32 CInteractionMarkerManager::GetItemConditionColor(float condition) const
{
	clamp(condition, 0.f, 1.f);

	Fcolor c1, c2, result;
	c1.set(m_prompt_cfg.item_condition.color_min);
	c2.set(m_prompt_cfg.item_condition.color_max);
	result.lerp(c1, c2, condition);
	return result.get();
}

u32 CInteractionMarkerManager::CountGroupedItemMarkers(const SInteractionMarker& focus_marker) const
{
	if (focus_marker.cls != EWSUIClass::Item)
		return 1;

	CObject* focus_obj = Level().Objects.net_Find(focus_marker.object_id);
	CInventoryItem* focus_item = focus_obj ? focus_obj->cast_inventory_item() : nullptr;
	if (!focus_item)
		return 1;

	const shared_str section = focus_item->m_section_id.size() ? focus_item->m_section_id : focus_obj->cNameSect();
	const Fvector2& focus_pos = focus_marker.screen_pos;
	u32 count = 0;

	for (const auto& [id, marker] : m_markers)
	{
		if (marker.cls != EWSUIClass::Item || !marker.visible || !marker.reachable)
			continue;

		if (marker.screen_pos.x < 0.f || focus_pos.x < 0.f)
			continue;

		if (marker.screen_pos.distance_to(focus_pos) > m_prompt_cfg.item_stack_count.group_distance)
			continue;

		CObject* obj = Level().Objects.net_Find(id);
		CInventoryItem* item = obj ? obj->cast_inventory_item() : nullptr;
		if (!item)
			continue;

		const shared_str item_section = item->m_section_id.size() ? item->m_section_id : obj->cNameSect();
		if (item_section != section)
			continue;

		++count;
	}

	return count > 0 ? count : 1;
}

LPCSTR CInteractionMarkerManager::ResolveKeyBindIcon(int dik, float& out_w, float& out_h) const
{
	auto it = m_dik_icons.find(dik);
	if (it == m_dik_icons.end() || !it->second.size())
		return nullptr;

	out_w = m_prompt_cfg.main_panel.keybind.icon_width;
	out_h = m_prompt_cfg.main_panel.keybind.icon_height;
	return it->second.c_str();
}

LPCSTR CInteractionMarkerManager::GetKeyName() const
{
	const int dik = get_action_dik(kUSE);
	return dik > 0 ? dik_to_keyname(dik) : nullptr;
}

bool CInteractionMarkerManager::BuildPromptParts(CActor* actor, const SInteractionMarker& marker, SWSUIPromptParts& out) const
{
	out.split = false;
	out.verb[0] = 0;
	out.name[0] = 0;
	out.full[0] = 0;
	out.show_condition = false;
	out.item_condition = 0.f;

	if (!actor)
		return false;

	CObject* object = Level().Objects.net_Find(marker.object_id);
	CGameObject* game_object = object ? object->cast_game_object() : nullptr;
	if (!game_object)
		return false;

	switch (marker.cls)
	{
	case EWSUIClass::Item:
	{
		if (CInventoryItem* item = game_object->cast_inventory_item())
		{
			xr_strcpy(out.verb, WsuiString("ui_st_wsui_pickup"));
			if (LPCSTR name = ItemDisplayName(item))
			{
				if (m_prompt_cfg.features.item_stack_count)
				{
					const u32 count = CountGroupedItemMarkers(marker);
					if (count > 1)
						xr_sprintf(out.name, sizeof(out.name), "%s x%u", name, count);
					else
						xr_strcpy(out.name, name);
				}
				else
				{
					xr_strcpy(out.name, name);
				}
			}

			if (m_prompt_cfg.features.item_condition && item->IsUsingCondition())
			{
				out.show_condition = true;
				out.item_condition = item->GetCondition();
			}

			out.split = true;
			return out.verb[0] != 0;
		}
		break;
	}
	case EWSUIClass::Npc:
	{
		if (CAI_Stalker* stalker = game_object->cast_stalker())
		{
			xr_strcpy(out.verb, WsuiString("ui_st_wsui_talk"));
			if (LPCSTR name = stalker->Name())
				xr_strcpy(out.name, name);
			out.split = true;
			return out.verb[0] != 0;
		}
		break;
	}
	case EWSUIClass::Body:
	{
		xr_strcpy(out.verb, WsuiString("ui_st_wsui_search"));
		if (LPCSTR name = game_object->Name())
			xr_strcpy(out.name, name);
		out.split = true;
		return out.verb[0] != 0;
	}
	case EWSUIClass::Stash:
	{
		xr_strcpy(out.verb, WsuiString("ui_st_wsui_open"));
		out.split = true;
		return out.verb[0] != 0;
	}
	case EWSUIClass::Usable:
	{
		if (CUsableScriptObject* usable = game_object->cast_usable_script_object())
		{
			if (usable->tip_text() && usable->tip_text()[0])
			{
				xr_strcpy(out.full, g_pStringTable->translate(usable->tip_text()).c_str());
				return out.full[0] != 0;
			}
		}
		break;
	}
	case EWSUIClass::Door:
	{
		if (CUsableScriptObject* usable = game_object->cast_usable_script_object())
		{
			if (usable->tip_text() && usable->tip_text()[0])
			{
				xr_strcpy(out.full, g_pStringTable->translate(usable->tip_text()).c_str());
				return out.full[0] != 0;
			}
		}
		break;
	}
	case EWSUIClass::Campfire:
	{
		if (CZoneCampfire* campfire = game_object->cast_zone_campfire())
		{
			LPCSTR key = campfire->is_on() ? "st_extinguish_fire" : "st_ignite_fire";
			xr_strcpy(out.full, g_pStringTable->translate(key).c_str());
			return out.full[0] != 0;
		}
		break;
	}
	case EWSUIClass::Zone:
	{
		if (LPCSTR prompt = ResolveZonePrompt(game_object))
		{
			xr_strcpy(out.full, g_pStringTable->translate(prompt).c_str());
			return out.full[0] != 0;
		}
		break;
	}
	default:
		break;
	}

	if (LPCSTR action = actor->GetDefaultActionForObject())
	{
		if (action[0])
		{
			xr_strcpy(out.full, action);
			return true;
		}
	}

	return false;
}

bool CInteractionMarkerManager::BuildPromptText(CActor* actor, const SInteractionMarker& marker, string512& out) const
{
	out[0] = 0;

	SWSUIPromptParts parts;
	if (!BuildPromptParts(actor, marker, parts))
		return false;

	if (parts.split)
	{
		if (parts.name[0])
			xr_sprintf(out, sizeof(out), "%s %s", parts.verb, parts.name);
		else
			xr_sprintf(out, sizeof(out), "%s", parts.verb);
	}
	else
	{
		xr_sprintf(out, sizeof(out), "%s", parts.full);
	}

	return out[0] != 0;
}

void CInteractionMarkerManager::SanitizeActionText(string512& text) const
{
	xr_string sanitized(text);

	for (;;)
	{
		const size_t pos = sanitized.find("$$ACTION_USE$$");
		if (pos == xr_string::npos)
			break;
		sanitized.erase(pos, 14);
	}

	for (;;)
	{
		const size_t pos = sanitized.find(" ()");
		if (pos == xr_string::npos)
			break;
		sanitized.erase(pos, 3);
	}

	for (;;)
	{
		const size_t pos = sanitized.find("( )");
		if (pos == xr_string::npos)
			break;
		sanitized.erase(pos, 3);
	}

	while (!sanitized.empty() && sanitized.back() == ' ')
		sanitized.pop_back();

	xr_strcpy(text, sanitized.c_str());
}

float CInteractionMarkerManager::PromptTextWidth(CGameFont* font, LPCSTR text, float kx) const
{
	if (!font || !text || !text[0])
		return 0.f;

	return (font->WidthOf(text) * m_prompt_cfg.font_scale_w) / kx;
}

float CInteractionMarkerManager::PromptTextHeight(CGameFont* font, float ky) const
{
	if (!font)
		return 0.f;

	return (font->CurrentHeight_() * m_prompt_cfg.font_scale_h) / ky;
}

namespace
{
bool IsUseKeyPressed()
{
	if (!pInput)
		return false;

	const int key1 = get_action_dik(kUSE, 0);
	const int key2 = get_action_dik(kUSE, 1);
	return (key1 > 0 && pInput->iGetAsyncKeyState(key1)) || (key2 > 0 && pInput->iGetAsyncKeyState(key2));
}
}

void CInteractionMarkerManager::RenderPromptBubble(float cx, float cy, const SWSUIPromptParts& parts_in, LPCSTR key_name, float alpha, bool center_anchor, const SInteractionMarker* marker) const
{
	SWSUIPromptParts parts = parts_in;
	if (parts.split)
	{
		SanitizeActionText(parts.verb);
		if (!parts.verb[0] && !parts.name[0])
			return;
	}
	else
	{
		SanitizeActionText(parts.full);
		if (!parts.full[0])
			return;
	}

	const auto& kb = m_prompt_cfg.main_panel.keybind;
	const auto& at = m_prompt_cfg.main_panel.action_text;
	const auto& bg = m_prompt_cfg.main_panel.background;
	const auto& cond_cfg = m_prompt_cfg.item_condition;

	CGameFont* key_font = kb.label.font ? kb.label.font : (g_FontManager ? g_FontManager->pFontSystem : nullptr);
	CGameFont* verb_font = at.verb.font ? at.verb.font : (g_FontManager ? g_FontManager->pFontSystem : nullptr);
	CGameFont* name_font = at.object_name.font ? at.object_name.font : (g_FontManager ? g_FontManager->pFontSystem : nullptr);
	CGameFont* full_font = at.full_line.font ? at.full_line.font : verb_font;
	if (!key_font || !verb_font || !name_font || !full_font)
		return;

	const float scale = UiScale();
	const float kx = Device.TargetWidth / UI_BASE_WIDTH;
	const float ky = Device.TargetHeight / UI_BASE_HEIGHT;

	float text_w = 0.f;
	float text_h = 0.f;
	if (parts.split)
	{
		text_w += PromptTextWidth(verb_font, parts.verb, kx);
		if (parts.name[0])
		{
			text_w += PromptTextWidth(verb_font, " ", kx);
			text_w += PromptTextWidth(name_font, parts.name, kx);
		}
		text_h = std::max(PromptTextHeight(verb_font, ky), PromptTextHeight(name_font, ky));
	}
	else
	{
		text_w = PromptTextWidth(full_font, parts.full, kx);
		text_h = PromptTextHeight(full_font, ky);
	}

	const bool show_keybind = m_prompt_cfg.features.keybind && key_name && key_name[0];
	const int use_dik = get_action_dik(kUSE, 0);
	float bind_icon_w = 0.f;
	float bind_icon_h = 0.f;
	LPCSTR bind_icon = (show_keybind && use_dik > 0) ? ResolveKeyBindIcon(use_dik, bind_icon_w, bind_icon_h) : nullptr;
	const float key_w = show_keybind ? (bind_icon ? bind_icon_w : kb.width) * scale : 0.f;
	const float key_h = show_keybind ? (bind_icon ? bind_icon_h : SquareHeight(kb.width * scale)) : 0.f;
	const float gap = show_keybind ? m_prompt_cfg.text_pad * scale : 0.f;
	const float drop_w = key_w + gap + text_w + m_prompt_cfg.text_pad * 2.f * scale;
	const float content_h = std::max(key_h, text_h);
	const float drop_h = std::max(bg.height * scale, content_h + m_prompt_cfg.text_pad * scale);

	const float drop_cx = cx;
	const float drop_cy = center_anchor ? cy : cy + drop_h * 0.5f;
	const float content_cy = drop_cy;
	const float panel_left = drop_cx - drop_w * 0.5f;
	const float panel_top = drop_cy - drop_h * 0.5f;

	DrawTextureMarker(bg.texture, drop_cx, drop_cy, drop_w, drop_h, ColorWithAlpha(0xCCFFFFFF, alpha), false);

	float cursor_x = drop_cx - drop_w * 0.5f + m_prompt_cfg.text_pad * scale;

	if (show_keybind)
	{
		const float key_cx = cursor_x + key_w * 0.5f;
		if (bind_icon)
		{
			DrawTextureMarker(bind_icon, key_cx, content_cy, key_w, key_h, ColorWithAlpha(0xFFFFFFFF, alpha), true);
		}
		else
		{
			const shared_str& key_tex = (kb.pressed_texture.size() && IsUseKeyPressed())
				? kb.pressed_texture
				: kb.texture;
			DrawTextureMarker(key_tex, key_cx, content_cy, key_w, key_h, ColorWithAlpha(0xFFFFFFFF, alpha), true);

			const float font_h = key_font->CurrentHeight_() * m_prompt_cfg.font_scale_h;
			key_font->SetAligment(CGameFont::alCenter);
			key_font->SetColor(ColorWithAlpha(kb.label.color, alpha));
			key_font->Out((key_cx + kb.label.x * scale) * kx,
				content_cy * ky - font_h * 0.5f + kb.label.y * scale, "%s", key_name);
			key_font->OnRender();
		}
		cursor_x += key_w + gap;
	}

	const float action_x = parts.split ? at.verb.x : at.full_line.x;
	const float action_y = parts.split ? at.verb.y : at.full_line.y;
	const float text_y = (content_cy - text_h * 0.5f + action_y * scale) * ky;
	const float text_x = (cursor_x + action_x * scale) * kx;

	if (parts.split)
	{
		verb_font->SetAligment(CGameFont::alLeft);
		verb_font->SetColor(ColorWithAlpha(at.verb.color, alpha));
		verb_font->Out(text_x, text_y, "%s", parts.verb);

		float segment_x = text_x + verb_font->WidthOf(parts.verb) * m_prompt_cfg.font_scale_w;
		if (parts.name[0])
		{
			segment_x += verb_font->WidthOf(" ") * m_prompt_cfg.font_scale_w;
			name_font->SetAligment(CGameFont::alLeft);
			name_font->SetColor(ColorWithAlpha(at.object_name.color, alpha));
			name_font->Out(segment_x, text_y, "%s", parts.name);
			name_font->OnRender();
		}
		verb_font->OnRender();
	}
	else
	{
		full_font->SetAligment(CGameFont::alLeft);
		full_font->SetColor(ColorWithAlpha(at.full_line.color, alpha));
		full_font->Out(text_x, text_y, "%s", parts.full);
		full_font->OnRender();
	}

	if (parts.show_condition)
	{
		CGameFont* cond_font = cond_cfg.label.font ? cond_cfg.label.font : name_font;
		if (cond_font)
		{
			const int percent = (int)(parts.item_condition * 100.f);
			string256 condition_str;
			xr_sprintf(condition_str, sizeof(condition_str), "%d%%", percent);

			shared_str condition_label = g_pStringTable->translate("ui_st_loot_info_condition");
			string256 cond_text;
			if (condition_label.size() > 0 && xr_strcmp(condition_label.c_str(), "ui_st_loot_info_condition") != 0)
				xr_sprintf(cond_text, sizeof(cond_text), "%s: %s", condition_label.c_str(), condition_str);
			else
			{
				condition_label = g_pStringTable->translate("ui_st_wsui_condition");
				if (condition_label.size() > 0 && xr_strcmp(condition_label.c_str(), "ui_st_wsui_condition") != 0)
					xr_sprintf(cond_text, sizeof(cond_text), "%s: %s", condition_label.c_str(), condition_str);
				else
					xr_sprintf(cond_text, sizeof(cond_text), "%s", condition_str);
			}

			const float cond_h = PromptTextHeight(cond_font, ky);
			const float cond_ui_x = panel_left + cond_cfg.x * scale;
			const float cond_ui_y = panel_top + drop_h + m_prompt_cfg.text_pad * scale + cond_cfg.y * scale;
			const float cond_x = cond_ui_x * kx;
			const float cond_y = cond_ui_y * ky;

			if (cond_cfg.background.enabled && cond_cfg.background.texture.size())
			{
				const float text_w_ui = PromptTextWidth(cond_font, cond_text, kx);
				const float text_h_ui = cond_h / ky;
				const float pad = cond_cfg.background.pad * scale;
				const float cond_drop_w = (cond_cfg.background.width > 0.f ? cond_cfg.background.width : text_w_ui + pad * 2.f) * scale;
				const float drop_h_cond = (cond_cfg.background.height > 0.f ? cond_cfg.background.height : text_h_ui + pad * 2.f) * scale;
				const float cond_drop_cx = cond_ui_x + cond_drop_w * 0.5f;
				const float cond_drop_cy = cond_ui_y + drop_h_cond * 0.5f;
				const u32 drop_color = cond_cfg.background.color ? cond_cfg.background.color : 0xCCFFFFFF;
				DrawTextureMarker(cond_cfg.background.texture, cond_drop_cx, cond_drop_cy, cond_drop_w, drop_h_cond, ColorWithAlpha(drop_color, alpha), false);
			}

			cond_font->SetAligment(CGameFont::alLeft);
			cond_font->SetColor(ColorWithAlpha(GetItemConditionColor(parts.item_condition), alpha));
			cond_font->Out(cond_x, cond_y + cond_h * (cond_cfg.line_spacing - 1.f), "%s", cond_text);
			cond_font->OnRender();
		}
	}

	if (m_prompt_cfg.features.item_card && marker && marker->cls == EWSUIClass::Item)
	{
		if (CObject* object = Level().Objects.net_Find(marker->object_id))
		{
			if (CInventoryItem* item = object->cast_inventory_item())
				RenderItemCard(panel_left, panel_top + drop_h, item, alpha);
		}
	}
}

void CInteractionMarkerManager::RenderItemCard(float panel_left, float panel_bottom, CInventoryItem* item, float alpha) const
{
	if (!item)
		return;

	const auto& card = m_prompt_cfg.item_card;
	const bool has_weight = card.weight.icon.size() > 0;
	const bool has_value = card.value.icon.size() > 0 && item->IsDrawCost();
	if (!has_weight && !has_value)
		return;

	const float scale = UiScale();
	const float kx = Device.TargetWidth / UI_BASE_WIDTH;
	const float ky = Device.TargetHeight / UI_BASE_HEIGHT;
	const float origin_x = panel_left + card.x * scale;
	const float origin_y = panel_bottom + card.y * scale;

	auto metric_right = [&](const SWSUIItemCardMetric& metric, LPCSTR text, CGameFont* font) -> float
	{
		float right = metric.x + metric.icon_x + metric.icon_w;
		if (text && text[0] && font)
		{
			const float text_right = metric.text_x + (font->WidthOf(text) * m_prompt_cfg.font_scale_w) / kx;
			right = std::max(right, text_right);
		}
		return right;
	};

	string32 weight_str;
	string32 cost_str;
	if (has_weight)
		xr_sprintf(weight_str, sizeof(weight_str), "%.1f", item->Weight());
	if (has_value)
		xr_sprintf(cost_str, sizeof(cost_str), "%u", item->Cost());

	float content_right = 0.f;
	float content_bottom = 0.f;
	if (has_weight)
	{
		content_right = std::max(content_right, metric_right(card.weight, weight_str, card.weight.font));
		content_bottom = std::max(content_bottom, card.weight.y + std::max(card.weight.icon_y + card.weight.icon_h, card.weight.text_y + 12.f));
	}
	if (has_value)
	{
		content_right = std::max(content_right, metric_right(card.value, cost_str, card.value.font));
		content_bottom = std::max(content_bottom, card.value.y + std::max(card.value.icon_y + card.value.icon_h, card.value.text_y + 12.f));
	}

	const float drop_h = (card.background.height > 0.f ? card.background.height : content_bottom + 2.f) * scale;
	const float drop_w = (card.background.width > 0.f ? card.background.width : content_right + 4.f) * scale;
	const float drop_cx = origin_x + drop_w * 0.5f;
	const float drop_cy = origin_y + drop_h * 0.5f;

	if (card.background.texture.size())
		DrawTextureMarker(card.background.texture, drop_cx, drop_cy, drop_w, drop_h, ColorWithAlpha(0xCCFFFFFF, alpha), false);

	auto draw_metric = [&](const SWSUIItemCardMetric& metric, LPCSTR text)
	{
		if (!metric.icon.size())
			return;

		CGameFont* font = metric.font ? metric.font : (g_FontManager ? g_FontManager->pFontSystem : nullptr);
		const float mx = origin_x + metric.x * scale;
		const float my = origin_y + metric.y * scale;
		const float iw = metric.icon_w * scale;
		const float ih = metric.icon_h * scale;
		const float icon_cx = mx + (metric.icon_x + metric.icon_w * 0.5f) * scale;
		const float icon_cy = my + (metric.icon_y + metric.icon_h * 0.5f) * scale;

		DrawTextureMarker(metric.icon, icon_cx, icon_cy, iw, ih, ColorWithAlpha(0xFFFFFFFF, alpha), true);

		if (text && text[0] && font)
		{
			font->SetAligment(CGameFont::alLeft);
			font->SetColor(ColorWithAlpha(metric.color, alpha));
			font->Out((mx + metric.text_x * scale) * kx, (my + metric.text_y * scale) * ky, "%s", text);
			font->OnRender();
		}
	};

	if (has_weight)
		draw_metric(card.weight, weight_str);
	if (has_value)
		draw_metric(card.value, cost_str);
}

void CInteractionMarkerManager::RenderPrompt(CActor* actor) const
{
	if (!actor || m_prompt_focus_id == 0xffff || m_prompt_alpha <= 0.01f)
		return;

	auto it = m_markers.find(m_prompt_focus_id);
	if (it == m_markers.end() || !it->second.visible || !it->second.reachable)
		return;

	if (it->second.distance > m_prompt_distance)
		return;

	SWSUIPromptParts parts;
	if (!BuildPromptParts(actor, it->second, parts))
		return;

	LPCSTR key_name = GetKeyName();
	const float alpha = m_prompt_alpha;
	const float scale = UiScale();

	if (m_prompt_cfg.features.fixed_screen)
	{
		RenderPromptBubble(m_prompt_cfg.features.fixed_x * scale, m_prompt_cfg.features.fixed_y * scale, parts, key_name, alpha, true, &it->second);
		return;
	}

	const float marker_x = it->second.screen_pos.x + m_prompt_cfg.anchor_x * scale;
	const float marker_y = it->second.screen_pos.y + m_prompt_cfg.anchor_y * scale;
	const float dot_h = SquareHeight(m_dot_size * scale);
	const float anchor_y = marker_y + dot_h * 0.5f + m_prompt_cfg.text_pad * scale;

	RenderPromptBubble(marker_x, anchor_y, parts, key_name, alpha, false, &it->second);
}

void CInteractionMarkerManager::RenderTutorialPrompt() const
{
	if (m_tutorial_prompt_alpha <= 0.01f)
		return;

	LPCSTR tutorial_name = GetActiveTutorialName();
	LPCSTR prompt_key = ResolveTutorialPrompt(tutorial_name);
	if (!prompt_key)
		return;

	LPCSTR action_text = g_pStringTable->translate(prompt_key).c_str();
	if (!action_text || !action_text[0])
		return;

	SWSUIPromptParts parts;
	parts.split = false;
	xr_strcpy(parts.full, action_text);

	const float alpha = m_tutorial_prompt_alpha;
	const float scale = UiScale();
	const float cx = m_prompt_cfg.tutorial_x * scale;
	const float cy = m_prompt_cfg.tutorial_y * scale;

	RenderPromptBubble(cx, cy, parts, GetKeyName(), alpha, true);
}

void CInteractionMarkerManager::OnRender()
{
	if (!m_enabled || !g_bRendering || !g_actor || !g_actor->g_Alive())
		return;

	if (ShouldHideUI() || ShouldSuspendMarkLoop())
		return;

	VERIFY(g_bRendering);

	const u32 color = 0xFFFFFFFF;

	if (!m_markers_cfg.features.hide_dots)
	{
		for (const auto& [id, marker] : m_markers)
		{
			if (!marker.visible || !marker.reachable)
				continue;

			const u32 idx = static_cast<u32>(marker.cls);
			if (idx >= static_cast<u32>(EWSUIClass::Count))
				continue;

			const SWSUIClassDef& def = m_classes[idx];
			const bool focused = id == m_focus_id;
			const float popin_scale = focused ? GetFocusPopinScale() : 1.f;
			const float dist_scale = GetDotDistanceScale(marker.distance, def.show_distance);
			const float dot_w = m_dot_size * UiScale() * popin_scale * dist_scale;
			const float dot_h = SquareHeight(dot_w);
			CGameObject* marker_obj = nullptr;
			if (CObject* object = Level().Objects.net_Find(id))
				marker_obj = object->cast_game_object();
			const shared_str& texture = ResolveActiveTexture(marker_obj, marker.cls, def, focused);
			const float dist_alpha = GetDotDistanceAlpha(marker.distance, def.show_distance);
			DrawTextureMarker(texture, marker.screen_pos.x, marker.screen_pos.y, dot_w, dot_h, ColorWithAlpha(color, dist_alpha), true);
		}
	}

	RenderPrompt(g_actor);
	RenderTutorialPrompt();
}
