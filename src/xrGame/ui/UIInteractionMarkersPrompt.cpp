#include "StdAfx.h"
#include "UIInteractionMarkers.h"
#include "UIInteractionMarkers_internal.h"

#include <algorithm>

#include "../Actor.h"
#include "../Level.h"
#include "../GameObject.h"
#include "../inventory_item.h"
#include "../InventoryOwner.h"
#include "../UsableScriptObject.h"
#include "../entity_alive.h"
#include "../monster_community.h"
#include "../ai/stalker/ai_stalker.h"
#include "../ZoneCampfire.h"
#include "../xrEngine/xr_level_controller.h"
#include "../xrEngine/string_table.h"
#include "../xrCore/_color.h"
#include "../../xrUI/UITextureMaster.h"
#include "../../xrUI/ui_base.h"

namespace
{
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

	LPCSTR TryWsuiCreatureString(LPCSTR section)
	{
		if (!section || !section[0])
			return nullptr;

		string64 key;
		xr_sprintf(key, "ui_st_wsui_creature_%s", section);
		LPCSTR translated = WSUIInternal::WsuiString(key);
		if (translated && translated[0] && xr_strcmp(translated, key) != 0)
			return translated;

		return nullptr;
	}

	static const LPCSTR g_creature_rank_suffixes[] = { "_strong", "_weak", "_normal", nullptr };

	LPCSTR ResolveCreatureSpeciesName(LPCSTR section)
	{
		if (LPCSTR name = TryWsuiCreatureString(section))
			return name;

		if (!section || !section[0])
			return nullptr;

		for (u32 i = 0; g_creature_rank_suffixes[i]; ++i)
		{
			LPCSTR suffix = g_creature_rank_suffixes[i];
			const size_t slen = xr_strlen(section);
			const size_t xlen = xr_strlen(suffix);
			if (slen > xlen && !xr_strcmp(section + slen - xlen, suffix))
			{
				string64 base;
				xr_strcpy(base, sizeof(base), section);
				base[slen - xlen] = 0;
				if (LPCSTR name = TryWsuiCreatureString(base))
					return name;
			}
		}

		return nullptr;
	}

	LPCSTR BodyDisplayName(CGameObject* obj)
	{
		if (!obj)
			return nullptr;

		if (CInventoryOwner* owner = obj->cast_inventory_owner())
		{
			LPCSTR owner_name = owner->Name();
			LPCSTR spawn_name = obj->cName().c_str();
			if (owner_name && owner_name[0] && xr_strcmp(owner_name, spawn_name) != 0)
				return owner_name;
		}

		LPCSTR section = obj->cNameSect().c_str();
		if (!section || !section[0])
			return nullptr;

		if (LPCSTR translated = WSUIInternal::WsuiString(section))
		{
			if (xr_strcmp(translated, section) != 0)
				return translated;
		}

		if (pSettings->line_exist(section, "inv_name"))
		{
			LPCSTR inv_id = pSettings->r_string(section, "inv_name");
			if (LPCSTR translated = WSUIInternal::WsuiString(inv_id))
				return translated;
		}

		if (pSettings->line_exist(section, "community"))
		{
			LPCSTR comm = pSettings->r_string(section, "community");
			if (LPCSTR translated = WSUIInternal::WsuiString(comm))
			{
				if (xr_strcmp(translated, comm) != 0)
					return translated;
			}
		}

		if (LPCSTR species = ResolveCreatureSpeciesName(section))
			return species;

		if (CEntityAlive* alive = obj->cast_entity_alive())
		{
			if (alive->monster_community && alive->monster_community->id().size())
			{
				LPCSTR comm = alive->monster_community->id().c_str();
				if (LPCSTR translated = WSUIInternal::WsuiString(comm))
				{
					if (xr_strcmp(translated, comm) != 0)
						return translated;
				}
			}
		}

		return nullptr;
	}

	bool IsUseKeyPressed()
	{
		if (!pInput)
			return false;

		const int key1 = get_action_dik(kUSE, 0);
		const int key2 = get_action_dik(kUSE, 1);
		return (key1 > 0 && pInput->iGetAsyncKeyState(key1)) || (key2 > 0 && pInput->iGetAsyncKeyState(key2));
	}
}

u32 CInteractionMarkerManager::ColorWithAlpha(u32 color, float alpha) const
{
	const u32 a = u32(clampr(alpha, 0.f, 1.f) * 255.f);
	return (color & 0x00ffffff) | (a << 24);
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
	if (!IsPromptStackCountEnabled(focus_marker.category_id))
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
		if (!IsPromptStackCountEnabled(marker.category_id) || !marker.visible || !marker.reachable)
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

const SWSUITextureSlot* CInteractionMarkerManager::ResolveKeyBindIcon(int dik, float& out_w, float& out_h) const
{
	auto it = m_dik_icons.find(dik);
	if (it == m_dik_icons.end() || !it->second.HasDrawable())
		return nullptr;

	out_w = m_prompt_cfg.main_panel.keybind.icon_width;
	out_h = m_prompt_cfg.main_panel.keybind.icon_height;
	return &it->second;
}

LPCSTR CInteractionMarkerManager::GetKeyName() const
{
	const int dik = get_action_dik(kUSE);
	return dik > 0 ? dik_to_keyname(dik) : nullptr;
}

bool CInteractionMarkerManager::ResolvePromptName(CGameObject* game_object, shared_str name_source, string256& out) const
{
	out[0] = 0;
	if (!game_object)
		return false;

	if (WSUIInternal::WsuiCategoryEq(name_source, "none"))
		return true;

	if (WSUIInternal::WsuiCategoryEq(name_source, "item_name"))
	{
		if (CInventoryItem* item = game_object->cast_inventory_item())
		{
			if (LPCSTR name = ItemDisplayName(item))
			{
				xr_strcpy(out, name);
				return true;
			}
		}
		return false;
	}

	if (WSUIInternal::WsuiCategoryEq(name_source, "stalker_name"))
	{
		if (CAI_Stalker* stalker = game_object->cast_stalker())
		{
			if (LPCSTR name = stalker->Name())
			{
				xr_strcpy(out, name);
				return true;
			}
		}
		return false;
	}

	if (WSUIInternal::WsuiCategoryEq(name_source, "body_name"))
	{
		if (LPCSTR name = BodyDisplayName(game_object))
		{
			xr_strcpy(out, name);
			return true;
		}
		return false;
	}

	return false;
}

bool CInteractionMarkerManager::BuildPromptByMode(CActor* actor, CGameObject* game_object, const SWSUICategoryDef& category, const SInteractionMarker& marker, SWSUIPromptParts& out) const
{
	if (WSUIInternal::WsuiCategoryEq(category.prompt_mode, "split"))
	{
		if (category.verb_id.size())
			xr_strcpy(out.verb, WSUIInternal::WsuiString(category.verb_id.c_str()));

		ResolvePromptName(game_object, category.name_source, out.name);

		if (IsPromptStackCountEnabled(marker.category_id))
		{
			const u32 count = CountGroupedItemMarkers(marker);
			if (count > 1 && out.name[0])
			{
				string256 name_copy;
				xr_strcpy(name_copy, out.name);
				xr_sprintf(out.name, sizeof(out.name), "%s x%u", name_copy, count);
			}
		}

		if (IsPromptConditionEnabled(category))
		{
			if (CInventoryItem* item = game_object->cast_inventory_item())
			{
				if (item->IsUsingCondition())
				{
					out.show_condition = true;
					out.item_condition = item->GetCondition();
				}
			}
		}

		out.split = true;
		return out.verb[0] != 0;
	}

	if (WSUIInternal::WsuiCategoryEq(category.prompt_mode, "usable_tip"))
	{
		if (CUsableScriptObject* usable = game_object->cast_usable_script_object())
		{
			if (usable->tip_text() && usable->tip_text()[0])
			{
				xr_strcpy(out.full, g_pStringTable->translate(usable->tip_text()).c_str());
				return out.full[0] != 0;
			}
		}
		return false;
	}

	if (WSUIInternal::WsuiCategoryEq(category.prompt_mode, "zone_lookup"))
	{
		if (LPCSTR prompt = ResolveZonePrompt(game_object))
		{
			xr_strcpy(out.full, g_pStringTable->translate(prompt).c_str());
			return out.full[0] != 0;
		}
		return false;
	}

	if (WSUIInternal::WsuiCategoryEq(category.prompt_mode, "campfire"))
	{
		if (CZoneCampfire* campfire = game_object->cast_zone_campfire())
		{
			LPCSTR key = campfire->is_on() ? category.campfire_on_tip.c_str() : category.campfire_off_tip.c_str();
			if (key && key[0])
			{
				xr_strcpy(out.full, g_pStringTable->translate(key).c_str());
				return out.full[0] != 0;
			}
		}
		return false;
	}

	if (WSUIInternal::WsuiCategoryEq(category.prompt_mode, "full"))
	{
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

	return false;
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

	const SWSUICategoryDef* cat = GetCategory(marker.category_id);
	if (!cat)
		return false;

	if (BuildPromptByMode(actor, game_object, *cat, marker, out))
		return true;

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

const SUIOutlineParams& CInteractionMarkerManager::ResolveTextShadow(const SUIOutlineParams& label_shadow) const
{
	if (label_shadow.enabled)
		return label_shadow;
	return m_prompt_cfg.default_text_shadow;
}

void CInteractionMarkerManager::DrawPromptText(CGameFont* font, float x, float y, float kx, float ky, LPCSTR text, u32 color, CGameFont::EAligment align, const SUIOutlineParams& label_shadow) const
{
	if (!font || !text || !text[0])
		return;

	font->SetAligment(align);

	const SUIOutlineParams& shadow = ResolveTextShadow(label_shadow);
	const u32 textAlpha = color_get_A(color);
	u32 outlineColor = 0;
	if (shadow.enabled && shadow.thickness > 0.f && textAlpha > 0)
	{
		const u32 outlineAlpha = (color_get_A(shadow.color) * textAlpha) / 255;
		if (outlineAlpha > 0)
			outlineColor = subst_alpha(shadow.color, outlineAlpha);
	}

	if (outlineColor != 0)
	{
		for (const Fvector2& d : UIOutline::kDirs8)
		{
			font->SetColor(outlineColor);
			font->Out(x + d.x * shadow.thickness * kx, y + d.y * shadow.thickness * ky, "%s", text);
		}
	}

	font->SetColor(color);
	font->Out(x, y, "%s", text);
	font->OnRender();
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
	const SWSUITextureSlot* bind_icon = (show_keybind && use_dik > 0) ? ResolveKeyBindIcon(use_dik, bind_icon_w, bind_icon_h) : nullptr;
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

	DrawTextureSlot(bg.texture, drop_cx, drop_cy, drop_w, drop_h, ColorWithAlpha(bg.color, alpha), false);

	float cursor_x = drop_cx - drop_w * 0.5f + m_prompt_cfg.text_pad * scale;

	if (show_keybind)
	{
		const float key_cx = cursor_x + key_w * 0.5f;
		if (bind_icon)
		{
			DrawTextureSlot(*bind_icon, key_cx, content_cy, key_w, key_h, ColorWithAlpha(0xFFFFFFFF, alpha), true);
		}
		else
		{
			const SWSUITextureSlot& key_tex = (kb.pressed_texture.HasDrawable() && IsUseKeyPressed())
				? kb.pressed_texture
				: kb.texture;
			DrawTextureSlot(key_tex, key_cx, content_cy, key_w, key_h, ColorWithAlpha(0xFFFFFFFF, alpha), true);

			const float font_h = key_font->CurrentHeight_() * m_prompt_cfg.font_scale_h;
			DrawPromptText(key_font,
				(key_cx + kb.label.x * scale) * kx,
				content_cy * ky - font_h * 0.5f + kb.label.y * scale,
				kx, ky, key_name,
				ColorWithAlpha(kb.label.color, alpha),
				CGameFont::alCenter, kb.label.text_shadow);
		}
		cursor_x += key_w + gap;
	}

	const float action_x = parts.split ? at.verb.x : at.full_line.x;
	const float action_y = parts.split ? at.verb.y : at.full_line.y;
	const float text_y = (content_cy - text_h * 0.5f + action_y * scale) * ky;
	const float text_x = (cursor_x + action_x * scale) * kx;

	if (parts.split)
	{
		DrawPromptText(verb_font, text_x, text_y, kx, ky, parts.verb,
			ColorWithAlpha(at.verb.color, alpha), CGameFont::alLeft, at.verb.text_shadow);

		float segment_x = text_x + verb_font->WidthOf(parts.verb) * m_prompt_cfg.font_scale_w;
		if (parts.name[0])
		{
			segment_x += verb_font->WidthOf(" ") * m_prompt_cfg.font_scale_w;
			DrawPromptText(name_font, segment_x, text_y, kx, ky, parts.name,
				ColorWithAlpha(at.object_name.color, alpha), CGameFont::alLeft, at.object_name.text_shadow);
		}
	}
	else
	{
		DrawPromptText(full_font, text_x, text_y, kx, ky, parts.full,
			ColorWithAlpha(at.full_line.color, alpha), CGameFont::alLeft, at.full_line.text_shadow);
	}

	if (parts.show_condition && m_prompt_cfg.features.item_condition)
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

			if (cond_cfg.background.enabled && cond_cfg.background.texture.HasDrawable())
			{
				const float text_w_ui = PromptTextWidth(cond_font, cond_text, kx);
				const float text_h_ui = cond_h / ky;
				const float pad = cond_cfg.background.pad * scale;
				const float cond_drop_w = (cond_cfg.background.width > 0.f ? cond_cfg.background.width : text_w_ui + pad * 2.f) * scale;
				const float drop_h_cond = (cond_cfg.background.height > 0.f ? cond_cfg.background.height : text_h_ui + pad * 2.f) * scale;
				const float cond_drop_cx = cond_ui_x + cond_drop_w * 0.5f;
				const float cond_drop_cy = cond_ui_y + drop_h_cond * 0.5f;
				const u32 drop_color = cond_cfg.background.color;
				DrawTextureSlot(cond_cfg.background.texture, cond_drop_cx, cond_drop_cy, cond_drop_w, drop_h_cond, ColorWithAlpha(drop_color, alpha), false);
			}

			DrawPromptText(cond_font, cond_x, cond_y + cond_h * (cond_cfg.line_spacing - 1.f), kx, ky, cond_text,
				ColorWithAlpha(GetItemConditionColor(parts.item_condition), alpha),
				CGameFont::alLeft, cond_cfg.label.text_shadow);
		}
	}

	if (marker && IsPromptItemCardEnabled(marker->category_id))
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
	const bool has_weight = card.weight.icon.HasDrawable();
	const bool has_value = card.value.icon.HasDrawable() && item->IsDrawCost();
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

	auto metric_text_height = [&](const SWSUIItemCardMetric& metric, CGameFont* font) -> float
	{
		if (metric.text_height > 0.f)
			return metric.text_height;
		if (font)
			return font->CurrentHeight_() * m_prompt_cfg.font_scale_h / ky;
		return 12.f;
	};

	auto metric_bottom = [&](const SWSUIItemCardMetric& metric, LPCSTR text, CGameFont* font) -> float
	{
		const float text_h = metric_text_height(metric, font);
		return metric.y + std::max(metric.icon_y + metric.icon_h, metric.text_y + text_h);
	};

	float content_right = 0.f;
	float content_bottom = 0.f;
	if (has_weight)
	{
		content_right = std::max(content_right, metric_right(card.weight, weight_str, card.weight.font));
		content_bottom = std::max(content_bottom, metric_bottom(card.weight, weight_str, card.weight.font));
	}
	if (has_value)
	{
		content_right = std::max(content_right, metric_right(card.value, cost_str, card.value.font));
		content_bottom = std::max(content_bottom, metric_bottom(card.value, cost_str, card.value.font));
	}

	const float drop_h = (card.background.height > 0.f ? card.background.height : content_bottom + 2.f) * scale;
	const float drop_w = (card.background.width > 0.f ? card.background.width : content_right + 4.f) * scale;
	const float drop_cx = origin_x + drop_w * 0.5f;
	const float drop_cy = origin_y + drop_h * 0.5f;

	if (card.background.texture.HasDrawable())
		DrawTextureSlot(card.background.texture, drop_cx, drop_cy, drop_w, drop_h, ColorWithAlpha(card.background.color, alpha), false);

	auto draw_metric = [&](const SWSUIItemCardMetric& metric, LPCSTR text)
	{
		if (!metric.icon.HasDrawable())
			return;

		CGameFont* font = metric.font ? metric.font : (g_FontManager ? g_FontManager->pFontSystem : nullptr);
		const float mx = origin_x + metric.x * scale;
		const float my = origin_y + metric.y * scale;
		const float iw = metric.icon_w * scale;
		const float ih = metric.icon_h * scale;
		const float icon_cx = mx + (metric.icon_x + metric.icon_w * 0.5f) * scale;
		const float icon_cy = my + (metric.icon_y + metric.icon_h * 0.5f) * scale;

		DrawTextureSlot(metric.icon, icon_cx, icon_cy, iw, ih, ColorWithAlpha(0xFFFFFFFF, alpha), true);

		if (text && text[0] && font)
		{
			DrawPromptText(font,
				(mx + metric.text_x * scale) * kx, (my + metric.text_y * scale) * ky,
				kx, ky, text, ColorWithAlpha(metric.color, alpha),
				CGameFont::alLeft, metric.text_shadow);
		}
	};

	if (has_weight)
		draw_metric(card.weight, weight_str);
	if (has_value)
		draw_metric(card.value, cost_str);
}

void CInteractionMarkerManager::RenderPrompt(CActor* actor) const
{
	if (!actor || m_focus.prompt_focus_id == 0xffff || m_prompt_fade.alpha <= 0.01f)
		return;

	auto it = m_markers.find(m_focus.prompt_focus_id);
	if (it == m_markers.end() || !it->second.visible || !it->second.reachable)
		return;

	if (it->second.distance > m_markers_cfg.scan.prompt_distance)
		return;

	SWSUIPromptParts parts;
	if (!BuildPromptParts(actor, it->second, parts))
		return;

	LPCSTR key_name = GetKeyName();
	const float alpha = m_prompt_fade.alpha;
	const float scale = UiScale();

	if (m_prompt_cfg.features.fixed_screen)
	{
		RenderPromptBubble(m_prompt_cfg.features.fixed_x * scale, m_prompt_cfg.features.fixed_y * scale, parts, key_name, alpha, true, &it->second);
		return;
	}

	const float marker_x = it->second.screen_pos.x + m_prompt_cfg.anchor_x * scale;
	const float marker_y = it->second.screen_pos.y + m_prompt_cfg.anchor_y * scale;
	const float dot_h = SquareHeight(m_markers_cfg.dot.size * scale);
	const float anchor_y = marker_y + dot_h * 0.5f + m_prompt_cfg.text_pad * scale;

	RenderPromptBubble(marker_x, anchor_y, parts, key_name, alpha, false, &it->second);
}

void CInteractionMarkerManager::RenderTutorialPrompt() const
{
	if (m_tutorial_fade.alpha <= 0.01f)
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

	const float alpha = m_tutorial_fade.alpha;
	const float scale = UiScale();
	const float cx = m_prompt_cfg.tutorial_x * scale;
	const float cy = m_prompt_cfg.tutorial_y * scale;

	RenderPromptBubble(cx, cy, parts, GetKeyName(), alpha, true);
}
