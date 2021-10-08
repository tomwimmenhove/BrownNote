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
	virtual std::vector<T> Pull() = 0;

	virtual ~PullDataStream() { }
};

template <typename T>
class DumbSource: public PullDataStream<T>
{
public:
	std::vector<T> Pull() override
	{
		return std::vector<T>({ 1, 2, 3, 4, });
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

	std::vector<T> Pull() override
	{
		// XXX: Replace this with a circular buffer
		while (tmpBuf.size() < len)
		{
			auto data = dataStream->Pull();
			for(auto& x: data)
			{
				tmpBuf.push(x);
			}
		}

		std::vector<T> result(len);
		for(size_t i = 0; i < len; i++)
		{
			result[i] = tmpBuf.front();
			tmpBuf.pop();
		}

		return result;
	}

private:
	std::shared_ptr<PullDataStream<T>> dataStream;
	size_t len;
	std::queue<T> tmpBuf;
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

	auto data = buf->Pull();
	for (auto& x: data)
	{
		std::cout << x << '\n';
	}
	data = buf->Pull();
	for (auto& x: data)
	{
		std::cout << x << '\n';
	}


	std::cout << "Hi" << std::endl; // prints !!!Hello World!!!
	return 0;
}
