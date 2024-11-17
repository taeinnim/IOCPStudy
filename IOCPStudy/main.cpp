#pragma once

#include "EchoServer.h"

#include <string>
#include <iostream>

const u_short SERVER_PORT = 11021;	// 사용할 포트 번호
const UINT16 MAX_CLIENT = 100;		// 총 접속할수 있는 클라이언트 수

int main()
{
	EchoServer server;

	// 소켓을 초기화
	server.InitSocket();

	// 소켓과 서버 주소를 연결하고 등록 시킨다.
	server.BindAndListen(SERVER_PORT);

	server.Start(MAX_CLIENT);

	printf("'quit'이 입력될 때까지 대기합니다\n");

	while (true)
	{
		std::string inputCmd;
		std::getline(std::cin, inputCmd); // '\n'을 구분자로 사용하여 문자열을 입력받는다. 이후 '\n'를 버퍼에서 지운다

		if (inputCmd == "quit")
		{
			break;
		}
	}

	server.End();
	return 0;
}
