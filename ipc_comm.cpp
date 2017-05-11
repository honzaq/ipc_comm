// ipc_comm.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "cmdp.h"
#include "ipc_comm_srv.h"

//////////////////////////////////////////////////////////////////////////
#include <windows.h>
#include <string>
#include <vector>
#include <codecvt>
//////////////////////////////////////////////////////////////////////////

std::vector<uint8_t> wstring_convert_to_bytes(const wchar_t *str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
	std::string string = converter.to_bytes(str);
	return std::vector<uint8_t>(string.begin(), string.end());
}

void start_slave(const wchar_t* params)
{
	PROCESS_INFORMATION piProcInfo;
	STARTUPINFO siStartInfo;
	::ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
	::ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	//siStartInfo.cb = sizeof(STARTUPINFO);
	::GetStartupInfo(&siStartInfo);

	WCHAR szPath[MAX_PATH];
	::GetModuleFileName(nullptr, szPath, MAX_PATH);

	WCHAR cmdLine[MAX_PATH];
	wcscpy_s(cmdLine, MAX_PATH, params);

	::CreateProcessW(szPath, cmdLine, NULL, NULL, TRUE, CREATE_NEW_CONSOLE/*spise 0*/, NULL, NULL, &siStartInfo, &piProcInfo);
}

int wmain(int argc, wchar_t *argv[])
{
	cmdp::parser cmdp(argc, argv);

	if(cmdp[L"pipe-master"]) {
		ipc_comm::server srv;
		srv.start();

		start_slave(srv.cmd_params().c_str());

		std::vector<uint8_t> response;
		std::vector<uint8_t> msg = wstring_convert_to_bytes(L"test");
		srv.send(msg, response);

		srv.stop();
	}
	else if(cmdp[L"pipe-slave"]) {

//		std::wstring pipes_param = cmdp(L"pipe-slave").str();

		HANDLE read_pipe = 0, write_pipe = 0;
		cmdp(L"pipe-r") >> read_pipe;
		cmdp(L"pipe-w") >> write_pipe;

		DWORD written_bytes;
		::WriteFile(write_pipe, L"a", (DWORD)2, &written_bytes, nullptr);
	}

    return 0;
}

