/*
    Copyright (C) 2005-2007 Tom Beaumont

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

/* SF3500 port: replaces SDL_mixer (no real audio *device* exists on this
 * hardware for it to open - confirmed during bring-up) with the
 * same direct hwaudio mixer every other port on this device uses. All
 * assets here are .ogg (music and one-shot effects alike), decoded via
 * stb_vorbis instead of libvorbis/libvorbisfile. */
#ifndef DISABLE_SOUND
#include <list>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include "sfx.h"
#include "system-directory.h"
#include "sf3500/sf3500_mixer.hpp"
#include "debuglog.h"

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include "sf3500/stb_vorbis.h"

#define MUSIC_VOLUME 0.75f
#define SOUND_START_DELAY -0.2

static const char* const music_names[HHOP_MUSIC_MAX] =
{
	"music-ending",
	"music-game",
};
static const char* const sound_names[HHOP_SOUND_MAX] =
{
	"sound-builder",
	"sound-collapse",
	"sound-crack",
	"sound-death",
	"sound-disintegrate",
	"sound-explode-big",
	"sound-explode-small",
	"sound-floater-enter",
	"sound-floater-move",
	"sound-found-antiice",
	"sound-found-jump",
	"sound-ice",
	"sound-laser",
	"sound-lift-up",
	"sound-lift-down",
	"sound-spinner",
	"sound-step",
	"sound-trampoline",
	"sound-ui-fade",
	"sound-ui-menu",
	"sound-used-antiice",
	"sound-used-jump",
	"sound-win"
};

namespace
{

double wall_now()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

std::vector<unsigned char> read_whole_file(const char* path)
{
	std::vector<unsigned char> buf;
	FILE* f = fopen(path, "rb");
	if (!f)
		return buf;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len > 0)
	{
		buf.resize((size_t)len);
		if (fread(buf.data(), 1, (size_t)len, f) != (size_t)len)
			buf.clear();
	}
	fclose(f);
	return buf;
}

// One-time linear resample + channel-fixup, used only at load for short
// one-shot effects (static voices must already be 48kHz stereo S16).
std::vector<int16_t> to_48k_stereo(const short* in, int in_frames, int in_channels, int in_rate)
{
	std::vector<int16_t> out;
	if (in_frames <= 0 || in_channels <= 0)
		return out;
	if (in_rate == 48000 && in_channels == 2)
	{
		out.assign(in, in + (size_t)in_frames * 2);
		return out;
	}
	double ratio = (double)in_rate / 48000.0;
	int out_frames = (int)((double)in_frames / ratio);
	out.resize((size_t)out_frames * 2);
	for (int i = 0 ; i < out_frames ; i++)
	{
		double pos = i * ratio;
		int i0 = (int)pos;
		if (i0 >= in_frames) i0 = in_frames - 1;
		int i1 = i0 + 1;
		if (i1 >= in_frames) i1 = in_frames - 1;
		double frac = pos - (int)pos;
		short l0, r0, l1, r1;
		if (in_channels == 1)
		{
			l0 = r0 = in[i0];
			l1 = r1 = in[i1];
		}
		else
		{
			l0 = in[i0 * 2]; r0 = in[i0 * 2 + 1];
			l1 = in[i1 * 2]; r1 = in[i1 * 2 + 1];
		}
		out[i * 2]     = (int16_t)(l0 + (l1 - l0) * frac);
		out[i * 2 + 1] = (int16_t)(r0 + (r1 - r0) * frac);
	}
	return out;
}

struct SoundAsset
{
	std::vector<int16_t> pcm; // interleaved stereo @ 48000Hz
};

struct MusicAsset
{
	std::vector<unsigned char> raw; // undecoded .ogg bytes
};

std::vector<SoundAsset> sound_chunks[HHOP_SOUND_MAX];
std::vector<MusicAsset> music_chunks[HHOP_MUSIC_MAX];

struct QueuedSound { int type; double time; };
std::list<QueuedSound> sound_queue;

struct EffectVoice { Sf3500Voice* voice; };
std::vector<EffectVoice> active_effects;

// Decodes one music track incrementally from its in-memory .ogg bytes - the
// mixer's own stream voice pulls from this on its background thread.
struct MusicStream
{
	stb_vorbis* vorbis;
	int channels; // stb_vorbis is opaque in header-only mode - cache this from stb_vorbis_get_info() at open time
};

int stream_fill_cb(void* userdata, int16_t* dst, int frames_wanted)
{
	MusicStream* ms = (MusicStream*)userdata;
	return stb_vorbis_get_samples_short_interleaved(ms->vorbis, ms->channels, dst,
		frames_wanted * ms->channels);
}

Sf3500Voice* music_voice = NULL;
MusicStream* music_stream = NULL;

enum MusicFade { FADE_NONE, FADE_IN, FADE_OUT };
MusicFade music_fade = FADE_NONE;
double music_fade_start = 0;

int disable_music = 0;
int disable_effects = 0;
int music_curr = -1;
int music_next = -1;

void destroy_music_voice()
{
	if (music_voice)
	{
		sf3500_voice_destroy(music_voice);
		music_voice = NULL;
	}
	if (music_stream)
	{
		stb_vorbis_close(music_stream->vorbis);
		delete music_stream;
		music_stream = NULL;
	}
	music_fade = FADE_NONE;
}

void start_music(int type)
{
	destroy_music_voice();
	if (type < 0 || type >= HHOP_MUSIC_MAX || music_chunks[type].empty())
		return;

	int idx = rand() % music_chunks[type].size();
	MusicAsset& asset = music_chunks[type][idx];

	int error = 0;
	stb_vorbis* v = stb_vorbis_open_memory(asset.raw.data(), (int)asset.raw.size(), &error, NULL);
	if (!v)
	{
		DebugLog("Cannot decode music track (stb_vorbis error %d)", error);
		return;
	}

	stb_vorbis_info info = stb_vorbis_get_info(v);

	music_stream = new MusicStream();
	music_stream->vorbis = v;
	music_stream->channels = info.channels;

	Sf3500Voice* voice = sf3500_voice_create_stream(stream_fill_cb, music_stream, (int)info.sample_rate, info.channels);
	if (!voice)
	{
		stb_vorbis_close(v);
		delete music_stream;
		music_stream = NULL;
		return;
	}

	music_voice = voice;
	sf3500_voice_set_relative(music_voice, true);
	sf3500_voice_set_looping(music_voice, false); // restart picks a fresh random variant, same as upstream
	sf3500_voice_set_gain(music_voice, 0.0f);
	sf3500_voice_play(music_voice);
	music_fade = FADE_IN;
	music_fade_start = wall_now();
}

void play_one_shot(int type)
{
	if (disable_effects)
		return;
	if (type < 0 || type >= HHOP_SOUND_MAX || sound_chunks[type].empty())
		return;
	int idx = rand() % sound_chunks[type].size();
	SoundAsset& a = sound_chunks[type][idx];
	if (a.pcm.empty())
		return;
	Sf3500Voice* v = sf3500_voice_create_static(a.pcm.data(), (int)(a.pcm.size() / 2));
	if (!v)
		return; // voice pool exhausted - drop the sound, not worth blocking on
	sf3500_voice_set_relative(v, true);
	sf3500_voice_set_gain(v, 1.0f);
	sf3500_voice_play(v);
	EffectVoice ev = { v };
	active_effects.push_back(ev);
}

void reap_effect_voices()
{
	for (size_t i = 0 ; i < active_effects.size() ; )
	{
		if (!sf3500_voice_playing(active_effects[i].voice))
		{
			sf3500_voice_destroy(active_effects[i].voice);
			active_effects.erase(active_effects.begin() + i);
		}
		else
			i++;
	}
}

void stop_all_effects()
{
	for (size_t i = 0 ; i < active_effects.size() ; i++)
		sf3500_voice_destroy(active_effects[i].voice);
	active_effects.clear();
}

} // namespace

void InitSound(const char* path)
{
	srand((unsigned)time(NULL));

	if (!sf3500_mixer_init())
	{
		DebugLog("InitSound: sf3500_mixer_init FAILED - continuing without sound");
		return;
	}
	DebugLog("InitSound: sf3500_mixer_init OK");

	lisysDir* dir = lisys_dir_open(path);
	if (dir == NULL)
	{
		DebugLog("InitSound: lisys_dir_open FAILED for `%s'", path);
		return;
	}
	lisys_dir_set_filter(dir, LISYS_DIR_FILTER_FILES);
	if (!lisys_dir_scan(dir))
	{
		DebugLog("InitSound: lisys_dir_scan FAILED");
		lisys_dir_free(dir);
		return;
	}

	for (int i = 0 ; i < lisys_dir_get_count(dir) ; i++)
	{
		const char* name = lisys_dir_get_name(dir, i);
		char* pth = lisys_dir_get_path(dir, i);

		for (int j = 0 ; j < HHOP_MUSIC_MAX ; j++)
		{
			if (strstr(name, music_names[j]) == name)
			{
				std::vector<unsigned char> raw = read_whole_file(pth);
				if (!raw.empty())
				{
					MusicAsset a;
					a.raw = raw;
					music_chunks[j].push_back(a);
				}
				else
					DebugLog("Cannot load music `%s'", name);
			}
		}
		for (int j = 0 ; j < HHOP_SOUND_MAX ; j++)
		{
			if (strstr(name, sound_names[j]) == name)
			{
				std::vector<unsigned char> raw = read_whole_file(pth);
				int channels = 0, rate = 0;
				short* decoded = NULL;
				int frames = raw.empty() ? 0 :
					stb_vorbis_decode_memory(raw.data(), (int)raw.size(), &channels, &rate, &decoded);
				if (frames > 0 && decoded)
				{
					SoundAsset a;
					a.pcm = to_48k_stereo(decoded, frames, channels, rate);
					sound_chunks[j].push_back(a);
				}
				else
					DebugLog("Cannot load effect `%s'", name);
				free(decoded);
			}
		}
		free(pth);
	}

	lisys_dir_free(dir);

	int total_sounds = 0;
	for (int j = 0 ; j < HHOP_SOUND_MAX ; j++)
		total_sounds += (int)sound_chunks[j].size();
	DebugLog("InitSound: music[ENDING]=%zu music[GAME/MAP/TITLE/WIN]=%zu sound_effects=%d",
		music_chunks[HHOP_MUSIC_ENDING].size(), music_chunks[HHOP_MUSIC_GAME].size(), total_sounds);
}

void FreeSound()
{
	stop_all_effects();
	destroy_music_voice();
	sf3500_mixer_deinit();
}

void PlayMusic(int type)
{
	if (disable_music)
		return;
	if (music_curr != type || music_next != type)
	{
		music_curr = -2;
		music_next = type;
		if (music_voice)
		{
			music_fade = FADE_OUT;
			music_fade_start = wall_now();
		}
	}
}

void PlaySound(int type)
{
	play_one_shot(type);
}

void QueueSound(int type, double time)
{
	if (disable_effects)
		return;
	QueuedSound q = { type, time + SOUND_START_DELAY };
	sound_queue.push_back(q);
}

void ToggleMusic()
{
	disable_music = !disable_music;
	if (disable_music)
		destroy_music_voice();
}

void ToggleEffects()
{
	disable_effects = !disable_effects;
	if (disable_effects)
	{
		stop_all_effects();
		sound_queue.clear();
	}
}

void UndoSound()
{
	sound_queue.clear();
}

void UpdateSound(double time)
{
	while (true)
	{
		std::list<QueuedSound>::iterator i;
		for (i = sound_queue.begin() ; i != sound_queue.end() ; i++)
		{
			if (time >= i->time)
			{
				play_one_shot(i->type);
				break;
			}
		}
		if (i != sound_queue.end())
			sound_queue.erase(i);
		else
			break;
	}

	reap_effect_voices();

	double now = wall_now();
	if (music_fade == FADE_OUT && music_voice)
	{
		double t = (now - music_fade_start) / (HHOP_FADE_MUSIC_OUT / 1000.0);
		if (t >= 1.0)
			destroy_music_voice();
		else
			sf3500_voice_set_gain(music_voice, (float)(MUSIC_VOLUME * (1.0 - t)));
	}
	else if (music_fade == FADE_IN && music_voice)
	{
		double t = (now - music_fade_start) / (HHOP_FADE_MUSIC_IN / 1000.0);
		if (t >= 1.0)
		{
			sf3500_voice_set_gain(music_voice, MUSIC_VOLUME);
			music_fade = FADE_NONE;
		}
		else
			sf3500_voice_set_gain(music_voice, (float)(MUSIC_VOLUME * t));
	}

	if (!disable_music && music_fade != FADE_OUT && sf3500_mixer_active())
	{
		bool playing = music_voice && sf3500_voice_playing(music_voice);
		if (!playing)
		{
			start_music(music_next);
			music_curr = music_next;
		}
	}
}

#else // DISABLE_SOUND

void InitSound(const char*) { }
void FreeSound() { }
void PlayMusic(int) { }
void PlaySound(int) { }
void QueueSound(int, double) { }
void ToggleMusic() { }
void ToggleEffects() { }
void UndoSound() { }
void UpdateSound(double) { }

#endif
