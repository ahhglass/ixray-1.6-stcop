#include "stdafx.h"
#include "item_pickup_sounds.h"

CItemPickupSounds::CItemPickupSounds()
{
	_lastPlayTime = 0;
	Initialize();
}

CItemPickupSounds::~CItemPickupSounds()
{
	clear();
}

void CItemPickupSounds::clear()
{
	for (auto& [categoryName, category] : _categories)
	{
		for (auto& sound : category.sounds)
			sound.destroy();
	}
	_categories.clear();
}

CItemPickupSounds& CItemPickupSounds::Instance()
{
	static CItemPickupSounds instance;
	return instance;
}

void CItemPickupSounds::LoadSoundsByPrefix(LPCSTR prefix, SSoundCategory& cat)
{
	if (!prefix || !prefix[0])
		return;

	if (!Sound)
		return;

	string_path pattern;
	xr_sprintf(pattern, "%s*", prefix);

	FS_FileSet files;
	FS.file_list(files, _game_sounds_, FS_ListFiles | FS_ClampExt, pattern);

	const xr_string prefixStr = prefix;
	const size_t prefixLen = prefixStr.size();

	for (FS_FileSetIt it = files.begin(), it_e = files.end(); it != it_e; ++it)
	{
		const xr_string soundName = it->name.c_str();
		if (soundName.size() < prefixLen || soundName.compare(0, prefixLen, prefixStr) != 0)
			continue;

		if (soundName.size() == prefixLen)
		{
			ref_sound snd;
			snd.create(it->name.c_str(), st_Effect, sg_SourceType);
			cat.sounds.push_back(snd);
			continue;
		}

		const char suffixFirst = soundName[prefixLen];
		if (suffixFirst < '0' || suffixFirst > '9')
			continue;

		ref_sound snd;
		snd.create(it->name.c_str(), st_Effect, sg_SourceType);
		cat.sounds.push_back(snd);
	}
}

void CItemPickupSounds::Initialize()
{
	if (!pSettings)
		return;

	LPCSTR section = "item_pickup_sounds";
	if (!pSettings->section_exist(section))
		return;

	clear();

	CInifile::Sect& sect = pSettings->r_section(section);
	for (CInifile::SectCIt it = sect.Data.begin(), it_e = sect.Data.end(); it != it_e; ++it)
	{
		xr_string categoryName = it->first.c_str();
		SSoundCategory& cat = _categories[categoryName];
		cat.sounds.clear();
		cat.initialized = false;

		LPCSTR prefix = it->second.c_str();
		if (prefix && prefix[0])
		{
			LoadSoundsByPrefix(prefix, cat);
			cat.initialized = (cat.sounds.size() > 0);
		}
	}
}

float CItemPickupSounds::CalculateVolume(float volume) const
{
	if (volume >= 0.99f && volume <= 1.01f)
		return 0.85f + Random.randF(0.15f);
	return volume;
}

bool CItemPickupSounds::PlaySoundInternal(ref_sound& snd, float volume)
{
	if (!snd.handle())
		return false;

	if (!Sound)
		return false;

	snd.play(nullptr, sm_2D);
	snd.set_volume(volume);
	_lastPlayTime = Device.dwTimeGlobal;
	return true;
}

bool CItemPickupSounds::CanPlaySound() const
{
	if (!Sound)
		return false;

	const u32 currentTime = Device.dwTimeGlobal;
	return (currentTime >= _lastPlayTime + MIN_SOUND_INTERVAL);
}

bool CItemPickupSounds::PlaySound(LPCSTR categoryName, float volume)
{
	if (!categoryName || !categoryName[0])
		return false;

	if (_categories.empty())
		Initialize();

	if (!CanPlaySound())
		return false;

	xr_string catName = categoryName;
	xr_map<xr_string, SSoundCategory>::iterator it = _categories.find(catName);
	if (it == _categories.end())
		return false;

	SSoundCategory& cat = it->second;
	if (!cat.initialized || cat.sounds.empty())
		return false;

	const float finalVolume = CalculateVolume(volume);
	const u32 soundCount = (u32)cat.sounds.size();
	const u32 idx = (soundCount > 1) ? Random.randI(soundCount) : 0;
	ref_sound& snd = cat.sounds[idx];
	return PlaySoundInternal(snd, finalVolume);
}

bool CItemPickupSounds::PlaySound(EItemPickupSoundType type, LPCSTR categoryName, float volume)
{
	if (type == EItemPickupSoundType::None)
		return false;

	if (type == EItemPickupSoundType::Custom)
	{
		return PlayCustomSound(categoryName, volume);
	}

	return PlaySound(categoryName, volume);
}

bool CItemPickupSounds::PlayCustomSound(LPCSTR soundPath, float volume)
{
	if (!soundPath || !soundPath[0])
		return false;

	if (!CanPlaySound())
		return false;

	string_path fn;
	if (!FS.exist(fn, _game_sounds_, soundPath, ".ogg"))
		return false;

	const float finalVolume = CalculateVolume(volume);
	ref_sound snd;
	snd.create(soundPath, st_Effect, sg_SourceType);

	return PlaySoundInternal(snd, finalVolume);
}

EItemPickupSoundType CItemPickupSounds::ParseSoundType(LPCSTR str, xr_string& outCategoryName)
{
	if (!str || !str[0])
	{
		outCategoryName.clear();
		return EItemPickupSoundType::None;
	}

	xr_string inputStr = str;

	CItemPickupSounds& instance = Instance();
	if (instance._categories.empty())
		instance.Initialize();

	if (instance._categories.empty())
	{
		outCategoryName = inputStr;
		return EItemPickupSoundType::Custom;
	}

	if (instance._categories.find(inputStr) != instance._categories.end())
	{
		outCategoryName = inputStr;
		return EItemPickupSoundType::Default;
	}

	outCategoryName = inputStr;
	return EItemPickupSoundType::Custom;
}

