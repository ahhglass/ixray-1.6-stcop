#include "StdAfx.h"
#include "UIInteractionMarkers_internal.h"

#include "../../xrUI/UITextureMaster.h"
#include "../xrEngine/string_table.h"
#include "../../xrCore/LocatorAPI_defs.h"
#include "../../xrCore/LocatorAPI.h"
#include "../../xrCore/Platform/Platform.h"

namespace WSUIInternal
{
	LPCSTR WsuiString(LPCSTR id)
	{
		return g_pStringTable->translate(id).c_str();
	}

	SWSUITextureSlot MakeRasterSlot(LPCSTR raster)
	{
		SWSUITextureSlot slot;
		if (raster && raster[0])
			slot.raster = raster;
		return slot;
	}

	shared_str NormalizeWsuiSvgSubpath(shared_str path)
	{
		if (!path.size())
			return path;

		bool needs_normalize = false;
		for (LPCSTR p = path.c_str(); *p; ++p)
		{
			if (*p == '/')
			{
				needs_normalize = true;
				break;
			}
		}

		if (!needs_normalize)
			return path;

		string_path normalized;
		xr_strcpy(normalized, path.c_str());
		for (char* p = normalized; *p; ++p)
		{
			if (*p == '/')
				*p = Platform::kPreferredSeparator[0];
		}
		return normalized;
	}

	bool WsuiSvgFileExists(LPCSTR subpath)
	{
		if (!subpath || !subpath[0])
			return false;

		string_path normalized_subpath;
		xr_strcpy(normalized_subpath, subpath);
		for (char* p = normalized_subpath; *p; ++p)
		{
			if (*p == '/')
				*p = Platform::kPreferredSeparator[0];
		}

		char buf[256];
		xr_sprintf(buf, sizeof(buf), "ui%s%s", Platform::kPreferredSeparator, normalized_subpath);
		string_path fn;
		FS.update_path(fn, _game_textures_, buf);

		IReader* reader = FS.r_open(fn);
		if (!reader)
			return false;

		FS.r_close(reader);
		return true;
	}

	bool WsuiCategoryEq(shared_str id, LPCSTR name)
	{
		return id.size() && name && name[0] && !xr_strcmp(id.c_str(), name);
	}

	u32 ParseCategoryFeatures(LPCSTR features)
	{
		u32 flags = 0;
		if (!features || !features[0])
			return flags;

		string256 copy;
		xr_strcpy(copy, features);

		for (char* token = strtok(copy, ",; "); token; token = strtok(nullptr, ",; "))
		{
			if (!xr_stricmp(token, "stack_count"))
				flags |= static_cast<u32>(EWSUICategoryFeature::StackCount);
			else if (!xr_stricmp(token, "item_card"))
				flags |= static_cast<u32>(EWSUICategoryFeature::ItemCard);
			else if (!xr_stricmp(token, "condition") || !xr_stricmp(token, "item_condition"))
				flags |= static_cast<u32>(EWSUICategoryFeature::ItemCondition);
			else if (!xr_stricmp(token, "wheel_cycle"))
				flags |= static_cast<u32>(EWSUICategoryFeature::WheelCycle);
			else if (!xr_stricmp(token, "suppress_tutorial"))
				flags |= static_cast<u32>(EWSUICategoryFeature::SuppressTutorial);
		}

		return flags;
	}

	bool CategoryHasFeature(const SWSUICategoryDef& def, EWSUICategoryFeature feature)
	{
		return (def.feature_flags & static_cast<u32>(feature)) != 0;
	}

	u32 ParseRuleFlags(LPCSTR token)
	{
		if (!token || !token[0])
			return 0;

		if (!xr_stricmp(token, "require_corpse"))
			return static_cast<u32>(EWSUIRuleFlag::RequireCorpse);
		if (!xr_stricmp(token, "can_take"))
			return static_cast<u32>(EWSUIRuleFlag::CanTake);
		if (!xr_stricmp(token, "require_usable_tip"))
			return static_cast<u32>(EWSUIRuleFlag::RequireUsableTip);

		return 0;
	}

	void ParseClassRuleValue(LPCSTR value, SWSUIClassRule& out)
	{
		out.category_id = nullptr;
		out.detector = nullptr;
		out.param = nullptr;
		out.flags = 0;

		if (!value || !value[0])
			return;

		string512 copy;
		xr_strcpy(copy, value);

		char* token = strtok(copy, "|");
		if (!token)
			return;

		TrimInPlace(token);
		out.category_id = token;

		token = strtok(nullptr, "|");
		if (!token)
			return;

		TrimInPlace(token);
		out.detector = token;

		while ((token = strtok(nullptr, "|")) != nullptr)
		{
			TrimInPlace(token);
			if (!token[0])
				continue;

			const u32 flag = ParseRuleFlags(token);
			if (flag)
				out.flags |= flag;
			else if (!out.param.size())
				out.param = token;
		}
	}

	void TrimInPlace(char* str)
	{
		if (!str || !str[0])
			return;

		char* start = str;
		while (*start == ' ' || *start == '\t')
			++start;

		char* end = start + xr_strlen(start);
		while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
			--end;
		*end = 0;

		if (start != str)
			memmove(str, start, xr_strlen(start) + 1);
	}

	void ApplyDefaultCategoryBehavior(SWSUICategoryDef& def, shared_str category_id)
	{
		if (!def.pos_mode.size())
		{
			if (WsuiCategoryEq(category_id, "door"))
				def.pos_mode = "door";
			else if (WsuiCategoryEq(category_id, "zone") || WsuiCategoryEq(category_id, "campfire"))
				def.pos_mode = "zone";
			else if (WsuiCategoryEq(category_id, "item"))
				def.pos_mode = "bone";
			else
				def.pos_mode = "bone";
		}

		if (!def.prompt_mode.size())
		{
			if (WsuiCategoryEq(category_id, "item") || WsuiCategoryEq(category_id, "npc") || WsuiCategoryEq(category_id, "body") || WsuiCategoryEq(category_id, "stash"))
				def.prompt_mode = "split";
			else if (WsuiCategoryEq(category_id, "zone"))
				def.prompt_mode = "zone_lookup";
			else if (WsuiCategoryEq(category_id, "campfire"))
				def.prompt_mode = "campfire";
			else if (WsuiCategoryEq(category_id, "usable") || WsuiCategoryEq(category_id, "door"))
				def.prompt_mode = "usable_tip";
			else
				def.prompt_mode = "full";
		}

		if (!def.verb_id.size())
		{
			if (WsuiCategoryEq(category_id, "item"))
				def.verb_id = "ui_st_wsui_pickup";
			else if (WsuiCategoryEq(category_id, "npc"))
				def.verb_id = "ui_st_wsui_talk";
			else if (WsuiCategoryEq(category_id, "body"))
				def.verb_id = "ui_st_wsui_search";
			else if (WsuiCategoryEq(category_id, "stash"))
				def.verb_id = "ui_st_wsui_open";
		}

		if (!def.name_source.size())
		{
			if (WsuiCategoryEq(category_id, "item"))
				def.name_source = "item_name";
			else if (WsuiCategoryEq(category_id, "npc"))
				def.name_source = "stalker_name";
			else if (WsuiCategoryEq(category_id, "body"))
				def.name_source = "body_name";
			else
				def.name_source = "none";
		}

		if (!def.campfire_on_tip.size())
			def.campfire_on_tip = "st_extinguish_fire";
		if (!def.campfire_off_tip.size())
			def.campfire_off_tip = "st_ignite_fire";
	}
}
