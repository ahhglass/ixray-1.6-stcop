#include "StdAfx.h"
#include "ui_alt_cost_row.h"

#include "../../xrUI/UIXmlInit.h"
#include "../../xrUI/Widgets/UIStatic.h"
#include "../../xrUI/ui_base.h"
#include "../../xrUI/uiabstract.h"
#include "UIInventoryUtilities.h"

void SAltCostRowLayout::LoadFromXml(CUIXml& uiXml, LPCSTR path, int index)
{
	icon_size = uiXml.ReadAttribFlt(path, index, "icon_size", icon_size);
	item_gap = uiXml.ReadAttribFlt(path, index, "item_gap", item_gap);
	count_offset_y = uiXml.ReadAttribFlt(path, index, "count_offset_y", count_offset_y);
	inner_pad = uiXml.ReadAttribFlt(path, index, "inner_pad", inner_pad);
}

namespace
{
	CUIStatic* CreateBracketStatic(CUIWindow* parent, LPCSTR text, CGameFont* font)
	{
		CUIStatic* st = new CUIStatic();
		st->SetAutoDelete(true);
		parent->AttachChild(st);
		st->SetText(text);
		if (font)
		{
			st->SetFont(font);
		}
		st->AdjustWidthToText();
		st->TextItemControl()->SetVTextAlignment(valCenter);
		return st;
	}

	void AlignBracketToRow(CUIStatic* bracket, float x, float row_y, float row_h, CGameFont* font)
	{
		if (!bracket)
		{
			return;
		}

		if (font)
		{
			bracket->SetFont(font);
		}

		bracket->AdjustWidthToText();
		bracket->TextItemControl()->SetVTextAlignment(valCenter);
		bracket->SetHeight(row_h);
		bracket->SetWndPos(Fvector2().set(x, row_y));
	}

	void SetItemIconOnStatic(CUIStatic* st, LPCSTR section, float icon_size)
	{
		if (!st || !section || !section[0] || !pSettings->section_exist(section))
		{
			return;
		}

		const auto params = InventoryUtilities::GetInventoryIconParams(section);
		Frect texture_rect;
		texture_rect.x1 = params.inv_grid_x * INV_GRID_WIDTH(params.scaleIcon);
		texture_rect.y1 = params.inv_grid_y * INV_GRID_HEIGHT(params.scaleIcon);
		texture_rect.x2 = params.inv_grid_width * INV_GRID_WIDTH(params.scaleIcon);
		texture_rect.y2 = params.inv_grid_height * INV_GRID_HEIGHT(params.scaleIcon);
		texture_rect.rb.add(texture_rect.lt);

		st->GetUIStaticItem().SetTextureRect(texture_rect);
		st->SetStretchTexture(true);
		st->SetShader(InventoryUtilities::GetEquipmentIconsShader(params.icons_texture));
		st->TextureOn();
		st->SetWidth(icon_size * UI().get_current_kx());
		st->SetHeight(icon_size);
	}

	void EnsureSlotCount(CUIWindow* container, xr_vector<SAltCostIconSlot>& slots, u32 needed, CGameFont* font)
	{
		while (slots.size() < needed)
		{
			SAltCostIconSlot slot;
			slot.bracket_l = CreateBracketStatic(container, "[", font);
			slot.icon = new CUIStatic();
			slot.icon->SetAutoDelete(true);
			container->AttachChild(slot.icon);

			slot.count = new CUIStatic();
			slot.count->SetAutoDelete(true);
			container->AttachChild(slot.count);

			slot.bracket_r = CreateBracketStatic(container, "]", font);
			slots.push_back(slot);
		}
	}
}

float CUIAltCostRowHelper::Update(
	CUIWindow* container,
	xr_vector<SAltCostIconSlot>& slots,
	const STradeBuyRequirements& req,
	const SAltCostRowLayout& layout,
	CGameFont* font)
{
	if (!container)
	{
		return 0.f;
	}

	const u32 needed = req.has_item_cost ? req.items.size() : 0;
	EnsureSlotCount(container, slots, needed, font);

	float total_width = 0.f;
	for (u32 i = 0; i < slots.size(); ++i)
	{
		const bool show = i < needed;
		SAltCostIconSlot& slot = slots[i];
		slot.bracket_l->Show(show);
		slot.icon->Show(show);
		slot.count->Show(show);
		slot.bracket_r->Show(show);

		if (!show)
		{
			continue;
		}

		const STradeItemCostEntry& entry = req.items[i];
		SetItemIconOnStatic(slot.icon, entry.section.c_str(), layout.icon_size);

		string32 count_text = {};
		xr_sprintf(count_text, "x%u", entry.count);
		slot.count->SetText(count_text);
		if (font)
		{
			slot.count->SetFont(font);
		}
		slot.count->AdjustWidthToText();

		float x = total_width;
		const float row_y = 0.f;
		const float row_h = layout.icon_size;

		AlignBracketToRow(slot.bracket_l, x, row_y, row_h, font);
		x += slot.bracket_l->GetWndSize().x + layout.inner_pad;

		slot.icon->SetWndPos(Fvector2().set(x, row_y));
		slot.count->SetWndPos(Fvector2().set(
			x + layout.icon_size - slot.count->GetWndSize().x + layout.inner_pad,
			row_y + layout.count_offset_y));
		x += layout.icon_size + layout.inner_pad;

		AlignBracketToRow(slot.bracket_r, x, row_y, row_h, font);
		x += slot.bracket_r->GetWndSize().x + layout.item_gap;

		total_width = x;
	}

	container->Show(needed > 0);
	return needed > 0 ? total_width : 0.f;
}
