#pragma once
#pragma comment(lib, "mswsock.lib")

#include "Define.h"

#include <winsock2.h>
#include <mswsock.h>

#include <stdio.h>
#include <mutex>
#include <queue>

class ClientInfo {
public:
	ClientInfo()
	{
		mSock = INVALID_SOCKET;
		ZeroMemory(&mAcceptOverlappedEx, sizeof(stOverlappedEx));
		ZeroMemory(&mRecvOverlappedEx, sizeof(stOverlappedEx));
		//ZeroMemory(&mSendOverlappedEx, sizeof(stOverlappedEx));
		
		ZeroMemory(&mAcceptBuf, sizeof(mAcceptBuf));
		ZeroMemory(&mRecvBuf, sizeof(mRecvBuf));
		//ZeroMemory(&mSendBuf, sizeof(mSendBuf));
	}

	void Init(const UINT32 index)
	{
		mIndex = index;
	}

	UINT32 GetIndex() const {
		return mIndex;
	}

	bool IsInvalidSocket() const {
		return mSock == INVALID_SOCKET;
	}

	char* GetRecvBuffer() { return mRecvBuf; }

	bool OnConnect(HANDLE iocpHandle_, SOCKET socket_)
	{
		mSock = socket_;

		//Clear();

		// I/O Completion Port객체와 소켓을 연결시킨다.
		if (BindIOCompletionPort(iocpHandle_) == false)
		{
			return false;
		}

		//Recv Overlapped I/O작업을 요청해 놓는다.
		return BindRecv();
	}

	// 소켓의 연결을 종료 시킨다.
	void OnClose(bool bIsForce = false)
	{
		struct linger stLinger = { 0, 0 };	// SO_DONTLINGER로 설정

		// bIsForce가 true이면 SO_LINGER, timeout = 0으로 설정하여 강제 종료 시킨다. 주의 : 데이터 손실이 있을수 있음 
		if (true == bIsForce)
		{
			stLinger.l_onoff = 1;
		}

		// 소켓의 데이터 송수신을 모두 중단 시킨다.
		shutdown(mSock, SD_BOTH);

		// 소켓 옵션을 설정한다.
		setsockopt(mSock, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		//소켓 연결을 종료 시킨다. 
		closesocket(mSock);

		mSock = INVALID_SOCKET;
	}

	bool PostAccept(SOCKET listenSock_, const UINT64 curTimeSec_)
	{
		printf_s("PostAccept. client Index: %d\n", GetIndex());
		
		//mLatestClosedTimeSec = UINT32_MAX;

		mSock = WSASocket(
			AF_INET, 
			SOCK_STREAM, 
			IPPROTO_IP,
			NULL, 
			0,
			WSA_FLAG_OVERLAPPED
		);

		if (mSock == INVALID_SOCKET)
		{
			printf_s("client Socket WSASocket Error : %d\n", GetLastError());
			return false;
		}

		ZeroMemory(&mAcceptOverlappedEx, sizeof(stOverlappedEx));
		
		DWORD bytes = 0;
		//DWORD flags = 0;
		mAcceptOverlappedEx.m_wsaBuf.len = 0;
		mAcceptOverlappedEx.m_wsaBuf.buf = nullptr;
		mAcceptOverlappedEx.m_eOperation = IOOperation::ACCEPT;
		mAcceptOverlappedEx.SessionIndex = mIndex;

		bool bRet = AcceptEx(
			listenSock_,
			mSock,
			mAcceptBuf,
			0,
			sizeof(SOCKADDR_IN) + 16,
			sizeof(SOCKADDR_IN) + 16,
			&bytes,
			(LPWSAOVERLAPPED) & (mAcceptOverlappedEx)
		);

		if (bRet == FALSE)
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
			{
				printf_s("AcceptEx Error : %d\n", GetLastError());
				return false;
			}
		}

		return true;
	}

	bool AcceptCompletion()
	{
		bool bRet = OnConnect(mIOCPHandle, mSock);
		if (bRet == false)
		{
			return false;
		}

		printf_s("AcceptCompletion : SessionIndex(%d)\n", mIndex);

		return true;
	}

	// WSARecv Overlapped I/O 작업을 시킨다.
	bool BindRecv()
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		// Overlapped I/O을 위해 각 정보를 셋팅해 준다.
		mRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		mRecvOverlappedEx.m_wsaBuf.buf = mRecvBuf;
		mRecvOverlappedEx.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(
			mSock,
			&(mRecvOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			&dwFlag,
			(LPWSAOVERLAPPED) &(mRecvOverlappedEx),
			NULL // null이면 어떻게 되지..? 겹치는 작업이 완료될 때 lpOverlapped의 hEvent 매개 변수가 유효한 이벤트 개체 핸들을 포함하는 경우 신호를 보냅니다.
		);

		// socket_error이면 client socket이 끊어진걸로 처리한다.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[에러] WSARecv()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	// 1개의 스레드에서만 호출해야 함!
	bool SendMsg(const UINT32 dataSize_, char* pMsg_)
	{
		auto sendOverlappedEx = new stOverlappedEx;
		ZeroMemory(sendOverlappedEx, sizeof(stOverlappedEx));

		sendOverlappedEx->m_wsaBuf.len = dataSize_;
		sendOverlappedEx->m_wsaBuf.buf = new char[dataSize_];
		CopyMemory(sendOverlappedEx->m_wsaBuf.buf, pMsg_, dataSize_);
		sendOverlappedEx->m_eOperation = IOOperation::SEND;

		std::lock_guard<std::mutex> guard(mSendLock);

		mSendDataqueue.push(sendOverlappedEx);

		if (mSendDataqueue.size() == 1)
		{
			SendIO();
		}

		return true;
	}

	void SendCompleted(const UINT32 dataSize_)
	{
		printf("[송신 완료] bytes : %d\n", dataSize_);

		std::lock_guard<std::mutex> guard(mSendLock);

		delete[] mSendDataqueue.front()->m_wsaBuf.buf;
		delete mSendDataqueue.front();

		mSendDataqueue.pop();

		if (mSendDataqueue.empty() == false)
		{
			SendIO();
		}
	}

private:
	INT32			mIndex = 0;

	HANDLE			mIOCPHandle = INVALID_HANDLE_VALUE;
	SOCKET			mSock; // Cliet와 연결되는 소켓
	stOverlappedEx	mAcceptOverlappedEx;
	stOverlappedEx	mRecvOverlappedEx; // RECV Overlapped I/O작업을 위한 변수
	//stOverlappedEx	mSendOverlappedEx; // SEND Overlapped I/O작업을 위한 변수

	char			mAcceptBuf[MAX_SOCKBUF];
	char			mRecvBuf[MAX_SOCKBUF]; // 데이터 버퍼
	//char			mSendBuf[MAX_SOCKBUF]; // 데이터 버퍼

	std::mutex mSendLock;

	std::queue<stOverlappedEx*> mSendDataqueue;

	bool BindIOCompletionPort(HANDLE iocpHandle_)
	{
		auto hIOCP = CreateIoCompletionPort(
			(HANDLE)mSock
			, iocpHandle_
			, (ULONG_PTR)(this)
			, 0
		);

		if (hIOCP == INVALID_HANDLE_VALUE)
		{
			printf("[에러] CreateIoCompletionPort()함수 실패: %d\n", GetLastError());
			return false;
		}

		//if (NULL == hIOCP || iocpHandle_ != hIOCP)
		//{
		//	printf("[에러] CreateIoCompletionPort()함수 실패: %d\n", GetLastError());
		//	return false;
		//}

		return true;
	}

	// WSASend Overlapped I/O작업을 시킨다.
	bool SendIO()
	{
		auto sendOverlappedEx = mSendDataqueue.front();

		DWORD dwRecvNumBytes = 0;
		int nRet = WSASend(mSock,
			&(sendOverlappedEx->m_wsaBuf),
			1,
			&dwRecvNumBytes,
			0,
			(LPWSAOVERLAPPED)sendOverlappedEx,
			NULL);

		// socket_error이면 client socket이 끊어진걸로 처리한다.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[에러] WSASend()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}
};