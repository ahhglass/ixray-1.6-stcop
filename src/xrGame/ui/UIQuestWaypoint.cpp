#include "StdAfx.h"
#include "UIQuestWaypoint.h"

#include "../Actor.h"
#include "../Level.h"
#include "../GametaskManager.h"
#include "../GameTask.h"
#include "../map_location.h"
#include "../game_cl_single.h"
#include "../../xrEngine/device.h"
#include "../../xrEngine/CustomHUD.h"
#include "../../xrUI/UIHelper.h"
#include "../../xrUI/UIXmlInit.h"
#include "../../xrUI/Widgets/UIStatic.h"
#include "../../xrUI/UITextureMaster.h"
#include "../../xrCore/EngineExternal.h"

extern ENGINE_API Flags32 psHUD_Flags;

Fvector2 World2Ui(Fvector pos, bool hud, bool allow_offscreen);

namespace
{
constexpr float kInvalidScreen = -9999.0f;

u32 PackObjectiveKey(SGameTaskObjective* objective)
{
	if (!objective)
		return u32(-1);

	u32 key = objective->m_map_object_id;
	key ^= u32(objective->m_map_location.size()) << 16;
	return key;
}

float ArrowHalfExtent(const CUIStatic* arrow)
{
	if (!arrow)
		return 0.0f;

	const float w = arrow->GetWidth();
	const float h = arrow->GetHeight();
	return (w > h ? w : h) * 0.5f;
}
} // namespace

CUIQuestWaypoint::CUIQuestWaypoint()
	: _marker(nullptr),
	  _border(nullptr),
	  _arrow(nullptr),
	  _distance_text(nullptr),
	  _cached_location(nullptr),
	  _display_mode(EQuestWaypointDisplayMode::Hidden),
	  _is_initialized(false)
{
	_cfg.distance_format = "%.0f m";
	_cfg.storyline_texture = "ui_qw_primary_mission";
	_cfg.additional_texture = "ui_qw_secondary_mission";
	_cfg.arrow_texture = "ui_hud_map_arrow";
	_cfg.border_texture = "ui_pda2_stask_last_02";
}

CUIQuestWaypoint::~CUIQuestWaypoint()
{
	_cached_location = nullptr;
	_marker = nullptr;
	_border = nullptr;
	_arrow = nullptr;
	_distance_text = nullptr;
}

void CUIQuestWaypoint::Reset()
{
	_cached_location = nullptr;
	_dirty.last_objective_key = u32(-1);
	HideWaypoint();
}

void CUIQuestWaypoint::Init()
{
	_is_initialized = false;
	_cached_location = nullptr;
	_display_mode = EQuestWaypointDisplayMode::Hidden;
	_marker = nullptr;
	_border = nullptr;
	_arrow = nullptr;
	_distance_text = nullptr;

	if (!m_ChildWndList.empty())
	{
		DetachAll();
	}

	CUIXml uiXml;
	if (!uiXml.Load(CONFIG_PATH, UI_PATH, "quest_waypoint.xml"))
	{
		return;
	}

	if (!uiXml.NavigateToNode("quest_waypoint", 0))
	{
		Msg("! CUIQuestWaypoint::Init: node 'quest_waypoint' not found in %s", uiXml.m_xml_file_name);
		return;
	}

	LoadConfig(uiXml);

	if (!_cfg.enabled)
	{
		return;
	}

	CUIXmlInit xmlInit;
	xmlInit.InitWindow(uiXml, "quest_waypoint", 0, this);
	SetWndPos(Fvector2().set(0.0f, 0.0f));
	SetWndSize(Fvector2().set(UI_BASE_WIDTH, UI_BASE_HEIGHT));

	CreateWidgets(uiXml, xmlInit);
	HideWaypoint();
	_is_initialized = (_marker != nullptr && _arrow != nullptr);
}

void CUIQuestWaypoint::LoadConfig(CUIXml& uiXml)
{
	LPCSTR root = "quest_waypoint";
	_cfg.enabled = uiXml.ReadAttribInt(root, 0, "enabled", 1) != 0;
	_cfg.y_offset = uiXml.ReadAttribFlt(root, 0, "y_offset", _cfg.y_offset);
	_cfg.edge_margin = uiXml.ReadAttribFlt(root, 0, "edge_margin", _cfg.edge_margin);
	_cfg.on_screen_margin = uiXml.ReadAttribFlt(root, 0, "on_screen_margin", _cfg.on_screen_margin);
	_cfg.edge_icon_inset = uiXml.ReadAttribFlt(root, 0, "edge_icon_inset", _cfg.edge_icon_inset);
	_cfg.edge_rotate = uiXml.ReadAttribInt(root, 0, "edge_rotate", _cfg.edge_rotate ? 1 : 0) != 0;
	_cfg.edge_heading_offset = deg2rad(uiXml.ReadAttribFlt(root, 0, "edge_heading_angle", 0.0f));
	_cfg.distance_epsilon = uiXml.ReadAttribFlt(root, 0, "distance_epsilon", _cfg.distance_epsilon);
	_cfg.show_distance = uiXml.ReadAttribInt(root, 0, "show_distance", 0) != 0;
	_cfg.show_border = uiXml.ReadAttribInt(root, 0, "show_border", 1) != 0;

	if (uiXml.NavigateToNode("quest_waypoint:icons", 0))
	{
		LPCSTR icons = "quest_waypoint:icons";
		_cfg.storyline_texture = uiXml.ReadAttrib(icons, 0, "storyline", _cfg.storyline_texture.c_str());
		_cfg.additional_texture = uiXml.ReadAttrib(icons, 0, "additional", _cfg.additional_texture.c_str());
		_cfg.arrow_texture = uiXml.ReadAttrib(icons, 0, "arrow", _cfg.arrow_texture.c_str());
		_cfg.border_texture = uiXml.ReadAttrib(icons, 0, "border", _cfg.border_texture.c_str());
	}

	if (uiXml.NavigateToNode("quest_waypoint:distance_text", 0))
	{
		LPCSTR dist = "quest_waypoint:distance_text";
		_cfg.show_distance = uiXml.ReadAttribInt(dist, 0, "show", _cfg.show_distance ? 1 : 0) != 0;
		_cfg.distance_format = uiXml.ReadAttrib(dist, 0, "format", _cfg.distance_format.c_str());
	}

	if (uiXml.NavigateToNode("quest_waypoint:arrow", 0))
	{
		LPCSTR arrow = "quest_waypoint:arrow";
		const int rotate_attr = uiXml.ReadAttribInt(arrow, 0, "rotate", -1);
		if (rotate_attr >= 0)
		{
			_cfg.edge_rotate = rotate_attr != 0;
		}
		const float heading_angle = uiXml.ReadAttribFlt(arrow, 0, "heading_angle", -9999.0f);
		if (heading_angle > -9000.0f)
		{
			_cfg.edge_heading_offset = deg2rad(heading_angle);
		}
	}
}

void CUIQuestWaypoint::CreateWidgets(CUIXml& uiXml, CUIXmlInit& xmlInit)
{
	_marker = UIHelper::CreateStatic(uiXml, "quest_waypoint:marker", this, false);
	_border = UIHelper::CreateStatic(uiXml, "quest_waypoint:marker:border", _marker, false);
	_arrow = UIHelper::CreateStatic(uiXml, "quest_waypoint:arrow", this, false);
	_distance_text = UIHelper::CreateStatic(uiXml, "quest_waypoint:distance_text", this, false);

	if (!_marker)
	{
		_marker = new CUIStatic();
		_marker->SetAutoDelete(true);
		_marker->SetWndSize(Fvector2().set(20.0f, 20.0f));
		AttachChild(_marker);
		CUITextureMaster::InitTexture(_cfg.storyline_texture.c_str(), &_marker->GetUIStaticItem());
	}

	if (!_border && _cfg.show_border)
	{
		_border = new CUIStatic();
		_border->SetAutoDelete(true);
		_border->SetWndSize(Fvector2().set(24.0f, 24.0f));
		_border->SetWndPos(Fvector2().set(-2.0f, -2.0f));
		_marker->AttachChild(_border);
		CUITextureMaster::InitTexture(_cfg.border_texture.c_str(), &_border->GetUIStaticItem());
	}

	if (!_arrow)
	{
		_arrow = new CUIStatic();
		_arrow->SetAutoDelete(true);
		_arrow->SetWndSize(Fvector2().set(18.0f, 18.0f));
		AttachChild(_arrow);
		CUITextureMaster::InitTexture(_cfg.arrow_texture.c_str(), &_arrow->GetUIStaticItem());
	}

	if (!_distance_text && _cfg.show_distance)
	{
		_distance_text = new CUIStatic();
		_distance_text->SetAutoDelete(true);
		_distance_text->SetWndSize(Fvector2().set(80.0f, 16.0f));
		AttachChild(_distance_text);
	}

	if (_border)
	{
		_border->Show(_cfg.show_border);
	}
	if (_distance_text)
	{
		_distance_text->Show(false);
	}

	SetupArrowPivot();
}

bool CUIQuestWaypoint::CanUpdateHud() const
{
	if (!g_pGameLevel || !Level().CurrentViewEntity())
	{
		return false;
	}

	CActor* actor = Level().CurrentViewEntity()->cast_actor();
	if (!actor || !actor->g_Alive())
	{
		return false;
	}

	const static bool no_hud_on_master = EngineExternal()[EEngineExternalUI::DisableHudRenderingOnMaster];
	const bool render_hud = no_hud_on_master ? (g_SingleGameDifficulty < egdVeteran) : true;

	return render_hud && psHUD_Flags.test(HUD_DRAW);
}

CMapLocation* CUIQuestWaypoint::ResolveTrackedLocation()
{
	if (!Level().GameTaskManager())
	{
		return nullptr;
	}

	SGameTaskObjective* objective = Level().GameTaskManager()->ActiveObjective();
	if (!objective)
	{
		_dirty.last_objective_key = u32(-1);
		return nullptr;
	}

	const u32 objective_key = PackObjectiveKey(objective);
	if (objective_key != _dirty.last_objective_key)
	{
		_cached_location = nullptr;
		_dirty.last_objective_key = objective_key;
	}

	if (objective->m_map_object_id == u16(-1) || !objective->m_map_location.size())
	{
		return nullptr;
	}

	if (!_cached_location)
	{
		_cached_location = objective->LinkedMapLocation();
	}

	return _cached_location;
}

bool CUIQuestWaypoint::IsTrackableLocation(CMapLocation* loc)
{
	if (!loc)
	{
		return false;
	}

	if (!loc->Update())
	{
		_cached_location = nullptr;
		return false;
	}

	if (!loc->SpotEnabled())
	{
		return false;
	}

	return loc->GetLevelName() == Level().name();
}

bool CUIQuestWaypoint::ResolveTargetWorldPos(CMapLocation* loc, Fvector& out_pos) const
{
	if (!loc)
	{
		return false;
	}

	out_pos = loc->GetLastPosition();
	out_pos.y += 0.5f;
	return true;
}

bool CUIQuestWaypoint::ProjectWorldToScreen(const Fvector& world_pos, Fvector2& out_screen, bool& out_behind) const
{
	out_behind = false;
	out_screen = World2Ui(world_pos, false, true);
	if (out_screen.x <= kInvalidScreen + 1.0f)
	{
		out_behind = true;
		return false;
	}
	return true;
}

bool CUIQuestWaypoint::ComputeHorizontalBearingDir(const Fvector& actor_pos, const Fvector& target_pos,
	float cam_heading, Fvector2& out_dir) const
{
	Fvector2 to_target;
	to_target.set(target_pos.x - actor_pos.x, target_pos.z - actor_pos.z);
	if (to_target.square_magnitude() < 0.0001f)
	{
		return false;
	}

	const float target_yaw = to_target.getH();
	const float rel = angle_normalize_signed(target_yaw - cam_heading);
	out_dir.x = sin(rel);
	out_dir.y = -cos(rel);

	if (out_dir.square_magnitude() < 0.0001f)
	{
		out_dir.set(0.0f, -1.0f);
	}
	else
	{
		out_dir.normalize();
	}

	return true;
}

float CUIQuestWaypoint::ComputePointerHeading(const Fvector2& target_screen_pos, float center_x, float center_y) const
{
	Fvector2 dir_to_center;
	dir_to_center.set(center_x, center_y);
	dir_to_center.sub(target_screen_pos);
	if (dir_to_center.square_magnitude() < 0.0001f)
	{
		return 0.0f;
	}
	dir_to_center.normalize();
	return -dir_to_center.getH();
}

bool CUIQuestWaypoint::ComputeBearingScreenPos(const Fvector& actor_pos, const Fvector& target_pos, float cam_heading,
	Fvector2& out_screen, float& out_angle) const
{
	Fvector2 dir;
	if (!ComputeHorizontalBearingDir(actor_pos, target_pos, cam_heading, dir))
	{
		return false;
	}

	const float center_x = UI_BASE_WIDTH * 0.5f;
	const float center_y = UI_BASE_HEIGHT * 0.5f;
	Fvector2 synthetic_target;
	synthetic_target.set(center_x + dir.x * UI_BASE_WIDTH, center_y + dir.y * UI_BASE_HEIGHT);
	out_angle = ComputePointerHeading(synthetic_target, center_x, center_y);

	out_screen = dir;
	const float icon_half = ArrowHalfExtent(_arrow);
	return ClampToScreenEdge(out_screen, icon_half);
}

bool CUIQuestWaypoint::ClampToScreenEdge(Fvector2& io_dir_from_center, float icon_half_extent) const
{
	const float center_x = UI_BASE_WIDTH * 0.5f;
	const float center_y = UI_BASE_HEIGHT * 0.5f;
	const float margin = _cfg.edge_margin + _cfg.edge_icon_inset + icon_half_extent;

	Fvector2 dir = io_dir_from_center;
	if (fabsf(dir.x) < 0.0001f && fabsf(dir.y) < 0.0001f)
	{
		dir.set(0.0f, -1.0f);
	}
	dir.normalize();

	float t = flt_max;
	if (dir.x > 0.0f)
	{
		t = std::min(t, (UI_BASE_WIDTH - margin - center_x) / dir.x);
	}
	else if (dir.x < 0.0f)
	{
		t = std::min(t, (margin - center_x) / dir.x);
	}

	if (dir.y > 0.0f)
	{
		t = std::min(t, (UI_BASE_HEIGHT - margin - center_y) / dir.y);
	}
	else if (dir.y < 0.0f)
	{
		t = std::min(t, (margin - center_y) / dir.y);
	}

	if (!(_valid(t) && t > 0.0f))
	{
		return false;
	}

	io_dir_from_center.set(center_x + dir.x * t, center_y + dir.y * t);
	return true;
}

void CUIQuestWaypoint::ApplyMarkerTexture(ETaskType task_type)
{
	const shared_str& tex = (task_type == eTaskTypeStoryline) ? _cfg.storyline_texture : _cfg.additional_texture;
	if (_dirty.last_marker_texture == tex || !_marker)
	{
		return;
	}

	CUITextureMaster::InitTexture(tex.c_str(), &_marker->GetUIStaticItem());
	_dirty.last_marker_texture = tex;
}

void CUIQuestWaypoint::ApplyArrowTexture(ETaskType task_type)
{
	if (!_arrow)
	{
		return;
	}

	shared_str tex = _cfg.arrow_texture;
	if (!tex.size())
	{
		tex = (task_type == eTaskTypeStoryline) ? _cfg.storyline_texture : _cfg.additional_texture;
	}

	if (_dirty.last_arrow_texture == tex)
	{
		return;
	}

	CUITextureMaster::InitTexture(tex.c_str(), &_arrow->GetUIStaticItem());
	_dirty.last_arrow_texture = tex;
	SetupArrowPivot();
}

void CUIQuestWaypoint::SetupArrowPivot()
{
	if (!_arrow)
	{
		return;
	}

	const Fvector2 size = _arrow->GetWndSize();
	const Fvector2 pivot(size.x * 0.5f, size.y * 0.5f);
	_arrow->SetHeadingPivot(pivot, pivot, true);
}

void CUIQuestWaypoint::UpdateDistanceText(const Fvector& actor_pos, const Fvector& target_pos)
{
	if (!_distance_text || !_cfg.show_distance)
	{
		return;
	}

	const float dist = actor_pos.distance_to(target_pos);
	const float dist_rounded = float(iFloor(dist + 0.5f));
	if (fabsf(dist_rounded - _dirty.last_distance_meters) < _cfg.distance_epsilon)
	{
		_distance_text->Show(true);
		return;
	}

	_dirty.last_distance_meters = dist_rounded;
	string64 buf;
	xr_sprintf(buf, sizeof(buf), _cfg.distance_format.c_str(), dist);
	_distance_text->SetText(buf);
	_distance_text->Show(true);
}

void CUIQuestWaypoint::SetDisplayMode(EQuestWaypointDisplayMode mode)
{
	if (_display_mode == mode)
	{
		return;
	}

	_display_mode = mode;
	const bool on_screen = mode == EQuestWaypointDisplayMode::OnScreen;
	const bool off_screen = mode == EQuestWaypointDisplayMode::OffScreen;

	if (_marker)
	{
		_marker->Show(on_screen);
	}
	if (_border)
	{
		_border->Show(on_screen && _cfg.show_border);
	}
	if (_arrow)
	{
		_arrow->Show(off_screen);
	}
	if (_distance_text && mode == EQuestWaypointDisplayMode::Hidden)
	{
		_distance_text->Show(false);
	}
}

void CUIQuestWaypoint::HideWaypoint()
{
	_display_mode = EQuestWaypointDisplayMode::Hidden;

	if (_marker)
	{
		_marker->Show(false);
	}
	if (_border)
	{
		_border->Show(false);
	}
	if (_arrow)
	{
		_arrow->Show(false);
	}
	if (_distance_text)
	{
		_distance_text->Show(false);
	}

	Show(false);
}

void CUIQuestWaypoint::UpdateOnScreenMarker(const Fvector2& screenPos, const Fvector& actorPos, const Fvector& targetPos)
{
	Fvector2 pos = screenPos;
	pos.y -= _cfg.y_offset;
	Show(true);
	SetDisplayMode(EQuestWaypointDisplayMode::OnScreen);
	PlaceWidget(_marker, pos, 0.0f, false);
	UpdateDistanceText(actorPos, targetPos);
}

bool CUIQuestWaypoint::TryUpdateOffScreenArrow(const Fvector& actorPos, const Fvector& targetPos, float camHeading,
	const Fvector2& screenPos, bool projected)
{
	float edge_angle = 0.0f;
	Fvector2 edge_pos;
	bool has_edge = false;

	const float center_x = UI_BASE_WIDTH * 0.5f;
	const float center_y = UI_BASE_HEIGHT * 0.5f;

	if (projected)
	{
		Fvector2 screen_dir;
		screen_dir.sub(screenPos, Fvector2().set(center_x, center_y));
		if (screen_dir.square_magnitude() < 0.0001f)
		{
			screen_dir.set(0.0f, -1.0f);
		}
		else
		{
			screen_dir.normalize();
		}

		Fvector2 clamp_dir = screen_dir;
		has_edge = ClampToScreenEdge(clamp_dir, ArrowHalfExtent(_arrow));
		if (has_edge)
		{
			edge_pos = clamp_dir;
			edge_angle = ComputePointerHeading(screenPos, center_x, center_y);
		}
	}
	else
	{
		has_edge = ComputeBearingScreenPos(actorPos, targetPos, camHeading, edge_pos, edge_angle);
	}

	if (!has_edge)
	{
		return false;
	}

	Show(true);
	SetDisplayMode(EQuestWaypointDisplayMode::OffScreen);
	PlaceWidget(_arrow, edge_pos, edge_angle, true);
	UpdateDistanceText(actorPos, targetPos);
	return true;
}

void CUIQuestWaypoint::PlaceWidget(CUIStatic* widget, const Fvector2& center, float heading, bool apply_heading)
{
	if (!widget)
	{
		return;
	}

	Fvector2 half = widget->GetWndSize();
	half.mul(0.5f);
	widget->SetWndPos(Fvector2().set(center.x - half.x, center.y - half.y));

	if (apply_heading)
	{
		const float final_heading = _cfg.edge_rotate
			? (heading + _cfg.edge_heading_offset)
			: _cfg.edge_heading_offset;
		widget->SetHeading(final_heading);
	}
	else
	{
		widget->SetHeading(0.0f);
	}
}

void CUIQuestWaypoint::Update()
{
	if (!_is_initialized)
	{
		return;
	}

	if (_dirty.last_logic_frame == Device.dwFrame)
	{
		return;
	}

	if (!CanUpdateHud())
	{
		HideWaypoint();
		return;
	}

	_dirty.last_logic_frame = Device.dwFrame;

	CMapLocation* loc = ResolveTrackedLocation();
	if (!IsTrackableLocation(loc))
	{
		HideWaypoint();
		return;
	}

	Fvector target_pos;
	if (!ResolveTargetWorldPos(loc, target_pos))
	{
		HideWaypoint();
		return;
	}

	CActor* actor = Level().CurrentViewEntity()->cast_actor();
	const Fvector actor_pos = actor->Position();
	const float cam_heading = Device.vCameraDirection.getH();

	if (SGameTaskObjective* objective = Level().GameTaskManager()->ActiveObjective())
	{
		if (CGameTask* task = objective->GetParent())
		{
			const ETaskType task_type = task->GetTaskType();
			ApplyMarkerTexture(task_type);
			ApplyArrowTexture(task_type);
		}
	}

	Fvector2 screen_pos;
	bool behind = false;
	const bool projected = ProjectWorldToScreen(target_pos, screen_pos, behind);

	const float margin = _cfg.on_screen_margin;
	const bool on_screen = projected
		&& screen_pos.x >= margin && screen_pos.x <= (UI_BASE_WIDTH - margin)
		&& screen_pos.y >= margin && screen_pos.y <= (UI_BASE_HEIGHT - margin);

	if (on_screen)
	{
		UpdateOnScreenMarker(screen_pos, actor_pos, target_pos);
		CUIWindow::Update();
		return;
	}

	if (!TryUpdateOffScreenArrow(actor_pos, target_pos, cam_heading, screen_pos, projected))
	{
		HideWaypoint();
		return;
	}

	CUIWindow::Update();
}
