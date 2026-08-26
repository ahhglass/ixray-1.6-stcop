#pragma once


struct HUD_SOUND_ITEM
{
    HUD_SOUND_ITEM() : m_activeSnd(NULL), m_b_exclusive(false)
    {
        m_alias = "";
    }

    static void LoadSound(const char* section, const char* line, ref_sound& hud_snd, int type = sg_SourceType, float* volume = nullptr, float* delay = nullptr, esound_type sound_type = st_Effect);

    static void LoadSound(const char* section, const char* line, HUD_SOUND_ITEM& hud_snd, int type = sg_SourceType, esound_type sound_type = st_Effect);

	static void DestroySound(HUD_SOUND_ITEM& hud_snd);

    static void PlaySound(HUD_SOUND_ITEM& snd, const Fvector& position, const CObject* parent, bool hud_mode, bool looped = false, bool allowOverlap = false, u8 index = u8(-1));

	static void	StopSound(HUD_SOUND_ITEM& snd);

	// Луп без HUD_SOUND_COLLECTION::PlaySound (тот останавливает exclusive-слоты коллекции).
	// Старт/обновление громкости и позиции; b_hud_mode=false - 3D в мире (для перегрева у дула).
	static void	UpdateLoopedHudSound(HUD_SOUND_ITEM& hud_snd, const Fvector& position, const CObject* parent, bool b_hud_mode, float volume, u8 index = u8(-1));

	ICF bool playing() const
	{
		auto has_feedback = [](const ref_sound& snd) -> bool
		{
			return snd._p && snd._p->feedback;
		};

		if (m_activeSnd && has_feedback(m_activeSnd->snd))
			return true;

		for (const SSnd& sound : sounds)
		{
			if (has_feedback(sound.snd))
				return true;
		}

		return false;
	}

    ICF void set_position(const Fvector& pos)
    {
		for (SSnd& sound : sounds)
		{
			if (!sound.snd._feedback())
				continue;

			if (!sound.snd._feedback()->is_2D())
				sound.snd.set_position(pos);
		}
    }

    static float g_fHudSndFrequency;
    ICF static void SetHudSndGlobalFrequency(const float& fFreq)
    {
        // SM_TODO: Bad for parallelization
        g_fHudSndFrequency = fFreq;
		}

    static float g_fHudSndVolumeFactor;
    ICF static void SetHudSndGlobalVolumeFactor(const float& fVolume)
    {
        // SM_TODO: Bad for parallelization
        g_fHudSndVolumeFactor = fVolume;
	}

    struct SSnd
    {
		ref_sound	snd;
		float		delay;		//задержка перед проигрыванием
		float		volume;		//громкость
	};
	xr_string		m_alias;
	SSnd*			m_activeSnd;
	bool			m_b_exclusive;
	xr_vector<SSnd> sounds;

	bool operator == (const char* alias) const{return 0==_stricmp(m_alias.c_str(),alias);}

	void SetVolume(float new_volume);
};

class HUD_SOUND_COLLECTION
{
    // xr_vector<HUD_SOUND_ITEM>	m_sound_items;
    // HUD_SOUND_ITEM*				FindSoundItem	(	const char* alias, bool b_assert);
public:
	xr_string m_alias; // Alundaio: For use when it's part of a layered Collection
	~HUD_SOUND_COLLECTION();

    void Clear();
	
    HUD_SOUND_COLLECTION();

    xr_vector<HUD_SOUND_ITEM> m_sound_items; // Alundaio: made public

    HUD_SOUND_ITEM* FindSoundItem(const char* alias, bool b_assert); // AVO: made public to check if sound is loaded
    void PlaySound(HUD_SOUND_ITEM* snd_item, const Fvector& position, const CObject* parent, bool hud_mode, bool looped, bool allowOverlap, u8 index);
    void PlaySound(const char* alias, const Fvector& position, const CObject* parent, bool hud_mode, bool looped = false, bool allowOverlap = false, u8 index = u8(-1));

	void						StopSound		(	const char* alias);

    void LoadSound(const char* section, const char* line, const char* alias, bool exclusive = false, int type = sg_SourceType, esound_type sound_type = st_Effect);

	void						SetPosition		(	const char* alias, 	const Fvector& pos);
	void						SetVolume		(	const char* alias,	float volume);
	void						StopAllSounds	();
};

//Alundaio:
class HUD_SOUND_COLLECTION_LAYERED
{
	xr_vector<HUD_SOUND_COLLECTION>	m_sound_items;
public:
	~HUD_SOUND_COLLECTION_LAYERED();
    HUD_SOUND_ITEM* FindSoundItem(const char* alias, bool b_assert);
    void PlaySound(const char* alias, const Fvector& position, const CObject* parent, bool hud_mode, bool looped = false, bool allowOverlap = false, u8 index = u8(-1));
    void StopSound(const char* alias);
    void StopAllSounds();
    void LoadSound(const char* section, const char* line, const char* alias, bool exclusive = false, int type = sg_SourceType, esound_type sound_type = st_Effect);
    void LoadSound(CInifile const* ini, const char* section, const char* line, const char* alias, bool exclusive = false, int type = sg_SourceType, esound_type sound_type = st_Effect);
    void SetPosition(const char* alias, const Fvector& pos);
};
//-Alundaio