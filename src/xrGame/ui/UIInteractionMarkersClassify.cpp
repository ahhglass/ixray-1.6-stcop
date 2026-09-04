#include "StdAfx.h"
#include "UIInteractionMarkers.h"
#include "UIInteractionMarkers_internal.h"

#include "../Actor.h"
#include "../Level.h"
#include "../GameObject.h"
#include "../inventory_item.h"
#include "../Inventory.h"
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
#include "../Include/xrRender/Kinematics.h"
#include "../../xrCore/EngineExternal.h"

namespace
{
	static const LPCSTR g_door_bones[] = { "door_right", "door_left", "lock", "door", nullptr };

	bool CorpseHasInventoryItems(CInventoryOwner* owner)
	{
		return owner && owner->inventory().dwfGetObjectCount() > 0;
	}

	bool MonstersInventoryEnabled()
	{
		return EngineExternal()[EEngineExternalGame::EnableMonstersInventory];
	}

	CInventoryBox* MatchLootInventoryBox(CGameObject* obj)
	{
		CInventoryBox* box = obj->cast_inventory_box();
		if (!box || box->closed())
			return nullptr;

		if (CUsableScriptObject* usable = obj->cast_usable_script_object())
		{
			if (usable->tip_text() && !xr_stricmp(usable->tip_text(), "actor_inventory_box_use"))
				return nullptr;
		}

		return box;
	}
}

const SWSUICategoryDef* CInteractionMarkerManager::GetCategory(shared_str category_id) const
{
	if (!category_id.size())
		return nullptr;

	const auto it = m_categories.find(category_id);
	return it != m_categories.end() ? &it->second : nullptr;
}

float CInteractionMarkerManager::GetCategoryPriorityScore(shared_str category_id) const
{
	if (!category_id.size())
		return 0.f;

	const auto it = m_category_priority.find(category_id);
	if (it != m_category_priority.end())
		return it->second;

	return 0.f;
}

bool CInteractionMarkerManager::CategoryHasFeature(shared_str category_id, EWSUICategoryFeature feature) const
{
	const SWSUICategoryDef* cat = GetCategory(category_id);
	return cat && WSUIInternal::CategoryHasFeature(*cat, feature);
}

bool CInteractionMarkerManager::IsPromptStackCountEnabled(shared_str category_id) const
{
	return m_prompt_cfg.features.item_stack_count
		&& CategoryHasFeature(category_id, EWSUICategoryFeature::StackCount);
}

bool CInteractionMarkerManager::IsPromptConditionEnabled(const SWSUICategoryDef& category) const
{
	return m_prompt_cfg.features.item_condition
		&& WSUIInternal::CategoryHasFeature(category, EWSUICategoryFeature::ItemCondition);
}

bool CInteractionMarkerManager::IsPromptItemCardEnabled(shared_str category_id) const
{
	return m_prompt_cfg.features.item_card
		&& CategoryHasFeature(category_id, EWSUICategoryFeature::ItemCard);
}

void CInteractionMarkerManager::LoadClassificationRules()
{
	m_class_rules.clear();

	if (!pSettings->section_exist("wsui_class_rules"))
		return;

	const CInifile::Sect& sect = pSettings->r_section("wsui_class_rules");
	for (const auto& line : sect.Data)
	{
		SWSUIClassRule rule;
		WSUIInternal::ParseClassRuleValue(*line.second, rule);
		if (rule.category_id.size() && rule.detector.size())
			m_class_rules.push_back(rule);
	}
}

bool CInteractionMarkerManager::MatchDetector(CGameObject* obj, const SWSUIClassRule& rule) const
{
	if (!obj || !rule.detector.size())
		return false;

	const LPCSTR detector = rule.detector.c_str();
	const u32 flags = rule.flags;

	if (!xr_stricmp(detector, "inventory_box_personal"))
	{
		CInventoryBox* box = obj->cast_inventory_box();
		if (!box || box->closed())
			return false;

		if (CUsableScriptObject* usable = obj->cast_usable_script_object())
			return usable->tip_text() && !xr_stricmp(usable->tip_text(), "actor_inventory_box_use");

		return false;
	}

	if (!xr_stricmp(detector, "inventory_box_full"))
	{
		CInventoryBox* box = MatchLootInventoryBox(obj);
		return box && !box->IsEmpty();
	}

	if (!xr_stricmp(detector, "inventory_box_empty"))
	{
		CInventoryBox* box = MatchLootInventoryBox(obj);
		return box && box->IsEmpty();
	}

	if (!xr_stricmp(detector, "inventory_box"))
	{
		return MatchLootInventoryBox(obj) != nullptr;
	}

	if (!xr_stricmp(detector, "stalker_alive"))
	{
		if (CAI_Stalker* stalker = obj->cast_stalker())
		{
			if (CEntityAlive* alive = stalker->cast_entity_alive())
				return alive->g_Alive();
		}
		return false;
	}

	if (!xr_stricmp(detector, "stalker_dead"))
	{
		if (CAI_Stalker* stalker = obj->cast_stalker())
		{
			if (CEntityAlive* alive = stalker->cast_entity_alive())
			{
				if (alive->g_Alive())
					return false;

				CInventoryOwner* owner = obj->cast_inventory_owner();
				if (owner && owner->deadbody_closed_status())
					return false;

				if (IsCorpseMarkerSuppressed(obj->ID()))
					return false;

				if (flags & static_cast<u32>(EWSUIRuleFlag::RequireCorpse))
				{
					if (!owner || !CorpseHasInventoryItems(owner))
						return false;
				}

				return true;
			}
		}
		return false;
	}

	if (!xr_stricmp(detector, "monster_dead"))
	{
		if (obj->cast_stalker())
			return false;

		if (CBaseMonster* monster = obj->cast_base_monster())
		{
			if (CEntityAlive* alive = monster->cast_entity_alive())
			{
				if (alive->g_Alive())
					return false;

				if (flags & static_cast<u32>(EWSUIRuleFlag::RequireCorpse) && !ShouldShowMonsterCorpse(obj))
					return false;

				return true;
			}
		}
		return false;
	}

	if (!xr_stricmp(detector, "inventory_item"))
	{
		if (CInventoryItem* item = obj->cast_inventory_item())
		{
			if (!item->Useful())
				return false;
			if (flags & static_cast<u32>(EWSUIRuleFlag::CanTake) && !item->CanTake())
				return false;
			return true;
		}
		return false;
	}

	if (!xr_stricmp(detector, "door"))
		return IsDoorObject(obj);

	if (!xr_stricmp(detector, "zone_campfire"))
		return obj->cast_zone_campfire() != nullptr;

	if (!xr_stricmp(detector, "restrictor_known"))
		return IsKnownZone(obj);

	if (!xr_stricmp(detector, "quest_scheme"))
		return IsQuestSchemeObject(obj);

	if (!xr_stricmp(detector, "breakable_box"))
		return IsBreakableBox(obj);

	if (!xr_stricmp(detector, "usable_tip"))
	{
		if (obj->cast_inventory_box())
			return false;

		if (!HasUsableTip(obj))
			return false;
		if (flags & static_cast<u32>(EWSUIRuleFlag::RequireUsableTip))
			return HasUsableTip(obj);
		return true;
	}

	if (!xr_stricmp(detector, "section_equals"))
	{
		if (!rule.param.size())
			return false;
		return !xr_strcmp(obj->cNameSect(), rule.param.c_str());
	}

	if (!xr_stricmp(detector, "section_contains"))
	{
		if (!rule.param.size())
			return false;
		LPCSTR sect = obj->cNameSect().c_str();
		return sect && strstr(sect, rule.param.c_str());
	}

	if (!xr_stricmp(detector, "name_equals"))
	{
		if (!rule.param.size())
			return false;
		return !xr_strcmp(obj->cName(), rule.param.c_str());
	}

	if (!xr_stricmp(detector, "name_contains"))
	{
		if (!rule.param.size())
			return false;
		LPCSTR name = obj->cName().c_str();
		return name && strstr(name, rule.param.c_str());
	}

	if (!xr_stricmp(detector, "visual_equals"))
	{
		if (!rule.param.size())
			return false;
		return !xr_strcmp(obj->cNameVisual(), rule.param.c_str());
	}

	return false;
}

shared_str CInteractionMarkerManager::EvaluateCategory(CGameObject* obj) const
{
	if (!obj || obj->getDestroy() || !obj->getVisible())
		return nullptr;

	if (CActor* actor = obj->cast_actor())
	{
		if (actor == Actor())
			return nullptr;
	}

	for (const SWSUIClassRule& rule : m_class_rules)
	{
		if (MatchDetector(obj, rule))
			return rule.category_id;
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
		for (u32 i = 0; g_door_bones[i]; ++i)
		{
			if (kinematics->LL_BoneID(g_door_bones[i]) != BI_NONE)
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

	if (MatchNamePattern(name, m_zone_name_patterns).HasDrawable())
		return true;

	return false;
}

bool CInteractionMarkerManager::ShouldShowMonsterCorpse(CGameObject* obj) const
{
	if (!obj || !MonstersInventoryEnabled())
		return false;

	CInventoryOwner* owner = obj->cast_inventory_owner();
	if (!owner || owner->deadbody_closed_status())
		return false;

	if (IsCorpseMarkerSuppressed(obj->ID()))
		return false;

	return CorpseHasInventoryItems(owner);
}

LPCSTR CInteractionMarkerManager::ResolveZonePrompt(CGameObject* obj) const
{
	if (!obj)
		return nullptr;

	auto it = m_zone_prompts_by_name.find(obj->cName());
	if (it != m_zone_prompts_by_name.end())
		return it->second.c_str();

	return nullptr;
}

LPCSTR CInteractionMarkerManager::ResolveDoorBone(CGameObject* obj) const
{
	if (!obj)
		return "nil";

	if (IKinematics* kinematics = PKinematics(obj->Visual()))
	{
		for (u32 i = 0; g_door_bones[i]; ++i)
		{
			if (kinematics->LL_BoneID(g_door_bones[i]) != BI_NONE)
				return g_door_bones[i];
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

LPCSTR CInteractionMarkerManager::ResolveBoneName(CGameObject* obj, const SWSUICategoryDef& def) const
{
	if (!obj)
		return nullptr;

	if (WSUIInternal::WsuiCategoryEq(def.pos_mode, "center"))
		return nullptr;

	if (WSUIInternal::WsuiCategoryEq(def.pos_mode, "door"))
		return ResolveDoorBone(obj);

	if (WSUIInternal::WsuiCategoryEq(def.pos_mode, "zone"))
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

bool CInteractionMarkerManager::ResolveMarkerDef(CGameObject* obj, shared_str category_id, SWSUICategoryDef& out_def, Fvector& out_offset) const
{
	const SWSUICategoryDef* cat = GetCategory(category_id);
	if (!cat)
		return false;

	out_def = *cat;
	if (!out_def.enabled || !out_def.texture.HasDrawable())
		return false;

	if (obj)
	{
		shared_str section = obj->cNameSect();
		auto bone_it = m_bones_by_section.find(section);
		if (bone_it != m_bones_by_section.end())
			out_def.bone = bone_it->second;

		auto pos_it = m_pos_adj_by_section.find(section);
		out_offset = (pos_it != m_pos_adj_by_section.end()) ? pos_it->second : Fvector().set(0.f, 0.f, 0.f);

		if (WSUIInternal::WsuiCategoryEq(out_def.pos_mode, "door"))
		{
			out_def.bone = ResolveDoorBone(obj);
			out_offset.y = GetDoorVisualYOffset(obj);
		}
	}
	else
	{
		out_offset.set(0.f, 0.f, 0.f);
	}

	return true;
}

bool CInteractionMarkerManager::GetMarkerWorldPos(CGameObject* obj, shared_str category_id, const SWSUICategoryDef& def, LPCSTR bone_name, const Fvector& offset, Fvector& out) const
{
	if (!obj)
		return false;

	const bool use_center =
		WSUIInternal::WsuiCategoryEq(def.pos_mode, "center") ||
		WSUIInternal::WsuiCategoryEq(def.pos_mode, "zone");

	if (!use_center && bone_name && bone_name[0] && xr_stricmp(bone_name, "nil") != 0)
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

	if (WSUIInternal::WsuiCategoryEq(def.pos_mode, "zone"))
	{
		Fvector to_viewer;
		to_viewer.sub(Device.vCameraPosition, out);
		const float dist = to_viewer.magnitude();
		if (dist > 0.01f)
		{
			to_viewer.mul(1.f / dist);
			float pull = 0.5f;
			if (WSUIInternal::WsuiCategoryEq(def.prompt_mode, "campfire"))
			{
				if (CAnomalyZone* zone = obj->cast_anomaly_zone())
					pull = std::min(zone->Radius() * 0.45f, dist * 0.85f);
			}
			else if (CSpaceRestrictor* restrictor = obj->cast_restrictor())
			{
				pull = std::min(restrictor->Radius() * 0.45f, dist * 0.85f);
			}

			out.mad(out, to_viewer, pull);
		}
	}

	out.add(offset);
	return true;
}

void CInteractionMarkerManager::RefreshMarkerClassifyCache(SInteractionMarker& marker, CGameObject* obj, shared_str category_id)
{
	marker.category_id = category_id;
	if (!category_id.size() || !ResolveMarkerDef(obj, category_id, marker.cached_def, marker.cached_offset))
	{
		marker.classify_valid = false;
		return;
	}

	if (LPCSTR bone = ResolveBoneName(obj, marker.cached_def))
		marker.cached_bone = bone;
	else
		marker.cached_bone = nullptr;

	marker.classify_valid = true;
}

SWSUITextureSlot CInteractionMarkerManager::ResolveSpecialIcons(CGameObject* obj, shared_str icon_lookup) const
{
	if (!obj || !icon_lookup.size())
		return {};

	auto pickRule = [this](LPCSTR event) -> SWSUITextureSlot
	{
		SWSUITextureSlot slot = ResolveIconRule(event);
		return slot.HasDrawable() ? slot : SWSUITextureSlot{};
	};

	if (WSUIInternal::WsuiCategoryEq(icon_lookup, "item"))
	{
		if (IsExplosiveObject(obj))
		{
			if (SWSUITextureSlot slot = pickRule("explosive"); slot.HasDrawable())
				return slot;
		}
	}
	else if (WSUIInternal::WsuiCategoryEq(icon_lookup, "usable"))
	{
		if (IsBreakableBox(obj))
		{
			if (SWSUITextureSlot slot = pickRule("breakable"); slot.HasDrawable())
				return slot;
		}

		if (IsExplosiveObject(obj))
		{
			if (SWSUITextureSlot slot = pickRule("explosive"); slot.HasDrawable())
				return slot;
		}
	}

	return {};
}

SWSUITextureSlot CInteractionMarkerManager::ResolveLookupIcons(CGameObject* obj, const SWSUICategoryDef& def) const
{
	if (!obj || !def.icon_lookup.size())
		return {};

	auto pickRule = [this](LPCSTR event) -> SWSUITextureSlot
	{
		SWSUITextureSlot slot = ResolveIconRule(event);
		return slot.HasDrawable() ? slot : SWSUITextureSlot{};
	};

	const shared_str section = obj->cNameSect();

	if (WSUIInternal::WsuiCategoryEq(def.icon_lookup, "npc"))
	{
		if (CAI_Stalker* stalker = obj->cast_stalker())
		{
			if (IsSquadLeaderNpc(stalker))
			{
				if (SWSUITextureSlot slot = pickRule("squad_leader"); slot.HasDrawable())
					return slot;
			}
		}

		if (const auto role_it = m_npc_roles_by_section.find(section); role_it != m_npc_roles_by_section.end())
		{
			if (SWSUITextureSlot slot = ResolveIcon(role_it->second); slot.HasDrawable())
				return slot;
		}

		if (SWSUITextureSlot slot = MatchSectionPattern(section, m_npc_section_patterns); slot.HasDrawable())
			return slot;
	}
	else if (WSUIInternal::WsuiCategoryEq(def.icon_lookup, "body"))
	{
		if (obj->cast_base_monster() && !obj->cast_stalker())
		{
			if (SWSUITextureSlot slot = pickRule("monster_corpse"); slot.HasDrawable())
				return slot;
		}
	}
	else if (WSUIInternal::WsuiCategoryEq(def.icon_lookup, "usable"))
	{
		if (IsQuestSchemeObject(obj))
		{
			if (SWSUITextureSlot slot = pickRule("quest_scheme_usable"); slot.HasDrawable())
				return slot;
		}

		if (SWSUITextureSlot slot = MatchSectionPattern(section, m_usable_section_patterns); slot.HasDrawable())
			return slot;
	}
	else if (WSUIInternal::WsuiCategoryEq(def.icon_lookup, "zone"))
	{
		if (const auto tex_it = m_zone_textures_by_name.find(obj->cName()); tex_it != m_zone_textures_by_name.end())
		{
			if (SWSUITextureSlot slot = ResolveIcon(tex_it->second); slot.HasDrawable())
				return slot;
		}

		if (SWSUITextureSlot slot = MatchNamePattern(obj->cName(), m_zone_name_patterns); slot.HasDrawable())
			return slot;

		if (!xr_strcmp(obj->cNameSect(), "camp_zone"))
		{
			if (SWSUITextureSlot slot = pickRule("zone_camp"); slot.HasDrawable())
				return slot;
		}
	}

	return {};
}

SWSUITextureSlot CInteractionMarkerManager::ResolveActiveTexture(CGameObject* obj, shared_str category_id, const SWSUICategoryDef& def, bool focused) const
{
	(void)category_id;

	if (m_markers_cfg.features.special_icons_always && obj && def.icon_lookup.size())
	{
		if (SWSUITextureSlot slot = ResolveSpecialIcons(obj, def.icon_lookup); slot.HasDrawable())
			return slot;
	}

	if (!focused)
		return def.texture;

	if (!obj)
		return def.active_texture.HasDrawable() ? def.active_texture : def.texture;

	auto pickRule = [this](LPCSTR event) -> SWSUITextureSlot
	{
		SWSUITextureSlot slot = ResolveIconRule(event);
		return slot.HasDrawable() ? slot : SWSUITextureSlot{};
	};

	if (m_markers_cfg.features.enable_task_icons)
	{
		bool is_storyline = false;
		if (IsTaskTarget(obj->ID(), is_storyline))
		{
			if (SWSUITextureSlot slot = pickRule(is_storyline ? "task_storyline" : "task_side"); slot.HasDrawable())
				return slot;
		}
	}

	if (SWSUITextureSlot slot = ResolveSpecialIcons(obj, def.icon_lookup); slot.HasDrawable())
		return slot;

	if (SWSUITextureSlot slot = ResolveLookupIcons(obj, def); slot.HasDrawable())
		return slot;

	return def.active_texture.HasDrawable() ? def.active_texture : def.texture;
}
