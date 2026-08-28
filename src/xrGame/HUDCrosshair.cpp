// HUDCrosshair.cpp:  крестик прицела, отображающий текущую дисперсию
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"

#include "HUDCrosshair.h"
#include "HUDCrosshairPresets.h"
#include "../../xrUI/ui_base.h"

extern ENGINE_API xr_atomic_bool g_bRendering;

namespace
{
	bool TextureResourceExists(LPCSTR subpath)
	{
		if (!subpath || !subpath[0])
			return false;

		char buf[256];
		xr_sprintf(buf, sizeof(buf), "ui%s%s", Platform::kPreferredSeparator, subpath);
		string_path fn;
		FS.update_path(fn, _game_textures_, buf);
		if (FS.exist(fn))
			return true;

		static const char* kExts[] = { ".dds", ".tga", ".png" };
		for (const char* ext : kExts)
		{
			string_path alt;
			xr_strconcat(alt, fn, ext);
			if (FS.exist(alt))
				return true;
		}

		return false;
	}

	LPCSTR NormalizeTexturePath(LPCSTR path, string_path& out)
	{
		if (!path || !path[0])
			return nullptr;

		if (_strnicmp(path, "ui\\", 3) == 0 || _strnicmp(path, "ui/", 3) == 0)
		{
			xr_strcpy(out, path);
			return out;
		}

		xr_sprintf(out, sizeof(out), "ui%s%s", Platform::kPreferredSeparator, path);
		return out;
	}
}

bool CHUDCrosshair::s_use_custom_crosshairs = false;
shared_str CHUDCrosshair::s_idle_preset_id;

CHUDCrosshair::CHUDCrosshair()
{
	hShader->create("hud\\crosshair");
	hTextureShader->create("hud\\cursor", "ui\\cursor");
	radius = 0.f;
	m_active_preset = nullptr;
}

CHUDCrosshair::~CHUDCrosshair()
{
}

void CHUDCrosshair::Load()
{
	cross_length_perc = pSettings->r_float(HUD_CURSOR_SECTION, "cross_length");
	min_radius_perc = pSettings->r_float(HUD_CURSOR_SECTION, "min_radius");
	max_radius_perc = pSettings->r_float(HUD_CURSOR_SECTION, "max_radius");
	cross_color = pSettings->r_fcolor(HUD_CURSOR_SECTION, "cross_color").get();

	s_use_custom_crosshairs = READ_IF_EXISTS(pSettings, r_bool, HUD_CURSOR_SECTION, "use_custom_crosshairs", false);
	s_idle_preset_id = READ_IF_EXISTS(pSettings, r_string, HUD_CURSOR_SECTION, "idle_crosshair", nullptr);

	if (s_use_custom_crosshairs)
		CHUDCrosshairPresets::Load();
}

bool CHUDCrosshair::HasIdlePreset()
{
	return s_use_custom_crosshairs && s_idle_preset_id.size() && CHUDCrosshairPresets::Has(s_idle_preset_id.c_str());
}

LPCSTR CHUDCrosshair::GetIdlePresetId()
{
	return HasIdlePreset() ? s_idle_preset_id.c_str() : nullptr;
}

bool CHUDCrosshair::ShouldUseIdleCrosshair(bool has_weapon, bool weapon_use_crosshair, bool weapon_holstered, bool has_bolt, bool device_in_hand)
{
	if (!HasIdlePreset())
		return false;

	if (has_bolt)
		return true;

	if (!has_weapon)
		return true;

	if (device_in_hand && (!has_weapon || weapon_holstered))
		return true;

	// Binocular and similar items that never use the weapon crosshair.
	if (has_weapon && !weapon_use_crosshair)
		return true;

	// Weapon stowed on back/inventory — custom idle dot instead of nothing.
	if (has_weapon && weapon_holstered)
		return true;

	return false;
}

bool CHUDCrosshair::IsIdleDotActive() const
{
	return s_use_custom_crosshairs
		&& HasIdlePreset()
		&& m_active_preset
		&& m_current_preset_id.size()
		&& !xr_strcmp(m_current_preset_id.c_str(), s_idle_preset_id.c_str());
}

void CHUDCrosshair::OnRenderDot(float cx, float cy, float size_px, u32 color)
{
	if (!m_active_preset)
		return;

	VERIFY(g_bRendering);

	const float size_x = size_px;
	const float size_y = size_px;
	const u32 C = color ? color : (cross_color ? cross_color : m_active_preset->color);

	string_path texture_path;
	const LPCSTR resolved_texture = NormalizeTexturePath(m_active_preset->texture.c_str(), texture_path);
	if (!resolved_texture || !TextureResourceExists(resolved_texture + 3))
	{
		static xr_set<shared_str> s_logged_missing_textures;
		if (s_logged_missing_textures.insert(m_active_preset->texture).second)
			Msg("! [HUDCrosshair] texture missing for preset '%s' (%s)", m_active_preset->id.c_str(), m_active_preset->texture.c_str());
		return;
	}

	auto shader_it = m_texture_shaders.find(resolved_texture);
	if (shader_it == m_texture_shaders.end())
	{
		ui_shader shader;
		shader->create("hud\\cursor", resolved_texture);
		shader_it = m_texture_shaders.emplace(resolved_texture, shader).first;
	}

	UIRender->StartPrimitive(6, IUIRender::ptTriList, UI().m_currentPointType);

	UIRender->PushPoint(cx - size_x, cy + size_y, 0, C, 0, 1);
	UIRender->PushPoint(cx - size_x, cy - size_y, 0, C, 0, 0);
	UIRender->PushPoint(cx + size_x, cy + size_y, 0, C, 1, 1);
	UIRender->PushPoint(cx + size_x, cy + size_y, 0, C, 1, 1);
	UIRender->PushPoint(cx - size_x, cy - size_y, 0, C, 0, 0);
	UIRender->PushPoint(cx + size_x, cy - size_y, 0, C, 1, 0);

	UIRender->SetShader(*shader_it->second);
	UIRender->FlushPrimitive();
}

void CHUDCrosshair::SetPreset(LPCSTR preset_id)
{
	if (!s_use_custom_crosshairs || !preset_id || !preset_id[0])
	{
		m_current_preset_id = nullptr;
		m_active_preset = nullptr;
		return;
	}

	if (m_current_preset_id.size() && !xr_strcmp(m_current_preset_id.c_str(), preset_id))
		return;

	m_current_preset_id = preset_id;
	m_active_preset = CHUDCrosshairPresets::Get(preset_id);

	if (!m_active_preset)
		Msg("! [HUDCrosshair] unknown crosshair preset '%s', fallback to legacy lines", preset_id);
}

void CHUDCrosshair::SetDispersion(float disp)
{
	Fvector4 r;
	Fvector R = { Device.fViewportNear * std::sin(disp), 0.f, Device.fViewportNear };
	Device.mProject.transform(r, R);

	Fvector2 scr_size;
	scr_size.set(float(::Render->getTarget()->get_width()), float(::Render->getTarget()->get_height()));
	target_radius = std::abs(r.x) * scr_size.x / 2.0f;
}

#ifdef DEBUG
void CHUDCrosshair::SetFirstBulletDispertion(float fbdisp)
{
	Fvector4 r;
	Fvector R = { Device.fViewportNear * std::sin(fbdisp), 0.f, Device.fViewportNear };
	Device.mProject.transform(r, R);

	Fvector2 scr_size;
	scr_size.set(float(::Render->getTarget()->get_width()), float(::Render->getTarget()->get_height()));
	fb_radius = std::abs(r.x) * scr_size.x / 2.0f;
}

bool g_bDrawFirstBulletCrosshair = false;

void CHUDCrosshair::OnRenderFirstBulletDispertion()
{
	VERIFY(g_bRendering);
	Fvector2 center;
	Fvector2 scr_size;
	scr_size.set(float(::Render->getTarget()->get_width()), float(::Render->getTarget()->get_height()));
	center.set(scr_size.x / 2.0f, scr_size.y / 2.0f);

	UIRender->StartPrimitive(10, IUIRender::ptLineList, UI().m_currentPointType);

	constexpr u32 fb_cross_color = color_rgba(255, 0, 0, 255);

	float cross_length = 0.008f * scr_size.x;
	float min_radius = min_radius_perc * scr_size.x;
	float max_radius = max_radius_perc * scr_size.x;

	clamp(target_radius, min_radius, max_radius);

	float x_min = min_radius + fb_radius;
	float x_max = x_min + cross_length;
	float y_min = x_min;
	float y_max = x_max;

	UIRender->PushPoint(center.x, center.y + y_min, 0, fb_cross_color, 0, 0);
	UIRender->PushPoint(center.x, center.y + y_max, 0, fb_cross_color, 0, 0);
	UIRender->PushPoint(center.x, center.y - y_min, 0, fb_cross_color, 0, 0);
	UIRender->PushPoint(center.x, center.y - y_max, 0, fb_cross_color, 0, 0);
	UIRender->PushPoint(center.x + x_min, center.y, 0, fb_cross_color, 0, 0);
	UIRender->PushPoint(center.x + x_max, center.y, 0, fb_cross_color, 0, 0);
	UIRender->PushPoint(center.x - x_min, center.y, 0, fb_cross_color, 0, 0);
	UIRender->PushPoint(center.x - x_max, center.y, 0, fb_cross_color, 0, 0);
	UIRender->PushPoint(center.x - 0.5f, center.y, 0, fb_cross_color, 0, 0);
	UIRender->PushPoint(center.x + 0.5f, center.y, 0, fb_cross_color, 0, 0);

	UIRender->SetShader(*hShader);
	UIRender->FlushPrimitive();
}
#endif

extern ENGINE_API xr_atomic_bool g_bRendering;

void CHUDCrosshair::OnRenderTexture(const SCrosshairPreset& preset)
{
	VERIFY(g_bRendering);

	Fvector2 scr_size;
	scr_size.set(float(Device.TargetWidth), float(Device.TargetHeight));
	const float center_x = scr_size.x / 2.0f;
	const float center_y = scr_size.y / 2.0f;

	const float min_radius = preset.min_radius_perc * scr_size.x;
	const float max_radius = preset.max_radius_perc * scr_size.x;
	float disp_radius = radius;
	clamp(disp_radius, min_radius, max_radius);

	float half_size = preset.base_size_perc * scr_size.x * 0.5f;
	if (preset.scale_with_dispersion)
		half_size += disp_radius * preset.disp_scale;

	const float size_x = half_size;
	const float size_y = half_size;
	const u32 C = cross_color ? cross_color : preset.color;

	string_path texture_path;
	const LPCSTR resolved_texture = NormalizeTexturePath(preset.texture.c_str(), texture_path);
	if (!resolved_texture || !TextureResourceExists(resolved_texture + 3))
	{
		static xr_set<shared_str> s_logged_missing_textures;
		if (s_logged_missing_textures.insert(preset.texture).second)
			Msg("! [HUDCrosshair] texture missing for preset '%s' (%s)", preset.id.c_str(), preset.texture.c_str());
		return;
	}

	auto shader_it = m_texture_shaders.find(resolved_texture);
	if (shader_it == m_texture_shaders.end())
	{
		ui_shader shader;
		shader->create("hud\\cursor", resolved_texture);
		shader_it = m_texture_shaders.emplace(resolved_texture, shader).first;
	}

	UIRender->StartPrimitive(6, IUIRender::ptTriList, UI().m_currentPointType);

	UIRender->PushPoint(center_x - size_x, center_y + size_y, 0, C, 0, 1);
	UIRender->PushPoint(center_x - size_x, center_y - size_y, 0, C, 0, 0);
	UIRender->PushPoint(center_x + size_x, center_y + size_y, 0, C, 1, 1);
	UIRender->PushPoint(center_x + size_x, center_y + size_y, 0, C, 1, 1);
	UIRender->PushPoint(center_x - size_x, center_y - size_y, 0, C, 0, 0);
	UIRender->PushPoint(center_x + size_x, center_y - size_y, 0, C, 1, 0);

	UIRender->SetShader(*shader_it->second);
	UIRender->FlushPrimitive();
}

void CHUDCrosshair::OnRenderLines()
{
	VERIFY(g_bRendering);
	Fvector2 center;
	Fvector2 scr_size;
	scr_size.set(float(::Render->getTarget()->get_width()), float(::Render->getTarget()->get_height()));
	center.set(scr_size.x / 2.0f, scr_size.y / 2.0f);

	UIRender->StartPrimitive(10, IUIRender::ptLineList, UI().m_currentPointType);

	float cross_length = cross_length_perc * scr_size.x;
	float min_radius = min_radius_perc * scr_size.x;
	float max_radius = max_radius_perc * scr_size.x;

	clamp(target_radius, min_radius, max_radius);

	float x_min = min_radius + radius;
	float x_max = x_min + cross_length;
	float y_min = x_min;
	float y_max = x_max;

	UIRender->PushPoint(center.x, center.y + y_min, 0, cross_color, 0, 0);
	UIRender->PushPoint(center.x, center.y + y_max, 0, cross_color, 0, 0);
	UIRender->PushPoint(center.x, center.y - y_min, 0, cross_color, 0, 0);
	UIRender->PushPoint(center.x, center.y - y_max, 0, cross_color, 0, 0);
	UIRender->PushPoint(center.x + x_min, center.y, 0, cross_color, 0, 0);
	UIRender->PushPoint(center.x + x_max, center.y, 0, cross_color, 0, 0);
	UIRender->PushPoint(center.x - x_min, center.y, 0, cross_color, 0, 0);
	UIRender->PushPoint(center.x - x_max, center.y, 0, cross_color, 0, 0);
	UIRender->PushPoint(center.x - 0.5f, center.y, 0, cross_color, 0, 0);
	UIRender->PushPoint(center.x + 0.5f, center.y, 0, cross_color, 0, 0);

	UIRender->SetShader(*hShader);
	UIRender->FlushPrimitive();
}

void CHUDCrosshair::OnRender()
{
	if (!fsimilar(target_radius, radius))
		radius = target_radius;

	if (s_use_custom_crosshairs && m_active_preset)
		OnRenderTexture(*m_active_preset);
	else
		OnRenderLines();

#ifdef DEBUG
	if (g_bDrawFirstBulletCrosshair)
		OnRenderFirstBulletDispertion();
#endif
}
