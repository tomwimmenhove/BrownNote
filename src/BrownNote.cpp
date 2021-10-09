//============================================================================
// Name        : BrownNote.cpp
// Author      : Tom Wimmenhove
// Version     :
// Copyright   : GPL 2
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <iostream>
#include <vector>
#include <functional>
#include <algorithm>
#include <memory>
#include <queue>
#include <cmath>

#include "alsa.h"

typedef float signalType;

template <typename T>
class DataStream
{
public:
	virtual std::vector<T>& getData() = 0;

	virtual ~DataStream() { }
};

template <typename T>
class DumbSource: public DataStream<T>
{
public:
	DumbSource(T start = 0)
		: buffer({ start + 0, start + 1, start + 2, start + 3, })
	{ }

	std::vector<T>& getData() override
	{
		return buffer;
	}

private:
	std::vector<T> buffer;
};

template <typename T>
class DcSource: public DataStream<T>
{
public:
	DcSource(T dcValue, size_t n = 1024)
		: dcValue(dcValue), buffer(n, dcValue)
	{ }

	std::vector<T>& getData() override { return buffer; }

private:
	T dcValue;
	std::vector<T> buffer;
};

template <typename T>
class SineSource: public DataStream<T>
{
public:
	SineSource(double rate, T amplitude = 1.0, size_t n = 1024)
		: inc(rate * M_PI * 2), amplitude(amplitude),  buf(n), x(0)
	{ }

	std::vector<T>& getData() override
	{
		for(size_t i = 0; i < buf.size(); i++)
		{
			buf[i] = sin(x) * amplitude;
			x += inc;

			if (x > M_PI * 2)
			{
				x -= M_PI * 2;
			}
		}

		return buf;
	}

private:
	double inc;
	T amplitude;
	std::vector<T> buf;
	T x;
};

template <typename T>
class Gain : public DataStream<T>
{
public:
	Gain(std::shared_ptr<DataStream<T>> dataStream, T gain)
	: dataStream(dataStream), gain(gain)
	{ }

	std::vector<T>& getData() override
	{
		auto& data = dataStream->getData();

		buf.resize(data.size());

		std::transform(data.begin(), data.end(), buf.begin(),
				std::bind(std::multiplies<T>(), std::placeholders::_1, gain));

		return buf;
	}

private:
	std::vector<T> buf;
	std::shared_ptr<DataStream<T>> dataStream;
	T gain;
};

template <typename T>
class Splitter : public DataStream<T>
{
public:
	Splitter(std::shared_ptr<DataStream<T>> dataStream, size_t channels)
	: dataStream(dataStream), channels(channels), channel(0)
	{ }

	std::vector<T>& getData() override
	{
		if (channel == 0)
		{
			buf.clear();
			auto& data = dataStream->getData();
			std::copy(data.begin(), data.end(), back_inserter(buf));
		}

		if (++channel >= channels)
		{
			channel = 0;
		}

		return buf;
	}

private:
	std::vector<T> buf;
	std::shared_ptr<DataStream<T>> dataStream;
	size_t channels;
	size_t channel;
};

template <typename T>
class Mixer : public DataStream<T>
{
public:
	Mixer(std::initializer_list<std::shared_ptr<DataStream<T>>> dataStreams)
		: dataStreams(dataStreams),
		  combiner([](const signalType a, const signalType b) { return a + b; })
	{ }

	Mixer(std::function<T(T, T)> combiner,
			std::initializer_list<std::shared_ptr<DataStream<T>>> dataStreams)
		: dataStreams(dataStreams), combiner(combiner)
	{ }

	std::vector<T>& getData() override
	{
		buf.clear();

		if (dataStreams.size() == 0)
		{
			buf.resize(1024);

			return buf;
		}

		auto& data0 = dataStreams[0]->getData();
		std::copy(data0.begin(), data0.end(), back_inserter(buf));

		for(auto it = dataStreams.begin() + 1; it < dataStreams.end(); it++)
		{
			auto& data = (*it)->getData();
			if (data0.size() != data.size())
			{
				std::cerr << "Size mismatch!\n";
				return buf;
			}

			/* Mix it */
			for(size_t i = 0; i < buf.size(); i++)
			{
				buf[i] = combiner(buf[i], data[i]);
				//buf[i] += data[i];
			}
		}

		return buf;
	}

	size_t numStreams() const { return dataStreams.size(); }

private:
	std::vector<T> buf;
	std::vector<std::shared_ptr<DataStream<T>>> dataStreams;
	std::function<T(T, T)> combiner;
};

template <typename T>
class AlsaMonoSink
{
public:
	AlsaMonoSink(std::shared_ptr<DataStream<T>> dataStream)
		: dataStream(dataStream), alsa(1, 48000, 500000)
	{ }

	void run()
	{
		while(true)
		{
			auto& data = dataStream->getData();

			audio.clear();
			audio.reserve(data.size());

			for(auto& sample: data)
			{
				int x = sample * 32768.0;

				/* Clip */
				if (x > 32767) x = 32767;
				if (x < -32768) x = -32768;

				audio.push_back(x);
			}

			alsa.write(audio);
		}
	}

private:
	std::vector<int16_t> audio;
	std::shared_ptr<DataStream<T>> dataStream;
	Alsa alsa;
};

template <typename T>
class DataBuffer : public DataStream<T>
{
public:
	DataBuffer(std::shared_ptr<DataStream<T>> dataStream, size_t len)
	: dataStream(dataStream), buf(len), len(len)
	{ }

	std::vector<T>& getData() override
	{
		// XXX: Replace this with a circular buffer
		while (tmpBuf.size() < buf.size())
		{
			auto& data = dataStream->getData();
			tmpBuf.insert(tmpBuf.end(), data.begin(), data.end());
		}

		buf.clear();
		buf.insert(buf.begin(), tmpBuf.begin(), tmpBuf.begin() + len);

		//std::copy(tmpBuf.begin(), tmpBuf.begin() + len, back_inserter(buf));
		tmpBuf.erase(tmpBuf.begin(), tmpBuf.begin() + len);

		return buf;
	}

	inline size_t size() const { return buf.size(); }

private:
	std::shared_ptr<DataStream<T>> dataStream;
	std::vector<T> buf;
	std::vector<T> tmpBuf;
	size_t len;
};

double fRand(double fMin, double fMax)
{
    double f = (double)rand() / RAND_MAX;
    return fMin + f * (fMax - fMin);
}

double littleError(double x, double scale = 50.0)
{
	double max = x / scale;
	return x + fRand(-max, max);
}

std::shared_ptr<DataStream<signalType>> shittyTone(double freq, signalType amplitude,
		double vibrFreq, signalType vibrAmplitude)
{
	auto tone = std::make_shared<SineSource<signalType>>(littleError(freq) / 48000.0, amplitude);

	auto one = std::make_shared<DcSource<signalType>>(1.0);
	auto vibrato = std::make_shared<SineSource<signalType>>(vibrFreq / 48000.0, vibrAmplitude);
	auto vibratoScaler = std::make_shared<Mixer<signalType>>(
			std::initializer_list<std::shared_ptr<DataStream<signalType>>>(
					{one, vibrato}));

	return std::make_shared<Mixer<signalType>>(
				[](const signalType a, const signalType b) { return a * b; },
				std::initializer_list<std::shared_ptr<DataStream<signalType>>>(
						{tone, vibratoScaler}));
}
#if 0
int main()
{
	return 0;
}
#else
int main()
{
#if 1
//	auto tone1 = std::make_shared<SineSource<signalType>>(littleError(125.0) / 2 / 48000.0, 0.2);
//	auto tone2 = std::make_shared<SineSource<signalType>>(littleError(125.0) / 4 / 48000.0, 0.3);
//	auto tone3 = std::make_shared<SineSource<signalType>>(littleError(125) / 48000.0, 0.5);
//	auto tone4 = std::make_shared<SineSource<signalType>>(littleError(250) / 48000.0, 1.0);
//	auto tone5 = std::make_shared<SineSource<signalType>>(littleError(500) / 48000.0, 0.5);
//	auto tone6 = std::make_shared<SineSource<signalType>>(littleError(1000.0) / 48000.0, 0.3);
//	auto tone7 = std::make_shared<SineSource<signalType>>(littleError(2000.0) / 48000.0, 0.2);
//	auto tone8 = std::make_shared<SineSource<signalType>>(littleError(4000.0) / 48000.0, 0.05);
//
//	auto tone9 = std::make_shared<SineSource<signalType>>(littleError(2500) * 3 / 48000.0, .05);
//	auto tone10 = std::make_shared<SineSource<signalType>>(littleError(250) / 3 / 48000.0, .15);

	auto tone1 = shittyTone(125.0 / 2, 0.2, .1, 0.2);
	auto tone2 = shittyTone(125.0 / 4, 0.3, .3, 0.2);
	auto tone3 = shittyTone(125, 0.5, .5, 0.2);
	auto tone4 = shittyTone(250, 1.0, .7, 0.2);
	auto tone5 = shittyTone(500, 0.5, .6, 0.2);
	auto tone6 = shittyTone(1000.0, 0.4, .5, 0.2);
	auto tone7 = shittyTone(2000.0, 0.1, .3, 0.2);

	auto tone9 = shittyTone(2500 * 3, .05, 5, 0.2);
	auto tone10 = shittyTone(250 / 3, .15, 5, 0.2);

	auto combinedTones = std::make_shared<Mixer<signalType>>(
			std::initializer_list<std::shared_ptr<DataStream<signalType>>>(
					{tone1, tone2, tone3, tone4, tone5, tone6, tone7,
					 tone9, tone10}));

//	auto one = std::make_shared<DcSource<signalType>>(1.0);
//	auto vibrato = std::make_shared<SineSource<signalType>>(1.0 / 48000.0, .05);
//	auto vibratoScaler = std::make_shared<Mixer<signalType>>(
//			std::initializer_list<std::shared_ptr<DataStream<signalType>>>(
//					{one, vibrato}));
//
//	auto organ = std::make_shared<Mixer<signalType>>(
//			[](const signalType a, const signalType b) { return a * b; },
//			std::initializer_list<std::shared_ptr<DataStream<signalType>>>(
//					{combinedTones, vibratoScaler}));
//
//	auto masterChannel = std::make_shared<Gain<signalType>>(organ, 1.0 / combinedTones->numStreams());

	auto masterChannel = std::make_shared<Gain<signalType>>(combinedTones, 1.0 / combinedTones->numStreams());

	AlsaMonoSink<signalType> sound(masterChannel);
//
//	auto beep = std::make_shared<SineSource<signalType>>(1000.0 / 48000.0, 1.0);
//	auto g = std::make_shared<Gain<signalType>>(beep, 1.0);
//	AlsaMonoSink<signalType> sound(g);

	sound.run();

	return 0;
#endif

	auto source1 = std::make_shared<DumbSource<signalType>>(1);
	auto source2 = std::make_shared<DumbSource<signalType>>(1);
	auto source3 = std::make_shared<DumbSource<signalType>>(2);

	auto buf1 = std::make_shared<DataBuffer<signalType>>(source1, 5);
	auto buf2 = std::make_shared<DataBuffer<signalType>>(source3, 5);

	auto split = std::make_shared<Splitter<signalType>>(buf1, 2);

	auto mix = std::make_shared<Mixer<signalType>>(
			std::initializer_list<std::shared_ptr<DataStream<signalType>>>(
					{split, split, buf2}));

	auto& d1 = mix->getData();
	for (auto& x: d1)
	{
		std::cout << x << '\n';
	}

	auto& d2 = mix->getData();
	for (auto& x: d2)
	{
		std::cout << x << '\n';
	}

	std::cout << "Done\n";

	return 0;
}
#endif
