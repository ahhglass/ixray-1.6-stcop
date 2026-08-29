#include "StdAfx.h"
#include "PickupManager.h"
#include "ui/UIInteractionMarkers.h"
#include "Actor.h"
#include "inventory_item.h"
#include "../xrEngine/GameMtlLib.h"
#include "../xrEngine/CameraBase.h"
#include "../xrUI/UIXmlInit.h"
#include "../xrEngine/xr_level_controller.h"
#include "../xrEngine/string_table.h"
#include "../xrCore/_color.h"
#include "Level.h"
#define PICKUP_INFO_COLOR 0xFFDDDDDD

CPickUpManager::CPickUpManager(CActor* NewOwner) :
	Owner(NewOwner)
{
	PickupMode = false;
	PickupInfoRadius = 100;
	_style.font = g_FontManager->pFontSystem;
	_style.color = PICKUP_INFO_COLOR;
	LoadStylesFromXML();
}

void CPickUpManager::RenderInfo()
{
	if (WSUI_ShouldHidePickupUI())
		return;

	Owner->feel_touch_update(Owner->cam_FirstEye()->vPosition, PickupInfoRadius);

	xr_vector<CObject*> visibleItems;
	xr_set<u16> feelTouchIds;

	for (CObject* Item : Owner->feel_touch)
	{
		feelTouchIds.insert(Item->ID());

		u16 objId = Item->ID();
		Fvector2& smoothPos = _textPositions[objId];
		Fvector2 targetPos = GetScreenPos(Item);

		if (targetPos.x >= 0)
		{
			if (std::abs(smoothPos.x) < 1.0f && std::abs(smoothPos.y) < 1.0f)
				smoothPos = targetPos;
			else
			{
				float lerpFactor = _style.lerp_speed * Device.fTimeDelta * 60.0f;
				clamp(lerpFactor, 0.0f, 1.0f);
				smoothPos.x = _lerp(smoothPos.x, targetPos.x, lerpFactor);
				smoothPos.y = _lerp(smoothPos.y, targetPos.y, lerpFactor);
			}
		}

		if (Item->getVisible() && smoothPos.x >= 0 && CanPickItem(Render->ViewBase, Owner->cam_FirstEye()->vPosition, Item))
			visibleItems.push_back(Item);
	}

	if (_style.group_same_items && !visibleItems.empty())
	{
		xr_vector<SPickupItemGroup> groups;
		GroupItems(visibleItems, groups);
		for (const auto& group : groups)
			PickupInfoDraw(group.items[0], (u32)group.items.size());
	}
	else
		for (CObject* item : visibleItems)
			PickupInfoDraw(item);

	for (auto it = _textPositions.begin(); it != _textPositions.end();)
		feelTouchIds.find(it->first) == feelTouchIds.end() ? it = _textPositions.erase(it) : ++it;
}

Fvector2 CPickUpManager::GetScreenPos(CObject* obj) const
{
	Fvector worldCenter;
	obj->Center(worldCenter);

	Fvector4 vRes;
	Device.mFullTransform.transform(vRes, worldCenter);

	if (vRes.z < 0 || vRes.w < 0 || vRes.x < -1.f || vRes.x > 1.f || vRes.y < -1.f || vRes.y > 1.f)
		return Fvector2().set(-1.f, -1.f);

	return Fvector2().set((1.f + vRes.x) / 2.f * Device.TargetWidth, (1.f - vRes.y) / 2.f * Device.TargetHeight);
}

void CPickUpManager::PickupInfoDraw(CObject* object, u32 item_count)
{
	CInventoryItem* item = object != nullptr ? object->cast_inventory_item() : nullptr;
	if (!item)
		return;

	u16 objId = object->ID();
	Fvector2& smoothPos = _textPositions[objId];

	if (smoothPos.x < 0)
		return;

	CGameFont* font = _style.font ? _style.font : g_FontManager->pFontSystem;
	font->SetAligment(_style.align_name);
	font->SetColor(_style.color);

	string512 text;
	if (_style.show_key_binding)
	{
		LPCSTR keyName = GetKeyName();
		if (keyName)
		{
			if (item_count > 1)
				xr_sprintf(text, sizeof(text), "[%s] %s x%u", keyName, item->NameItem(), item_count);
			else
				xr_sprintf(text, sizeof(text), "[%s] %s", keyName, item->NameItem());
		}
		else
			xr_sprintf(text, sizeof(text), item_count > 1 ? "%s x%u" : "%s", item->NameItem(), item_count);
	}
	else
		xr_sprintf(text, sizeof(text), item_count > 1 ? "%s x%u" : "%s", item->NameItem(), item_count);

	font->Out(smoothPos.x, smoothPos.y, text);

	if (_style.show_condition && item->IsUsingCondition())
	{
		float condition = item->GetCondition();
		int percent = (int)(condition * 100.0f);

		string256 conditionStr;
		xr_sprintf(conditionStr, sizeof(conditionStr), "%d%%", percent);

		shared_str conditionLabel = g_pStringTable->translate("ui_st_loot_info_condition");
		string256 condText;
		if (conditionLabel.size() > 0 && xr_strcmp(conditionLabel.c_str(), "ui_st_loot_info_condition") != 0)
			xr_sprintf(condText, sizeof(condText), "%s: %s", conditionLabel.c_str(), conditionStr);
		else
			xr_sprintf(condText, sizeof(condText), "%s", conditionStr);

		font->SetColor(GetConditionColor(condition));
		font->SetAligment(_style.align_info);
		font->Out(smoothPos.x, smoothPos.y + font->CurrentHeight_() * _style.line_spacing, "%s", condText);
	}
}

#include "DestroyablePhysicsObject.h"
bool CPickUpManager::CanPickItem(const CFrustum& frustum, const Fvector& from, CObject* item)
{
	if (!item->getVisible())
		return false;

	bool bOverlaped = false;
	Fvector dir, to;
	item->Center(to);
	float range = dir.sub(to, from).magnitude();
	if (range > 0.25f)
	{
		if (frustum.testSphere_dirty(to, item->Radius()))
		{
			dir.div(range);

			collide::ray_defs RD(from, dir, range, CDB::OPT_CULL, collide::rqtBoth);
			VERIFY(!fis_zero(RD.dir.square_magnitude()));

			RQR.r_clear();
			Level().ObjectSpace.RayQuery(RQR, RD, [](collide::rq_result& result, LPVOID params) -> bool
			{
				bool& bOverlaped = *(bool*)params;
				if (result.O)
				{
					if (Level().CurrentEntity() == result.O)
					{ //ignore self-actor
						return true;
					}
					else
					{ //check obstacle flag
						if ((result.O->SpatialComponent->type & ESPATIAL_TYPE::OBSTACLE) != ESPATIAL_TYPE::NONE)
							bOverlaped = true;

						return true;
					}
				}
				else
				{
					//получить треугольник и узнать его материал
					CDB::TRI& T = Level().ObjectSpace.GetStaticTris()[result.element];
					if (GMLib.GetMaterialByIdx(T.material)->Flags.is(SGameMtl::flPassable))
						return true;
				}

				bOverlaped = true;
				return false;
			}, &bOverlaped, nullptr, item);

			for (collide::rq_result& result : RQR.r_results())
			{
				CGameObject* GO = result.O != nullptr ? result.O->cast_game_object() : nullptr;
				if (GO == nullptr)
				{
					continue;
				}

				if (GO == Owner->cast_game_object())
				{
					continue;
				}

				if (GO->cast_inventory_item())
				{
					continue;
				}

				CEntity* entity = GO->cast_entity();
				if (entity != nullptr && !entity->g_Alive())
				{
					continue;
				}

				CDestroyablePhysicsObject* dstobj = smart_cast<CDestroyablePhysicsObject*>(GO);
				if (dstobj != nullptr && dstobj->HasChildPart())
				{
					continue;
				}

				if (GO->spawn_ini() && GO->spawn_ini()->section_exist("story_object"))
				{
					continue;
				}

				return false;
			}
		}
		else
			return false;
	}
	else
		return false;

	return !bOverlaped;
}

LPCSTR CPickUpManager::GetKeyName() const
{
	int dik = get_action_dik(kUSE);
	return dik > 0 ? dik_to_keyname(dik) : nullptr;
}

u32 CPickUpManager::GetConditionColor(float condition) const
{
	clamp(condition, 0.0f, 1.0f);

	Fcolor c1, c2, result;
	c1.set(_style.condition_color_min);
	c2.set(_style.condition_color_max);
	result.lerp(c1, c2, condition);

	return result.get();
}

void CPickUpManager::GroupItems(xr_vector<CObject*>& items, xr_vector<SPickupItemGroup>& out_groups) const
{
	out_groups.clear();
	for (CObject* obj : items)
	{
		CInventoryItem* item = obj->cast_inventory_item();
		if (!item)
			continue;

		shared_str section = item->m_section_id.size() ? item->m_section_id : obj->cNameSect();
		Fvector2 pos = GetScreenPos(obj);
		if (pos.x < 0)
			continue;

		bool found = false;
		for (auto& group : out_groups)
		{
			if (group.section_name != section)
				continue;
			Fvector2 groupPos = GetScreenPos(group.items[0]);
			if (groupPos.x >= 0 && pos.distance_to(groupPos) <= _style.group_distance)
			{
				group.items.push_back(obj);
				found = true;
				break;
			}
		}
		if (!found)
		{
			SPickupItemGroup group;
			group.section_name = section;
			group.representative_id = obj->ID();
			group.items.push_back(obj);
			out_groups.push_back(group);
		}
	}
}

void CPickUpManager::LoadStylesFromXML()
{
	CUIXml uiXml;
	if (!uiXml.Load(CONFIG_PATH, UI_PATH, "backend\\pickup_info.xml") || !uiXml.NavigateToNode("pickup_info", 0))
		return;

	LPCSTR path = "pickup_info";
	u32 color = _style.color;
	CGameFont* pFont = nullptr;
	if (CUIXmlInit::InitFont(uiXml, path, 0, color, pFont) && pFont)
	{
		_style.font = pFont;
		_style.color = color;
	}

	LPCSTR align = uiXml.ReadAttrib(path, 0, "align_name", nullptr);
	if (align)
	{
		if (!xr_strcmp(align, "left"))
			_style.align_name = CGameFont::alLeft;
		else if (!xr_strcmp(align, "right"))
			_style.align_name = CGameFont::alRight;
		else
			_style.align_name = CGameFont::alCenter;
	}

	align = uiXml.ReadAttrib(path, 0, "align_info", nullptr);
	if (align)
	{
		if (!xr_strcmp(align, "left"))
			_style.align_info = CGameFont::alLeft;
		else if (!xr_strcmp(align, "right"))
			_style.align_info = CGameFont::alRight;
		else
			_style.align_info = CGameFont::alCenter;
	}

	_style.lerp_speed = uiXml.ReadAttribFlt(path, 0, "lerp_speed", _style.lerp_speed);
	_style.line_spacing = uiXml.ReadAttribFlt(path, 0, "line_spacing", _style.line_spacing);
	_style.group_distance = uiXml.ReadAttribFlt(path, 0, "group_distance", _style.group_distance);
	_style.show_key_binding = uiXml.ReadAttribInt(path, 0, "show_key", _style.show_key_binding) != 0;
	_style.group_same_items = uiXml.ReadAttribInt(path, 0, "group_same_items", _style.group_same_items) != 0;
	_style.show_condition = uiXml.ReadAttribInt(path, 0, "show_condition", _style.show_condition) != 0;

	int r_min = uiXml.ReadAttribInt(path, 0, "condition_color_min_r", -1);
	int g_min = uiXml.ReadAttribInt(path, 0, "condition_color_min_g", -1);
	int b_min = uiXml.ReadAttribInt(path, 0, "condition_color_min_b", -1);
	int a_min = uiXml.ReadAttribInt(path, 0, "condition_color_min_a", -1);
	if (r_min >= 0 && g_min >= 0 && b_min >= 0 && a_min >= 0)
		_style.condition_color_min = color_argb(a_min, r_min, g_min, b_min);

	int r_max = uiXml.ReadAttribInt(path, 0, "condition_color_max_r", -1);
	int g_max = uiXml.ReadAttribInt(path, 0, "condition_color_max_g", -1);
	int b_max = uiXml.ReadAttribInt(path, 0, "condition_color_max_b", -1);
	int a_max = uiXml.ReadAttribInt(path, 0, "condition_color_max_a", -1);
	if (r_max >= 0 && g_max >= 0 && b_max >= 0 && a_max >= 0)
		_style.condition_color_max = color_argb(a_max, r_max, g_max, b_max);

	clamp(_style.lerp_speed, 0.0f, 1.0f);
	clamp(_style.line_spacing, 0.5f, 3.0f);
	clamp(_style.group_distance, 0.0f, 1000.0f);
}
