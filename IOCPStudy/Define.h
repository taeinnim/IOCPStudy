#pragma once

#include <winsock2.h>
#include <Ws2tcpip.h>

const UINT32 MAX_SOCKBUF = 256;	// ��Ŷ ũ��
const UINT32 MAX_WORKERTHREAD = 5;  //������ Ǯ�� ���� ������ ��


enum class IOOperation
{
	RECV,
	SEND
};

//WSAOVERLAPPED����ü�� Ȯ�� ���Ѽ� �ʿ��� ������ �� �־���.
struct stOverlappedEx
{
	WSAOVERLAPPED m_wsaOverlapped;		//Overlapped I/O����ü
	SOCKET		m_socketClient;			//Ŭ���̾�Ʈ ����
	WSABUF		m_wsaBuf;				//Overlapped I/O�۾� ����
	IOOperation m_eOperation;			//�۾� ���� ����
};