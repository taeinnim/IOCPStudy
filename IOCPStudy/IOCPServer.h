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
		// 윈속의 사용을 끝낸다.
		WSACleanup();
	}

	virtual void OnConnect(const UINT32 clientIndex_) {}
	virtual void OnClose(const UINT32 clientIndex_) {}
	virtual void OnReceive(const UINT32 clientIndex_, const UINT32 size_, char* pData_) {}

	// 소켓을 초기화하는 함수
	bool InitSocket()
	{
		WSADATA wsaData;

		int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData); // 윈속을 로드
		if (nRet != 0)
		{
			printf("[에러] WSAStartup()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		// 연결지향형 TCP , Overlapped I/O 소켓을 생성
		mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

		if (mListenSocket == INVALID_SOCKET)
		{
			printf("[에러] socket()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		printf("소켓 초기화 성공\n");
		return true;
	}

	// 서버의 주소정보를 소켓과 연결시키고 접속 요청을 받기 위해 소켓을 등록하는 함수
	bool BindAndListen(u_short nBindPort) 
	{
		SOCKADDR_IN		stServerAddr;
		stServerAddr.sin_family = AF_INET;

		// 서버 포트를 설정
		stServerAddr.sin_port = htons(nBindPort); // htons : u_short 호스트에서 TCP/IP 네트워크 바이트 순서(big-endian)로 변환		

		//어떤 주소에서 들어오는 접속이라도 받아들이겠다.
		//보통 서버라면 이렇게 설정한다. 만약 한 아이피에서만 접속을 받고 싶다면
		//그 주소를 inet_addr함수를 이용해 넣으면 된다.
		stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

		//위에서 지정한 서버 주소 정보와 cIOCompletionPort 소켓을 연결한다.
		int nRet = bind(mListenSocket, (SOCKADDR*)&stServerAddr, sizeof(SOCKADDR_IN));
		if (0 != nRet)
		{
			printf("[에러] bind()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		//접속 요청을 받아들이기 위해 cIOCompletionPort소켓을 등록하고 
		//접속대기큐를 5개로 설정 한다.
		nRet = listen(mListenSocket, 5);
		if (0 != nRet)
		{
			printf("[에러] listen()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		printf("서버 등록 성공..\n");
		return true;
	}

	bool StartServer(const UINT32 maxClientCount) {
		CreateClient(maxClientCount);

		// CompletionPort 생성 & 핸들 적용
		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);
		if (NULL == mIOCPHandle)
		{
			printf("[에러] CreateIoCompletionPort()함수 실패: %d\n", GetLastError());
			return false;
		}

		// Waiting Thread Queue에서 대기할 워커 쓰레드 생성
		bool bRet = CreateWokerThread();
		if (false == bRet) {
			return false;
		}

		bRet = CreateAccepterThread();
		if (false == bRet) {
			return false;
		}

		printf("서버 시작\n");
		return true;
	}

	bool SendMsg(const UINT32 sessionIndex_, const UINT32 dataSize_, char* pData)
	{
		auto pClient = GetClientInfo(sessionIndex_);
		return pClient->SendMsg(dataSize_, pData);
	}

	// 소켓의 연결을 종료 시킨다.
	void CloseSocket(ClientInfo* pClientInfo, bool bIsForce = false)
	{
		auto clientIndex = pClientInfo->GetIndex();

		pClientInfo->OnClose(bIsForce);

		OnClose(clientIndex);
	}

	// 생성되어있는 쓰레드들을 종료시킨다.
	void DestroyThread()
	{
		// worker 쓰레드 종료
		mIsWorkerRun = false;
		CloseHandle(mIOCPHandle);
		for (auto& th : mIOWorkerThreads)
		{
			if (th.joinable()) // 활성 상태라면 종료
			{
				th.join(); 
			}
		}

		// Accepter 쓰레드도 종료
		mIsAccepterRun = false;
		closesocket(mListenSocket);

		if (mAccepterThread.joinable())
		{
			mAccepterThread.join();
		}
	}

private:
	// CompletionPort객체 핸들
	HANDLE		mIOCPHandle = INVALID_HANDLE_VALUE;

	// 클라이언트 정보 저장 구조체
	std::vector<ClientInfo> mClientInfos;

	// 소켓 버퍼
	char		mSocketBuf[1024] = { 0, };

	// 작업 쓰레드 동작 플래그
	bool		mIsWorkerRun = true;

	// 접속 쓰레드 동작 플래그
	bool		mIsAccepterRun = true;

	// 데이터 전송 플래그
	bool		mIsSenderRun = true;

	// IO Worker 스레드
	std::vector<std::thread> mIOWorkerThreads;

	// Accept 스레드
	std::thread	mAccepterThread;

	// 접속 되어있는 클라이언트 수
	int			mClientCnt = 0;

	// 클라이언트의 접속을 받기위한 리슨 소켓
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

	// Waiting Thread Queue에서 대기할 쓰레드들을 생성
	bool CreateWokerThread()
	{
		//unsigned int uiThreadId = 0;
		//WaingThread Queue에 대기 상태로 넣을 쓰레드들 생성 권장되는 개수 : (cpu개수 * 2) + 1 
		for (int i = 0; i < MAX_WORKERTHREAD; i++)
		{
			mIOWorkerThreads.emplace_back([this]() { WokerThread(); });
		}

		printf("WokerThread 시작..\n");
		return true;
	}

	// accept요청을 처리하는 쓰레드 생성
	bool CreateAccepterThread()
	{
		mAccepterThread = std::thread([this]() { AccepterThread(); });

		printf("AccepterThread 시작..\n");
		return true;
	}

	// Overlapped I/O작업에 대한 완료 통보를 받아 그에 해당하는 처리를 하는 함수
	void WokerThread()
	{
		//CompletionKey를 받을 포인터 변수
		ClientInfo* pClientInfo = NULL;
		//함수 호출 성공 여부
		BOOL bSuccess = TRUE;
		//Overlapped I/O작업에서 전송된 데이터 크기
		DWORD dwIoSize = 0;
		//I/O 작업을 위해 요청한 Overlapped 구조체를 받을 포인터
		LPOVERLAPPED lpOverlapped = NULL;

		// 작업 쓰레드 플래그가 true일때만 동작
		while (mIsWorkerRun)
		{
			//////////////////////////////////////////////////////
			//이 함수로 인해 쓰레드들은 WaitingThread Queue에
			//대기 상태로 들어가게 된다.
			//완료된 Overlapped I/O작업이 발생하면 IOCP Queue에서
			//완료된 작업을 가져와 뒤 처리를 한다.
			//그리고 PostQueuedCompletionStatus()함수에의해 사용자
			//메세지가 도착되면 쓰레드를 종료한다.
			//////////////////////////////////////////////////////
			bSuccess = GetQueuedCompletionStatus(
				mIOCPHandle,
				&dwIoSize,					// 실제로 전송된 바이트
				(PULONG_PTR)&pClientInfo,	// CompletionKey => 핸들별 데이터(소켓의을 구분하거나 계속해서 I/0를 요청하는데 사용됨)
				&lpOverlapped,				// Overlapped IO 객체 => I/O작업별 데이터
				INFINITE);					// 대기할 시간

			//사용자 쓰레드 종료 메세지 처리..
			if (bSuccess == TRUE && dwIoSize == 0 && lpOverlapped == NULL)
			{
				mIsWorkerRun = false;
				continue;
			}

			if (NULL == lpOverlapped)
			{
				continue;
			}

			// client가 접속을 끊었을때			
			if (bSuccess == FALSE || (dwIoSize == 0 && bSuccess == TRUE))
			{
				CloseSocket(pClientInfo);
				continue;
			}

			stOverlappedEx* pOverlappedEx = (stOverlappedEx*)lpOverlapped;

			// Overlapped I/O Recv작업 결과 뒤 처리
			if (pOverlappedEx->m_eOperation == IOOperation::RECV)
			{
				OnReceive(pClientInfo->GetIndex(), dwIoSize, pClientInfo->GetRecvBuffer());
				pClientInfo->BindRecv();
			}
			// Overlapped I/O Send작업 결과 뒤 처리
			else if (pOverlappedEx->m_eOperation == IOOperation::SEND)
			{
				pClientInfo->SendCompleted(dwIoSize);
			}
			// 예외 상황
			else
			{
				printf("Client Index(%d)에서 예외상황\n", pClientInfo->GetIndex());
			}
		}
	}

	// 사용자의 접속을 받는 쓰레드
	void AccepterThread()
	{
		SOCKADDR_IN		stClientAddr;
		int nAddrLen = sizeof(SOCKADDR_IN);

		while (mIsAccepterRun)
		{
			// 접속을 받을 구조체의 인덱스를 얻어온다.
			ClientInfo* pClientInfo = GetEmptyClientInfo();
			if (pClientInfo == NULL)
			{
				printf("[에러] Client Full\n");
				return;
			}

			// 클라이언트 접속 요청이 들어올 때까지 기다린다.
			auto newSocket = accept(mListenSocket, (SOCKADDR*)&stClientAddr, &nAddrLen);
			if (newSocket == INVALID_SOCKET)
			{
				continue;
			}

			// I/O Completion Port객체와 소켓을 연결시킨다. (+ Recv Overlapped I/O작업을 요청)
			bool bRet = pClientInfo->OnConnect(mIOCPHandle, newSocket);
			if (false == bRet)
			{
				return;
			}

			// 받았을때의 처리
			OnConnect(pClientInfo->GetIndex());

			//char clientIP[32] = { 0, };
			//inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
			//printf("클라이언트 접속 : IP(%s) SOCKET(%d)\n", clientIP, (int)pClientInfo->m_socketClient);

			//클라이언트 갯수 증가
			++mClientCnt;
		}
	}

	ClientInfo* GetClientInfo(const UINT32 sessionIndex)
	{
		return &mClientInfos[sessionIndex];
	}

	// 사용하지 않는 클라이언트 정보 구조체를 반환한다.
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
