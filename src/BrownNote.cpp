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
#include <list>
#include <string>
#include <iterator>
#include <fstream>
#include <map>

#include "alsa.h"

typedef float signalType;

template <typename T>
class DataStream
{
public:
	virtual std::vector<T>& getData(int channel) = 0;

	virtual ~DataStream() { }
};

template <typename T>
struct DataChannel
{
	std::shared_ptr<DataStream<T>> stream;
	int channel;
};

template <typename T>
class SharedPool
{
public:
	T get()
	{
		if (pool.empty())
		{
			std::cout << "Allocating new element for pool\n";
			return T();
		}

		auto res = std::move(pool.front());
		pool.pop_front();

		return res;
	}

	void giveBack(T&& x)
	{
		pool.push_back(std::move(x));
	}

private:
	std::deque<T> pool;
};

template <typename T>
class DumbSource: public DataStream<T>
{
public:
	DumbSource(std::initializer_list<T> values)
		: buffer(values)
	{ }

	std::vector<T>& getData(int channel) override
	{
		return buffer;
	}

private:
	std::vector<T> buffer;
};

template <typename T>
class FileReaderSoure : public DataStream<T>
{
public:
	FileReaderSoure(std::string filename, size_t n = 1024)
		: n(n), buf(n)
	{
		file = std::ifstream(filename, std::ios::binary);
	}

	std::vector<T>& getData(int channel) override
	{
		buf.resize(n);
		size_t byteSize = n * sizeof(T);

		file.read((char *) buf.data(), byteSize);

		size_t cnt = file.gcount();

		if (cnt < byteSize)
		{
			buf.resize(cnt / sizeof(T));
		}

		return buf;
	}

private:
	size_t n;
	std::vector<T> buf;
	std::ifstream file;
};

template <typename T, typename U>
class DataStreamConverter: public DataStream<T>
{
public:
	DataStreamConverter(std::shared_ptr<DataStream<U>> dataStream, std::function<T(U)> converter)
		: dataStream(dataStream), converter(converter)
	{ }

	std::vector<T>& getData(int channel) override
	{
		buf.clear();
		auto& data = dataStream->getData(channel);

		for(auto& x: data)
		{
			buf.push_back(converter(x));
		}

		return buf;
	}

private:
	std::vector<T> buf;
	std::shared_ptr<DataStream<U>> dataStream;
	std::function<T(U)> converter;
};

template <typename T>
class DcSource: public DataStream<T>
{
public:
	DcSource(T dcValue, size_t n = 1024)
		: dcValue(dcValue), buffer(n, dcValue)
	{ }

	std::vector<T>& getData(int channel) override { return buffer; }

private:
	T dcValue;
	std::vector<T> buffer;
};

template <typename T>
class InterleavedVectorSource: public DataStream<T>
{
public:
	InterleavedVectorSource(typename std::vector<T>::iterator it,
			size_t space, size_t n = 1024)
	: it(it), buf(n), space(space), n(n)
	{ }

	std::vector<T>& getData(int channel) override
	{
		for(size_t i = 0; i < buf.size(); i++)
		{
			buf[i] = *it;
			it += space;
		}

		return buf;
	}

private:
	typename std::vector<T>::iterator it;
	std::vector<T> buf;
	size_t space;
	size_t n;
};

template <typename T>
class SineSource: public DataStream<T>
{
public:
	SineSource(double rate, T amplitude = 1.0, size_t n = 1024)
		: inc(rate * M_PI * 2), amplitude(amplitude),  buf(n), x(0)
	{ }

	std::vector<T>& getData(int channel) override
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
		: amplitude(amplitude), buf(n)
	{
		std::random_device rd;
	    rnd = std::default_random_engine(rd());
	    distr = std::uniform_real_distribution<T>(-amplitude, amplitude);
	}

	std::vector<T>& getData(int channel) override
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
class IncrementSource: public DataStream<T>
{
public:
	IncrementSource(T start = 0, size_t n = 1024)
		: c(start), buf(n)
	{ }

	std::vector<T>& getData(int channel) override
	{
		for(size_t i = 0; i < buf.size(); i++)
		{
			buf[i] = c++;
		}

		return buf;
	}

private:
	T c;
	std::vector<T> buf;
};

template <typename T>
class DataDuplicator : public DataStream<T>
{
public:
	DataDuplicator(std::shared_ptr<DataStream<T>> dataStream, int channels)
		: dataStream(dataStream), channels(channels), curChannel(0)
	{ }

	std::vector<T>& getData(int channel) override
	{
		if (channel == 0)
		{
			buf.clear();
			auto& data = dataStream->getData(channel);
			buf.insert(buf.end(), data.begin(), data.end());
		}

		if (++channel >= channels)
		{
			channel = 0;
		}
	}

private:
	std::vector<T> buf;
	std::shared_ptr<DataStream<T>> dataStream;
	int channels;
	int curChannel;
};

template <typename T>
class Deinterleaver : public DataStream<T>
{
public:
	Deinterleaver(DataChannel<T> dataChannel, size_t start, size_t inc)
		: dataChannel(dataChannel), start(start), inc(inc)
	{ }

	std::vector<T>& getData(int channel) override
	{
		auto& data = dataChannel.stream->getData(dataChannel.channel);

		buf.clear();
		for (size_t i = start; i < data.size(); i += inc)
		{
			buf.push_back(data[i]);
		}

		return buf;
	}

private:
	std::vector<T> buf;
	DataChannel<T> dataChannel;
	size_t start;
	size_t inc;
};

template <typename T>
class Splitter : public DataStream<T>
{
public:
	Splitter(DataChannel<T> dataChannel, int channels)
		: dataChannel(dataChannel),
		  channelPositions(channels), channels(channels)
	{ }

	std::vector<T>& getData(int channel) override
	{
		size_t minPos = *std::min_element(channelPositions.begin(), channelPositions.end());
		if (minPos > 0)
		{
			for (int i = 0; i < channels; i++)
			{
				channelPositions[i]--;
			}

			pool.giveBack(std::move(bufs.front()));
			bufs.pop_front();
		}

		size_t channelPos = channelPositions[channel]++;

		if (channelPos >= bufs.size())
		{
			auto& data = dataChannel.stream->getData(dataChannel.channel);

			bufs.push_back(std::move(getNewVector(data)));
		}

		return bufs[channelPos];
	}

private:
	std::vector<T> getNewVector(std::vector<T>& original)
	{
		auto newVector = pool.get();
		newVector.clear();
		newVector.insert(newVector.end(), original.begin(), original.end());

		return newVector;
	}

	std::deque<std::vector<T>> bufs;
	DataChannel<T> dataChannel;
	std::vector<size_t> channelPositions;
	int channels;
	SharedPool<std::vector<T>> pool;
};

template <typename T>
class Chopper : public DataStream<T>
{
public:
	Chopper(DataChannel<T> dataChannel,
			double onTime, double offTime)
		: dataChannel(dataChannel), t(0), onTime(onTime), period(onTime + offTime)
	{ }

	std::vector<T>& getData(int channel) override
	{
		auto& data = dataChannel.stream->getData(dataChannel.channel);

		buf.clear();
		for(auto& x: data)
		{
			buf.push_back(t <= onTime ? x : 0);

			t++;

			if (t > period)
			{
				t -= period;
			}
		}

		return buf;
	}

private:
	std::vector<T> buf;
	DataChannel<T> dataChannel;
	double t;
	double onTime;
	double period;
};

template <typename T>
class Transformer : public DataStream<T>
{
public:
	Transformer(DataChannel<T> dataChannel)
		: dataChannel(dataChannel)
	{ }

	std::vector<T>& getData(int channel) override
	{
		auto& data = dataChannel.stream->getData(dataChannel.channel);

		buf.resize(data.size());

		std::transform(data.begin(), data.end(), buf.begin(),
				std::bind(&Transformer::transform, this, std::placeholders::_1));

		return buf;
	}

protected:
	virtual T transform(const T& x) = 0;

private:
	std::vector<T> buf;
	DataChannel<T> dataChannel;
};

template <typename T>
class Gain : public Transformer<T>
{
public:
	Gain(DataChannel<T> dataChannel, T gain)
		: Transformer<T>(dataChannel), gain(gain)
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
	Adder(DataChannel<T> dataChannel, T offset)
		: Transformer<T>(dataChannel), offset(offset)
	{ }

	void setOffset(const T& offset) { this->offset = offset; }
	T getOffset(const T& offset) const { return offset; }

protected:
	T transform(const T& x) override { return offset + x; }

private:
	T offset;
};

template <typename T>
class FirFilter : public DataStream<T>
{
public:
	FirFilter(DataChannel<T> dataChannel,
			std::shared_ptr<std::vector<T>> coefficients)
		: dataChannel(dataChannel),
		  coefficients(coefficients), first(true)
	{ }

	std::vector<T>& getData(int channel) override
	{
		auto& data = dataChannel.stream->getData(dataChannel.channel);

		buf.clear();
		if (first)
		{
			buf.resize(coefficients->size() / 2);
			first = false;
		}

		for(auto& x: data)
		{
			taps.push_front(x);
			if (taps.size() == coefficients->size())
			{
				T sum = 0;
				typename std::list<T>::iterator t;
				typename std::vector<T>::iterator c;
				for(c  = coefficients->begin(), t  = taps.begin();
					c != coefficients->end(),   t != taps.end();
						++c, ++t)
				{
					sum += (*c) * (*t);
				}

				taps.pop_back();
				buf.push_back(sum);
			}
		}

		return buf;
	}

private:
	DataChannel<T> dataChannel;
	std::shared_ptr<std::vector<T>> coefficients;
	std::vector<T> buf;
	std::list<T> taps;
	bool first;
};

template <typename T, typename U>
class Combiner : public DataStream<T>
{
public:
	Combiner(std::initializer_list<DataChannel<T>> dataChannels,
			U combiner = U())
		: dataChannels(dataChannels), combiner(combiner)
	{ }

	std::vector<T>& getData(int channel) override
	{
		buf.clear();

		if (dataChannels.size() == 0)
		{
			buf.resize(1024);

			return buf;
		}

		auto& data0 = dataChannels[0].stream->getData(dataChannels[0].channel);
		buf.insert(buf.end(), data0.begin(), data0.end());

		for(auto it = dataChannels.begin() + 1; it < dataChannels.end(); it++)
		{
			auto& data = it->stream->getData(it->channel);
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

	size_t numStreams() const { return dataChannels.size(); }

private:
	std::vector<T> buf;
	std::vector<DataChannel<T>> dataChannels;
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
	AlsaMonoSink(DataChannel<T> dataChannel)
		: dataChannel(dataChannel),
		  alsa(1, 48000, 500000)
	{ }

	void run()
	{
		alsa.write(dataChannel.stream->getData(dataChannel.channel));
	}

private:
	DataChannel<T> dataChannel;
	Alsa<T> alsa;
};

template <typename T>
class AlsaStereoSink
{
public:
	AlsaStereoSink(DataChannel<T> dataChannelLeft, DataChannel<T> dataChannelRight)
		: dataChannelLeft(dataChannelLeft), dataChannelRight(dataChannelRight),
		  alsa(2, 48000, 500000)
	{ }

	void run()
	{
		auto& dataLeft = dataChannelLeft.stream->getData(dataChannelLeft.channel);
		auto& dataRight = dataChannelRight.stream->getData(dataChannelRight.channel);

		if (dataLeft.size() != dataRight.size())
		{
			std::cerr << "Size mismatch! (" << dataLeft.size() << " vs " << dataRight.size() << ")\n";
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
	DataChannel<T> dataChannelLeft;
	DataChannel<T> dataChannelRight;
	Alsa<T> alsa;
};

template <typename T>
class DelayLine : public DataStream<T>
{
public:
	DelayLine(DataChannel<T> dataChannel, size_t delay)
	: dataChannel(dataChannel), buf(delay), first(true)
	{ }

	std::vector<T>& getData(int channel) override
	{
		if (first)
		{
			first = false;

			return buf;
		}

		/* Deallocate the silent buffer */
		buf = std::vector<T>();

		return dataChannel.stream->getData(dataChannel.channel);
	}

private:
	DataChannel<T> dataChannel;
	std::vector<T> buf;
	bool first;
};

template <typename T>
class DataBuffer : public DataStream<T>
{
public:
	DataBuffer(DataChannel<T> dataChannel, size_t len)
	: dataChannel(dataChannel), buf(len), len(len)
	{ }

	std::vector<T>& getData(int channel) override
	{
		// XXX: Replace this with a circular buffer
		while (tmpBuf.size() < buf.size())
		{
			auto& data = dataChannel.stream->getData(dataChannel.channel);
			tmpBuf.insert(tmpBuf.end(), data.begin(), data.end());
		}

		buf.clear();
		buf.insert(buf.begin(), tmpBuf.begin(), tmpBuf.begin() + len);

		tmpBuf.erase(tmpBuf.begin(), tmpBuf.begin() + len);

		return buf;
	}

	inline size_t size() const { return buf.size(); }

private:
	DataChannel<T> dataChannel;
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
	auto vibratoScaler = std::make_shared<Adder<signalType>>(DataChannel<signalType>{vibrato, 0}, 1.0);

	return std::make_shared<Modulator<signalType>>(
			std::initializer_list<DataChannel<signalType>>(
					{{tone, 0}, {vibratoScaler, 0}}));
}

template <typename T>
std::vector<T> readFileIntoVector(std::string filename)
{
    std::ifstream file(filename, std::ios::binary);

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<int16_t> vec;
	vec.resize(fileSize / sizeof(int16_t));

	file.read((char *) vec.data(), fileSize);

	std::vector<T> res;
	res.reserve(vec.size());

	for(auto& x: vec)
	{
		res.push_back(x / 32768.0);
	}

	return res;
}

int main()
{
	std::string filename = "/home/tom/git/BrownNote/file.raw";

	auto fileReader = std::make_shared<FileReaderSoure<int16_t>>(filename, 2048);

	auto converter = std::make_shared<DataStreamConverter<signalType, int16_t>>(fileReader,
			[] (int16_t x) { return (float) x / 32768.0f; });

	auto fileSplitter = std::make_shared<Splitter<signalType>>(DataChannel<signalType>{converter, 0}, 2);

	auto left = std::make_shared<Deinterleaver<signalType>>(DataChannel<signalType>{fileSplitter, 0}, 0, 2);
	auto right = std::make_shared<Deinterleaver<signalType>>(DataChannel<signalType>{fileSplitter, 1}, 1, 2);

	AlsaStereoSink<signalType> s({left, 0}, {right, 1});
	//AlsaMonoSink<signalType> s({right, 0});

	while (true)
	{
		s.run();
	}

	return 0;
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
			std::initializer_list<DataChannel<signalType>>(
					{ { tone1, 0 }, { tone2, 0 },
					  { tone3, 0 }, { tone4, 0 },
					  { tone5, 0 }, { tone6, 0 },
					  { tone7, 0 }, { tone9, 0 },
					  { tone10, 0 }, }));

	auto masterChannel = std::make_shared<Gain<signalType>>(DataChannel<signalType>{combinedTones, 0},
			1.0 / combinedTones->numStreams());

	auto coeffs = std::make_shared<std::vector<signalType>>(
			std::initializer_list<signalType>({
		//0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
		    -0.002358258944270119,
		    -0.007526945344873014,
		    -0.015573611628181589,
		    -0.026138064594218191,
		    -0.038466251452612468,
		    -0.024411419250561235,
		    -0.005436182231086863,
		    0.015184898862007697,
		    0.033601900459646179,
		    0.046130905137397156,
		    0.049986057973503535,
		    0.046130905137396407,
		    0.033601900459646381,
		    0.015184898862008177,
		    -0.005436182231086864,
		    -0.024411419250561336,
		    -0.038466251452612427,
		    -0.026138064594218201,
		    -0.015573611628181553,
		    -0.007526945344872948,
		    -0.002358258944270082,
	}));

	//auto clap = std::make_shared<Chopper<signalType>>(hisssss, 48000.0 / 100.0, 48000.0 / 100.0);

	//auto fir = std::make_shared<FirFilter<signalType>>(hisssss, coeffs);

	auto fileVector = readFileIntoVector<signalType>(filename);

	auto stdinSourceLeft  = std::make_shared<InterleavedVectorSource<signalType>>(fileVector.begin() + 0, 2);
	auto stdinSourceRight = std::make_shared<InterleavedVectorSource<signalType>>(fileVector.begin() + 1, 2);

	auto splitter = std::make_shared<Splitter<signalType>>(DataChannel<signalType>{stdinSourceLeft, 0}, 2);

	//auto fir = std::make_shared<FirFilter<signalType>>(stdinSourceLeft, 0, coeffs);
	//auto buffered = std::make_shared<DataBuffer<signalType>>(fir, 1024);

	//AlsaStereoSink<signalType> sound(buffered, 0, stdinSourceRight, 0);

	//auto fir = std::make_shared<FirFilter<signalType>>(splitter, 1, coeffs);
	//auto buffered = std::make_shared<DataBuffer<signalType>>(fir, 1024);

	auto delay = std::make_shared<DelayLine<signalType>>(DataChannel<signalType>{splitter, 0}, 48000 / 8);
	auto buffered = std::make_shared<DataBuffer<signalType>>(DataChannel<signalType>{delay, 0}, 1024);

	auto echo = std::make_shared<Mixer<signalType>>(
			std::initializer_list<DataChannel<signalType>>(
					{{buffered, 0}, {splitter, 1}}));

	AlsaStereoSink<signalType> sound({echo, 0}, {stdinSourceRight, 0});

	//AlsaMonoSink<signalType> sound(fir);
	//AlsaStereoSink<signalType> sound(masterChannel, fir);

	while (true)
	{
		sound.run();
	}

	return 0;
#endif

	auto source1 = std::make_shared<IncrementSource<signalType>>(0, 4);

	auto split= std::make_shared<Splitter<signalType>>(DataChannel<signalType>{source1, 0}, 2);

	std::cout << "0: \n";
	auto d = split->getData(0);
	for (auto& x: d)
	{
		std::cout << x << '\n';
	}

	std::cout << "0: \n";
	d = split->getData(0);
	for (auto& x: d)
	{
		std::cout << x << '\n';
	}

	std::cout << '\n';

	std::cout << "1: \n";
	d = split->getData(1);
	for (auto& x: d)
	{
		std::cout << x << '\n';
	}
	std::cout << "1: \n";
	d = split->getData(1);
	for (auto& x: d)
	{
		std::cout << x << '\n';
	}
	std::cout << "1: \n";
	d = split->getData(1);
	for (auto& x: d)
	{
		std::cout << x << '\n';
	}


	std::cout << "0: \n";
	d = split->getData(0);
	for (auto& x: d)
	{
		std::cout << x << '\n';
	}

	std::cout << "0: \n";
	d = split->getData(0);
	for (auto& x: d)
	{
		std::cout << x << '\n';
	}

	std::cout << "1: \n";
	d = split->getData(1);
	for (auto& x: d)
	{
		std::cout << x << '\n';
	}

	std::cout << "1: \n";
	d = split->getData(1);
	for (auto& x: d)
	{
		std::cout << x << '\n';
	}


	std::cout << "Done\n";

	return 0;
}
