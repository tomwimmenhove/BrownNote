/*
 * alsa.cpp
 *
 *  Created on: Oct 8, 2021
 *      Author: tom
 */

#include <system_error>

#include "alsa.h"

Alsa::Alsa(int channels, int rate, int latency)
: channels(channels), rate(rate), latency(latency)
{
	int err;
	if ((err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0)
	{
		throw std::system_error(err, std::generic_category());
	}
	if ((err = snd_pcm_set_params(handle,
			SND_PCM_FORMAT_S16_LE,
			SND_PCM_ACCESS_RW_INTERLEAVED,
			channels,
			rate,
			1,
			latency)) < 0)
	{
		throw std::system_error(err, std::generic_category());
	}
}

void Alsa::write(std::vector<int16_t>& data)
{
	snd_pcm_sframes_t frames = snd_pcm_writei(handle, data.data(), data.size());
	if (frames < 0)
	{
		frames = snd_pcm_recover(handle, frames, 0);
	}
	if (frames < 0)
	{
		fprintf(stderr, "snd_pcm_writei failed: %s\n", snd_strerror(frames));
	}
	if (frames > 0 && frames < data.size())
	{
		fprintf(stderr, "Short write (expected %li, wrote %li)\n", data.size(), frames);
	}

}

Alsa::~Alsa()
{
	/* pass the remaining samples, otherwise they're dropped in close */
	int err = snd_pcm_drain(handle);
	if (err < 0)
	{
		throw std::system_error(err, std::generic_category());
	}

	snd_pcm_close(handle);
}
