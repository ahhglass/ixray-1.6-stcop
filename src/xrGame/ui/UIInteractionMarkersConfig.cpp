#include "StdAfx.h"
#include "UIInteractionMarkers.h"
#include "UIInteractionMarkers_internal.h"

#include <algorithm>

#include "../../xrUI/UIXmlInit.h"
#include "../../xrUI/UITextureMaster.h"
#include "../../xrUI/UIVectorBinding.h"
#include "../../xrCore/FormatParsers/XML/xrXMLParser.h"
#include "../../xrUI/ui_base.h"
#include "../xrCore/_color.h"
#include "../xrEngine/xr_level_controller.h"

namespace
{
	Fvector ParseVector3(LPCSTR value)
	{
		Fvector result = {};
		if (!value || !value[0])
			return result;

		const int count = sscanf(value, "%f,%f,%f", &result.x, &result.y, &result.z);
		if (count < 3)
			result.set(0.f, 0.f, 0.f);
		return result;
	}

}


void CInteractionMarkerManager::LoadWsuiXml()
{
	InvalidateSvgCache();
	m_markers_cfg = {};
	m_prompt_cfg = {};

	if (!m_ui_xml.size())
		return;

	CUIXml xml;
	if (!xml.Load(CONFIG_PATH, UI_PATH, m_ui_xml.c_str()))
		return;

	LoadWsuiMarkers(xml);
	LoadWsuiPrompt(xml);
	PrecacheAllSvgs();
}

void CInteractionMarkerManager::LoadWsuiMarkers(CUIXml& xml)
{
	LoadMarkersScan(xml);
	LoadMarkersDot(xml);
	LoadMarkersLosCache(xml);
	LoadMarkersFeatures(xml);
	LoadMarkersDistanceFade(xml);
	LoadMarkersPriority(xml);
	LoadMarkersPopinAnimation(xml);
	LoadMarkerIcons(xml);
	LoadMarkerCategories(xml);
	LoadMarkersDikIcons(xml);
}

void CInteractionMarkerManager::LoadMarkerIcons(CUIXml& xml)
{
	m_icon_registry.clear();
	m_icon_rules.clear();

	const LPCSTR root = "wsui_markers:marker_icons";
	XML_NODE* icons_node = xml.NavigateToNode(root, 0);
	if (!icons_node)
		return;

	const int count = xml.GetNodesNum(icons_node, "icon");
	for (int i = 0; i < count; ++i)
	{
		LPCSTR id = xml.ReadAttrib(icons_node, "icon", i, "id", nullptr);
		if (!id || !id[0])
			continue;

		SWSUITextureSlot slot;
		if (LPCSTR tex = xml.ReadAttrib(icons_node, "icon", i, "texture", nullptr))
		{
			if (tex[0])
				slot.raster = tex;
		}

		XML_NODE* icon_node = xml.NavigateToNode(icons_node, "icon", i);
		if (icon_node)
		{
			if (LPCSTR val = xml.Read(icon_node, "svg", 0, nullptr))
			{
				if (val[0])
					slot.svg = WSUIInternal::NormalizeWsuiSvgSubpath(val);
			}
		}

		if (!slot.svg.size())
		{
			if (LPCSTR attr = xml.ReadAttrib(icons_node, "icon", i, "svg", nullptr))
			{
				if (attr[0])
					slot.svg = WSUIInternal::NormalizeWsuiSvgSubpath(attr);
			}
		}

		slot.size_scale = xml.ReadAttribFlt(icons_node, "icon", i, "size_scale", 1.f);

		if (slot.HasDrawable())
			m_icon_registry[id] = slot;
	}

	LoadMarkerIconRules(xml);
}

void CInteractionMarkerManager::LoadMarkerIconRules(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:marker_icon_rules";
	XML_NODE* rules_node = xml.NavigateToNode(path, 0);
	if (!rules_node)
		return;

	const int count = xml.GetNodesNum(rules_node, "rule");
	for (int i = 0; i < count; ++i)
	{
		LPCSTR event = xml.ReadAttrib(rules_node, "rule", i, "event", nullptr);
		LPCSTR icon = xml.ReadAttrib(rules_node, "rule", i, "icon", nullptr);
		if (event && event[0] && icon && icon[0])
			m_icon_rules[event] = icon;
	}
}

void CInteractionMarkerManager::LoadMarkersScan(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:scan";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& scan = m_markers_cfg.scan;
	scan.radius = xml.ReadAttribFlt(path, 0, "radius", 0.f);
	scan.interval_ms = xml.ReadAttribInt(path, 0, "interval_ms", 0);
	scan.max_markers = xml.ReadAttribInt(path, 0, "max_markers", 0);
	scan.prompt_distance = xml.ReadAttribFlt(path, 0, "prompt_distance", 0.f);
	scan.adaptive = xml.ReadAttribInt(path, 0, "adaptive", 0) != 0;
	scan.idle_interval_ms = xml.ReadAttribInt(path, 0, "idle_interval_ms", 0);
	scan.actor_pos_eps = xml.ReadAttribFlt(path, 0, "actor_pos_eps", 0.f);
	scan.camera_dir_eps = xml.ReadAttribFlt(path, 0, "camera_dir_eps", 0.f);
}

void CInteractionMarkerManager::LoadMarkersDot(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:dot";
	if (!xml.NavigateToNode(path, 0))
		return;

	m_markers_cfg.dot.size = xml.ReadAttribFlt(path, 0, "size", 0.f);
	m_markers_cfg.dot.lerp_speed = xml.ReadAttribFlt(path, 0, "lerp_speed", 0.f);
}

void CInteractionMarkerManager::LoadMarkersLosCache(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:los_cache";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& los = m_markers_cfg.los_cache;
	los.cache_ttl_ms = xml.ReadAttribInt(path, 0, "cache_ttl_ms", 0);
	los.checks_per_frame = xml.ReadAttribInt(path, 0, "checks_per_frame", 0);
	los.camera_pos_eps = xml.ReadAttribFlt(path, 0, "camera_pos_eps", 0.f);
	los.camera_dir_eps = xml.ReadAttribFlt(path, 0, "camera_dir_eps", 0.f);
}

void CInteractionMarkerManager::LoadMarkersFeatures(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:features";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& f = m_markers_cfg.features;
	f.hide_dots = xml.ReadAttribInt(path, 0, "hide_dots", 0) != 0;
	f.wheel_cycle_pickups = xml.ReadAttribInt(path, 0, "wheel_cycle_pickups", 0) != 0;
	f.special_icons_always = xml.ReadAttribInt(path, 0, "special_icons_always", 0) != 0;
	f.enable_task_icons = xml.ReadAttribInt(path, 0, "enable_task_icons", 0) != 0;
	f.enable_focus_sound = xml.ReadAttribInt(path, 0, "enable_focus_sound", 0) != 0;
}

void CInteractionMarkerManager::LoadMarkersDistanceFade(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:distance_fade";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& cfg = m_markers_cfg.distance_fade;
	cfg.enable_scale = xml.ReadAttribInt(path, 0, "enable_scale", 0) != 0;
	cfg.enable_alpha = xml.ReadAttribInt(path, 0, "enable_alpha", 0) != 0;
	cfg.min_scale = xml.ReadAttribFlt(path, 0, "min_scale", 0.f);
	cfg.max_scale = xml.ReadAttribFlt(path, 0, "max_scale", 0.f);
	cfg.min_alpha = xml.ReadAttribFlt(path, 0, "min_alpha", 0.f);
}

void CInteractionMarkerManager::LoadMarkersPriority(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:marker_priority";
	XML_NODE* priority_node = xml.NavigateToNode(path, 0);
	if (!priority_node)
		return;

	auto& p = m_markers_cfg.marker_priority;
	p.focus = xml.ReadAttribFlt(path, 0, "focus", 0.f);
	p.task = xml.ReadAttribFlt(path, 0, "task", 0.f);

	m_category_priority.clear();

	const int count = xml.GetNodesNum(priority_node, "category");
	for (int i = 0; i < count; ++i)
	{
		LPCSTR id = xml.ReadAttrib(priority_node, "category", i, "id", nullptr);
		if (!id || !id[0])
			continue;

		const float value = xml.ReadAttribFlt(priority_node, "category", i, "value", 0.f);
		m_category_priority[id] = value;
	}
}

void CInteractionMarkerManager::LoadMarkersPopinAnimation(CUIXml& xml)
{
	const LPCSTR path = "wsui_markers:popin_animation";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& popin = m_markers_cfg.popin_animation;
	popin.duration_ms = xml.ReadAttribInt(path, 0, "duration_ms", 0);
	popin.min_scale = xml.ReadAttribFlt(path, 0, "min_scale", 0.f);
}

void CInteractionMarkerManager::LoadCategoryNode(CUIXml& xml, XML_NODE* category_node, int index, float default_distance)
{
	LPCSTR id = xml.ReadAttrib(category_node, "category", index, "id", nullptr);
	if (!id || !id[0])
		return;

	XML_NODE* cat_node = xml.NavigateToNode(category_node, "category", index);
	if (!cat_node)
		return;

	SWSUICategoryDef& def = m_categories[id];
	def.enabled = xml.ReadAttribInt(cat_node, "enabled", 1) != 0;

	const auto load_category_icon = [&](LPCSTR attr, SWSUITextureSlot& out, LPCSTR raster_attr, LPCSTR svg_child, LPCSTR svg_attr)
	{
		out = {};
		if (LPCSTR icon_id = xml.ReadAttrib(cat_node, attr, nullptr))
		{
			if (icon_id[0])
			{
				out = ResolveIcon(icon_id);
				if (out.HasDrawable())
					return;
			}
		}

		if (LPCSTR tex = xml.ReadAttrib(cat_node, raster_attr, nullptr))
		{
			if (tex[0])
				out.raster = tex;
		}

		if (LPCSTR val = xml.Read(cat_node, svg_child, 0, nullptr))
		{
			if (val[0])
				out.svg = WSUIInternal::NormalizeWsuiSvgSubpath(val);
		}

		if (!out.svg.size())
		{
			if (LPCSTR attr_svg = xml.ReadAttrib(cat_node, svg_attr, nullptr))
			{
				if (attr_svg[0])
					out.svg = WSUIInternal::NormalizeWsuiSvgSubpath(attr_svg);
			}
		}
	};

	load_category_icon("texture", def.texture, "texture", "svg", "svg");
	load_category_icon("active_texture", def.active_texture, "active_texture", "active_svg", "active_svg");
	if (!def.active_texture.HasDrawable() && def.texture.HasDrawable())
		def.active_texture = def.texture;

	if (LPCSTR bone = xml.ReadAttrib(cat_node, "bone", nullptr))
		def.bone = bone;

	def.show_distance = xml.ReadAttribFlt(cat_node, "show_distance", default_distance);

	if (LPCSTR pos_mode = xml.ReadAttrib(cat_node, "pos_mode", nullptr))
		def.pos_mode = pos_mode;
	if (LPCSTR prompt_mode = xml.ReadAttrib(cat_node, "prompt_mode", nullptr))
		def.prompt_mode = prompt_mode;
	if (LPCSTR verb = xml.ReadAttrib(cat_node, "verb", nullptr))
		def.verb_id = verb;
	if (LPCSTR name_source = xml.ReadAttrib(cat_node, "name_source", nullptr))
		def.name_source = name_source;

	if (LPCSTR features = xml.ReadAttrib(cat_node, "features", nullptr))
		def.feature_flags = WSUIInternal::ParseCategoryFeatures(features);

	if (LPCSTR on_tip = xml.ReadAttrib(cat_node, "campfire_on_tip", nullptr))
		def.campfire_on_tip = on_tip;
	if (LPCSTR off_tip = xml.ReadAttrib(cat_node, "campfire_off_tip", nullptr))
		def.campfire_off_tip = off_tip;
	if (LPCSTR icon_lookup = xml.ReadAttrib(cat_node, "icon_lookup", nullptr))
		def.icon_lookup = icon_lookup;
}

void CInteractionMarkerManager::LoadMarkerCategories(CUIXml& xml)
{
	const LPCSTR root = "wsui_markers:marker_categories";
	XML_NODE* categories_node = xml.NavigateToNode(root, 0);
	if (!categories_node)
		return;

	const float default_distance = m_markers_cfg.scan.radius > 0.f ? m_markers_cfg.scan.radius : 5.f;

	const int count = xml.GetNodesNum(categories_node, "category");
	for (int i = 0; i < count; ++i)
		LoadCategoryNode(xml, categories_node, i, default_distance);
}

void CInteractionMarkerManager::LoadMarkersDikIcons(CUIXml& xml)
{
	if (!xml.NavigateToNode("wsui_markers:dik_icons", 0))
		return;

	static const LPCSTR keys[] = { "mouse1", "mouse2", "mouse3", "mouse4", "mouse5" };
	m_dik_icons.clear();

	for (LPCSTR key : keys)
	{
		string256 node_path;
		xr_sprintf(node_path, "wsui_markers:dik_icons:%s", key);
		if (!xml.NavigateToNode(node_path, 0))
			continue;

		SWSUITextureSlot slot;
		LoadIconRefSlot(xml, node_path, slot, "icon");
		if (slot.HasDrawable())
		{
			const int dik = keyname_to_dik(key);
			if (dik > 0)
				m_dik_icons[dik] = slot;
		}
	}
}

void CInteractionMarkerManager::LoadWsuiPrompt(CUIXml& xml)
{
	LoadPromptLayout(xml);
	LoadPromptFeatures(xml);
	LoadPromptFadeAnimation(xml);
	LoadPromptMainPanel(xml);
	LoadPromptItemStackCount(xml);
	LoadPromptItemCondition(xml);
	LoadPromptItemCard(xml);
	LoadPromptTutorialScreen(xml);
}

void CInteractionMarkerManager::LoadPromptLayout(CUIXml& xml)
{
	const LPCSTR root = "wsui_prompt";
	if (!xml.NavigateToNode(root, 0))
		return;

	m_prompt_cfg.ui_scale = xml.ReadAttribFlt(root, 0, "ui_scale", 0.f);
	m_prompt_cfg.aspect_correction = xml.ReadAttribFlt(root, 0, "aspect_correction", 0.f);
	m_prompt_cfg.font_scale_w = xml.ReadAttribFlt(root, 0, "font_scale_w", 0.f);
	m_prompt_cfg.font_scale_h = xml.ReadAttribFlt(root, 0, "font_scale_h", 0.f);
	m_prompt_cfg.text_pad = xml.ReadAttribFlt(root, 0, "text_pad", 0.f);
	CUIXmlInit::ReadShadowsNode(xml, root, 0, m_prompt_cfg.default_text_shadow);

	if (xml.NavigateToNode("wsui_prompt:anchor", 0))
	{
		m_prompt_cfg.anchor_x = xml.ReadAttribFlt("wsui_prompt:anchor", 0, "x", 0.f);
		m_prompt_cfg.anchor_y = xml.ReadAttribFlt("wsui_prompt:anchor", 0, "y", 0.f);
	}
}

void CInteractionMarkerManager::LoadPromptFeatures(CUIXml& xml)
{
	const LPCSTR path = "wsui_prompt:features";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& f = m_prompt_cfg.features;
	f.item_stack_count = xml.ReadAttribInt(path, 0, "item_stack_count", 0) != 0;
	f.item_condition = xml.ReadAttribInt(path, 0, "item_condition", 0) != 0;
	f.item_card = xml.ReadAttribInt(path, 0, "item_card", 0) != 0;
	f.keybind = xml.ReadAttribInt(path, 0, "keybind", 0) != 0;
	f.fixed_screen = xml.ReadAttribInt(path, 0, "fixed_screen", 0) != 0;
	f.fixed_x = xml.ReadAttribFlt(path, 0, "fixed_x", 0.f);
	f.fixed_y = xml.ReadAttribFlt(path, 0, "fixed_y", 0.f);
}

void CInteractionMarkerManager::LoadPromptFadeAnimation(CUIXml& xml)
{
	const LPCSTR path = "wsui_prompt:fade_animation";
	if (!xml.NavigateToNode(path, 0))
		return;

	m_prompt_cfg.fade_animation.fade_in_ms = xml.ReadAttribInt(path, 0, "fade_in_ms", 0);
	m_prompt_cfg.fade_animation.fade_out_ms = xml.ReadAttribInt(path, 0, "fade_out_ms", 0);
}

void CInteractionMarkerManager::LoadTextLabel(CUIXml& xml, LPCSTR path, SWSUITextLabel& out)
{
	if (!xml.NavigateToNode(path, 0))
		return;

	out.x = xml.ReadAttribFlt(path, 0, "x", 0.f);
	out.y = xml.ReadAttribFlt(path, 0, "y", 0.f);
	if (LPCSTR font_name = xml.ReadAttrib(path, 0, "font", nullptr))
		out.font_name = font_name;

	u32 color = 0;
	CGameFont* loaded_font = nullptr;
	if (CUIXmlInit::InitFont(xml, path, 0, color, loaded_font) && loaded_font)
	{
		out.font = loaded_font;
		out.color = color;
	}

	CUIXmlInit::ReadShadowsNode(xml, path, 0, out.text_shadow);
}

void CInteractionMarkerManager::LoadTextureSlot(CUIXml& xml, LPCSTR path, SWSUITextureSlot& out, LPCSTR raster_attr, LPCSTR svg_child) const
{
	out.raster = nullptr;
	out.svg = nullptr;

	if (LPCSTR tex = xml.ReadAttrib(path, 0, raster_attr, nullptr))
		out.raster = tex;

	if (svg_child && svg_child[0])
	{
		string256 svg_node;
		xr_strconcat(svg_node, path, ":", svg_child);
		if (xml.NavigateToNode(svg_node, 0))
		{
			if (LPCSTR val = xml.Read(svg_node, 0, nullptr))
			{
				if (val[0])
					out.svg = val;
			}
		}
	}

	if (!out.svg.size() && svg_child && !xr_strcmp(svg_child, "svg"))
	{
		if (LPCSTR svg = CUIVectorBinding::QueryFileNameFromXml(xml, path, 0))
			out.svg = svg;
	}
	else if (!out.svg.size() && svg_child && svg_child[0])
	{
		if (LPCSTR attr = xml.ReadAttrib(path, 0, svg_child, nullptr))
		{
			if (attr[0])
				out.svg = attr;
		}
	}

	if (out.svg.size())
		out.svg = WSUIInternal::NormalizeWsuiSvgSubpath(out.svg);
}

void CInteractionMarkerManager::LoadIconRefSlot(CUIXml& xml, LPCSTR path, SWSUITextureSlot& out, LPCSTR icon_attr, LPCSTR raster_attr, LPCSTR svg_child) const
{
	out = {};
	if (icon_attr && icon_attr[0])
	{
		if (LPCSTR icon_id = xml.ReadAttrib(path, 0, icon_attr, nullptr))
		{
			if (icon_id[0])
			{
				out = ResolveIcon(icon_id);
				if (out.HasDrawable())
					return;
			}
		}
	}

	LoadTextureSlot(xml, path, out, raster_attr, svg_child);
}

void CInteractionMarkerManager::LoadActiveTextureSlot(CUIXml& xml, LPCSTR path, SWSUITextureSlot& out) const
{
	out.raster = nullptr;
	out.svg = nullptr;

	if (LPCSTR tex = xml.ReadAttrib(path, 0, "active_texture", nullptr))
		out.raster = tex;

	string256 active_svg_node;
	xr_strconcat(active_svg_node, path, ":active_svg");
	if (xml.NavigateToNode(active_svg_node, 0))
	{
		if (LPCSTR val = xml.Read(active_svg_node, 0, nullptr))
		{
			if (val[0])
				out.svg = val;
		}
	}

	if (!out.svg.size())
	{
		if (LPCSTR attr = xml.ReadAttrib(path, 0, "active_svg", nullptr))
		{
			if (attr[0])
				out.svg = attr;
		}
	}

	if (out.svg.size())
		out.svg = WSUIInternal::NormalizeWsuiSvgSubpath(out.svg);
}

void CInteractionMarkerManager::LoadBackground(CUIXml& xml, LPCSTR path, SWSUIBackground& out, bool read_enable)
{
	if (!xml.NavigateToNode(path, 0))
		return;

	if (read_enable)
		out.enabled = xml.ReadAttribInt(path, 0, "enable", 0) != 0;
	else
		out.enabled = true;

	LoadIconRefSlot(xml, path, out.texture, "icon");
	out.height = xml.ReadAttribFlt(path, 0, "height", 0.f);
	out.width = xml.ReadAttribFlt(path, 0, "width", 0.f);
	out.pad = xml.ReadAttribFlt(path, 0, "pad", 0.f);

	const int r = xml.ReadAttribInt(path, 0, "r", 255);
	const int g = xml.ReadAttribInt(path, 0, "g", 255);
	const int b = xml.ReadAttribInt(path, 0, "b", 255);
	const int a = xml.ReadAttribInt(path, 0, "a", 255);
	out.color = color_rgba(r, g, b, a);
}

void CInteractionMarkerManager::LoadPromptMainPanel(CUIXml& xml)
{
	LoadBackground(xml, "wsui_prompt:main_panel:background", m_prompt_cfg.main_panel.background);

	if (xml.NavigateToNode("wsui_prompt:main_panel:keybind", 0))
	{
		const LPCSTR path = "wsui_prompt:main_panel:keybind";
		auto& kb = m_prompt_cfg.main_panel.keybind;
		kb.width = xml.ReadAttribFlt(path, 0, "width", 0.f);
		kb.height = xml.ReadAttribFlt(path, 0, "height", 0.f);
		kb.icon_width = xml.ReadAttribFlt(path, 0, "icon_width", 0.f);
		kb.icon_height = xml.ReadAttribFlt(path, 0, "icon_height", 0.f);
		LoadIconRefSlot(xml, path, kb.texture, "icon");
		LoadIconRefSlot(xml, path, kb.pressed_texture, "pressed_icon", "pressed_texture", "pressed_svg");
		LoadTextLabel(xml, "wsui_prompt:main_panel:keybind:label", kb.label);
	}

	auto& at = m_prompt_cfg.main_panel.action_text;
	LoadTextLabel(xml, "wsui_prompt:main_panel:action_text:verb", at.verb);
	LoadTextLabel(xml, "wsui_prompt:main_panel:action_text:object_name", at.object_name);
	LoadTextLabel(xml, "wsui_prompt:main_panel:action_text:full_line", at.full_line);
}

void CInteractionMarkerManager::LoadPromptItemStackCount(CUIXml& xml)
{
	const LPCSTR path = "wsui_prompt:item_stack_count";
	if (!xml.NavigateToNode(path, 0))
		return;

	m_prompt_cfg.item_stack_count.group_distance = xml.ReadAttribFlt(path, 0, "group_distance", 0.f);
}

void CInteractionMarkerManager::LoadPromptItemCondition(CUIXml& xml)
{
	const LPCSTR path = "wsui_prompt:item_condition";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& cond = m_prompt_cfg.item_condition;
	cond.x = xml.ReadAttribFlt(path, 0, "x", 0.f);
	cond.y = xml.ReadAttribFlt(path, 0, "y", 0.f);
	cond.line_spacing = xml.ReadAttribFlt(path, 0, "line_spacing", 0.f);
	LoadTextLabel(xml, "wsui_prompt:item_condition:label", cond.label);
	LoadBackground(xml, "wsui_prompt:item_condition:background", cond.background, true);

	const LPCSTR grad_path = "wsui_prompt:item_condition:color_gradient";
	if (xml.NavigateToNode(grad_path, 0))
	{
		const int min_r = xml.ReadAttribInt(grad_path, 0, "min_r", 0);
		const int min_g = xml.ReadAttribInt(grad_path, 0, "min_g", 0);
		const int min_b = xml.ReadAttribInt(grad_path, 0, "min_b", 0);
		const int min_a = xml.ReadAttribInt(grad_path, 0, "min_a", 0);
		const int max_r = xml.ReadAttribInt(grad_path, 0, "max_r", 0);
		const int max_g = xml.ReadAttribInt(grad_path, 0, "max_g", 0);
		const int max_b = xml.ReadAttribInt(grad_path, 0, "max_b", 0);
		const int max_a = xml.ReadAttribInt(grad_path, 0, "max_a", 0);
		cond.color_min = color_rgba(min_r, min_g, min_b, min_a);
		cond.color_max = color_rgba(max_r, max_g, max_b, max_a);
	}
}

void CInteractionMarkerManager::LoadPromptItemCard(CUIXml& xml)
{
	const LPCSTR path = "wsui_prompt:item_card";
	if (!xml.NavigateToNode(path, 0))
		return;

	auto& card = m_prompt_cfg.item_card;
	card.x = xml.ReadAttribFlt(path, 0, "x", 0.f);
	card.y = xml.ReadAttribFlt(path, 0, "y", 0.f);
	LoadBackground(xml, "wsui_prompt:item_card:background", card.background);
	LoadItemCardMetric(xml, "wsui_prompt:item_card:weight_metric", card.weight);
	LoadItemCardMetric(xml, "wsui_prompt:item_card:value_metric", card.value);
}

void CInteractionMarkerManager::LoadPromptTutorialScreen(CUIXml& xml)
{
	const LPCSTR path = "wsui_prompt:tutorial_screen";
	if (!xml.NavigateToNode(path, 0))
		return;

	m_prompt_cfg.tutorial_x = xml.ReadAttribFlt(path, 0, "x", 0.f);
	m_prompt_cfg.tutorial_y = xml.ReadAttribFlt(path, 0, "y", 0.f);
}

void CInteractionMarkerManager::LoadItemCardMetric(CUIXml& xml, LPCSTR path, SWSUIItemCardMetric& out) const
{
	if (!xml.NavigateToNode(path, 0))
		return;

	out.x = xml.ReadAttribFlt(path, 0, "x", out.x);
	out.y = xml.ReadAttribFlt(path, 0, "y", out.y);
	out.icon_x = xml.ReadAttribFlt(path, 0, "icon_x", out.icon_x);
	out.icon_y = xml.ReadAttribFlt(path, 0, "icon_y", out.icon_y);
	out.icon_w = xml.ReadAttribFlt(path, 0, "icon_w", out.icon_w);
	out.icon_h = xml.ReadAttribFlt(path, 0, "icon_h", out.icon_h);
	out.text_x = xml.ReadAttribFlt(path, 0, "text_x", out.text_x);
	out.text_y = xml.ReadAttribFlt(path, 0, "text_y", out.text_y);
	out.text_height = xml.ReadAttribFlt(path, 0, "text_height", out.text_height);
	LoadIconRefSlot(xml, path, out.icon, "icon", "icon", "svg");

	if (LPCSTR font_name = xml.ReadAttrib(path, 0, "font", nullptr))
		out.font_name = font_name;

	u32 color = out.color;
	CGameFont* loaded_font = nullptr;
	if (CUIXmlInit::InitFont(xml, path, 0, color, loaded_font) && loaded_font)
	{
		out.font = loaded_font;
		out.color = color;
	}
	else if (g_FontManager && g_FontManager->pFontSystem)
		out.font = g_FontManager->pFontSystem;

	CUIXmlInit::ReadShadowsNode(xml, path, 0, out.text_shadow);
}

void CInteractionMarkerManager::LoadFocusSound()
{
	HUD_SOUND_ITEM::DestroySound(m_focus_snd);
	m_focus_sound_loaded = false;

	if (!m_markers_cfg.features.enable_focus_sound)
		return;

	if (!pSettings->section_exist("wsui_sounds"))
		return;

	if (!pSettings->line_exist("wsui_sounds", "snd_focus"))
		return;

	HUD_SOUND_ITEM::LoadSound("wsui_sounds", "snd_focus", m_focus_snd, SOUND_TYPE_IDLE);
	m_focus_sound_loaded = !m_focus_snd.sounds.empty();
}

void CInteractionMarkerManager::LoadFloatLookupSection(LPCSTR section_name, xr_map<shared_str, float>& out)
{
	out.clear();
	if (!section_name || !pSettings->section_exist(section_name))
		return;

	const CInifile::Sect& sect = pSettings->r_section(section_name);
	for (const auto& line : sect.Data)
		out[line.first] = (float)atof(*line.second);
}

void CInteractionMarkerManager::LoadTextureLookupSection(LPCSTR section_name, xr_map<shared_str, shared_str>& out)
{
	out.clear();
	if (!section_name || !pSettings->section_exist(section_name))
		return;

	const CInifile::Sect& sect = pSettings->r_section(section_name);
	for (const auto& line : sect.Data)
		out[line.first] = line.second;
}

namespace
{
	void CopyTrimmedToken(LPSTR dst, size_t dst_size, LPCSTR src, size_t len)
	{
		if (!dst || dst_size == 0)
			return;

		dst[0] = 0;
		if (!src || len == 0)
			return;

		while (len > 0 && (src[0] == ' ' || src[0] == '\t'))
		{
			++src;
			--len;
		}

		while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t'))
			--len;

		const size_t copy_len = std::min(len, dst_size - 1);
		strncpy_s(dst, dst_size, src, copy_len);
		dst[copy_len] = 0;
	}
}

void CInteractionMarkerManager::LoadPromptSplitSection(LPCSTR section_name)
{
	m_prompt_split_by_string_id.clear();
	if (!section_name || !pSettings->section_exist(section_name))
		return;

	const CInifile::Sect& sect = pSettings->r_section(section_name);
	for (const auto& line : sect.Data)
	{
		LPCSTR value = *line.second;
		if (!value || !value[0])
			continue;

		SWSUIPromptSplitDef def;
		LPCSTR pipe = strchr(value, '|');
		if (pipe)
		{
			string256 verb_buf = {};
			string256 name_buf = {};
			CopyTrimmedToken(verb_buf, sizeof(verb_buf), value, pipe - value);
			CopyTrimmedToken(name_buf, sizeof(name_buf), pipe + 1, strlen(pipe + 1));
			if (!verb_buf[0])
				continue;

			def.verb_id = verb_buf;
			if (name_buf[0])
				def.name_id = name_buf;
		}
		else
		{
			string256 verb_buf = {};
			CopyTrimmedToken(verb_buf, sizeof(verb_buf), value, strlen(value));
			if (!verb_buf[0])
				continue;

			def.verb_id = verb_buf;
		}

		m_prompt_split_by_string_id[line.first] = def;
	}
}

void CInteractionMarkerManager::LoadLookupSection(LPCSTR section_name, bool is_pos_adj)
{
	if (!section_name || !pSettings->section_exist(section_name))
		return;

	const CInifile::Sect& sect = pSettings->r_section(section_name);
	for (const auto& line : sect.Data)
	{
		if (is_pos_adj)
			m_pos_adj_by_section[line.first] = ParseVector3(*line.second);
		else
			m_bones_by_section[line.first] = line.second;
	}
}

void CInteractionMarkerManager::LoadBonePriority()
{
	m_bone_priority.clear();

	if (!pSettings->section_exist("bones"))
		return;

	const CInifile::Sect& sect = pSettings->r_section("bones");
	for (const auto& line : sect.Data)
		m_bone_priority.push_back(line.first);
}
