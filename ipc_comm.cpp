// ipc.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "cmdp.h"
#include "ipc_comm_srv.h"
#include "convert.h"

//////////////////////////////////////////////////////////////////////////
#include <windows.h>
#include <string>
#include <vector>
#include <iostream>
//////////////////////////////////////////////////////////////////////////


void start_slave(const wchar_t* params)
{
	PROCESS_INFORMATION piProcInfo;
	STARTUPINFO siStartInfo;
	::ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
	::ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	::GetStartupInfo(&siStartInfo);

	WCHAR szPath[MAX_PATH];
	::GetModuleFileName(nullptr, szPath, MAX_PATH);

	WCHAR cmdLine[MAX_PATH];
	wcscpy_s(cmdLine, MAX_PATH, params);
	std::wcout << L" params: " << cmdLine << std::endl;

	if(!::CreateProcessW(szPath, cmdLine, NULL, NULL, TRUE, CREATE_NEW_CONSOLE/*spise 0*/, NULL, NULL, &siStartInfo, &piProcInfo)) {
		std::wcout << L"Create process fail:" << ::GetLastError() << std::endl;
	}
}

int wmain(int argc, wchar_t *argv[])
{
	cmdp::parser cmdp(argc, argv);

	if(cmdp[L"pipe-master"]) {
		ipc::server srv;
		srv.start();

		start_slave(srv.cmd_params().c_str());

		srv.post_slave_start();

		std::vector<uint8_t> response;
		std::vector<uint8_t> msg = ipc::wstring_convert_to_bytes(L"test");
		//srv.send(msg, response);

		::Sleep(1000);

		srv.stop();
	}
	else if(cmdp[L"pipe-slave"]) {

#ifdef _DEBUG
// 		std::wcout << L"Waiting for debugger...." << std::endl;
// 		while(!::IsDebuggerPresent())
// 			::Sleep(100);
// 		::DebugBreak();
#endif

		HANDLE read_pipe = 0, write_pipe = 0;
		cmdp(L"pipe-r") >> read_pipe;
		cmdp(L"pipe-w") >> write_pipe;

		std::wcout << L"Hello I'm your SLAVE (read-pipe:" << std::hex << read_pipe << L", write-pipe:" << std::hex << write_pipe << L")" << std::endl;

		DWORD written_bytes = 0;

		std::wstring msgText(L"slave-msg");

		ipc::header new_header;
		new_header.id = 9;
		new_header.flags = 3;
		new_header.message_size = msgText.size();
		if(!::WriteFile(write_pipe, &new_header, (DWORD)sizeof(new_header), &written_bytes, nullptr)) {
			std::wcout << L"Write header to pipe fail: " << ::GetLastError() << std::endl;
		}
		std::wcout << L"Header send" << std::endl;

		std::vector<uint8_t> msg = ipc::wstring_convert_to_bytes(msgText);
		if(!::WriteFile(write_pipe, msg.data(), (DWORD)msg.size(), &written_bytes, nullptr)) {
			std::wcout << L"Write message pipe fail: " << ::GetLastError() << std::endl;
		}

		::Sleep(15000);

		::CloseHandle(read_pipe);
		::CloseHandle(write_pipe);
	}

    return 0;
}

