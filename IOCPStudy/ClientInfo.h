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

		// I/O Completion Port��ü�� ������ �����Ų��.
		if (BindIOCompletionPort(iocpHandle_) == false)
		{
			return false;
		}

		//Recv Overlapped I/O�۾��� ��û�� ���´�.
		return BindRecv();
	}

	// ������ ������ ���� ��Ų��.
	void OnClose(bool bIsForce = false)
	{
		struct linger stLinger = { 0, 0 };	// SO_DONTLINGER�� ����

		// bIsForce�� true�̸� SO_LINGER, timeout = 0���� �����Ͽ� ���� ���� ��Ų��. ���� : ������ �ս��� ������ ���� 
		if (true == bIsForce)
		{
			stLinger.l_onoff = 1;
		}

		// ������ ������ �ۼ����� ��� �ߴ� ��Ų��.
		shutdown(mSock, SD_BOTH);

		// ���� �ɼ��� �����Ѵ�.
		setsockopt(mSock, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		//���� ������ ���� ��Ų��. 
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

	// WSARecv Overlapped I/O �۾��� ��Ų��.
	bool BindRecv()
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		// Overlapped I/O�� ���� �� ������ ������ �ش�.
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
			NULL // null�̸� ��� ����..? ��ġ�� �۾��� �Ϸ�� �� lpOverlapped�� hEvent �Ű� ������ ��ȿ�� �̺�Ʈ ��ü �ڵ��� �����ϴ� ��� ��ȣ�� �����ϴ�.
		);

		// socket_error�̸� client socket�� �������ɷ� ó���Ѵ�.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[����] WSARecv()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	// 1���� �����忡���� ȣ���ؾ� ��!
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
		printf("[�۽� �Ϸ�] bytes : %d\n", dataSize_);

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
	SOCKET			mSock; // Cliet�� ����Ǵ� ����
	stOverlappedEx	mAcceptOverlappedEx;
	stOverlappedEx	mRecvOverlappedEx; // RECV Overlapped I/O�۾��� ���� ����
	//stOverlappedEx	mSendOverlappedEx; // SEND Overlapped I/O�۾��� ���� ����

	char			mAcceptBuf[MAX_SOCKBUF];
	char			mRecvBuf[MAX_SOCKBUF]; // ������ ����
	//char			mSendBuf[MAX_SOCKBUF]; // ������ ����

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
			printf("[����] CreateIoCompletionPort()�Լ� ����: %d\n", GetLastError());
			return false;
		}

		//if (NULL == hIOCP || iocpHandle_ != hIOCP)
		//{
		//	printf("[����] CreateIoCompletionPort()�Լ� ����: %d\n", GetLastError());
		//	return false;
		//}

		return true;
	}

	// WSASend Overlapped I/O�۾��� ��Ų��.
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

		// socket_error�̸� client socket�� �������ɷ� ó���Ѵ�.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[����] WSASend()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}
};