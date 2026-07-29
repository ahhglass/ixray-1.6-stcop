#include "StdAfx.h"
#include "LootSearchSystem.h"

#include "InventoryOwner.h"
#include "inventory_item.h"
#include "Inventory.h"
#include "GameObject.h"
#include "ai/monsters/basemonster/base_monster.h"
#include "Car.h"
#include "ui/UIActorMenuBase.h"
#include "ui/UICellItem.h"
#include "ui/UIDragDropListEx.h"

#include "../xrCore/EngineExternal.h"
#include "../xrUI/Widgets/UIStatic.h"
#include "../xrUI/UITextureMaster.h"
#include "../xrRHI/RHI.h"
#include "Actor_Flags.h"

#include <algorithm>

namespace
{
constexpr const char* kSectionLootSearch = "loot_search";

u32 SecToMs(float seconds)
{
	return static_cast<u32>(std::max(0.0f, seconds) * 1000.0f);
}

float Lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

u8 ClampU8(int value)
{
	return static_cast<u8>(std::clamp(value, 0, 255));
}
} // namespace

CLootSearchSystem& CLootSearchSystem::Get()
{
	static CLootSearchSystem instance;
	return instance;
}

CLootSearchSystem::CLootSearchSystem() = default;

CLootSearchSystem::~CLootSearchSystem()
{
	StopActiveSound();
	DestroySounds();
	ClearVisuals();
}

bool CLootSearchSystem::IsEnabled() const
{
	return EngineExternal()[EEngineExternalGame::EnableLootSearch];
}

void CLootSearchSystem::EnsureLoaded()
{
	if (!_loaded)
	{
		Load();
	}
}

void CLootSearchSystem::ResetConfigDefaults()
{
	_cfg = {};
	_cfg.spinnerStyle = "tarkov_like";
	_cfg.spinnerTexture = "ui\\loot_searching\\tarkov_like\\spinner_atlas";
	_cfg.soundLootingPrefix = "looting\\looting_";
}

void CLootSearchSystem::Load()
{
	ResetConfigDefaults();

	if (CInifile* ini = pSettings)
	{
		if (ini->section_exist(kSectionLootSearch))
		{
			_cfg.enableCorpses = READ_IF_EXISTS(ini, r_bool, kSectionLootSearch, "enable_corpses", _cfg.enableCorpses);
			_cfg.placeholderMode = static_cast<u8>(READ_IF_EXISTS(ini, r_u32, kSectionLootSearch, "placeholder_mode", _cfg.placeholderMode));
			_cfg.hideHiddenItems = READ_IF_EXISTS(ini, r_bool, kSectionLootSearch, "hide_hidden_items", _cfg.hideHiddenItems);
			_cfg.timeMin = READ_IF_EXISTS(ini, r_float, kSectionLootSearch, "time_min", _cfg.timeMin);
			_cfg.timeMax = READ_IF_EXISTS(ini, r_float, kSectionLootSearch, "time_max", _cfg.timeMax);
			_cfg.firstItemDelay = READ_IF_EXISTS(ini, r_float, kSectionLootSearch, "first_item_delay", _cfg.firstItemDelay);
			_cfg.delayBetweenMin = READ_IF_EXISTS(ini, r_float, kSectionLootSearch, "delay_between_min", _cfg.delayBetweenMin);
			_cfg.delayBetweenMax = READ_IF_EXISTS(ini, r_float, kSectionLootSearch, "delay_between_max", _cfg.delayBetweenMax);
			_cfg.silhouetteAppearMin = READ_IF_EXISTS(ini, r_float, kSectionLootSearch, "silhouette_appear_min", _cfg.silhouetteAppearMin);
			_cfg.silhouetteAppearMax = READ_IF_EXISTS(ini, r_float, kSectionLootSearch, "silhouette_appear_max", _cfg.silhouetteAppearMax);
			_cfg.minRevealInterval = READ_IF_EXISTS(ini, r_float, kSectionLootSearch, "min_reveal_interval", _cfg.minRevealInterval);
			_cfg.rankMultMin = READ_IF_EXISTS(ini, r_float, kSectionLootSearch, "rank_mult_min", _cfg.rankMultMin);
			_cfg.rankMultMax = READ_IF_EXISTS(ini, r_float, kSectionLootSearch, "rank_mult_max", _cfg.rankMultMax);

			_cfg.enableSpinners = READ_IF_EXISTS(ini, r_bool, kSectionLootSearch, "enable_spinners", _cfg.enableSpinners);
			_cfg.spinnerStyle = READ_IF_EXISTS(ini, r_string, kSectionLootSearch, "spinner_style", _cfg.spinnerStyle.c_str());
			_cfg.spinnerFps = READ_IF_EXISTS(ini, r_u32, kSectionLootSearch, "spinner_fps", _cfg.spinnerFps);
			_cfg.spinnerFrames = READ_IF_EXISTS(ini, r_u32, kSectionLootSearch, "spinner_frames", _cfg.spinnerFrames);
			_cfg.spinnerCols = READ_IF_EXISTS(ini, r_u32, kSectionLootSearch, "spinner_cols", _cfg.spinnerCols);
			_cfg.spinnerTileW = READ_IF_EXISTS(ini, r_u32, kSectionLootSearch, "spinner_tile_w", _cfg.spinnerTileW);
			_cfg.spinnerTileH = READ_IF_EXISTS(ini, r_u32, kSectionLootSearch, "spinner_tile_h", _cfg.spinnerTileH);
			_cfg.spinnerSize = READ_IF_EXISTS(ini, r_u32, kSectionLootSearch, "spinner_size", _cfg.spinnerSize);
			_cfg.spinnerScaleMode = static_cast<u8>(READ_IF_EXISTS(ini, r_u32, kSectionLootSearch, "spinner_scale_mode", _cfg.spinnerScaleMode));
			_cfg.spinnerOpacity = static_cast<u8>(READ_IF_EXISTS(ini, r_u32, kSectionLootSearch, "spinner_opacity", _cfg.spinnerOpacity));

			const shared_str spinnerTextureOverride = READ_IF_EXISTS(ini, r_string, kSectionLootSearch, "spinner_texture", "");
			if (spinnerTextureOverride.size())
			{
				_cfg.spinnerTexture = spinnerTextureOverride;
			}
			else
			{
				string_path texturePath;
				xr_sprintf(texturePath, "ui\\loot_searching\\%s\\spinner_atlas", _cfg.spinnerStyle.c_str());
				_cfg.spinnerTexture = texturePath;
			}

			_cfg.enableSounds = READ_IF_EXISTS(ini, r_bool, kSectionLootSearch, "enable_sounds", _cfg.enableSounds);
			_cfg.enableRevealSounds = READ_IF_EXISTS(ini, r_bool, kSectionLootSearch, "enable_reveal_sounds", _cfg.enableRevealSounds);
			_cfg.enableBackgroundSounds = READ_IF_EXISTS(ini, r_bool, kSectionLootSearch, "enable_background_sounds", _cfg.enableBackgroundSounds);
			_cfg.soundLootingPrefix = READ_IF_EXISTS(ini, r_string, kSectionLootSearch, "sound_looting_prefix", _cfg.soundLootingPrefix.c_str());
			_cfg.soundLootingCount = READ_IF_EXISTS(ini, r_u32, kSectionLootSearch, "sound_looting_count", _cfg.soundLootingCount);
			_cfg.soundVolume = READ_IF_EXISTS(ini, r_float, kSectionLootSearch, "sound_volume", _cfg.soundVolume);
			_cfg.soundBackgroundVolume = READ_IF_EXISTS(ini, r_float, kSectionLootSearch, "sound_background_volume", _cfg.soundBackgroundVolume);
		}
	}

	_loaded = true;
	_soundsLoaded = false;
	DestroySounds();
}

void CLootSearchSystem::EnsureSoundsLoaded()
{
	if (_soundsLoaded || !_cfg.enableSounds || _cfg.soundLootingCount == 0)
	{
		return;
	}

	_lootSounds.clear();
	_lootSounds.resize(_cfg.soundLootingCount);

	for (u32 i = 0; i < _cfg.soundLootingCount; ++i)
	{
		string_path soundPath;
		xr_sprintf(soundPath, "%s%d", _cfg.soundLootingPrefix.c_str(), i + 1);
		::Sound->create(_lootSounds[i], soundPath, st_Effect, sg_SourceType);
	}

	_soundsLoaded = true;
}

void CLootSearchSystem::DestroySounds()
{
	for (ref_sound& sound : _lootSounds)
	{
		sound.destroy();
	}
	_lootSounds.clear();
	_activeSound.destroy();
	_soundsLoaded = false;
}

void CLootSearchSystem::ClearVisuals()
{
	_nextBackgroundSoundMs = 0;
}

void CLootSearchSystem::StopActiveSound()
{
	if (_activeSound.is_playing())
	{
		_activeSound.stop();
	}
	_activeSound.destroy();
}

bool CLootSearchSystem::IsValidCorpse(CInventoryOwner* corpse) const
{
	if (!_cfg.enableCorpses || !corpse || corpse->is_alive())
	{
		return false;
	}

	if (corpse->cast_base_monster() || corpse->cast_car())
	{
		return false;
	}

	return true;
}

u16 CLootSearchSystem::ResolveCorpseId(CInventoryOwner* corpse) const
{
	if (corpse && corpse->cast_game_object())
	{
		return corpse->cast_game_object()->ID();
	}

	return u16(-1);
}

CLootSearchSystem::SSession* CLootSearchSystem::FindSession(u16 targetId)
{
	auto it = _sessions.find(targetId);
	return it != _sessions.end() ? &it->second : nullptr;
}

const CLootSearchSystem::SSession* CLootSearchSystem::FindSession(u16 targetId) const
{
	auto it = _sessions.find(targetId);
	return it != _sessions.end() ? &it->second : nullptr;
}

CLootSearchSystem::SItemEntry* CLootSearchSystem::FindItemEntry(SSession& session, u16 itemId)
{
	for (SItemEntry& entry : session.items)
	{
		if (entry.itemId == itemId)
		{
			return &entry;
		}
	}
	return nullptr;
}

const CLootSearchSystem::SItemEntry* CLootSearchSystem::FindItemEntry(const SSession& session, u16 itemId) const
{
	for (const SItemEntry& entry : session.items)
	{
		if (entry.itemId == itemId)
		{
			return &entry;
		}
	}
	return nullptr;
}

float CLootSearchSystem::GetRankMultiplier(CInventoryOwner* partner) const
{
	if (!partner)
	{
		return 1.0f;
	}

	const s32 rank = partner->Rank();
	const float t = std::clamp(static_cast<float>(rank) / 4.0f, 0.0f, 1.0f);
	return Lerp(_cfg.rankMultMax, _cfg.rankMultMin, t);
}

float CLootSearchSystem::RandomDelay(float minSec, float maxSec) const
{
	if (maxSec <= minSec)
	{
		return minSec;
	}

	const float rnd = static_cast<float>(Random.randI(0, 10001)) / 10000.0f;
	return minSec + (maxSec - minSec) * rnd;
}

void CLootSearchSystem::InitializeSession(SSession& session, CInventoryOwner* corpse)
{
	TIItemContainer itemsList;
	if (!corpse)
	{
		return;
	}

	corpse->inventory().AddAvailableItems(itemsList, false);
	session.rankMult = GetRankMultiplier(corpse);

	session.items.clear();
	session.items.reserve(itemsList.size());
	session.searchStartMs = Device.dwTimeGlobal;

	struct SSortedItem
	{
		PIItem item = nullptr;
		u32 area = 0;
	};

	xr_vector<SSortedItem> sorted;
	sorted.reserve(itemsList.size());

	for (PIItem item : itemsList)
	{
		if (!item)
		{
			continue;
		}

		const Irect grid = item->GetInvGridRect();
		SSortedItem row;
		row.item = item;
		row.area = grid.x2 * grid.y2;
		sorted.push_back(row);
	}

	std::sort(sorted.begin(), sorted.end(), [](const SSortedItem& a, const SSortedItem& b)
	{
		return a.area > b.area;
	});

	const u32 itemCount = sorted.size();
	float effectiveMax = _cfg.timeMax * session.rankMult;
	if (itemCount <= 2)
	{
		effectiveMax = std::min(effectiveMax, 3.0f * session.rankMult);
	}
	else if (itemCount <= 5)
	{
		effectiveMax = std::min(effectiveMax, 7.0f * session.rankMult);
	}

	float cumulativeSilhouette = _cfg.firstItemDelay * session.rankMult;
	float lastRevealSec = 0.0f;
	const bool twoStage = _cfg.placeholderMode >= 2;

	for (const SSortedItem& row : sorted)
	{
		SItemEntry entry;
		entry.itemId = row.item->object_id();
		entry.gridArea = row.area;
		entry.state = twoStage ? ELootItemState::Hidden : ELootItemState::Silhouette;

		if (twoStage)
		{
			cumulativeSilhouette += RandomDelay(_cfg.silhouetteAppearMin, _cfg.silhouetteAppearMax) * session.rankMult;
			float silhouetteSec = std::min(cumulativeSilhouette, effectiveMax - _cfg.delayBetweenMax * session.rankMult * 0.5f);
			silhouetteSec = std::max(silhouetteSec, 0.05f);
			entry.silhouetteAtMs = session.searchStartMs + SecToMs(silhouetteSec);

			float revealSec = silhouetteSec + RandomDelay(_cfg.delayBetweenMin, _cfg.delayBetweenMax) * session.rankMult;
			revealSec = std::min(revealSec, effectiveMax);
			if (revealSec < lastRevealSec + _cfg.minRevealInterval)
			{
				revealSec = lastRevealSec + _cfg.minRevealInterval;
			}
			lastRevealSec = revealSec;
			entry.revealAtMs = session.searchStartMs + SecToMs(revealSec);
		}
		else
		{
			const float revealSec = std::min(
				_cfg.firstItemDelay * session.rankMult + static_cast<float>(session.items.size()) * _cfg.delayBetweenMin * session.rankMult,
				effectiveMax);
			entry.silhouetteAtMs = session.searchStartMs;
			entry.revealAtMs = session.searchStartMs + SecToMs(revealSec);
		}

		session.items.push_back(entry);
	}

	session.initialized = true;
}

void CLootSearchSystem::AdvanceItemStates(SSession& session, u32 nowMs)
{
	for (SItemEntry& entry : session.items)
	{
		if (entry.state == ELootItemState::Revealed)
		{
			continue;
		}

		if (entry.state == ELootItemState::Hidden && nowMs >= entry.silhouetteAtMs)
		{
			entry.state = ELootItemState::Silhouette;
		}

		if (entry.state != ELootItemState::Revealed && nowMs >= entry.revealAtMs)
		{
			entry.state = ELootItemState::Revealed;
		}
	}
}

bool CLootSearchSystem::SessionHasPendingItems(const SSession& session) const
{
	for (const SItemEntry& entry : session.items)
	{
		if (entry.state != ELootItemState::Revealed)
		{
			return true;
		}
	}
	return false;
}

void CLootSearchSystem::PlayRandomLootingSound(float volumeScale)
{
	if (!_cfg.enableSounds || _cfg.soundLootingCount == 0)
	{
		return;
	}

	EnsureSoundsLoaded();
	if (_lootSounds.empty())
	{
		return;
	}

	const u32 index = static_cast<u32>(Random.randI(0, static_cast<int>(_lootSounds.size())));
	ref_sound& sound = _lootSounds[index];
	if (!sound.handle())
	{
		return;
	}

	StopActiveSound();
	_activeSound.clone(sound, st_Effect, sg_SourceType);
	_activeSound.set_volume(_cfg.soundVolume * volumeScale);
	_activeSound.play(nullptr, sm_2D);
}

void CLootSearchSystem::PlayRevealSound()
{
	if (_cfg.enableSounds && _cfg.enableRevealSounds)
	{
		PlayRandomLootingSound(_cfg.soundVolume);
	}
}

void CLootSearchSystem::UpdateBackgroundSound(const SSession& session, u32 nowMs)
{
	if (!_cfg.enableSounds || !_cfg.enableBackgroundSounds || !SessionHasPendingItems(session))
	{
		return;
	}

	if (_activeSound.is_playing() || nowMs < _nextBackgroundSoundMs)
	{
		return;
	}

	EnsureSoundsLoaded();
	if (_lootSounds.empty())
	{
		return;
	}

	const u32 index = static_cast<u32>(Random.randI(0, static_cast<int>(_lootSounds.size())));
	ref_sound& sound = _lootSounds[index];
	if (!sound.handle())
	{
		return;
	}

	StopActiveSound();
	_activeSound.clone(sound, st_Effect, sg_SourceType);
	_activeSound.set_volume(_cfg.soundVolume * _cfg.soundBackgroundVolume);
	_activeSound.play(nullptr, sm_2D);

	const float lengthSec = _activeSound.get_length_sec();
	_nextBackgroundSoundMs = nowMs + SecToMs(lengthSec > 0.0f ? lengthSec * 0.5f : 0.75f);
}

void CLootSearchSystem::BeginSearch(CInventoryOwner* corpse, CUIActorMenuBase* menu)
{
	if (!IsEnabled())
	{
		return;
	}

	EnsureLoaded();

	if (!IsValidCorpse(corpse))
	{
		_activeTargetId = u16(-1);
		_activeMenu = nullptr;
		return;
	}

	const u16 targetId = ResolveCorpseId(corpse);
	if (targetId == u16(-1))
	{
		return;
	}

	SSession& session = _sessions[targetId];
	session.targetId = targetId;

	if (!session.initialized)
	{
		InitializeSession(session, corpse);
	}

	_activeTargetId = targetId;
	_activeMenu = menu;
	_nextBackgroundSoundMs = Device.dwTimeGlobal;
}

void CLootSearchSystem::EndSearch(CUIActorMenuBase* menu)
{
	if (_activeMenu == menu)
	{
		StopActiveSound();
		ClearVisuals();
		_activeMenu = nullptr;
	}
}

void CLootSearchSystem::ClearTarget(u16 targetId)
{
	_sessions.erase(targetId);
	if (_activeTargetId == targetId)
	{
		StopActiveSound();
		ClearVisuals();
		_activeTargetId = u16(-1);
		_activeMenu = nullptr;
	}
}

ELootUpdateFlags CLootSearchSystem::Update(CInventoryOwner* corpse, CUIActorMenuBase* /*menu*/)
{
	if (!IsEnabled() || !IsActiveTarget(corpse))
	{
		return ELootUpdateFlags::None;
	}

	SSession* session = FindSession(_activeTargetId);
	if (!session || !session->initialized)
	{
		return ELootUpdateFlags::None;
	}

	const u32 nowMs = Device.dwTimeGlobal;
	xr_vector<u8> before;
	before.reserve(session->items.size());
	for (const SItemEntry& entry : session->items)
	{
		before.push_back(static_cast<u8>(entry.state));
	}

	AdvanceItemStates(*session, nowMs);
	UpdateBackgroundSound(*session, nowMs);

	ELootUpdateFlags flags = ELootUpdateFlags::None;
	for (size_t i = 0; i < session->items.size(); ++i)
	{
		const ELootItemState prevState = static_cast<ELootItemState>(before[i]);
		const ELootItemState nextState = session->items[i].state;
		if (prevState == nextState)
		{
			continue;
		}

		if (nextState == ELootItemState::Revealed)
		{
			PlayRevealSound();
		}

		if (_cfg.hideHiddenItems &&
			prevState == ELootItemState::Hidden &&
			(nextState == ELootItemState::Silhouette || nextState == ELootItemState::Revealed))
		{
			flags = static_cast<ELootUpdateFlags>(static_cast<u32>(flags) | static_cast<u32>(ELootUpdateFlags::ListRefresh));
		}
		else
		{
			flags = static_cast<ELootUpdateFlags>(static_cast<u32>(flags) | static_cast<u32>(ELootUpdateFlags::VisualRefresh));
		}
	}

	return flags;
}

u32 CLootSearchSystem::GetAtlasFrame(u32 nowMs) const
{
	const u32 fps = std::max(_cfg.spinnerFps, 1u);
	const u32 frames = std::max(_cfg.spinnerFrames, 1u);
	const u32 frameDurationMs = 1000 / fps;
	return (nowMs / frameDurationMs) % frames;
}

void CLootSearchSystem::SetSpinnerFrame(CUIStatic* spinner, u32 frame) const
{
	if (!spinner || _cfg.spinnerCols == 0 || _cfg.spinnerTileW == 0 || _cfg.spinnerTileH == 0)
	{
		return;
	}

	const u32 col = frame % _cfg.spinnerCols;
	const u32 row = frame / _cfg.spinnerCols;
	const float x = static_cast<float>(col * _cfg.spinnerTileW);
	const float y = static_cast<float>(row * _cfg.spinnerTileH);
	const Frect rect = {
		x,
		y,
		x + static_cast<float>(_cfg.spinnerTileW),
		y + static_cast<float>(_cfg.spinnerTileH)
	};
	spinner->SetTextureRect(rect);
}

float CLootSearchSystem::GetAspectCorrection() const
{
	const u32 screenW = psCurrentVidMode[0];
	const u32 screenH = psCurrentVidMode[1];
	if (screenW == 0)
	{
		return 1.0f;
	}

	return (static_cast<float>(screenH) / static_cast<float>(screenW)) * (4.0f / 3.0f);
}

float CLootSearchSystem::CalcSpinnerSize(CUICellItem* cell) const
{
	if (_cfg.spinnerScaleMode == 2 && cell)
	{
		const float cellW = cell->GetWidth();
		const float cellH = cell->GetHeight();
		float size = std::min(cellW, cellH) * 0.5f;
		size = std::max(size, 16.0f);
		size = std::min(size, static_cast<float>(_cfg.spinnerSize));
		return size;
	}

	return static_cast<float>(_cfg.spinnerSize);
}

CUIStatic* CLootSearchSystem::FindSpinner(CUICellItem* cell) const
{
	if (!cell)
	{
		return nullptr;
	}

	for (CUIWindow* child : cell->GetChildWndList())
	{
		if (child && child->WindowName() == kSpinnerWindowName)
		{
			return smart_cast<CUIStatic*>(child);
		}
	}

	return nullptr;
}

CUIStatic* CLootSearchSystem::EnsureSpinner(CUICellItem* cell)
{
	if (!cell || !_cfg.enableSpinners || _cfg.placeholderMode < 2 || !_cfg.spinnerTexture.size())
	{
		return nullptr;
	}

	const u32 frame = GetAtlasFrame(Device.dwTimeGlobal);

	if (CUIStatic* existing = FindSpinner(cell))
	{
		SetSpinnerFrame(existing, frame);
		return existing;
	}

	CUIStatic* spinner = new CUIStatic();
	spinner->SetAutoDelete(true);
	spinner->SetWindowName(kSpinnerWindowName);

	if (CUITextureMaster::ItemExist(_cfg.spinnerTexture))
	{
		spinner->InitTexture(_cfg.spinnerTexture.c_str(), true);
	}
	else
	{
		spinner->InitTextureEx(_cfg.spinnerTexture.c_str(), "hud\\default", true);
	}

	spinner->SetStretchTexture(true);

	const float size = CalcSpinnerSize(cell);
	const float correctedW = size * GetAspectCorrection();
	spinner->SetWndSize(Fvector2().set(correctedW, size));

	const float cellW = cell->GetWidth();
	const float cellH = cell->GetHeight();
	spinner->SetWndPos(Fvector2().set((cellW - correctedW) * 0.5f, (cellH - size) * 0.5f));

	const u8 alpha = ClampU8(static_cast<int>(_cfg.spinnerOpacity) * 255 / 100);
	spinner->SetTextureColor(color_rgba(255, 255, 255, alpha));
	SetSpinnerFrame(spinner, frame);
	spinner->Show(true);
	cell->AttachChild(spinner);
	return spinner;
}

void CLootSearchSystem::RemoveSpinner(CUICellItem* cell) const
{
	if (!cell)
	{
		return;
	}

	if (CUIStatic* spinner = FindSpinner(cell))
	{
		spinner->Show(false);
		cell->DetachChild(spinner);
	}
}

void CLootSearchSystem::UpdateVisuals(CUIActorMenuBase* menu, CInventoryOwner* corpse, bool refreshCellVisuals)
{
	if (!IsEnabled() || !menu || !IsActiveTarget(corpse))
	{
		return;
	}

	const SSession* session = FindSession(_activeTargetId);
	if (!session || !session->initialized)
	{
		return;
	}

	CUIDragDropListEx* partnerList = menu->GetPartnerList();
	if (!partnerList)
	{
		return;
	}

	const u32 frame = GetAtlasFrame(Device.dwTimeGlobal);

	for (u32 i = 0; i < partnerList->ItemsCount(); ++i)
	{
		CUICellItem* cell = partnerList->GetItemIdx(i);
		PIItem item = cell ? static_cast<PIItem>(cell->m_pData) : nullptr;
		if (!cell || !item)
		{
			continue;
		}

		const SItemEntry* entry = FindItemEntry(*session, item->object_id());
		if (!entry)
		{
			continue;
		}

		if (refreshCellVisuals)
		{
			ApplyItemVisual(cell, item, corpse);
			continue;
		}

		if (entry->state == ELootItemState::Silhouette && _cfg.enableSpinners && _cfg.placeholderMode >= 2)
		{
			if (CUIStatic* spinner = EnsureSpinner(cell))
			{
				SetSpinnerFrame(spinner, frame);
			}
		}
		else
		{
			RemoveSpinner(cell);
		}
	}
}

bool CLootSearchSystem::IsActiveTarget(CInventoryOwner* corpse) const
{
	if (_activeTargetId == u16(-1))
	{
		return false;
	}

	return ResolveCorpseId(corpse) == _activeTargetId;
}

bool CLootSearchSystem::ShouldDisplayItem(CInventoryItem* item, CInventoryOwner* corpse) const
{
	if (!IsEnabled() || !item || !IsValidCorpse(corpse))
	{
		return true;
	}

	const u16 targetId = ResolveCorpseId(corpse);
	const SSession* session = FindSession(targetId);
	if (!session || !session->initialized)
	{
		return true;
	}

	const SItemEntry* entry = FindItemEntry(*session, item->object_id());
	if (!entry)
	{
		return true;
	}

	if (_cfg.hideHiddenItems && entry->state == ELootItemState::Hidden)
	{
		return false;
	}

	return true;
}

bool CLootSearchSystem::CanTakeItem(CInventoryItem* item, CInventoryOwner* corpse) const
{
	if (!IsEnabled() || !item || !IsValidCorpse(corpse))
	{
		return true;
	}

	const u16 targetId = ResolveCorpseId(corpse);
	const SSession* session = FindSession(targetId);
	if (!session || !session->initialized)
	{
		return true;
	}

	const SItemEntry* entry = FindItemEntry(*session, item->object_id());
	if (!entry)
	{
		return true;
	}

	return entry->state == ELootItemState::Revealed;
}

// этот метод используется для проверки, можно ли взять все предметы с трупа
bool CLootSearchSystem::CanTakeAll(CInventoryOwner* corpse) const
{
	if (!IsEnabled() || !IsValidCorpse(corpse))
	{
		return true;
	}

	const u16 targetId = ResolveCorpseId(corpse);
	const SSession* session = FindSession(targetId);
	if (!session || !session->initialized)
	{
		return true;
	}

	return !SessionHasPendingItems(*session);
}

void CLootSearchSystem::ApplyItemVisual(CUICellItem* cell, CInventoryItem* item, CInventoryOwner* corpse)
{
	if (!cell || !item || !IsEnabled() || !IsValidCorpse(corpse))
	{
		return;
	}

	const u16 targetId = ResolveCorpseId(corpse);
	const SSession* session = FindSession(targetId);
	if (!session || !session->initialized)
	{
		return;
	}

	const SItemEntry* entry = FindItemEntry(*session, item->object_id());
	if (!entry)
	{
		cell->SetLootSearchHideCondition(false);
		return;
	}

	const bool use3dIcons = psActorFlags.test(AF_3D_ICONS_INV);
	const bool isRevealed = entry->state == ELootItemState::Revealed;

	cell->SetLootSearchHideCondition(!isRevealed);
	cell->ShowConditionIndicators(isRevealed);
	if (isRevealed)
	{
		cell->UpdateConditionProgressBar();
	}

	switch (entry->state)
	{
	case ELootItemState::Hidden:
		if (use3dIcons)
		{
			cell->SetVisual(nullptr);
		}
		cell->SetTextureColor(color_rgba(kSilhouetteColor, kSilhouetteColor, kSilhouetteColor, 255));
		RemoveSpinner(cell);
		break;
	case ELootItemState::Silhouette:
		if (use3dIcons)
		{
			cell->SetVisual(nullptr);
		}
		cell->SetTextureColor(color_rgba(kSilhouetteColor, kSilhouetteColor, kSilhouetteColor, 220));
		if (_cfg.enableSpinners && _cfg.placeholderMode >= 2)
		{
			if (CUIStatic* spinner = EnsureSpinner(cell))
			{
				SetSpinnerFrame(spinner, GetAtlasFrame(Device.dwTimeGlobal));
			}
		}
		else
		{
			RemoveSpinner(cell);
		}
		break;
	case ELootItemState::Revealed:
	default:
		if (use3dIcons)
		{
			cell->SetVisual(item->m_3d_static_visual_name);
			if (CGameObject* gameObject = item->cast_game_object())
			{
				if (IRenderVisual* visual = gameObject->Visual())
				{
					cell->SetBonesVisible(visual->dcast_PKinematics());
				}
			}
		}
		cell->SetTextureColor(color_rgba(255, 255, 255, 255));
		RemoveSpinner(cell);
		break;
	}
}
