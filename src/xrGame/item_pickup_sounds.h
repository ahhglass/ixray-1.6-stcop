#pragma once

#include "HudSound.h"

class CObject;

enum class EItemPickupSoundType
{
	None = 0,
	Category,
	CustomPath
};

enum class EItemPickupPlayMode
{
	UI_2D,		// legacy: плоский HUD-звук
	World_3D,	// подбор с земли: позиция предмета
	Actor_3D	// инвентарь: 3D у актора
};

class CItemPickupSounds
{
private:
	struct SCategory
	{
		shared_str layerSection;
		HUD_SOUND_ITEM hudSound;
		bool loaded = false;
	};

	struct SCustomPath
	{
		HUD_SOUND_ITEM hudSound;
		bool loaded = false;
	};

	xr_map<shared_str, SCategory> _categories;
	xr_map<shared_str, SCustomPath> _customPaths;
	u32 _lastPlayTime = 0;
	bool _initialized = false;

	static const u32 MIN_SOUND_INTERVAL = 50;

	bool CanPlaySound() const;
	void EnsureInitialized();
	bool IsLayerSection(LPCSTR sect) const;
	bool LoadCategory(SCategory& cat);
	bool LoadCustomPath(const shared_str& path, SCustomPath& custom);
	bool PlayHudSound(HUD_SOUND_ITEM& hud, EItemPickupPlayMode mode, CObject* worldObj, bool skipDebounce);
	void ClearCategories();
	void ClearCustomPaths();

public:
	CItemPickupSounds();
	~CItemPickupSounds();

	static CItemPickupSounds& Instance();

	void Initialize();
	bool Play(EItemPickupSoundType type, LPCSTR id, EItemPickupPlayMode mode, CObject* worldObj = nullptr, bool skipDebounce = false);
	bool PlayBulk(EItemPickupPlayMode mode, CObject* worldObj = nullptr);
	static EItemPickupSoundType ParseSoundType(LPCSTR str, shared_str& outId);
};

IC CItemPickupSounds& ItemPickupSounds() { return CItemPickupSounds::Instance(); }
