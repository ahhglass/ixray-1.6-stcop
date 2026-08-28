#include "StdAfx.h"
#include "HUDCrosshairPresets.h"
#include "../../xrUI/UIXmlInit.h"

xr_map<shared_str, SCrosshairPreset> CHUDCrosshairPresets::s_presets;
bool CHUDCrosshairPresets::s_loaded = false;

bool CHUDCrosshairPresets::Load()
{
	if (s_loaded)
		return !s_presets.empty();

	s_loaded = true;
	s_presets.clear();

	CUIXml xml;
	if (!xml.Load(CONFIG_PATH, UI_PATH, "backend\\crosshairs.xml"))
	{
		Msg("! [HUDCrosshairPresets] failed to load backend\\crosshairs.xml");
		return false;
	}

	XML_NODE* root = xml.GetRoot();
	if (!root)
		return false;

	for (XML_NODE* child = root->FirstChildElement(); child; child = child->NextSiblingElement())
	{
		LPCSTR id = child->Value();
		if (!id || !id[0])
			continue;

		SCrosshairPreset preset;
		preset.id = id;

		LPCSTR texture = xml.ReadAttrib(child, "texture", nullptr);
		if (texture && texture[0])
			preset.texture = texture;

		preset.base_size_perc = xml.ReadAttribFlt(child, "base_size", preset.base_size_perc);
		preset.disp_scale = xml.ReadAttribFlt(child, "disp_scale", preset.disp_scale);
		preset.scale_with_dispersion = xml.ReadAttribInt(child, "scale_with_dispersion", preset.scale_with_dispersion ? 1 : 0) != 0;
		preset.min_radius_perc = xml.ReadAttribFlt(child, "min_radius", preset.min_radius_perc);
		preset.max_radius_perc = xml.ReadAttribFlt(child, "max_radius", preset.max_radius_perc);

		int r = xml.ReadAttribInt(child, "r", -1);
		int g = xml.ReadAttribInt(child, "g", -1);
		int b = xml.ReadAttribInt(child, "b", -1);
		int a = xml.ReadAttribInt(child, "a", -1);
		if (r >= 0 && g >= 0 && b >= 0 && a >= 0)
			preset.color = color_argb(a, r, g, b);

		if (preset.texture.size() == 0)
		{
			Msg("! [HUDCrosshairPresets] preset '%s' has no texture", id);
			continue;
		}

		s_presets[preset.id] = preset;
	}

	Msg("[HUDCrosshairPresets] loaded %u preset(s)", (u32)s_presets.size());
	return !s_presets.empty();
}

const SCrosshairPreset* CHUDCrosshairPresets::Get(LPCSTR id)
{
	if (!id || !id[0])
		return nullptr;

	if (!s_loaded)
		Load();

	auto it = s_presets.find(id);
	return it != s_presets.end() ? &it->second : nullptr;
}
