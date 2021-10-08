//============================================================================
// Name        : BrownNote.cpp
// Author      : Tom Wimmenhove
// Version     :
// Copyright   : GPL 2
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <iostream>
#include <vector>
#include <memory>
#include <queue>

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
	DumbSource(T start = 1)
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
class Mixer: public DataStream<T>
{
public:
	Mixer(std::initializer_list<std::shared_ptr<DataStream<T>>> dataStreams)
		: dataStreams(dataStreams)
	{ }

	size_t PushToVector(std::vector<T>& v) override
	{
		if (dataStreams.size() == 0)
		{
			v.resize(v.size() + 1024);
		}

		auto start = v.end();
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
				start[i] += tmpBuf[i];
			}
		}

		return len;
	}

private:
	std::vector<T> tmpBuf;
	std::vector<std::shared_ptr<DataStream<T>>> dataStreams;
};

//template <typename T>
//class DumbSink
//{
//public:
//	DumbSink()
//	{
//
//	}
//};

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

int main() {
	//auto source1 = std::shared_ptr<DataStream<signalType>>(new DumbSource<signalType>());
	auto source1 = std::make_shared<DumbSource<signalType>>();
	auto source2 = std::make_shared<DumbSource<signalType>>();
	auto source3 = std::make_shared<DumbSource<signalType>>(10);

	auto buf1 = std::make_shared<DataBuffer<signalType>>(source1, 5);
	auto buf2 = std::make_shared<DataBuffer<signalType>>(source2, 5);
	auto buf3 = std::make_shared<DataBuffer<signalType>>(source3, 5);

	auto mix = std::make_shared<Mixer<signalType>>(
			std::initializer_list<std::shared_ptr<DataStream<signalType>>>({buf1, buf2, buf3}));

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
