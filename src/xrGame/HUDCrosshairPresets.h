#pragma once

#include "../xrCore/_stl_extensions.h"

struct SCrosshairPreset
{
	shared_str id;
	shared_str texture;
	float base_size_perc = 0.035f;
	float disp_scale = 0.5f;
	bool scale_with_dispersion = true;
	float min_radius_perc = 0.0f;
	float max_radius_perc = 1.0f;
	u32 color = 0xE6FFFFFF;

	SCrosshairPreset() = default;
};

class CHUDCrosshairPresets
{
	static xr_map<shared_str, SCrosshairPreset> s_presets;
	static bool s_loaded;

public:
	static bool Load();
	static const SCrosshairPreset* Get(LPCSTR id);
	static bool Has(LPCSTR id) { return Get(id) != nullptr; }
};
