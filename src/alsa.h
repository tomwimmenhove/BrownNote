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

class Alsa {
public:
	Alsa(int channels, int rate, int latency);

	void write(std::vector<int16_t>& data);

	virtual ~Alsa();

private:
	int channels;
	int rate;
	int latency;

	snd_pcm_t *handle;
};

#endif /* ALSA_H_ */
