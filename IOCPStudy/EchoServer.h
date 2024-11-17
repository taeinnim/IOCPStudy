#pragma once

#include "IOCPServer.h"
#include "Packet.h"

#include <vector>
#include <deque>
#include <mutex>

class EchoServer : public IOCPServer {
public:
	EchoServer() {}

	~EchoServer() {
		
	}

	void OnConnect(const UINT32 clientIndex_)
	{
		printf("[OnConnect] Index:(%d)\n", clientIndex_);
	}

	void OnClose(const UINT32 clientIndex_) override
	{
		printf("[OnClose] Index:(%d)\n", clientIndex_);
	}

	void OnReceive(const UINT32 clientIndex_, const UINT32 size_, char* pData_) override
	{
		PacketData packet;
		packet.Set(clientIndex_, size_, pData_);

		std::lock_guard<std::mutex> guard(mLock);
		mPacketDataQueue.push_back(packet);

		printf("[OnReceive] Index:(%d), dataSize(%d)\n", clientIndex_, size_);
	}

	void Start(const UINT32 maxClientCount) 
	{
		mIsSenderRun = true;
		mSenderThread = std::thread([this]() { SendPacketThread(); });

		StartServer(maxClientCount);
	}

	void End() 
	{
		mIsSenderRun = false;
		if (mSenderThread.joinable())
		{
			mSenderThread.join();
		}

		DestroyThread();
	}

private:
	bool mIsSenderRun = false;

	std::thread	mSenderThread;

	std::deque<PacketData> mPacketDataQueue; // FIFO

	std::mutex mLock;

	void SendPacketThread()
	{
		while (mIsSenderRun)
		{
			auto packetData = DequePacketData();
			if (packetData.DataSize != 0)
			{
				SendMsg(packetData.SessionIndex, packetData.DataSize, packetData.pPacketData);
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}

	PacketData DequePacketData()
	{
		PacketData packetData;

		std::lock_guard<std::mutex> guard(mLock); // 지정된 뮤텍스를 생성 시점에 잠그고, 객체가 소멸될 때 자동으로 잠금을 해제

		if (mPacketDataQueue.empty())
		{
			return PacketData();
		}

		packetData.Set(mPacketDataQueue.front());

		mPacketDataQueue.front().Release();
		mPacketDataQueue.pop_front();

		return packetData;
	}
};