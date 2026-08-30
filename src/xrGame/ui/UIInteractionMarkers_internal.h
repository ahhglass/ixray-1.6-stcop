#pragma once

#include "UIInteractionMarkers.h"

namespace WSUIInternal
{
	LPCSTR WsuiString(LPCSTR id);
	SWSUITextureSlot MakeRasterSlot(LPCSTR raster);
	shared_str NormalizeWsuiSvgSubpath(shared_str path);
	bool WsuiSvgFileExists(LPCSTR subpath);
	bool WsuiCategoryEq(shared_str id, LPCSTR name);
	u32 ParseCategoryFeatures(LPCSTR features);
	bool CategoryHasFeature(const SWSUICategoryDef& def, EWSUICategoryFeature feature);
	u32 ParseRuleFlags(LPCSTR token);
	void ParseClassRuleValue(LPCSTR value, SWSUIClassRule& out);
	void TrimInPlace(char* text);
	void ApplyDefaultCategoryBehavior(SWSUICategoryDef& def, shared_str category_id);
}
