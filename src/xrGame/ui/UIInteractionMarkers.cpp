#include "StdAfx.h"
#include "UIInteractionMarkers.h"
#include "UIInteractionMarkers_internal.h"

#include <algorithm>

#include "../Actor.h"
#include "../Level.h"
#include "../GameObject.h"
#include "../inventory_item.h"
#include "../InventoryBox.h"
#include "../InventoryOwner.h"
#include "../entity_alive.h"
#include "../ai/stalker/ai_stalker.h"
#include "../ZoneCampfire.h"
#include "../space_restrictor.h"
#include "../AnomalyZone.h"
#include "../GametaskManager.h"
#include "../GameTask.h"
#include "../ai_space.h"
#include "../alife_simulator.h"
#include "../alife_object_registry.h"
#include "../xrServerEntities/xrServer_Objects_ALife_Monsters.h"
#include "../xrEngine/xr_level_controller.h"
#include "../xrEngine/CameraBase.h"
#include "../xrEngine/xr_collide_form.h"
#include "../ScriptsSubsystems/StoryID/StoryIDManager.h"
#include "UIGameCustom.h"
#include "ui/UITalkWnd.h"
#include "ui/UITradeWnd.h"
#include "ui/UIPdaWnd.h"
#include "ui/UIActorMenu.h"
#include "ui/UIInventoryWnd.h"
#include "ui/UIGameTutorial.h"
#include "../../xrCore/LocatorAPI_defs.h"
#include "../../xrCore/LocatorAPI.h"
#include "../../xrCore/EngineExternal.h"

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
	float EaseOutElastic(float t)
	{
		if (t <= 0.f)
			return 0.f;
		if (t >= 1.f)
			return 1.f;

		const float p = 0.3f;
		return powf(2.f, -10.f * t) * sinf((t - p / 4.f) * (2.f * PI) / p) + 1.f;
	}

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

bool CInteractionMarkerManager::IsRuntimeEnabled() const
{
	if (!m_config_loaded)
		return false;
	if (m_script_enabled_override >= 0)
		return m_script_enabled_override != 0;
	return m_enabled;
}

void CInteractionMarkerManager::ClearMarkerState()
{
	m_markers.clear();
	m_los.rr_order.clear();
	m_los.rr_cursor = 0;
	m_los.rr_order_size = 0;
	m_focus.focus_id = 0xffff;
	m_focus.engine_focus_id = 0xffff;
	m_focus.wheel_focus_id = 0xffff;
	m_focus.prev_focus_id = 0xffff;
	m_focus.popin_focus_id = 0xffff;
	m_focus.prompt_focus_id = 0xffff;
	m_prompt_fade.alpha = 0.f;
	m_tutorial_fade.alpha = 0.f;
	m_suppressed_corpse_ids.clear();
}

void CInteractionMarkerManager::SuppressCorpseMarker(u16 object_id)
{
	if (object_id != 0xffff)
		m_suppressed_corpse_ids.insert(object_id);
}

bool CInteractionMarkerManager::IsCorpseMarkerSuppressed(u16 object_id) const
{
	return object_id != 0xffff && m_suppressed_corpse_ids.find(object_id) != m_suppressed_corpse_ids.end();
}

void CInteractionMarkerManager::SetScriptEnabled(bool enabled)
{
	const s8 override_val = enabled ? s8(1) : s8(0);
	if (m_script_enabled_override == override_val)
		return;

	m_script_enabled_override = override_val;
	if (!enabled)
		ClearMarkerState();
}

bool CInteractionMarkerManager::GetScriptBool(LPCSTR key) const
{
	if (!key || !key[0])
		return false;

	if (!xr_strcmp(key, "hide_dots")) return m_markers_cfg.features.hide_dots;
	if (!xr_strcmp(key, "special_icons_always")) return m_markers_cfg.features.special_icons_always;
	if (!xr_strcmp(key, "wheel_cycle_pickups")) return m_markers_cfg.features.wheel_cycle_pickups;
	if (!xr_strcmp(key, "enable_task_icons")) return m_markers_cfg.features.enable_task_icons;
	if (!xr_strcmp(key, "enable_focus_sound")) return m_markers_cfg.features.enable_focus_sound;
	if (!xr_strcmp(key, "item_stack_count")) return m_prompt_cfg.features.item_stack_count;
	if (!xr_strcmp(key, "item_condition")) return m_prompt_cfg.features.item_condition;
	if (!xr_strcmp(key, "item_card")) return m_prompt_cfg.features.item_card;
	if (!xr_strcmp(key, "keybind")) return m_prompt_cfg.features.keybind;

	return false;
}

void CInteractionMarkerManager::SetScriptBool(LPCSTR key, bool value)
{
	if (!key || !key[0] || !m_config_loaded)
		return;

	if (!xr_strcmp(key, "hide_dots")) m_markers_cfg.features.hide_dots = value;
	else if (!xr_strcmp(key, "special_icons_always")) m_markers_cfg.features.special_icons_always = value;
	else if (!xr_strcmp(key, "wheel_cycle_pickups")) m_markers_cfg.features.wheel_cycle_pickups = value;
	else if (!xr_strcmp(key, "enable_task_icons")) m_markers_cfg.features.enable_task_icons = value;
	else if (!xr_strcmp(key, "enable_focus_sound")) m_markers_cfg.features.enable_focus_sound = value;
	else if (!xr_strcmp(key, "item_stack_count")) m_prompt_cfg.features.item_stack_count = value;
	else if (!xr_strcmp(key, "item_condition")) m_prompt_cfg.features.item_condition = value;
	else if (!xr_strcmp(key, "item_card")) m_prompt_cfg.features.item_card = value;
	else if (!xr_strcmp(key, "keybind")) m_prompt_cfg.features.keybind = value;

	if (!xr_strcmp(key, "item_condition") || !xr_strcmp(key, "item_card"))
		InvalidateSvgCache();
}

bool CInteractionMarkerManager::ShouldSuppressTutorialUiWhenActive() const
{
	if (!m_suppress_tutorial_ui)
		return false;

	LPCSTR tutorial_name = GetActiveTutorialName();
	return tutorial_name && HasTutorialPromptMapping(tutorial_name);
}

bool CInteractionMarkerManager::ShouldSuppressNpcNameAtDistance(float distance) const
{
	for (const auto& [id, def] : m_categories)
	{
		(void)id;
		if (!def.enabled)
			continue;

		if (!WSUIInternal::CategoryHasFeature(def, EWSUICategoryFeature::SuppressVanillaName))
			continue;

		if (distance <= def.show_distance)
			return true;
	}

	return false;
}

void CInteractionMarkerManager::EnsureQuestSchemeIndex()
{
	if (!m_enable_quest_scheme_scan || m_quest_scheme_index_built)
		return;

	BuildQuestSchemeIndex();
	m_quest_scheme_index_built = true;
}

void CInteractionMarkerManager::Load()
{
	m_enabled = false;
	m_markers.clear();
	m_los.rr_order.clear();
	m_los.rr_cursor = 0;
	m_los.rr_order_size = 0;
	m_los.cam_valid = false;
	m_scan.motion_valid = false;
	m_bones_by_section.clear();
	m_pos_adj_by_section.clear();
	m_door_visuals_y.clear();
	m_npc_roles_by_section.clear();
	m_zone_textures_by_name.clear();
	m_zone_prompts_by_name.clear();
	m_tutorial_prompts_by_name.clear();
	m_prompt_split_by_string_id.clear();
	m_icon_registry.clear();
	m_icon_rules.clear();
	m_npc_section_patterns.clear();
	m_usable_section_patterns.clear();
	m_zone_name_patterns.clear();
	m_categories.clear();
	m_category_priority.clear();
	m_class_rules.clear();
	m_quest_scheme_stories.clear();
	m_breakable_box_visuals.clear();
	m_bone_priority.clear();
	m_focus.focus_id = 0xffff;
	HUD_SOUND_ITEM::DestroySound(m_focus_snd);
	m_focus_sound_loaded = false;
	m_markers_cfg = {};
	m_prompt_cfg = {};
	m_focus.wheel_focus_id = 0xffff;
	m_focus.engine_focus_id = 0xffff;
	m_dik_icons.clear();
	m_config_loaded = false;
	m_script_enabled_override = -1;
	m_quest_scheme_index_built = false;

	if (!pSettings->section_exist("wsui"))
		return;

	m_enabled = READ_IF_EXISTS(pSettings, r_bool, "wsui", "enabled", false);
	m_ui_xml = READ_IF_EXISTS(pSettings, r_string, "wsui", "ui_xml", nullptr);
	m_hide_mute_stalkers = READ_IF_EXISTS(pSettings, r_bool, "wsui", "hide_mute_stalkers", true);
	m_suppress_tutorial_ui = READ_IF_EXISTS(pSettings, r_bool, "wsui", "suppress_tutorial_ui", true);
	m_suppress_marker_on_tutorial_object = READ_IF_EXISTS(pSettings, r_bool, "wsui", "suppress_marker_on_tutorial_object", true);
	m_enable_quest_scheme_scan = READ_IF_EXISTS(pSettings, r_bool, "wsui", "enable_quest_scheme_scan", true);
	LoadWsuiXml();
	LoadFocusSound();
	LoadBonePriority();
	LoadLookupSection("bones_by_section", false);
	LoadLookupSection("pos_adj_by_section", true);
	LoadFloatLookupSection("door_visuals", m_door_visuals_y);
	LoadTextureLookupSection("npc_roles_by_section", m_npc_roles_by_section);
	LoadTextureLookupSection("zone_textures_by_name", m_zone_textures_by_name);
	LoadTextureLookupSection("zone_prompts_by_name", m_zone_prompts_by_name);
	LoadTextureLookupSection("tutorial_prompts_by_name", m_tutorial_prompts_by_name);
	LoadPromptSplitSection("prompt_split_by_string_id");
	LoadTextureLookupSection("npc_section_contains", m_npc_section_patterns);
	LoadTextureLookupSection("usable_section_contains", m_usable_section_patterns);
	LoadTextureLookupSection("zone_name_contains", m_zone_name_patterns);
	LoadClassificationRules();

	if (m_enabled && m_categories.empty())
		Msg("! [wsui] marker_categories missing or empty in %s", m_ui_xml.c_str());

	if (m_enabled && m_class_rules.empty())
		Msg("! [wsui] wsui_class_rules missing or empty");

	if (pSettings->section_exist("breakable_box_visuals"))
	{
		const CInifile::Sect& sect = pSettings->r_section("breakable_box_visuals");
		for (const auto& line : sect.Data)
			m_breakable_box_visuals.insert(line.first);
	}

	m_config_loaded = true;
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

	if (!g_tutorial->m_tutorial_name.size())
		return nullptr;

	return g_tutorial->m_tutorial_name.c_str();
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

	if (!m_markers_cfg.features.enable_task_icons || !Level().GameTaskManager())
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

Fvector2 CInteractionMarkerManager::WorldToScreen(const Fvector& world_pos, bool allow_offscreen) const
{
	return ::World2Ui(world_pos, false, allow_offscreen);
}

void CInteractionMarkerManager::RebuildLosRoundRobinOrder()
{
	m_los.rr_order.clear();
	m_los.rr_order.reserve(m_markers.size());

	for (const auto& [id, marker] : m_markers)
	{
		VERIFY(id == marker.object_id);
		m_los.rr_order.push_back(id);
	}

	if (m_los.rr_order.size() != m_los.rr_order_size)
	{
		m_los.rr_cursor = 0;
		m_los.rr_order_size = m_los.rr_order.size();
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

	EnsureQuestSchemeIndex();

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

	g_SpatialSpace->q_sphere(spatial_results, 0, ESPATIAL_TYPE(spatial_mask), origin, m_markers_cfg.scan.radius);

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
		const shared_str category_id = EvaluateCategory(game_object);
		if (!category_id.size())
			continue;

		if (m_hide_mute_stalkers && CategoryHasFeature(category_id, EWSUICategoryFeature::HideIfMute))
		{
			if (CInventoryOwner* owner = game_object->cast_inventory_owner())
			{
				if (!owner->IsTalkEnabled())
					continue;
			}
		}

		SWSUICategoryDef def;
		Fvector offset;
		if (!ResolveMarkerDef(game_object, category_id, def, offset))
			continue;

		if (distance > def.show_distance)
			continue;

		const u16 id = game_object->ID();
		found_ids.insert(id);

		const bool is_new = m_markers.find(id) == m_markers.end();
		SInteractionMarker& marker = m_markers[id];
		marker.object_id = id;
		RefreshMarkerClassifyCache(marker, game_object, category_id);
		if (!marker.classify_valid)
		{
			m_markers.erase(id);
			found_ids.erase(id);
			continue;
		}

		if (is_new)
		{
			marker.screen_pos.set(-1.f, -1.f);
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
	m_focus.engine_focus_id = 0xffff;
	m_focus.focus_id = 0xffff;

	CGameObject* focus_object = actor ? actor->ObjectWeLookingAt() : nullptr;
	if (focus_object)
		m_focus.engine_focus_id = focus_object->ID();

	if (m_markers_cfg.features.wheel_cycle_pickups && m_focus.wheel_focus_id != 0xffff)
	{
		auto wheel_it = m_markers.find(m_focus.wheel_focus_id);
		if (wheel_it != m_markers.end() &&
			CategoryHasFeature(wheel_it->second.category_id, EWSUICategoryFeature::WheelCycle))
			m_focus.focus_id = m_focus.wheel_focus_id;
		else
			m_focus.wheel_focus_id = 0xffff;
	}

	if (m_focus.focus_id == 0xffff)
		m_focus.focus_id = m_focus.engine_focus_id;

	const u32 now = Device.dwTimeGlobal;
	const Fvector& cam_pos = Device.vCameraPosition;
	const Fvector& cam_dir = Device.vCameraDirection;

	const auto& los = m_markers_cfg.los_cache;
	const float los_pos_eps = los.camera_pos_eps > 0.f ? los.camera_pos_eps : 0.05f;
	const float los_dir_eps = los.camera_dir_eps > 0.f ? los.camera_dir_eps : 0.001f;

	bool invalidate_los = false;
	if (!m_los.cam_valid)
	{
		invalidate_los = true;
		m_los.cam_valid = true;
	}
	else
	{
		if (m_los.cam_pos.distance_to_sqr(cam_pos) > los_pos_eps * los_pos_eps)
			invalidate_los = true;
		else if (1.f - m_los.cam_dir.dotproduct(cam_dir) > los_dir_eps)
			invalidate_los = true;
	}

	if (invalidate_los)
	{
		MarkLosCachesStale();
		m_los.cam_pos = cam_pos;
		m_los.cam_dir = cam_dir;
	}

	xr_set<u16> los_check_ids;
	if (!m_los.rr_order.empty())
	{
		const u32 order_size = m_los.rr_order.size();
		const u32 checks = std::min(los.checks_per_frame > 0 ? los.checks_per_frame : 2u, order_size);
		for (u32 i = 0; i < checks; ++i)
		{
			const u32 idx = (m_los.rr_cursor + i) % order_size;
			los_check_ids.insert(m_los.rr_order[idx]);
		}
		m_los.rr_cursor = (m_los.rr_cursor + checks) % order_size;
	}

	const float lerp_factor = clampr(m_markers_cfg.dot.lerp_speed * Device.fTimeDelta * 60.f, 0.f, 1.f);

	for (auto& [id, marker] : m_markers)
	{
		marker.visible = false;
		marker.reachable = false;
		marker.distance = 0.f;

		if (!marker.classify_valid || !marker.category_id.size())
			continue;

		CObject* object = Level().Objects.net_Find(id);
		CGameObject* game_object = object ? object->cast_game_object() : nullptr;
		if (!game_object)
			continue;

		SWSUICategoryDef def = marker.cached_def;
		Fvector offset = marker.cached_offset;
		LPCSTR bone_name = marker.cached_bone.size() ? marker.cached_bone.c_str() : nullptr;

		if (WSUIInternal::WsuiCategoryEq(def.pos_mode, "door"))
		{
			def.bone = ResolveDoorBone(game_object);
			offset.y = GetDoorVisualYOffset(game_object);
			if (def.bone.size())
				bone_name = def.bone.c_str();
		}

		Fvector world_pos;
		if (!GetMarkerWorldPos(game_object, marker.category_id, def, bone_name, offset, world_pos))
			continue;

		const float distance = actor->Position().distance_to(world_pos);
		marker.distance = distance;
		if (distance > def.show_distance)
			continue;

		const bool is_focus = id == m_focus.focus_id;
		bool inside_volume = false;
		if (WSUIInternal::WsuiCategoryEq(def.pos_mode, "zone"))
		{
			if (CSpaceRestrictor* restrictor = game_object->cast_restrictor())
			{
				Fsphere probe;
				probe.P = actor->Position();
				probe.R = 0.25f;
				inside_volume = restrictor->inside(probe);
			}
			else if (CAnomalyZone* zone = game_object->cast_anomaly_zone())
			{
				inside_volume = actor->Position().distance_to(zone->Position()) <= zone->Radius();
			}
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
			const u32 los_ttl = los.cache_ttl_ms > 0 ? los.cache_ttl_ms : 150u;
			const bool needs_recheck = !marker.los_has_result
				|| marker.los_stale
				|| (now - marker.los_check_time >= los_ttl);

			bool rechecked = false;
			if (needs_recheck && los_check_ids.find(id) != los_check_ids.end())
			{
				marker.los_reachable = HasLineOfSight(actor, game_object, world_pos);
				marker.los_has_result = true;
				marker.los_stale = false;
				marker.los_check_time = now;
				rechecked = true;
			}

			if (!marker.los_has_result)
				marker.reachable = false;
			else if (marker.los_stale && !rechecked)
				marker.reachable = false;
			else
				marker.reachable = marker.los_reachable;

			if (!marker.reachable)
				continue;
		}

		Fvector2 target = WorldToScreen(world_pos, is_focus || inside_volume);
		if (target.x < 0.f)
			continue;

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
	if (!actor || m_markers_cfg.scan.max_markers == 0)
		return;

	xr_vector<std::pair<float, u16>> ranked;
	ranked.reserve(m_markers.size());

	for (const auto& [id, marker] : m_markers)
	{
		if (!marker.visible || !marker.reachable)
			continue;
		ranked.emplace_back(GetMarkerSortScore(id, marker), id);
	}

	if (ranked.size() <= m_markers_cfg.scan.max_markers)
		return;

	std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b)
	{
		if (a.first != b.first)
			return a.first > b.first;
		return a.second < b.second;
	});

	xr_set<u16> keep_ids;
	for (u32 i = 0; i < m_markers_cfg.scan.max_markers; ++i)
		keep_ids.insert(ranked[i].second);

	if (m_focus.focus_id != 0xffff)
		keep_ids.insert(m_focus.focus_id);

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

	if (m_markers_cfg.features.enable_task_icons)
	{
		bool is_storyline = false;
		if (IsTaskTarget(id, is_storyline))
			score += m_markers_cfg.marker_priority.task;
	}

	if (id == m_focus.focus_id)
		score += m_markers_cfg.marker_priority.focus;

	score += GetCategoryPriorityScore(marker.category_id);
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
		}

		return true;
	}, &los_data, nullptr, los_data.actor_obj);

	return !los_data.blocked;
}

float CInteractionMarkerManager::SquareHeight(float width_ui) const
{
	return width_ui * AspectScaleX();
}

void CInteractionMarkerManager::UpdateTutorialPromptFade()
{
	LPCSTR tutorial_name = GetActiveTutorialName();
	const bool want = tutorial_name && HasTutorialPromptMapping(tutorial_name);

	if (want != m_tutorial_fade.target_visible)
	{
		m_tutorial_fade.target_visible = want;
		m_tutorial_fade.fade_start_time = Device.dwTimeGlobal;
		m_tutorial_fade.fade_start_alpha = m_tutorial_fade.alpha;
	}

	const u32 fade_dur = want ? m_prompt_cfg.fade_animation.fade_in_ms : m_prompt_cfg.fade_animation.fade_out_ms;
	const float target = want ? 1.f : 0.f;

	if (!fade_dur)
	{
		m_tutorial_fade.alpha = target;
		return;
	}

	float t = float(Device.dwTimeGlobal - m_tutorial_fade.fade_start_time) / float(fade_dur);
	t = clampr(t, 0.f, 1.f);
	m_tutorial_fade.alpha = m_tutorial_fade.fade_start_alpha + (target - m_tutorial_fade.fade_start_alpha) * t;

	if (!want && m_tutorial_fade.alpha <= 0.01f)
		m_tutorial_fade.alpha = 0.f;
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
	if (!IsRuntimeEnabled() || !m_markers_cfg.features.wheel_cycle_pickups || !g_actor || !g_actor->g_Alive())
		return;

	if (ShouldHideUI() || ShouldSuspendMarkLoop())
		return;

	xr_vector<std::pair<float, u16>> items;
	items.reserve(m_markers.size());

	for (const auto& [id, marker] : m_markers)
	{
		if (!CategoryHasFeature(marker.category_id, EWSUICategoryFeature::WheelCycle) || !marker.visible || !marker.reachable)
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

	u16 current = m_focus.wheel_focus_id;
	if (current == 0xffff)
		current = m_focus.engine_focus_id;

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

	m_focus.wheel_focus_id = items[index].second;
}

bool CInteractionMarkerManager::IsScanEnvironmentMoving(CActor* actor) const
{
	if (!actor)
		return true;

	if (!m_scan.motion_valid)
		return true;

	const auto& scan = m_markers_cfg.scan;
	const float actor_eps = scan.actor_pos_eps > 0.f ? scan.actor_pos_eps : m_markers_cfg.los_cache.camera_pos_eps;
	const float cam_eps = scan.camera_dir_eps > 0.f ? scan.camera_dir_eps : m_markers_cfg.los_cache.camera_dir_eps;

	if (m_scan.actor_pos.distance_to_sqr(actor->Position()) > actor_eps * actor_eps)
		return true;

	if (1.f - m_scan.cam_dir.dotproduct(Device.vCameraDirection) > cam_eps)
		return true;

	return false;
}

u32 CInteractionMarkerManager::GetEffectiveScanIntervalMs(CActor* actor) const
{
	const auto& scan = m_markers_cfg.scan;
	if (!scan.adaptive || !actor || IsScanEnvironmentMoving(actor))
		return m_markers_cfg.scan.interval_ms > 0 ? m_markers_cfg.scan.interval_ms : 150u;

	if (scan.idle_interval_ms > m_markers_cfg.scan.interval_ms)
		return scan.idle_interval_ms;

	return m_markers_cfg.scan.interval_ms > 0 ? m_markers_cfg.scan.interval_ms : 150u;
}

void CInteractionMarkerManager::Update()
{
	if (!IsRuntimeEnabled() || !g_actor || !g_actor->g_Alive())
		return;

	const float ui_scale = UiScale();
	if (m_svg_cache_ui_scale > 0.f && !fsimilar(ui_scale, m_svg_cache_ui_scale, 0.001f))
		PrecacheAllSvgs();

	if (ShouldHideUI())
	{
		m_markers.clear();
		m_focus.focus_id = 0xffff;
		m_focus.engine_focus_id = 0xffff;
		m_focus.wheel_focus_id = 0xffff;
		m_focus.prev_focus_id = 0xffff;
		m_focus.popin_focus_id = 0xffff;
		m_focus.prompt_focus_id = 0xffff;
		m_prompt_fade.alpha = 0.f;
		m_prompt_fade.target_visible = false;
		m_scan.motion_valid = false;
		return;
	}

	const u32 now = Device.dwTimeGlobal;
	const bool environment_moving = IsScanEnvironmentMoving(g_actor);
	const u32 scan_interval = GetEffectiveScanIntervalMs(g_actor);
	if (!ShouldSuspendScan() && (environment_moving || now - m_scan.last_scan_time >= scan_interval))
	{
		Scan(g_actor);
		m_scan.last_scan_time = now;
		m_scan.actor_pos = g_actor->Position();
		m_scan.cam_dir = Device.vCameraDirection;
		m_scan.motion_valid = true;
	}

	if (!ShouldSuspendMarkLoop())
	{
		UpdateMarkerPositions(g_actor);
		UpdateAnimations(g_actor);
	}
}

bool CInteractionMarkerManager::WantsPromptVisible(CActor* actor) const
{
	if (!actor || m_focus.focus_id == 0xffff)
		return false;

	auto it = m_markers.find(m_focus.focus_id);
	if (it == m_markers.end())
		return false;

	const SInteractionMarker& marker = it->second;
	if (!marker.visible || !marker.reachable || marker.distance > m_markers_cfg.scan.prompt_distance)
		return false;

	if (m_suppress_tutorial_ui)
	{
		if (LPCSTR tutorial_name = GetActiveTutorialName())
		{
			if (HasTutorialPromptMapping(tutorial_name))
			{
				if (CategoryHasFeature(marker.category_id, EWSUICategoryFeature::SuppressTutorial))
					return false;

				if (m_suppress_marker_on_tutorial_object)
				{
					CObject* object = Level().Objects.net_Find(m_focus.focus_id);
					if (CGameObject* game_object = object ? object->cast_game_object() : nullptr)
					{
						if (game_object->cName().size() &&
							!xr_stricmp(game_object->cName().c_str(), tutorial_name))
							return false;
					}
				}
			}
		}
	}

	string512 action_text;
	return BuildPromptText(actor, marker, action_text);
}

void CInteractionMarkerManager::UpdatePromptFade(CActor* actor)
{
	const bool want = WantsPromptVisible(actor);

	if (want != m_prompt_fade.target_visible)
	{
		m_prompt_fade.target_visible = want;
		m_prompt_fade.fade_start_time = Device.dwTimeGlobal;
		m_prompt_fade.fade_start_alpha = m_prompt_fade.alpha;
		if (want)
			m_focus.prompt_focus_id = m_focus.focus_id;
	}

	if (want)
		m_focus.prompt_focus_id = m_focus.focus_id;

	const u32 fade_dur = want ? m_prompt_cfg.fade_animation.fade_in_ms : m_prompt_cfg.fade_animation.fade_out_ms;
	const float target = want ? 1.f : 0.f;

	if (!fade_dur)
	{
		m_prompt_fade.alpha = target;
		if (!want)
			m_focus.prompt_focus_id = 0xffff;
		return;
	}

	float t = float(Device.dwTimeGlobal - m_prompt_fade.fade_start_time) / float(fade_dur);
	t = clampr(t, 0.f, 1.f);
	m_prompt_fade.alpha = m_prompt_fade.fade_start_alpha + (target - m_prompt_fade.fade_start_alpha) * t;

	if (!want && m_prompt_fade.alpha <= 0.01f)
	{
		m_prompt_fade.alpha = 0.f;
		m_focus.prompt_focus_id = 0xffff;
	}
}

float CInteractionMarkerManager::GetFocusPopinScale() const
{
	if (!m_markers_cfg.popin_animation.duration_ms || m_focus.focus_id == 0xffff || m_focus.popin_focus_id != m_focus.focus_id)
		return 1.f;

	const float elapsed = float(Device.dwTimeGlobal - m_focus.popin_start_time);
	const float t = elapsed / float(m_markers_cfg.popin_animation.duration_ms);
	if (t >= 1.f)
		return 1.f;

	const float min_scale = m_markers_cfg.popin_animation.min_scale > 0.f ? m_markers_cfg.popin_animation.min_scale : 0.55f;
	return min_scale + (1.f - min_scale) * EaseOutElastic(t);
}

void CInteractionMarkerManager::PlayFocusSound() const
{
	if (!m_focus_sound_loaded)
		return;

	HUD_SOUND_ITEM::PlaySound(const_cast<HUD_SOUND_ITEM&>(m_focus_snd), Fvector().set(0.f, 0.f, 0.f), nullptr, true);
}

void CInteractionMarkerManager::UpdateFocusSound(CActor* actor)
{
	if (!m_markers_cfg.features.enable_focus_sound || !m_focus_sound_loaded || !actor || ShouldHideUI())
		return;

	if (m_focus.focus_id == 0xffff || m_focus.focus_id == m_focus.prev_focus_id)
		return;

	auto it = m_markers.find(m_focus.focus_id);
	if (it == m_markers.end() || !it->second.visible || !it->second.reachable)
		return;

	if (it->second.distance > m_markers_cfg.scan.prompt_distance)
		return;

	string512 action_text;
	if (!BuildPromptText(actor, it->second, action_text))
		return;

	PlayFocusSound();
}

void CInteractionMarkerManager::UpdateAnimations(CActor* actor)
{
	if (m_focus.focus_id != m_focus.prev_focus_id)
		UpdateFocusSound(actor);

	if (m_focus.focus_id != m_focus.prev_focus_id)
	{
		if (m_focus.focus_id != 0xffff && m_markers_cfg.popin_animation.duration_ms > 0)
		{
			m_focus.popin_focus_id = m_focus.focus_id;
			m_focus.popin_start_time = Device.dwTimeGlobal;
		}
		m_focus.prev_focus_id = m_focus.focus_id;
	}

	UpdatePromptFade(actor);
	UpdateTutorialPromptFade();
}

void CInteractionMarkerManager::OnRender()
{
	if (!IsRuntimeEnabled() || !g_bRendering || !g_actor || !g_actor->g_Alive())
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

			if (!marker.category_id.size() || !marker.cached_def.texture.HasDrawable())
				continue;

			const SWSUICategoryDef& def = marker.cached_def;
			const bool focused = id == m_focus.focus_id;
			const float popin_scale = focused ? GetFocusPopinScale() : 1.f;
			const float dist_scale = GetDotDistanceScale(marker.distance, def.show_distance);
			const float dot_w = m_markers_cfg.dot.size * UiScale() * popin_scale * dist_scale;
			const float dot_h = SquareHeight(dot_w);
			CGameObject* marker_obj = nullptr;
			if (CObject* object = Level().Objects.net_Find(id))
				marker_obj = object->cast_game_object();
			const SWSUITextureSlot texture = ResolveActiveTexture(marker_obj, marker.category_id, def, focused);
			const float icon_scale = texture.EffectiveSizeScale();
			const float dist_alpha = GetDotDistanceAlpha(marker.distance, def.show_distance);
			DrawTextureSlot(texture, marker.screen_pos.x, marker.screen_pos.y, dot_w * icon_scale, dot_h * icon_scale, ColorWithAlpha(color, dist_alpha), true);
		}
	}

	RenderPrompt(g_actor);
	RenderTutorialPrompt();
}
