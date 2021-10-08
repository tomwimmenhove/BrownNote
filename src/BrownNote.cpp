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
class PullDataStream
{
public:
	virtual size_t PushToVector(std::vector<T>&) = 0;

	virtual ~PullDataStream() { }
};

template <typename T>
class DumbSource: public PullDataStream<T>
{
public:
	size_t PushToVector(std::vector<T>& v) override
	{
		T x[] = { 1, 2, 3, 4, };
		size_t n = sizeof(x) / sizeof(T);

		v.reserve(v.size() + n);
		copy(&x[0], &x[n], back_inserter(v));

		return n;
	}
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
class DataBuffer : public PullDataStream<T>
{
public:
	DataBuffer(std::shared_ptr<PullDataStream<T>> dataStream, size_t len)
	: dataStream(dataStream), len(len)
	{ }

	size_t PushToVector(std::vector<T>& v) override
	{
		v.reserve(v.size() + len);

		// XXX: Replace this with a circular buffer
		while (tmpQueue.size() < len)
		{
			tmpBuf.clear();
			dataStream->PushToVector(tmpBuf);

			for(auto& x: tmpBuf)
			{
				tmpQueue.push(x);
			}
		}

		std::vector<T> result(len);
		for(size_t i = 0; i < len; i++)
		{
			v.push_back(tmpQueue.front());
			tmpQueue.pop();
		}

		return len;
	}

private:
	std::shared_ptr<PullDataStream<T>> dataStream;
	size_t len;
	std::queue<T> tmpQueue;
	std::vector<T> tmpBuf;
};


//template <typename T>
//class DataStream
//{
//	virtual void Push(std::vector<T>&& data) = 0;
//};
//
//template <typename T>
//class DumbSink : public DataStream<T>
//{
//	void Push(std::vector<T>&&) override
//	{ }
//};
//
//template <typename T>
//class DumbSource
//{
//public:
//	DumbSource(std::shared_ptr<DataStream<T>> dataStream)
//	{
//		this->dataStream = dataStream;
//	}
//
//private:
//	std::shared_ptr<DataStream<T>> dataStream;
//};

int main() {
//	auto sink = std::make_shared<DumbSink<signalType>>();
//
//	auto source = std::make_shared<DumbSource<signalType>>(sink);

	auto source = std::shared_ptr<PullDataStream<signalType>>(new DumbSource<signalType>());

	auto buf = std::make_shared<DataBuffer<signalType>>(source, 3);

	std::vector<signalType> v;

	buf->PushToVector(v);
	for (auto& x: v)
	{
		std::cout << x << '\n';
	}

	v.clear();
	buf->PushToVector(v);
	for (auto& x: v)
	{
		std::cout << x << '\n';
	}

	std::cout << "Hi" << std::endl; // prints !!!Hello World!!!
	return 0;
}
