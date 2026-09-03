#pragma once

#include "../../xrCore/vector.h"
#include "../../xrUI/Widgets/UIWindow.h"
#include "../GameTaskDefs.h"

class CUIStatic;
class CUIXml;
class CUIXmlInit;
class CMapLocation;

struct SQuestWaypointRuntimeConfig
{
	bool enabled = true;
	float y_offset = 34.0f;
	float edge_margin = 12.0f;
	float on_screen_margin = 32.0f;
	float edge_icon_inset = 0.0f;
	bool edge_rotate = true;
	float edge_heading_offset = 0.0f;
	float distance_epsilon = 1.0f;
	bool show_distance = false;
	bool show_border = true;
	shared_str distance_format;
	shared_str storyline_texture;
	shared_str additional_texture;
	shared_str arrow_texture;
	shared_str border_texture;
};

struct SQuestWaypointDirtyState
{
	u32 last_logic_frame = u32(-1);
	float last_distance_meters = -1.0f;
	shared_str last_marker_texture;
	shared_str last_arrow_texture;
	u32 last_objective_key = u32(-1);
};

enum class EQuestWaypointDisplayMode : u8
{
	Hidden = 0,
	OnScreen,
	OffScreen,
};

class CUIQuestWaypoint final : public CUIWindow
{
	using inherited = CUIWindow;

public:
	CUIQuestWaypoint();
	~CUIQuestWaypoint() override;

	void Init();
	void Update() override;
	void Reset();

	bool IsInitialized() const { return _is_initialized; }

private:
	void LoadConfig(CUIXml& uiXml);
	void CreateWidgets(CUIXml& uiXml, CUIXmlInit& xmlInit);
	bool CanUpdateHud() const;
	CMapLocation* ResolveTrackedLocation();
	bool IsTrackableLocation(CMapLocation* loc);
	bool ResolveTargetWorldPos(CMapLocation* loc, Fvector& out_pos) const;
	bool ProjectWorldToScreen(const Fvector& world_pos, Fvector2& out_screen, bool& out_behind) const;
	bool ComputeHorizontalBearingDir(const Fvector& actor_pos, const Fvector& target_pos, float cam_heading,
		Fvector2& out_dir) const;
	float ComputePointerHeading(const Fvector2& target_screen_pos, float center_x, float center_y) const;
	bool ComputeBearingScreenPos(const Fvector& actor_pos, const Fvector& target_pos, float cam_heading,
		Fvector2& out_screen, float& out_angle) const;
	bool ClampToScreenEdge(Fvector2& io_dir_from_center, float icon_half_extent) const;
	void ApplyMarkerTexture(ETaskType task_type);
	void ApplyArrowTexture(ETaskType task_type);
	void SetupArrowPivot();
	void UpdateDistanceText(const Fvector& actor_pos, const Fvector& target_pos);
	void SetDisplayMode(EQuestWaypointDisplayMode mode);
	void PlaceWidget(CUIStatic* widget, const Fvector2& center, float heading, bool apply_heading);
	void HideWaypoint();
	void UpdateOnScreenMarker(const Fvector2& screenPos, const Fvector& actorPos, const Fvector& targetPos);
	bool TryUpdateOffScreenArrow(const Fvector& actorPos, const Fvector& targetPos, float camHeading,
		const Fvector2& screenPos, bool projected);

private:
	CUIStatic* _marker;
	CUIStatic* _border;
	CUIStatic* _arrow;
	CUIStatic* _distance_text;

	SQuestWaypointRuntimeConfig _cfg;
	SQuestWaypointDirtyState _dirty;

	CMapLocation* _cached_location;
	EQuestWaypointDisplayMode _display_mode;
	bool _is_initialized;
};
