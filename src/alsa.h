/*
 * alsa.h
 *
 *  Created on: Oct 8, 2021
 *      Author: tom
 */

#ifndef ALSA_H_
#define ALSA_H_

#include <alsa/asoundlib.h>
#include <cstdint>
#include <vector>
#include <type_traits>

template <typename T>
class Alsa {
public:
	Alsa(int channels, int rate, int latency)
		: channels(channels), rate(rate), latency(latency)
	{
		int err;
		if ((err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0)
		{
			throw std::system_error(err, std::generic_category());
		}
		if ((err = snd_pcm_set_params(handle,
				getFormat(),
				SND_PCM_ACCESS_RW_INTERLEAVED,
				channels,
				rate,
				1,
				latency)) < 0)
		{
			throw std::system_error(err, std::generic_category());
		}
	}

	void write(std::vector<T>& data)
	{
		snd_pcm_sframes_t sendFrames = (snd_pcm_sframes_t) data.size() / channels;
		snd_pcm_sframes_t frames = snd_pcm_writei(handle, data.data(), sendFrames);
		if (frames < 0)
		{
			frames = snd_pcm_recover(handle, frames, 0);
		}
		if (frames < 0)
		{
			std::cerr << "snd_pcm_writei failed" << snd_strerror(frames) << '\n';
		}
		if (frames > 0 && frames < (snd_pcm_sframes_t) sendFrames)
		{
			std::cerr << "Short write (expected " << sendFrames << ", wrote " << frames << ")\n";
		}
	}

	virtual ~Alsa()
	{
		/* pass the remaining samples, otherwise they're dropped in close */
		int err = snd_pcm_drain(handle);
		if (err < 0)
		{
	        std::cerr << "snd_pcm_drain failed: " << snd_strerror(err) << '\n';
		}

		snd_pcm_close(handle);
	}


private:
	snd_pcm_format_t getFormat()
	{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		if (std::is_same<T, int8_t>::value)
		{
			return SND_PCM_FORMAT_S8;
		}
		if (std::is_same<T, uint8_t>::value)
		{
			return SND_PCM_FORMAT_U8;
		}
		if (std::is_same<T, int16_t>::value)
		{
			return SND_PCM_FORMAT_S16_BE;
		}
		if (std::is_same<T, uint16_t>::value)
		{
			return SND_PCM_FORMAT_U16_BE;
		}
		if (std::is_same<T, int32_t>::value)
		{
			return SND_PCM_FORMAT_S32_BE;
		}
		if (std::is_same<T, uint32_t>::value)
		{
			return SND_PCM_FORMAT_U32_BE;
		}
		if (std::is_same<T, float>::value)
		{
			return SND_PCM_FORMAT_FLOAT_BE;
		}
		if (std::is_same<T, double>::value)
		{
			return SND_PCM_FORMAT_FLOAT64_BE;
		}
#else
		if (std::is_same<T, int8_t>::value)
		{
			return SND_PCM_FORMAT_S8;
		}
		if (std::is_same<T, uint8_t>::value)
		{
			return SND_PCM_FORMAT_U8;
		}
		if (std::is_same<T, int16_t>::value)
		{
			return SND_PCM_FORMAT_S16_LE;
		}
		if (std::is_same<T, uint16_t>::value)
		{
			return SND_PCM_FORMAT_U16_LE;
		}
		if (std::is_same<T, int32_t>::value)
		{
			return SND_PCM_FORMAT_S32_LE;
		}
		if (std::is_same<T, uint32_t>::value)
		{
			return SND_PCM_FORMAT_U32_LE;
		}
		if (std::is_same<T, float>::value)
		{
			return SND_PCM_FORMAT_FLOAT_LE;
		}
		if (std::is_same<T, double>::value)
		{
			return SND_PCM_FORMAT_FLOAT64_LE;
		}

		return SND_PCM_FORMAT_UNKNOWN;
#endif
	}

	int channels;
	int rate;
	int latency;

	snd_pcm_t *handle;
};

#endif /* ALSA_H_ */
