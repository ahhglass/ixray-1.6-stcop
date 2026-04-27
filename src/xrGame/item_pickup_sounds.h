#pragma once

#include "../xrSound/Sound.h"

enum class EItemPickupSoundType
{
	None = 0,
	Default,
	Custom
};

class CItemPickupSounds
{
private:
	struct SSoundCategory
	{
		xr_vector<ref_sound> sounds;
		bool initialized;

		SSoundCategory() : initialized(false) {}
	};

	xr_map<xr_string, SSoundCategory> _categories;
	u32 _lastPlayTime;

	static const u32 MIN_SOUND_INTERVAL = 25;

	void LoadSoundsByPrefix(LPCSTR prefix, SSoundCategory& cat);
	bool CanPlaySound() const;
	float CalculateVolume(float volume) const;
	bool PlaySoundInternal(ref_sound& snd, float volume);
	void clear();

public:
	CItemPickupSounds();
	~CItemPickupSounds();

	static CItemPickupSounds& Instance();

	void Initialize();
	bool PlaySound(LPCSTR categoryName, float volume = 1.0f);
	bool PlaySound(EItemPickupSoundType type, LPCSTR categoryName, float volume = 1.0f);
	bool PlayCustomSound(LPCSTR soundPath, float volume = 1.0f);
	static EItemPickupSoundType ParseSoundType(LPCSTR str, xr_string& outCategoryName);
};

IC CItemPickupSounds& ItemPickupSounds() { return CItemPickupSounds::Instance(); }

