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
#include <memory>
#include <queue>
#include <cmath>

#include "alsa.h"

typedef float signalType;

template <typename T>
class DataStream
{
public:
	virtual size_t PushToVector(std::vector<T>&) = 0;

	virtual ~DataStream() { }
};

template <typename T>
class DumbSource: public DataStream<T>
{
public:
	DumbSource(T start = 0)
		: start(start)
	{ }

	size_t PushToVector(std::vector<T>& v) override
	{
		T x[] = { start + 0, start + 1, start + 2, start + 3, };
		size_t n = sizeof(x) / sizeof(T);

		v.reserve(v.size() + n);
		std::copy(&x[0], &x[n], back_inserter(v));

		return n;
	}

private:
	T start;
};

template <typename T>
class DcSource: public DataStream<T>
{
public:
	DcSource(T dcValue, size_t n = 1024)
		: dcValue(dcValue), n(n)
	{ }

	size_t PushToVector(std::vector<T>& v) override
	{
		v.reserve(v.size() + n);

		for (size_t i = 0; i < n; i++)
		{
			v.push_back(dcValue);
		}

		return n;
	}

private:
	T dcValue;
	size_t n;
};

template <typename T>
class SineSource: public DataStream<T>
{
public:
	SineSource(double rate, T amplitude = 1.0, size_t n = 1024)
		: inc(rate * M_PI * 2), amplitude(amplitude),  n(n), x(0)
	{ }

	size_t PushToVector(std::vector<T>& v) override
	{
		for(size_t i = 0; i < n; i++)
		{
			v.push_back(sin(x) * amplitude);
			x += inc;

			if (x > M_PI * 2)
			{
				x -= M_PI * 2;
			}
		}

		return n;
	}

private:
	double inc;
	T amplitude;
	size_t n;
	T x;
};

template <typename T>
class Gain : public DataStream<T>
{
public:
	Gain(std::shared_ptr<DataStream<T>> dataStream, T gain)
	: dataStream(dataStream), gain(gain)
	{ }

	size_t PushToVector(std::vector<T>& v) override
	{
		auto start = v.size();

		size_t ret = dataStream->PushToVector(v);

		for(auto i = start; i < v.size(); i++)
		{
			v[i] *= gain;
		}

		return ret;
	}

private:
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

	size_t PushToVector(std::vector<T>& v) override
	{
		if (channel == 0)
		{
			buf.clear();
			dataStream->PushToVector(buf);
		}

		std::copy(buf.begin(), buf.end(), std::back_inserter(v));

		if (++channel >= channels)
		{
			channel = 0;
		}

		return buf.size();
	}

private:
	std::vector<T> buf;
	std::shared_ptr<DataStream<T>> dataStream;
	size_t channels;
	size_t channel;
};

template <typename T>
class Mixer: public DataStream<T>
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

	size_t PushToVector(std::vector<T>& v) override
	{
		if (dataStreams.size() == 0)
		{
			v.resize(v.size() + 1024);
		}

		auto start = v.size();
		size_t len = dataStreams[0]->PushToVector(v);

		for(auto it = dataStreams.begin() + 1; it < dataStreams.end(); it++)
		{
			tmpBuf.clear();
			if ((*it)->PushToVector(tmpBuf) != len)
			{
				// Die
			}

			/* Mix it */
			for(size_t i = 0; i < tmpBuf.size(); i++)
			{
				//v[start + i] += tmpBuf[i];
				v[start + i] = combiner(v[start + i], tmpBuf[i]);
			}
		}

		return len;
	}

	size_t numStreams() const { return dataStreams.size(); }

private:
	std::vector<T> tmpBuf;
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
			buf.clear();
			dataStream->PushToVector(buf);

			audio.clear();
			audio.reserve(buf.size());

			for(auto& sample: buf)
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
	std::vector<T> buf;
	std::vector<int16_t> audio;
	std::shared_ptr<DataStream<T>> dataStream;
	Alsa alsa;
};

template <typename T>
class DataBuffer : public DataStream<T>
{
public:
	DataBuffer(std::shared_ptr<DataStream<T>> dataStream, size_t len)
	: dataStream(dataStream), len(len)
	{ }

	size_t PushToVector(std::vector<T>& v) override
	{
		v.reserve(v.size() + len);

		// XXX: Replace this with a circular buffer
		while (tmpBuf.size() < len)
		{
			dataStream->PushToVector(tmpBuf);
		}

		std::copy(tmpBuf.begin(), tmpBuf.begin() + len, back_inserter(v));
		tmpBuf.erase(tmpBuf.begin(), tmpBuf.begin() + len);

		return len;
	}

	inline size_t size() const { return len; }

private:
	std::shared_ptr<DataStream<T>> dataStream;
	size_t len;
	std::vector<T> tmpBuf;
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

int main()
{
	auto tone1 = std::make_shared<SineSource<signalType>>(littleError(125.0) / 2 / 48000.0, 0.2);
	auto tone2 = std::make_shared<SineSource<signalType>>(littleError(125.0) / 4 / 48000.0, 0.3);
	auto tone3 = std::make_shared<SineSource<signalType>>(littleError(125) / 48000.0, 0.5);
	auto tone4 = std::make_shared<SineSource<signalType>>(littleError(250) / 48000.0, 1.0);
	auto tone5 = std::make_shared<SineSource<signalType>>(littleError(500) / 48000.0, 0.5);
	auto tone6 = std::make_shared<SineSource<signalType>>(littleError(1000.0) / 48000.0, 0.3);
	auto tone7 = std::make_shared<SineSource<signalType>>(littleError(2000.0) / 48000.0, 0.2);
	auto tone8 = std::make_shared<SineSource<signalType>>(littleError(4000.0) / 48000.0, 0.05);

	auto tone9 = std::make_shared<SineSource<signalType>>(littleError(2500) * 3 / 48000.0, .05);
	auto tone10 = std::make_shared<SineSource<signalType>>(littleError(250) / 3 / 48000.0, .15);

	auto combinedTones = std::make_shared<Mixer<signalType>>(
			std::initializer_list<std::shared_ptr<DataStream<signalType>>>(
					{tone1, tone2, tone3, tone4, tone5, tone6, tone7, tone8,
					 tone9, tone10}));

	auto one = std::make_shared<DcSource<signalType>>(1.0);
	auto vibrato = std::make_shared<SineSource<signalType>>(1.0 / 48000.0, .05);
	auto vibratoScaler = std::make_shared<Mixer<signalType>>(
			std::initializer_list<std::shared_ptr<DataStream<signalType>>>(
					{one, vibrato}));

	auto organ = std::make_shared<Mixer<signalType>>(
			[](const signalType a, const signalType b) { return a * b; },
			std::initializer_list<std::shared_ptr<DataStream<signalType>>>(
					{combinedTones, vibratoScaler}));

	auto masterChannel = std::make_shared<Gain<signalType>>(organ, 1.0 / combinedTones->numStreams());

	AlsaMonoSink<signalType> sound(masterChannel);

	sound.run();

	return 0;
	auto source1 = std::make_shared<DumbSource<signalType>>(1);
	auto source2 = std::make_shared<DumbSource<signalType>>(1);
	auto source3 = std::make_shared<DumbSource<signalType>>(2);

	auto buf1 = std::make_shared<DataBuffer<signalType>>(source1, 5);
	auto buf2 = std::make_shared<DataBuffer<signalType>>(source3, 5);

	auto split = std::make_shared<Splitter<signalType>>(buf1, 2);

	auto mix = std::make_shared<Mixer<signalType>>(
			std::initializer_list<std::shared_ptr<DataStream<signalType>>>(
					{split, split, buf2}));

	std::vector<signalType> v;
	v.reserve(buf1->size());

	mix->PushToVector(v);
	for (auto& x: v)
	{
		std::cout << x << '\n';
	}

	v.clear();
	mix->PushToVector(v);
	for (auto& x: v)
	{
		std::cout << x << '\n';
	}

	std::cout << "Done\n";

	return 0;
}
