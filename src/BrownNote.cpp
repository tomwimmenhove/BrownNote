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
#include <system_error>
#include <random>

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
class NoiseSource: public DataStream<T>
{
public:
	NoiseSource(T amplitude = 1.0, size_t n = 1024)
		: amplitude(amplitude),  buf(n)
	{
		std::random_device rd;
	    rnd = std::default_random_engine(rd());
	    distr = std::uniform_real_distribution<T>(-amplitude, amplitude);
	}

	std::vector<T>& getData() override
	{
		for(size_t i = 0; i < buf.size(); i++)
		{
			buf[i] = distr(rnd);
		}

		return buf;
	}

private:
	T amplitude;
	std::vector<T> buf;
	std::uniform_real_distribution<T> distr;
	std::default_random_engine rnd;
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
			buf.insert(buf.end(), data.begin(), data.end());
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
class Transformer : public DataStream<T>
{
public:
	Transformer(std::shared_ptr<DataStream<T>> dataStream)
		: dataStream(dataStream)
	{ }

	std::vector<T>& getData() override
	{
		auto& data = dataStream->getData();

		buf.resize(data.size());

		std::transform(data.begin(), data.end(), buf.begin(),
				std::bind(&Transformer::transform, this, std::placeholders::_1));

		return buf;
	}

protected:
	virtual T transform(const T& x) = 0;

private:
	std::vector<T> buf;
	std::shared_ptr<DataStream<T>> dataStream;
};

template <typename T>
class Gain : public Transformer<T>
{
public:
	Gain(std::shared_ptr<DataStream<T>> dataStream, T gain)
		: Transformer<T>(dataStream), gain(gain)
	{ }

	void setGain(const T& gain) { this->gain = gain; }
	T getGain(const T& gain) const { return gain; }

protected:
	T transform(const T& x) override { return gain * x; }

private:
	T gain;
};

template <typename T>
class Adder : public Transformer<T>
{
public:
	Adder(std::shared_ptr<DataStream<T>> dataStream, T offset)
		: Transformer<T>(dataStream), offset(offset)
	{ }

	void setOffset(const T& offset) { this->offset = offset; }
	T getOffset(const T& offset) const { return offset; }

protected:
	T transform(const T& x) override { return offset + x; }

private:
	T offset;
};

template <typename T, typename U>
class Combiner : public DataStream<T>
{
public:
	Combiner(std::initializer_list<std::shared_ptr<DataStream<T>>> dataStreams,
			U combiner = U())
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
		buf.insert(buf.end(), data0.begin(), data0.end());

		for(auto it = dataStreams.begin() + 1; it < dataStreams.end(); it++)
		{
			auto& data = (*it)->getData();
			if (data0.size() != data.size())
			{
				std::cerr << "Size mismatch!\n";
				return buf;
			}

			std::transform(data.begin(), data.end(), buf.begin(),
					buf.begin(), combiner);
		}

		return buf;
	}

	size_t numStreams() const { return dataStreams.size(); }

private:
	std::vector<T> buf;
	std::vector<std::shared_ptr<DataStream<T>>> dataStreams;
	U combiner;
};

template <typename T>
struct Mixer : public Combiner<T, std::plus<T>>
{
	using Combiner<T, std::plus<T>>::Combiner;
};

template <typename T>
struct Modulator : public Combiner<T, std::multiplies<T>>
{
	using Combiner<T, std::multiplies<T>>::Combiner;
};

template <typename T>
class AlsaMonoSink
{
public:
	AlsaMonoSink(std::shared_ptr<DataStream<T>> dataStream)
		: dataStream(dataStream),
		  alsa(1, 48000, 500000)
	{ }

	void run()
	{
		alsa.write(dataStream->getData());
	}

private:
	std::shared_ptr<DataStream<T>> dataStream;
	Alsa<T> alsa;
};

template <typename T>
class AlsaStereoSink
{
public:
	AlsaStereoSink(std::shared_ptr<DataStream<T>> dataStreamLeft,
			std::shared_ptr<DataStream<T>> dataStreamRight)
		: dataStreamLeft(dataStreamLeft), dataStreamRight(dataStreamRight),
		  alsa(2, 48000, 500000)
	{ }

	void run()
	{
		auto& dataLeft = dataStreamLeft->getData();
		auto& dataRight = dataStreamRight->getData();

		if (dataLeft.size() != dataRight.size())
		{
			std::cerr << "Size mismatch!\n";
			return;
		}

		buf.clear();
		for (size_t i = 0; i < dataLeft.size(); i++)
		{
			buf.push_back(dataLeft[i]);
			buf.push_back(dataRight[i]);
		}

		alsa.write(buf);
	}

private:
	std::vector<T> buf;
	std::shared_ptr<DataStream<T>> dataStreamLeft;
	std::shared_ptr<DataStream<T>> dataStreamRight;
	Alsa<T> alsa;
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
	auto vibrato = std::make_shared<SineSource<signalType>>(vibrFreq / 48000.0, vibrAmplitude);
	auto vibratoScaler = std::make_shared<Adder<signalType>>(vibrato, 1.0);

	return std::make_shared<Modulator<signalType>>(
			std::initializer_list<std::shared_ptr<DataStream<signalType>>>(
					{tone, vibratoScaler}));
}

int main()
{
#if 1
	auto tone1 = shittyTone(125.0 / 2, 0.2, .1, 0.2);
	auto tone2 = shittyTone(125.0 / 4, 0.3, .3, 0.2);
	auto tone3 = shittyTone(125, 0.5, .5, 0.2);
	auto tone4 = shittyTone(250, 1.0, .7, 0.2);
	auto tone5 = shittyTone(500, 0.5, .6, 0.2);
	auto tone6 = shittyTone(1000.0, 0.4, .5, 0.2);
	auto tone7 = shittyTone(2000.0, 0.1, .3, 0.2);

	auto tone9 = shittyTone(2500 * 3, .05, 5, 0.2);
	auto tone10 = shittyTone(250 / 3, .15, 5, 0.2);

	auto hisssss =  std::make_shared<NoiseSource<signalType>>(1.0);

	auto combinedTones = std::make_shared<Mixer<signalType>>(
			std::initializer_list<std::shared_ptr<DataStream<signalType>>>(
					{tone1, tone2, tone3, tone4, tone5, tone6, tone7,
					 tone9, tone10}));

	auto masterChannel = std::make_shared<Gain<signalType>>(combinedTones, 1.0 / combinedTones->numStreams());

	//AlsaMonoSink<signalType> sound(masterChannel);
	AlsaStereoSink<signalType> sound(masterChannel, hisssss);

	while (true)
	{
		sound.run();
	}

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
