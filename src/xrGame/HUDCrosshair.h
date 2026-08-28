#pragma once

#define HUD_CURSOR_SECTION "hud_cursor"

#include "../../xrUI/ui_defs.h"

struct SCrosshairPreset;

class CHUDCrosshair
{
private:
	float cross_length_perc;
	float min_radius_perc;
	float max_radius_perc;

	float radius;
	float target_radius;
#ifdef DEBUG
	float fb_radius;
#endif
	ui_shader hShader;
	ui_shader hTextureShader;

	shared_str m_current_preset_id;
	const SCrosshairPreset* m_active_preset;
	xr_map<shared_str, ui_shader> m_texture_shaders;

	static bool s_use_custom_crosshairs;
	static shared_str s_idle_preset_id;

public:
	u32 cross_color;

	CHUDCrosshair();
	~CHUDCrosshair();

	void OnRender();
	void OnRenderLines();
	void OnRenderTexture(const SCrosshairPreset& preset);
	void OnRenderDot(float cx, float cy, float size_px, u32 color);
	bool IsIdleDotActive() const;
	void SetDispersion(float disp);
	void SetPreset(LPCSTR preset_id);
	static bool UseCustomCrosshairs() { return s_use_custom_crosshairs; }
	static bool HasIdlePreset();
	static LPCSTR GetIdlePresetId();
	static bool ShouldUseIdleCrosshair(bool has_weapon, bool weapon_use_crosshair, bool weapon_holstered, bool has_bolt, bool device_in_hand);

#ifdef DEBUG
	void SetFirstBulletDispertion(float fbdisp);
	void OnRenderFirstBulletDispertion();
#endif

	void Load();
};
