#include "StdAfx.h"
#include "UIInteractionMarkers.h"
#include "UIInteractionMarkers_internal.h"

#include <algorithm>

#include "../../xrUI/UITextureMaster.h"
#include "../../xrUI/Widgets/UIStaticItem.h"
#include "../../Include/xrRender/SVGTypes.h"
#include "../../xrUI/ui_base.h"

void CInteractionMarkerManager::LoadIconPatternSection(LPCSTR section_name, xr_map<shared_str, shared_str>& out)
{
	LoadTextureLookupSection(section_name, out);
}

SWSUITextureSlot CInteractionMarkerManager::ResolveIcon(shared_str icon_id) const
{
	if (!icon_id.size())
		return {};

	const auto it = m_icon_registry.find(icon_id);
	if (it != m_icon_registry.end())
		return it->second;

	// Legacy configs used full texture ids (ui_wsui_*), not short icon ids.
	if (strstr(icon_id.c_str(), "ui_") || CUITextureMaster::ItemExist(icon_id))
		return WSUIInternal::MakeRasterSlot(icon_id.c_str());

	return {};
}

SWSUITextureSlot CInteractionMarkerManager::ResolveIconRule(LPCSTR event) const
{
	if (!event || !event[0])
		return {};

	const auto it = m_icon_rules.find(event);
	if (it == m_icon_rules.end())
		return {};

	return ResolveIcon(it->second);
}

SWSUITextureSlot CInteractionMarkerManager::MatchSectionPattern(shared_str section, const xr_map<shared_str, shared_str>& patterns) const
{
	if (!section.size())
		return {};

	const LPCSTR sect = section.c_str();
	for (const auto& [pattern, icon_id] : patterns)
	{
		if (strstr(sect, pattern.c_str()))
			return ResolveIcon(icon_id);
	}
	return {};
}

SWSUITextureSlot CInteractionMarkerManager::MatchNamePattern(shared_str name, const xr_map<shared_str, shared_str>& patterns) const
{
	return MatchSectionPattern(name, patterns);
}

void CInteractionMarkerManager::InvalidateSvgCache()
{
	m_svg_cache.clear();
	m_svg_cache_ui_scale = -1.f;
}

float CInteractionMarkerManager::GetMarkerSvgRefSize() const
{
	const float dot = m_markers_cfg.dot.size > 0.f ? m_markers_cfg.dot.size : 10.f;
	const float max_scale = m_markers_cfg.distance_fade.max_scale > 0.f ? m_markers_cfg.distance_fade.max_scale : 1.4f;
	constexpr float kPopinPeak = 1.1f;
	return dot * UiScale() * max_scale * kPopinPeak;
}

bool CInteractionMarkerManager::EnsureSvgCache(shared_str path, float ref_w, float ref_h) const
{
	path = WSUIInternal::NormalizeWsuiSvgSubpath(path);
	if (!path.size())
		return false;

	ref_w = std::max(ref_w, 1.f);
	ref_h = std::max(ref_h, 1.f);

	const auto it = m_svg_cache.find(path);
	if (it != m_svg_cache.end() && it->second.valid && it->second.ref_w >= ref_w && it->second.ref_h >= ref_h)
		return true;

	float raster_w = ref_w;
	float raster_h = ref_h;
	if (it != m_svg_cache.end() && it->second.valid)
	{
		raster_w = std::max(ref_w, it->second.ref_w);
		raster_h = std::max(ref_h, it->second.ref_h);
	}

	if (!WSUIInternal::WsuiSvgFileExists(path.c_str()))
		return false;

	CUIStaticItem tmp;
	SVGTintRGBA tint;
	if (!CUITextureMaster::InitTexture(path, &tmp, raster_w, raster_h, tint))
		return false;

	SWSUISvgCacheEntry& entry = m_svg_cache[path];
	entry.shader = tmp.GetShader();
	entry.uv = tmp.GetTextureRect();
	entry.ref_w = raster_w;
	entry.ref_h = raster_h;
	entry.valid = true;
	return true;
}

void CInteractionMarkerManager::PrecacheSvgPath(shared_str path, float ref_w, float ref_h)
{
	if (!path.size())
		return;
	EnsureSvgCache(path, ref_w, ref_h);
}

void CInteractionMarkerManager::PrecacheAllSvgs()
{
	m_svg_cache.clear();

	const float marker_ref_w = GetMarkerSvgRefSize();
	const float marker_ref_h = SquareHeight(marker_ref_w);

	for (const auto& [id, slot] : m_icon_registry)
	{
		(void)id;
		if (slot.svg.size())
		{
			const float scale = slot.EffectiveSizeScale();
			PrecacheSvgPath(slot.svg, marker_ref_w * scale, marker_ref_h * scale);
		}
	}

	const auto& kb = m_prompt_cfg.main_panel.keybind;
	const float key_w = kb.icon_width > 0.f ? kb.icon_width : (kb.width > 0.f ? kb.width : 64.f);
	const float key_h = kb.icon_height > 0.f ? kb.icon_height : (kb.height > 0.f ? kb.height : 64.f);
	if (kb.texture.svg.size())
		PrecacheSvgPath(kb.texture.svg, key_w, key_h);
	if (kb.pressed_texture.svg.size())
		PrecacheSvgPath(kb.pressed_texture.svg, key_w, key_h);

	const auto precache_bg = [this](const SWSUIBackground& bg)
	{
		if (!bg.texture.svg.size())
			return;
		const float w = bg.width > 0.f ? bg.width : 64.f;
		const float h = bg.height > 0.f ? bg.height : 64.f;
		PrecacheSvgPath(bg.texture.svg, w, h);
	};

	precache_bg(m_prompt_cfg.main_panel.background);
	precache_bg(m_prompt_cfg.item_condition.background);
	precache_bg(m_prompt_cfg.item_card.background);

	const auto precache_metric = [this](const SWSUIItemCardMetric& metric)
	{
		if (!metric.icon.svg.size())
			return;
		const float w = metric.icon_w > 0.f ? metric.icon_w : 32.f;
		const float h = metric.icon_h > 0.f ? metric.icon_h : 32.f;
		PrecacheSvgPath(metric.icon.svg, w, h);
	};

	precache_metric(m_prompt_cfg.item_card.weight);
	precache_metric(m_prompt_cfg.item_card.value);

	m_svg_cache_ui_scale = UiScale();
}

bool CInteractionMarkerManager::DrawTextureSlot(const SWSUITextureSlot& slot, float cx, float cy, float w, float h, u32 color, bool keep_square, float angle) const
{
	if (!slot.HasDrawable())
		return false;

	if (keep_square)
		h = SquareHeight(w);

	CUIStaticItem item;
	bool initialized = false;

	const shared_str svg_path = WSUIInternal::NormalizeWsuiSvgSubpath(slot.svg);
	if (svg_path.size())
	{
		if (EnsureSvgCache(svg_path, w, h))
		{
			const SWSUISvgCacheEntry& cache = m_svg_cache[svg_path];
			item.SetShader(cache.shader);
			item.SetTextureRect(cache.uv);
			item.SetTextureColor(color);
			initialized = true;
		}
		else if (slot.svg.size())
		{
			Msg("! [wsui] SVG not found, using raster fallback: %s", slot.svg.c_str());
		}
	}

	if (!initialized && slot.raster.size())
	{
		if (!CUITextureMaster::ItemExist(slot.raster))
			return false;

		initialized = CUITextureMaster::InitTexture(slot.raster, &item, "hud\\cursor", false);
		if (initialized)
			item.SetTextureColor(color);
	}

	if (!initialized)
		return false;

	item.SetSize(Fvector2().set(w, h));
	item.SetPos(cx - w * 0.5f, cy - h * 0.5f);
	if (angle != 0.f)
		item.Render(angle);
	else
		item.Render();
	return true;
}
