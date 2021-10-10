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
	DumbSource(std::initializer_list<T> values)
		: buffer(values)
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
class Chopper : public DataStream<T>
{
public:
	Chopper(std::shared_ptr<DataStream<T>> dataStream, double onTime, double offTime)
		: dataStream(dataStream), t(0), onTime(onTime), period(onTime + offTime)
	{ }

	std::vector<T>& getData() override
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

template <typename T>
class FirFilter : public DataStream<T>
{
public:
	FirFilter(std::shared_ptr<DataStream<T>> dataStream,
			std::shared_ptr<std::vector<T>> coefficients)
		: dataStream(dataStream), coefficients(coefficients), first(true)
	{ }

	std::vector<T>& getData() override
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

	auto coeffs = std::make_shared<std::vector<signalType>>(
			std::initializer_list<signalType>({
		//0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
		    0.000000000000000000,
		    0.000000205220313655,
		    0.000000800565252165,
		    0.000001738083935335,
		    0.000002946668469349,
		    0.000004332551256612,
		    0.000005780199056154,
		    0.000007153625780856,
		    0.000008298145995883,
		    0.000009042589869549,
		    0.000009201997711926,
		    0.000008580808045391,
		    0.000006976547260869,
		    0.000004184021254115,
		    0.000000000000000000,
		    -0.000005771625133890,
		    -0.000013314244284874,
		    -0.000022792539308313,
		    -0.000034346163705638,
		    -0.000048083170778645,
		    -0.000064073285067461,
		    -0.000082341128675520,
		    -0.000102859530154631,
		    -0.000125543058709001,
		    -0.000150241940052304,
		    -0.000176736521783521,
		    -0.000204732465110968,
		    -0.000233856845647756,
		    -0.000263655348361988,
		    -0.000293590740179479,
		    -0.000323042797860379,
		    -0.000351309858337795,
		    -0.000377612143541712,
		    -0.000401096991761649,
		    -0.000420846102862702,
		    -0.000435884875313021,
		    -0.000445193879276383,
		    -0.000447722472360536,
		    -0.000442404523496970,
		    -0.000428176166479462,
		    -0.000403995458630660,
		    -0.000368863772715206,
		    -0.000321848702472727,
		    -0.000262108214967427,
		    -0.000188915737353484,
		    -0.000101685822674966,
		    0.000000000000000000,
		    0.000116367620440045,
		    0.000247425445384273,
		    0.000392940680566994,
		    0.000552417292138758,
		    0.000725076149684481,
		    0.000909837850931089,
		    0.001105308716856385,
		    0.001309770424950760,
		    0.001521173717709981,
		    0.001737136583101248,
		    0.001954947253987342,
		    0.002171572314750029,
		    0.002383670136252239,
		    0.002587609785635392,
		    0.002779495476259425,
		    0.002955196536521158,
		    0.003110382785644688,
		    0.003240565111268042,
		    0.003341140949303757,
		    0.003407444272757235,
		    0.003434799604628945,
		    0.003418579482408673,
		    0.003354264719686795,
		    0.003237506735710646,
		    0.003064191157880448,
		    0.002830501846679000,
		    0.002532984448694338,
		    0.002168608552391007,
		    0.001734827504085592,
		    0.001229634938942527,
		    0.000651617094246841,
		    -0.000000000000000001,
		    -0.000725309314971827,
		    -0.001523688404975113,
		    -0.002393772508264427,
		    -0.003333435810973696,
		    -0.004339781419200553,
		    -0.005409140473684443,
		    -0.006537080721082672,
		    -0.007718424727313622,
		    -0.008947277783720996,
		    -0.010217065417981456,
		    -0.011520580280906533,
		    -0.012850038039829838,
		    -0.014197141771409535,
		    -0.015553154213697896,
		    -0.016908977111474092,
		    -0.018255236772257873,
		    -0.019582374845152281,
		    -0.020880743242572337,
		    -0.022140702047678169,
		    -0.023352719189390539,
		    -0.024507470623414705,
		    -0.025595939732644749,
		    -0.026609514654277543,
		    -0.027540082254233701,
		    -0.028380117502042779,
		    -0.029122767050860483,
		    -0.029761925897087635,
		    -0.030292306081184828,
		    -0.030709496494461141,
		    -0.031010012974323684,
		    -0.031191338000921467,
		    0.968748050550699014,
		    -0.031191338000921467,
		    -0.031010012974323684,
		    -0.030709496494461141,
		    -0.030292306081184828,
		    -0.029761925897087635,
		    -0.029122767050860480,
		    -0.028380117502042779,
		    -0.027540082254233701,
		    -0.026609514654277543,
		    -0.025595939732644749,
		    -0.024507470623414705,
		    -0.023352719189390539,
		    -0.022140702047678169,
		    -0.020880743242572341,
		    -0.019582374845152285,
		    -0.018255236772257873,
		    -0.016908977111474096,
		    -0.015553154213697901,
		    -0.014197141771409535,
		    -0.012850038039829842,
		    -0.011520580280906531,
		    -0.010217065417981458,
		    -0.008947277783721000,
		    -0.007718424727313622,
		    -0.006537080721082674,
		    -0.005409140473684441,
		    -0.004339781419200552,
		    -0.003333435810973697,
		    -0.002393772508264428,
		    -0.001523688404975113,
		    -0.000725309314971827,
		    -0.000000000000000001,
		    0.000651617094246841,
		    0.001229634938942527,
		    0.001734827504085592,
		    0.002168608552391009,
		    0.002532984448694338,
		    0.002830501846679000,
		    0.003064191157880449,
		    0.003237506735710646,
		    0.003354264719686798,
		    0.003418579482408676,
		    0.003434799604628947,
		    0.003407444272757236,
		    0.003341140949303757,
		    0.003240565111268045,
		    0.003110382785644690,
		    0.002955196536521160,
		    0.002779495476259425,
		    0.002587609785635392,
		    0.002383670136252239,
		    0.002171572314750031,
		    0.001954947253987344,
		    0.001737136583101249,
		    0.001521173717709984,
		    0.001309770424950761,
		    0.001105308716856385,
		    0.000909837850931088,
		    0.000725076149684482,
		    0.000552417292138759,
		    0.000392940680566994,
		    0.000247425445384273,
		    0.000116367620440046,
		    0.000000000000000000,
		    -0.000101685822674966,
		    -0.000188915737353484,
		    -0.000262108214967427,
		    -0.000321848702472727,
		    -0.000368863772715206,
		    -0.000403995458630660,
		    -0.000428176166479462,
		    -0.000442404523496970,
		    -0.000447722472360536,
		    -0.000445193879276384,
		    -0.000435884875313022,
		    -0.000420846102862703,
		    -0.000401096991761649,
		    -0.000377612143541712,
		    -0.000351309858337795,
		    -0.000323042797860378,
		    -0.000293590740179479,
		    -0.000263655348361988,
		    -0.000233856845647756,
		    -0.000204732465110968,
		    -0.000176736521783522,
		    -0.000150241940052304,
		    -0.000125543058709001,
		    -0.000102859530154631,
		    -0.000082341128675520,
		    -0.000064073285067461,
		    -0.000048083170778645,
		    -0.000034346163705638,
		    -0.000022792539308313,
		    -0.000013314244284874,
		    -0.000005771625133890,
		    0.000000000000000000,
		    0.000004184021254115,
		    0.000006976547260869,
		    0.000008580808045392,
		    0.000009201997711926,
		    0.000009042589869549,
		    0.000008298145995883,
		    0.000007153625780856,
		    0.000005780199056154,
		    0.000004332551256612,
		    0.000002946668469349,
		    0.000001738083935335,
		    0.000000800565252165,
		    0.000000205220313655,
		    0.000000000000000000,
	}));

	//auto clap = std::make_shared<Chopper<signalType>>(hisssss, 48000.0 / 100.0, 48000.0 / 100.0);

	auto fir = std::make_shared<FirFilter<signalType>>(masterChannel, coeffs);

	AlsaMonoSink<signalType> sound(fir);
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
