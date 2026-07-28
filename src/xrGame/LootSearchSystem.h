#pragma once

#include "../xrSound/Sound.h"

class CInventoryOwner;
class CInventoryItem;
class CUIActorMenuBase;
class CUICellItem;
class CUIStatic;

enum class ELootItemState : u8
{
	Hidden = 0,
	Silhouette,
	Revealed
};

enum class ELootUpdateFlags : u32
{
	None = 0,
	VisualRefresh = 1 << 0,
	ListRefresh = 1 << 1,
};

class CLootSearchSystem final
{
public:
	static CLootSearchSystem& Get();

	CLootSearchSystem();
	~CLootSearchSystem();
	CLootSearchSystem(const CLootSearchSystem&) = delete;
	CLootSearchSystem& operator=(const CLootSearchSystem&) = delete;

	bool IsEnabled() const;
	void EnsureLoaded();

	void BeginSearch(CInventoryOwner* corpse, CUIActorMenuBase* menu);
	void EndSearch(CUIActorMenuBase* menu);
	void ClearTarget(u16 targetId);

	ELootUpdateFlags Update(CInventoryOwner* corpse, CUIActorMenuBase* menu);
	void UpdateVisuals(CUIActorMenuBase* menu, CInventoryOwner* corpse, bool refreshCellVisuals = false);

	bool IsActiveTarget(CInventoryOwner* corpse) const;
	bool ShouldDisplayItem(CInventoryItem* item, CInventoryOwner* corpse) const;
	bool CanTakeItem(CInventoryItem* item, CInventoryOwner* corpse) const;
	bool CanTakeAll(CInventoryOwner* corpse) const;
	void ApplyItemVisual(CUICellItem* cell, CInventoryItem* item, CInventoryOwner* corpse);

private:
	struct SConfig
	{
		bool enableCorpses = true;
		u8 placeholderMode = 2;
		bool hideHiddenItems = true;
		float timeMin = 0.35f;
		float timeMax = 12.0f;
		float firstItemDelay = 0.25f;
		float delayBetweenMin = 0.15f;
		float delayBetweenMax = 1.2f;
		float silhouetteAppearMin = 0.08f;
		float silhouetteAppearMax = 0.45f;
		float minRevealInterval = 0.12f;
		float rankMultMin = 0.65f;
		float rankMultMax = 1.25f;

		bool enableSpinners = true;
		shared_str spinnerStyle = "tarkov_like";
		shared_str spinnerTexture;
		u32 spinnerFps = 26;
		u32 spinnerFrames = 13;
		u32 spinnerCols = 4;
		u32 spinnerTileW = 128;
		u32 spinnerTileH = 128;
		u32 spinnerSize = 32;
		u8 spinnerScaleMode = 1;
		u8 spinnerOpacity = 100;

		bool enableSounds = true;
		bool enableRevealSounds = true;
		bool enableBackgroundSounds = true;
		shared_str soundLootingPrefix = "looting\\looting_";
		u32 soundLootingCount = 12;
		float soundVolume = 1.0f;
		float soundBackgroundVolume = 1.25f;
	};

	struct SItemEntry
	{
		u16 itemId = u16(-1);
		ELootItemState state = ELootItemState::Hidden;
		u32 silhouetteAtMs = 0;
		u32 revealAtMs = 0;
		u32 gridArea = 0;
	};

	struct SSession
	{
		u16 targetId = u16(-1);
		bool initialized = false;
		float rankMult = 1.0f;
		u32 searchStartMs = 0;
		xr_vector<SItemEntry> items;
	};

private:
	void Load();
	void ResetConfigDefaults();
	void EnsureSoundsLoaded();
	void DestroySounds();
	void ClearVisuals();
	void StopActiveSound();

	bool IsValidCorpse(CInventoryOwner* corpse) const;
	u16 ResolveCorpseId(CInventoryOwner* corpse) const;
	SSession* FindSession(u16 targetId);
	const SSession* FindSession(u16 targetId) const;
	SItemEntry* FindItemEntry(SSession& session, u16 itemId);
	const SItemEntry* FindItemEntry(const SSession& session, u16 itemId) const;
	void InitializeSession(SSession& session, CInventoryOwner* corpse);
	void AdvanceItemStates(SSession& session, u32 nowMs);
	float GetRankMultiplier(CInventoryOwner* corpse) const;
	float RandomDelay(float minSec, float maxSec) const;

	bool SessionHasPendingItems(const SSession& session) const;
	void UpdateBackgroundSound(const SSession& session, u32 nowMs);
	void PlayRevealSound();
	void PlayRandomLootingSound(float volumeScale);

	u32 GetAtlasFrame(u32 nowMs) const;
	void SetSpinnerFrame(CUIStatic* spinner, u32 frame) const;
	float GetAspectCorrection() const;
	float CalcSpinnerSize(CUICellItem* cell) const;
	CUIStatic* FindSpinner(CUICellItem* cell) const;
	CUIStatic* EnsureSpinner(CUICellItem* cell);
	void RemoveSpinner(CUICellItem* cell) const;

private:
	static constexpr const char* kSpinnerWindowName = "loot_search_spinner";
	static constexpr u8 kSilhouetteDarknessPercent = 95;
	static constexpr u8 kSilhouetteColor = static_cast<u8>((255u * (100u - kSilhouetteDarknessPercent) + 50u) / 100u);

	bool _loaded = false;
	bool _soundsLoaded = false;
	SConfig _cfg{};

	xr_map<u16, SSession> _sessions;
	u16 _activeTargetId = u16(-1);
	CUIActorMenuBase* _activeMenu = nullptr;

	xr_vector<ref_sound> _lootSounds;
	ref_sound _activeSound{};
	u32 _nextBackgroundSoundMs = 0;
};
