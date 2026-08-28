#pragma once
#include "../xrCore/_stl_extensions.h"
#include "../xrCore/_vector2.h"

class CActor;
class CUIXml;
class CGameFont;
class CObject;
class CFrustum;

struct SPickupItemGroup
{
	shared_str section_name;
	xr_vector<CObject*> items;
	u16 representative_id;

	SPickupItemGroup() : representative_id(0) {}
};

struct SPickupTextStyle
{
	CGameFont* font;
	u32 color;
	CGameFont::EAligment align_name, align_info;
	float lerp_speed, line_spacing, group_distance;
	bool show_key_binding, group_same_items, show_condition;
	u32 condition_color_min, condition_color_max;

	SPickupTextStyle() : font(nullptr), color(0xFFDDDDDD), align_name(CGameFont::alCenter), align_info(CGameFont::alCenter),
		lerp_speed(0.15f), line_spacing(1.2f), group_distance(50.0f),
		show_key_binding(true), group_same_items(true), show_condition(false),
		condition_color_min(0xFFFF0000), condition_color_max(0xFF00FF00) {}
};

class CPickUpManager
{
	CActor* Owner;
	float PickupInfoRadius;
	bool PickupMode;
	collide::rq_results RQR;

	xr_map<u16, Fvector2> _textPositions;
	SPickupTextStyle _style;

public:
	CPickUpManager(CActor* Owner);

	void RenderInfo();
	void LoadStylesFromXML();
	bool CanPickItem(const CFrustum& frustum, const Fvector& from, CObject* item);

	IC void SetPickupRadius(float Radius) { PickupInfoRadius = Radius; }
	IC void SetPickupMode(bool State) { PickupMode = State; }
	IC bool GetPickupMode() const { return PickupMode; }

private:
	void PickupInfoDraw(CObject* object, u32 item_count = 1);
	Fvector2 GetScreenPos(CObject* obj) const;
	LPCSTR GetKeyName() const;
	u32 GetConditionColor(float condition) const;
	void GroupItems(xr_vector<CObject*>& items, xr_vector<SPickupItemGroup>& out_groups) const;
};
