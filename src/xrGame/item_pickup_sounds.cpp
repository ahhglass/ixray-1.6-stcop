#include "StdAfx.h"
#include "item_pickup_sounds.h"
#include "../xrEngine/xr_object.h"

namespace
{
	constexpr float kDefaultCustomVolume = 0.9f;
}

CItemPickupSounds::CItemPickupSounds()
{}

CItemPickupSounds::~CItemPickupSounds()
{
	ClearCategories();
	ClearCustomPaths();
}

void CItemPickupSounds::ClearCategories()
{
	for (auto& [name, cat] : _categories)
		HUD_SOUND_ITEM::DestroySound(cat.hudSound);
	_categories.clear();
}

void CItemPickupSounds::ClearCustomPaths()
{
	for (auto& [path, custom] : _customPaths)
		HUD_SOUND_ITEM::DestroySound(custom.hudSound);
	_customPaths.clear();
}

CItemPickupSounds& CItemPickupSounds::Instance()
{
	static CItemPickupSounds instance;
	return instance;
}

bool CItemPickupSounds::IsLayerSection(LPCSTR sect) const
{
	return pSettings && sect && sect[0]
		&& pSettings->section_exist(sect)
		&& pSettings->line_exist(sect, "snd_1_layer");
}

void CItemPickupSounds::EnsureInitialized()
{
	if (!_initialized)
		Initialize();
}

void CItemPickupSounds::Initialize()
{
	if (!pSettings)
		return;

	LPCSTR section = "item_pickup_sounds";
	if (!pSettings->section_exist(section))
	{
		_initialized = true;
		return;
	}

	ClearCategories();

	CInifile::Sect& sect = pSettings->r_section(section);
	for (CInifile::SectCIt it = sect.Data.begin(), it_e = sect.Data.end(); it != it_e; ++it)
	{
		const shared_str categoryName = it->first.c_str();
		const shared_str layerSection = it->second.c_str();
		if (!categoryName.size() || !layerSection.size())
			continue;

		SCategory& cat = _categories[categoryName];
		cat.layerSection = layerSection;
		cat.loaded = false;
	}

	_initialized = true;
}

bool CItemPickupSounds::LoadCategory(SCategory& cat)
{
	if (cat.loaded && !cat.hudSound.sounds.empty())
		return true;

	if (!IsLayerSection(cat.layerSection.c_str()))
		return false;

	HUD_SOUND_ITEM::DestroySound(cat.hudSound);
	HUD_SOUND_ITEM::LoadSound(cat.layerSection.c_str(), "snd_1_layer", cat.hudSound, sg_SourceType);
	cat.loaded = !cat.hudSound.sounds.empty();
	return cat.loaded;
}

bool CItemPickupSounds::LoadCustomPath(const shared_str& path, SCustomPath& custom)
{
	if (custom.loaded && !custom.hudSound.sounds.empty())
		return true;

	if (!path.size())
		return false;

	string_path fn;
	if (!FS.exist(fn, _game_sounds_, path.c_str(), ".ogg"))
		return false;

	HUD_SOUND_ITEM::DestroySound(custom.hudSound);

	HUD_SOUND_ITEM::SSnd sndEntry;
	sndEntry.volume = kDefaultCustomVolume;
	sndEntry.delay = 0.f;
	sndEntry.snd.create(path.c_str(), st_Effect, sg_SourceType);
	if (!sndEntry.snd.handle())
		return false;

	custom.hudSound.sounds.push_back(sndEntry);
	custom.loaded = true;
	return true;
}

bool CItemPickupSounds::CanPlaySound() const
{
	if (!Sound)
		return false;

	return Device.dwTimeGlobal >= _lastPlayTime + MIN_SOUND_INTERVAL;
}

bool CItemPickupSounds::PlayHudSound(HUD_SOUND_ITEM& hud, EItemPickupPlayMode mode, CObject* worldObj, bool skipDebounce)
{
	if (!Sound || hud.sounds.empty())
		return false;

	if (!skipDebounce && !CanPlaySound())
		return false;

	const bool hudMode = (mode == EItemPickupPlayMode::UI_2D);

	Fvector pos = zero_vel;
	CObject* parent = nullptr;

	if (!hudMode && worldObj)
	{
		parent = worldObj;
		if (mode == EItemPickupPlayMode::World_3D || mode == EItemPickupPlayMode::Actor_3D)
			pos = worldObj->Position();
	}

	HUD_SOUND_ITEM::PlaySound(hud, pos, parent, hudMode);
	_lastPlayTime = Device.dwTimeGlobal;
	return true;
}

bool CItemPickupSounds::Play(EItemPickupSoundType type, LPCSTR id, EItemPickupPlayMode mode, CObject* worldObj, bool skipDebounce)
{
	if (type == EItemPickupSoundType::None || !id || !id[0])
		return false;

	EnsureInitialized();

	if (type == EItemPickupSoundType::Category)
	{
		const shared_str categoryName = id;
		xr_map<shared_str, SCategory>::iterator it = _categories.find(categoryName);
		if (it == _categories.end())
			return false;

		if (!LoadCategory(it->second))
			return false;

		return PlayHudSound(it->second.hudSound, mode, worldObj, skipDebounce);
	}

	const shared_str path = id;
	SCustomPath& custom = _customPaths[path];
	if (!LoadCustomPath(path, custom))
		return false;

	return PlayHudSound(custom.hudSound, mode, worldObj, skipDebounce);
}

bool CItemPickupSounds::PlayBulk(EItemPickupPlayMode mode, CObject* worldObj)
{
	return Play(EItemPickupSoundType::Category, "bulk", mode, worldObj, true);
}

EItemPickupSoundType CItemPickupSounds::ParseSoundType(LPCSTR str, shared_str& outId)
{
	outId = nullptr;
	if (!str || !str[0])
		return EItemPickupSoundType::None;

	CItemPickupSounds& instance = Instance();
	instance.EnsureInitialized();

	const shared_str input = str;
	if (instance._categories.find(input) != instance._categories.end())
	{
		outId = input;
		return EItemPickupSoundType::Category;
	}

	if (strchr(str, '\\') != nullptr)
	{
		outId = input;
		return EItemPickupSoundType::CustomPath;
	}

	outId = input;
	return EItemPickupSoundType::CustomPath;
}
