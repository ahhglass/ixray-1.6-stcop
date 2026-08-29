#pragma once

#include "../xrCore/_stl_extensions.h"
#include "../xrCore/_vector2.h"
#include "../HudSound.h"

class CGameObject;
class CGameFont;
class CAI_Stalker;

enum class EWSUIClass : u8
{
	None = 0,
	Item,
	Npc,
	Body,
	Stash,
	Usable,
	Door,
	Campfire,
	Zone,
	Count
};

struct SWSUIClassDef
{
	bool enabled = true;
	shared_str texture;
	shared_str active_texture;
	shared_str bone;
	float show_distance = 15.f;
};

struct SInteractionMarker
{
	u16 object_id = 0xffff;
	EWSUIClass cls = EWSUIClass::None;
	Fvector2 screen_pos = {};
	Fvector2 target_screen_pos = {};
	float distance = 0.f;
	bool visible = false;
	bool reachable = false;

	SWSUIClassDef cached_def = {};
	Fvector cached_offset = {};
	shared_str cached_bone;
	bool classify_valid = false;

	bool los_has_result = false;
	bool los_stale = true;
	bool los_reachable = false;
	u32 los_check_time = 0;
};

struct SWSUIItemCardMetric
{
	float x = 0.f;
	float y = 0.f;
	float icon_x = 0.f;
	float icon_y = 0.f;
	float icon_w = 0.f;
	float icon_h = 0.f;
	float text_x = 0.f;
	float text_y = 0.f;
	shared_str icon;
	shared_str font_name;
	CGameFont* font = nullptr;
	u32 color = 0;
};

struct SWSUIPromptParts
{
	bool split = false;
	string512 verb = {};
	string256 name = {};
	string512 full = {};
	bool show_condition = false;
	float item_condition = 0.f;
};

class CInteractionMarkerManager
{
public:
	CInteractionMarkerManager() = default;
	~CInteractionMarkerManager();

	void Load();
	void Update();
	void OnRender();
	void OnMouseWheel(int direction);

	static bool IsEnabled();
	static bool ShouldSuppressVanilla();
	static bool ShouldSuppressNpcName(float distance);
	static bool ShouldSuppressTutorialUi();

private:
	void LoadClassDefs();
	void LoadLookupSection(LPCSTR section_name, bool is_pos_adj);
	void LoadFloatLookupSection(LPCSTR section_name, xr_map<shared_str, float>& out);
	void LoadTextureLookupSection(LPCSTR section_name, xr_map<shared_str, shared_str>& out);
	void LoadBonePriority();
	void BuildQuestSchemeIndex();
	void CollectQuestSchemesInDir(LPCSTR dir_path);
	bool IsQuestSchemeStory(LPCSTR story_id) const;
	shared_str ResolveStoryId(CGameObject* obj) const;
	bool IsBreakableBox(CGameObject* obj) const;
	bool IsExplosiveObject(CGameObject* obj) const;
	bool IsQuestSchemeObject(CGameObject* obj) const;
	bool IsSquadLeaderNpc(CAI_Stalker* stalker) const;
	bool MatchSquadOrLeader(u16 object_id, u16 compare_id) const;
	bool IsTaskTarget(u16 object_id, bool& is_storyline) const;
	LPCSTR ResolveBoneName(CGameObject* obj, EWSUIClass cls, const SWSUIClassDef& def) const;
	bool HasValidBone(CGameObject* obj, LPCSTR bone_name) const;
	void UpdateAnimations(CActor* actor);
	void LoadFocusSound();
	void LoadPromptStyle();
	void LoadPromptNodes(CUIXml& xml);
	void UpdateFocusSound(CActor* actor);
	void PlayFocusSound() const;
	float GetFocusPopinScale() const;
	bool WantsPromptVisible(CActor* actor) const;
	void UpdatePromptFade(CActor* actor);
	void UpdateTutorialPromptFade();
	LPCSTR GetActiveTutorialName() const;
	LPCSTR ResolveTutorialPrompt(LPCSTR tutorial_name) const;
	bool HasTutorialPromptMapping(LPCSTR tutorial_name) const;
	void SanitizeActionText(string512& text) const;
	void RenderPromptBubble(float cx, float cy, const SWSUIPromptParts& parts, LPCSTR key_name, float alpha, bool center_anchor, const SInteractionMarker* marker = nullptr) const;
	void RenderItemCard(float panel_left, float panel_bottom, CInventoryItem* item, float alpha) const;
	void LoadItemCardMetric(CUIXml& xml, LPCSTR path, SWSUIItemCardMetric& out) const;
	float GetDotDistanceScale(float distance, float show_distance) const;
	float GetDotDistanceAlpha(float distance, float show_distance) const;
	float GetMarkerSortScore(u16 id, const SInteractionMarker& marker) const;
	LPCSTR ResolveKeyBindIcon(int dik, float& out_w, float& out_h) const;
	void LoadDikIcons();
	u32 GetItemConditionColor(float condition) const;
	u32 CountGroupedItemMarkers(const SInteractionMarker& focus_marker) const;
	float PromptTextWidth(CGameFont* font, LPCSTR text, float kx) const;
	float PromptTextHeight(CGameFont* font, float ky) const;
	u32 ColorWithAlpha(u32 color, float alpha) const;
	float UiScale() const;
	float AspectScaleX() const;
	void Scan(CActor* actor);
	void UpdateMarkerPositions(CActor* actor);
	EWSUIClass ClassifyObject(CGameObject* obj) const;
	bool IsDoorObject(CGameObject* obj) const;
	bool HasUsableTip(CGameObject* obj) const;
	bool IsKnownZone(CGameObject* obj) const;
	LPCSTR ResolveZonePrompt(CGameObject* obj) const;
	LPCSTR ResolveDoorBone(CGameObject* obj) const;
	float GetDoorVisualYOffset(CGameObject* obj) const;
	shared_str ResolveActiveTexture(CGameObject* obj, EWSUIClass cls, const SWSUIClassDef& def, bool focused) const;
	bool ResolveMarkerDef(CGameObject* obj, EWSUIClass cls, SWSUIClassDef& out_def, Fvector& out_offset) const;
	bool GetMarkerWorldPos(CGameObject* obj, EWSUIClass cls, LPCSTR bone_name, const Fvector& offset, Fvector& out) const;
	Fvector2 WorldToScreen(const Fvector& world_pos, bool allow_offscreen = false) const;
	bool DrawTextureMarker(const shared_str& texture_id, float cx, float cy, float w, float h, u32 color, bool keep_square = false, float angle = 0.f) const;
	bool BuildPromptParts(CActor* actor, const SInteractionMarker& marker, SWSUIPromptParts& out) const;
	bool BuildPromptText(CActor* actor, const SInteractionMarker& marker, string512& out) const;
	void RenderPrompt(CActor* actor) const;
	void RenderTutorialPrompt() const;
	LPCSTR GetKeyName() const;
	bool ShouldHideUI() const;
	bool IsPdaOpen() const;
	bool IsInventoryOpen() const;
	bool ShouldSuspendScan() const;
	bool ShouldSuspendMarkLoop() const;
	bool HasLineOfSight(CActor* actor, CGameObject* obj, const Fvector& world_pos) const;
	float SquareHeight(float width_ui) const;
	void ApplyMarkerLimit(CActor* actor);
	void RefreshMarkerClassifyCache(SInteractionMarker& marker, CGameObject* obj, EWSUIClass cls);
	void RebuildLosRoundRobinOrder();
	void MarkLosCachesStale();

private:
	bool m_enabled = false;
	bool m_suppress_vanilla = true;
	bool m_suppress_tutorial_ui = true;
	bool m_hide_mute_stalkers = true;
	bool m_enable_task_icons = true;
	bool m_enable_quest_scheme_scan = true;
	bool m_enable_focus_sound = false;
	bool m_focus_sound_loaded = false;
	bool m_enable_item_stack_count = true;
	bool m_enable_item_condition = true;
	bool m_enable_special_icons_always = true;
	bool m_enable_item_card = true;
	bool m_dot_distance_scale = true;
	bool m_dot_distance_fade = true;
	bool m_hide_dots = false;
	bool m_wheel_cycle_pickups = true;
	bool m_prompt_fixed = false;
	float m_dot_min_scale = 0.6f;
	float m_dot_max_scale = 1.f;
	float m_dot_min_alpha = 0.45f;
	float m_marker_priority_task = 10000.f;
	float m_marker_priority_npc = 3000.f;
	float m_scan_radius = 5.f;
	float m_prompt_distance = 4.f;
	u32 m_max_markers = 10;
	float m_dot_size = 6.f;
	float m_lerp_speed = 0.15f;
	u32 m_scan_interval_ms = 150;
	u32 m_last_scan_time = 0;

	static constexpr u32 WSUI_LOS_CACHE_TTL_MS = 150;
	static constexpr u32 WSUI_LOS_CHECKS_PER_FRAME = 2;
	static constexpr float WSUI_LOS_CAM_POS_EPS = 0.05f;
	static constexpr float WSUI_LOS_CAM_DIR_EPS = 0.001f;

	u32 m_los_rr_cursor = 0;
	u32 m_los_rr_order_size = 0;
	xr_vector<u16> m_los_rr_order;
	Fvector m_los_cam_pos = {};
	Fvector m_los_cam_dir = {};
	bool m_los_cam_valid = false;

	float m_ui_scale = 1.f;
	float m_aspect_correction = 1.f;
	float m_font_scale_w = 1.f;
	float m_font_scale_h = 1.f;
	float m_anchor_x = 0.f;
	float m_anchor_y = 0.f;
	float m_key_text_x = 0.f;
	float m_key_text_y = 0.f;
	float m_verb_text_x = 0.f;
	float m_verb_text_y = 0.f;
	float m_full_text_x = 0.f;
	float m_full_text_y = 0.f;
	float m_tutorial_x = 0.f;
	float m_tutorial_y = 0.f;
	float m_prompt_fixed_x = 0.f;
	float m_prompt_fixed_y = 0.f;

	shared_str m_prompt_drop_texture;
	shared_str m_prompt_keybind_texture;
	shared_str m_prompt_keybind_pressed_texture;
	shared_str m_item_card_drop_texture;
	float m_item_card_x = 0.f;
	float m_item_card_y = 0.f;
	float m_item_card_drop_w = 0.f;
	float m_item_card_drop_h = 0.f;
	SWSUIItemCardMetric m_item_card_weight;
	SWSUIItemCardMetric m_item_card_value;
	float m_keybind_icon_w = 0.f;
	float m_keybind_icon_h = 0.f;
	float m_prompt_drop_height = 0.f;
	float m_prompt_keybind_w = 0.f;
	float m_prompt_keybind_h = 0.f;
	float m_prompt_text_pad = 0.f;
	float m_item_group_distance = 0.f;
	float m_item_condition_line_spacing = 0.f;
	float m_condition_text_x = 0.f;
	float m_condition_text_y = 0.f;
	bool m_condition_drop_enabled = false;
	shared_str m_condition_drop_texture;
	float m_condition_drop_w = 0.f;
	float m_condition_drop_h = 0.f;
	float m_condition_drop_pad = 0.f;
	u32 m_condition_drop_color = 0;
	u32 m_item_condition_color_min = 0;
	u32 m_item_condition_color_max = 0;
	u32 m_popin_anim_dur = 0;
	u32 m_prompt_fade_in_time = 0;
	u32 m_prompt_fade_out_time = 0;

	shared_str m_prompt_ui_xml;
	shared_str m_prompt_key_font_name;
	shared_str m_prompt_verb_font_name;
	shared_str m_prompt_name_font_name;
	shared_str m_prompt_full_font_name;
	shared_str m_prompt_condition_font_name;
	CGameFont* m_prompt_key_font = nullptr;
	CGameFont* m_prompt_verb_font = nullptr;
	CGameFont* m_prompt_name_font = nullptr;
	CGameFont* m_prompt_full_font = nullptr;
	CGameFont* m_prompt_condition_font = nullptr;
	u32 m_prompt_key_color = 0;
	u32 m_prompt_verb_color = 0;
	u32 m_prompt_name_color = 0;
	u32 m_prompt_full_color = 0;
	u32 m_prompt_condition_color = 0;

	HUD_SOUND_ITEM m_focus_snd;

	SWSUIClassDef m_classes[static_cast<u32>(EWSUIClass::Count)];
	xr_map<shared_str, shared_str> m_bones_by_section;
	xr_map<shared_str, Fvector> m_pos_adj_by_section;
	xr_map<shared_str, float> m_door_visuals_y;
	xr_map<shared_str, shared_str> m_npc_roles_by_section;
	xr_map<shared_str, shared_str> m_usable_textures_by_section;
	xr_map<shared_str, shared_str> m_zone_textures_by_name;
	xr_map<shared_str, shared_str> m_zone_prompts_by_name;
	xr_map<shared_str, shared_str> m_tutorial_prompts_by_name;
	xr_set<shared_str> m_quest_scheme_stories;
	xr_set<shared_str> m_breakable_box_visuals;
	xr_vector<shared_str> m_bone_priority;
	xr_map<int, shared_str> m_dik_icons;

	xr_map<u16, SInteractionMarker> m_markers;
	u16 m_focus_id = 0xffff;
	u16 m_engine_focus_id = 0xffff;
	u16 m_wheel_focus_id = 0xffff;
	u16 m_prev_focus_id = 0xffff;
	u16 m_popin_focus_id = 0xffff;
	u32 m_popin_start_time = 0;
	u16 m_prompt_focus_id = 0xffff;
	float m_prompt_alpha = 0.f;
	float m_prompt_fade_start_alpha = 0.f;
	bool m_prompt_target_visible = false;
	u32 m_prompt_fade_start_time = 0;

	float m_tutorial_prompt_alpha = 0.f;
	float m_tutorial_fade_start_alpha = 0.f;
	bool m_tutorial_target_visible = false;
	u32 m_tutorial_fade_start_time = 0;
};

extern CInteractionMarkerManager* g_pInteractionMarkerManager;
