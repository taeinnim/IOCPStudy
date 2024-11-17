#pragma once
#pragma comment(lib, "ws2_32")

#include "Define.h"
#include "ClientInfo.h"

#include <thread>
#include <vector>

class IOCPServer{
public:
	IOCPServer() {}

	~IOCPServer()
	{
		// ������ ����� ������.
		WSACleanup();
	}

	virtual void OnConnect(const UINT32 clientIndex_) {}
	virtual void OnClose(const UINT32 clientIndex_) {}
	virtual void OnReceive(const UINT32 clientIndex_, const UINT32 size_, char* pData_) {}

	// ������ �ʱ�ȭ�ϴ� �Լ�
	bool InitSocket()
	{
		WSADATA wsaData;

		int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData); // ������ �ε�
		if (nRet != 0)
		{
			printf("[����] WSAStartup()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		// ���������� TCP , Overlapped I/O ������ ����
		mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

		if (mListenSocket == INVALID_SOCKET)
		{
			printf("[����] socket()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		printf("���� �ʱ�ȭ ����\n");
		return true;
	}

	// ������ �ּ������� ���ϰ� �����Ű�� ���� ��û�� �ޱ� ���� ������ ����ϴ� �Լ�
	bool BindAndListen(u_short nBindPort) 
	{
		SOCKADDR_IN		stServerAddr;
		stServerAddr.sin_family = AF_INET;

		// ���� ��Ʈ�� ����
		stServerAddr.sin_port = htons(nBindPort); // htons : u_short ȣ��Ʈ���� TCP/IP ��Ʈ��ũ ����Ʈ ����(big-endian)�� ��ȯ		

		//� �ּҿ��� ������ �����̶� �޾Ƶ��̰ڴ�.
		//���� ������� �̷��� �����Ѵ�. ���� �� �����ǿ����� ������ �ް� �ʹٸ�
		//�� �ּҸ� inet_addr�Լ��� �̿��� ������ �ȴ�.
		stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

		//������ ������ ���� �ּ� ������ cIOCompletionPort ������ �����Ѵ�.
		int nRet = bind(mListenSocket, (SOCKADDR*)&stServerAddr, sizeof(SOCKADDR_IN));
		if (0 != nRet)
		{
			printf("[����] bind()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		//���� ��û�� �޾Ƶ��̱� ���� cIOCompletionPort������ ����ϰ� 
		//���Ӵ��ť�� 5���� ���� �Ѵ�.
		nRet = listen(mListenSocket, 5);
		if (0 != nRet)
		{
			printf("[����] listen()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		printf("���� ��� ����..\n");
		return true;
	}

	bool StartServer(const UINT32 maxClientCount) {
		CreateClient(maxClientCount);

		// CompletionPort ���� & �ڵ� ����
		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);
		if (NULL == mIOCPHandle)
		{
			printf("[����] CreateIoCompletionPort()�Լ� ����: %d\n", GetLastError());
			return false;
		}

		// Waiting Thread Queue���� ����� ��Ŀ ������ ����
		bool bRet = CreateWokerThread();
		if (false == bRet) {
			return false;
		}

		bRet = CreateAccepterThread();
		if (false == bRet) {
			return false;
		}

		printf("���� ����\n");
		return true;
	}

	bool SendMsg(const UINT32 sessionIndex_, const UINT32 dataSize_, char* pData)
	{
		auto pClient = GetClientInfo(sessionIndex_);
		return pClient->SendMsg(dataSize_, pData);
	}

	// ������ ������ ���� ��Ų��.
	void CloseSocket(ClientInfo* pClientInfo, bool bIsForce = false)
	{
		auto clientIndex = pClientInfo->GetIndex();

		pClientInfo->OnClose(bIsForce);

		OnClose(clientIndex);
	}

	// �����Ǿ��ִ� ��������� �����Ų��.
	void DestroyThread()
	{
		// worker ������ ����
		mIsWorkerRun = false;
		CloseHandle(mIOCPHandle);
		for (auto& th : mIOWorkerThreads)
		{
			if (th.joinable()) // Ȱ�� ���¶�� ����
			{
				th.join(); 
			}
		}

		// Accepter �����嵵 ����
		mIsAccepterRun = false;
		closesocket(mListenSocket);

		if (mAccepterThread.joinable())
		{
			mAccepterThread.join();
		}
	}

private:
	// CompletionPort��ü �ڵ�
	HANDLE		mIOCPHandle = INVALID_HANDLE_VALUE;

	// Ŭ���̾�Ʈ ���� ���� ����ü
	std::vector<ClientInfo> mClientInfos;

	// ���� ����
	char		mSocketBuf[1024] = { 0, };

	// �۾� ������ ���� �÷���
	bool		mIsWorkerRun = true;

	// ���� ������ ���� �÷���
	bool		mIsAccepterRun = true;

	// ������ ���� �÷���
	bool		mIsSenderRun = true;

	// IO Worker ������
	std::vector<std::thread> mIOWorkerThreads;

	// Accept ������
	std::thread	mAccepterThread;

	// ���� �Ǿ��ִ� Ŭ���̾�Ʈ ��
	int			mClientCnt = 0;

	// Ŭ���̾�Ʈ�� ������ �ޱ����� ���� ����
	SOCKET		mListenSocket = INVALID_SOCKET;

	void CreateClient(const UINT32 maxClientCount)
	{
		for (UINT32 i = 0; i < maxClientCount; ++i)
		{
			auto client = new ClientInfo;
			client->Init(i);
			//mClientInfos.push_back(client);
		}
	}

	// Waiting Thread Queue���� ����� ��������� ����
	bool CreateWokerThread()
	{
		//unsigned int uiThreadId = 0;
		//WaingThread Queue�� ��� ���·� ���� ������� ���� ����Ǵ� ���� : (cpu���� * 2) + 1 
		for (int i = 0; i < MAX_WORKERTHREAD; i++)
		{
			mIOWorkerThreads.emplace_back([this]() { WokerThread(); });
		}

		printf("WokerThread ����..\n");
		return true;
	}

	// accept��û�� ó���ϴ� ������ ����
	bool CreateAccepterThread()
	{
		mAccepterThread = std::thread([this]() { AccepterThread(); });

		printf("AccepterThread ����..\n");
		return true;
	}

	// Overlapped I/O�۾��� ���� �Ϸ� �뺸�� �޾� �׿� �ش��ϴ� ó���� �ϴ� �Լ�
	void WokerThread()
	{
		//CompletionKey�� ���� ������ ����
		ClientInfo* pClientInfo = NULL;
		//�Լ� ȣ�� ���� ����
		BOOL bSuccess = TRUE;
		//Overlapped I/O�۾����� ���۵� ������ ũ��
		DWORD dwIoSize = 0;
		//I/O �۾��� ���� ��û�� Overlapped ����ü�� ���� ������
		LPOVERLAPPED lpOverlapped = NULL;

		// �۾� ������ �÷��װ� true�϶��� ����
		while (mIsWorkerRun)
		{
			//////////////////////////////////////////////////////
			//�� �Լ��� ���� ��������� WaitingThread Queue��
			//��� ���·� ���� �ȴ�.
			//�Ϸ�� Overlapped I/O�۾��� �߻��ϸ� IOCP Queue����
			//�Ϸ�� �۾��� ������ �� ó���� �Ѵ�.
			//�׸��� PostQueuedCompletionStatus()�Լ������� �����
			//�޼����� �����Ǹ� �����带 �����Ѵ�.
			//////////////////////////////////////////////////////
			bSuccess = GetQueuedCompletionStatus(
				mIOCPHandle,
				&dwIoSize,					// ������ ���۵� ����Ʈ
				(PULONG_PTR)&pClientInfo,	// CompletionKey => �ڵ麰 ������(�������� �����ϰų� ����ؼ� I/0�� ��û�ϴµ� ����)
				&lpOverlapped,				// Overlapped IO ��ü => I/O�۾��� ������
				INFINITE);					// ����� �ð�

			//����� ������ ���� �޼��� ó��..
			if (bSuccess == TRUE && dwIoSize == 0 && lpOverlapped == NULL)
			{
				mIsWorkerRun = false;
				continue;
			}

			if (NULL == lpOverlapped)
			{
				continue;
			}

			// client�� ������ ��������			
			if (bSuccess == FALSE || (dwIoSize == 0 && bSuccess == TRUE))
			{
				CloseSocket(pClientInfo);
				continue;
			}

			stOverlappedEx* pOverlappedEx = (stOverlappedEx*)lpOverlapped;

			// Overlapped I/O Recv�۾� ��� �� ó��
			if (pOverlappedEx->m_eOperation == IOOperation::RECV)
			{
				OnReceive(pClientInfo->GetIndex(), dwIoSize, pClientInfo->GetRecvBuffer());
				pClientInfo->BindRecv();
			}
			// Overlapped I/O Send�۾� ��� �� ó��
			else if (pOverlappedEx->m_eOperation == IOOperation::SEND)
			{
				pClientInfo->SendCompleted(dwIoSize);
			}
			// ���� ��Ȳ
			else
			{
				printf("Client Index(%d)���� ���ܻ�Ȳ\n", pClientInfo->GetIndex());
			}
		}
	}

	// ������� ������ �޴� ������
	void AccepterThread()
	{
		SOCKADDR_IN		stClientAddr;
		int nAddrLen = sizeof(SOCKADDR_IN);

		while (mIsAccepterRun)
		{
			// ������ ���� ����ü�� �ε����� ���´�.
			ClientInfo* pClientInfo = GetEmptyClientInfo();
			if (pClientInfo == NULL)
			{
				printf("[����] Client Full\n");
				return;
			}

			// Ŭ���̾�Ʈ ���� ��û�� ���� ������ ��ٸ���.
			auto newSocket = accept(mListenSocket, (SOCKADDR*)&stClientAddr, &nAddrLen);
			if (newSocket == INVALID_SOCKET)
			{
				continue;
			}

			// I/O Completion Port��ü�� ������ �����Ų��. (+ Recv Overlapped I/O�۾��� ��û)
			bool bRet = pClientInfo->OnConnect(mIOCPHandle, newSocket);
			if (false == bRet)
			{
				return;
			}

			// �޾������� ó��
			OnConnect(pClientInfo->GetIndex());

			//char clientIP[32] = { 0, };
			//inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
			//printf("Ŭ���̾�Ʈ ���� : IP(%s) SOCKET(%d)\n", clientIP, (int)pClientInfo->m_socketClient);

			//Ŭ���̾�Ʈ ���� ����
			++mClientCnt;
		}
	}

	ClientInfo* GetClientInfo(const UINT32 sessionIndex)
	{
		return &mClientInfos[sessionIndex];
	}

	// ������� �ʴ� Ŭ���̾�Ʈ ���� ����ü�� ��ȯ�Ѵ�.
	ClientInfo* GetEmptyClientInfo()
	{
		for (auto& client : mClientInfos)
		{
			if (client.IsInvalidSocket())
			{
				return &client;
			}
		}

		return nullptr;
	}
};
