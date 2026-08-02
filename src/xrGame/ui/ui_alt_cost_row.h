#pragma once

#include "../trade_item_cost.h"
#include "../../xrUI/Widgets/UIWindow.h"

class CUIStatic;
class CGameFont;
class CUIXml;

struct SAltCostRowLayout
{
	float icon_size = 20.f;
	float item_gap = 6.f;
	float count_offset_y = -6.f;
	float inner_pad = 1.f;

	void LoadFromXml(CUIXml& uiXml, LPCSTR path, int index = 0);
};

struct SAltCostIconSlot
{
	CUIStatic* bracket_l = nullptr;
	CUIStatic* icon = nullptr;
	CUIStatic* count = nullptr;
	CUIStatic* bracket_r = nullptr;
};

class CUIAltCostRowHelper
{
public:
	static float Update(
		CUIWindow* container,
		xr_vector<SAltCostIconSlot>& slots,
		const STradeBuyRequirements& req,
		const SAltCostRowLayout& layout,
		CGameFont* font);
};
