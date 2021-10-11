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

#include "alsa.h"

typedef float signalType;

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

template <typename T>
class DataStream
{
public:
	virtual std::vector<T>& getData(bool reset = true) = 0;

	virtual ~DataStream() { }
};

template <typename T>
class DumbSource: public DataStream<T>
{
public:
	DumbSource(std::initializer_list<T> values)
		: buffer(values)
	{ }

	std::vector<T>& getData(bool reset = true) override
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

	std::vector<T>& getData(bool reset = true) override { return buffer; }

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

	std::vector<T>& getData(bool reset = true) override
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
class FileInt16Source: public DataStream<T>
{
public:
	FileInt16Source(std::string fileName, size_t n = 1024)
		: ioBuf(n * 2)
	{
		file = std::ifstream(fileName);
	}

	std::vector<T>& getData(bool reset = true) override
	{
		size_t byteSize = ioBuf.size() * sizeof(int16_t);

		file.read((char*) ioBuf.data(), byteSize);

		if (reset)
		{
			bufLeft.clear();
			bufRight.clear();
		}

		for (size_t i = 0; i < ioBuf.size(); i++)
		{
			T x = ioBuf[i] / 32768.0;
			if ((i & 1) == 0)
			{
				bufLeft.push_back(x);
			}
			else
			{
				bufRight.push_back(x);
			}
		}

		return reset ? bufLeft : bufRight;
	}

private:
	std::vector<int16_t> ioBuf;
	std::vector<T> bufLeft;
	std::vector<T> bufRight;
	std::ifstream file;
};

template <typename T>
class SineSource: public DataStream<T>
{
public:
	SineSource(double rate, T amplitude = 1.0, size_t n = 1024)
		: inc(rate * M_PI * 2), amplitude(amplitude),  buf(n), x(0)
	{ }

	std::vector<T>& getData(bool reset = true) override
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

	std::vector<T>& getData(bool reset = true) override
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

	std::vector<T>& getData(bool reset = true) override
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

	}

private:
	std::vector<T> buf;
	std::shared_ptr<DataStream<T>> dataStream;
	size_t channels;
	size_t channel;
};

template <typename T>
class Chopper : public DataStream<T>
{
public:
	Chopper(std::shared_ptr<DataStream<T>> dataStream, double onTime, double offTime)
		: dataStream(dataStream), t(0), onTime(onTime), period(onTime + offTime)
	{ }

	std::vector<T>& getData(bool reset = true) override
	{
		auto& data = dataStream->getData();

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
	std::shared_ptr<DataStream<T>> dataStream;
	double t;
	double onTime;
	double period;
};

template <typename T>
class Transformer : public DataStream<T>
{
public:
	Transformer(std::shared_ptr<DataStream<T>> dataStream)
		: dataStream(dataStream)
	{ }

	std::vector<T>& getData(bool reset = true) override
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

template <typename T>
class FirFilter : public DataStream<T>
{
public:
	FirFilter(std::shared_ptr<DataStream<T>> dataStream,
			std::shared_ptr<std::vector<T>> coefficients)
		: dataStream(dataStream), coefficients(coefficients), first(true)
	{ }

	std::vector<T>& getData(bool reset = true) override
	{
		auto& data = dataStream->getData();

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
	std::shared_ptr<DataStream<T>> dataStream;
	std::shared_ptr<std::vector<T>> coefficients;
	std::vector<T> buf;
	std::list<T> taps;
	bool first;
};

template <typename T, typename U>
class Combiner : public DataStream<T>
{
public:
	Combiner(std::initializer_list<std::shared_ptr<DataStream<T>>> dataStreams,
			U combiner = U())
		: dataStreams(dataStreams), combiner(combiner)
	{ }

	std::vector<T>& getData(bool reset = true) override
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

	std::vector<T>& getData(bool reset = true) override
	{
		// XXX: Replace this with a circular buffer
		while (tmpBuf.size() < buf.size())
		{
			auto& data = dataStream->getData(reset);
			tmpBuf.insert(tmpBuf.end(), data.begin(), data.end());

			reset = false;
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

	//auto stdinSource = std::make_shared<StdinInt16Source<signalType>>();
	auto fileSource = std::make_shared<FileInt16Source<signalType>>("/home/tom/git/BrownNote/file.raw");

//    std::ifstream file("/home/tom/git/BrownNote/file.raw", std::ios::binary);
//
//    file.seekg(0, std::ios::end);
//    size_t fileSize = file.tellg();
//    file.seekg(0, std::ios::beg);
//
//    std::vector<int16_t> vec;
//	vec.resize(fileSize / sizeof(int16_t));
//
//	file.read((char *) vec.data(), fileSize);

	auto fileVector = readFileIntoVector<signalType>("/home/tom/git/BrownNote/file.raw");


	auto stdinSourceLeft  = std::make_shared<InterleavedVectorSource<signalType>>(fileVector.begin() + 0, 2);
	auto stdinSourceRight = std::make_shared<InterleavedVectorSource<signalType>>(fileVector.begin() + 1, 2);

	auto fir = std::make_shared<FirFilter<signalType>>(stdinSourceLeft, coeffs);
	auto buffered = std::make_shared<DataBuffer<signalType>>(fir, 1024);

	AlsaStereoSink<signalType> sound(buffered, stdinSourceRight);
	//AlsaMonoSink<signalType> sound(fir);
	//AlsaStereoSink<signalType> sound(masterChannel, fir);

	while (true)
	{
		sound.run();
	}

	return 0;
#endif

	auto source1 = std::make_shared<DumbSource<signalType>>(
			std::initializer_list<signalType>({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10})
	);

//	auto coeffs = std::make_shared<std::vector<signalType>>(
//			std::initializer_list<signalType>({0, 0, 1, 0, 0}));

	//auto fir = std::make_shared<FirFilter<signalType>>(source1, coeffs);

	auto& d1 = fir->getData();
	for (auto& x: d1)
	{
		std::cout << x << '\n';
	}

	std::cout << '\n';

	auto& d2 = fir->getData();
	for (auto& x: d2)
	{
		std::cout << x << '\n';
	}

	std::cout << "Done\n";

	return 0;
}
