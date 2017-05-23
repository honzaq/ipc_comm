// ipc.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "cmdp.h"
#include "ipc_master.h"
#include "ipc_slave.h"
#include "convert.h"

//////////////////////////////////////////////////////////////////////////
#include <windows.h>
#include <string>
#include <vector>
#include <iostream>
//////////////////////////////////////////////////////////////////////////

void WaitForDebugger()
{
#ifdef _DEBUG
	std::wcout << L"Waiting for debugger...." << std::endl;
	while(!::IsDebuggerPresent())
		::Sleep(100);
	::DebugBreak();
#endif
}

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
		std::wcout << L"Hello I'm your MASTER" << std::endl;

		int msg_receive_count = 0;

		ipc::master::factory master_factory;
		std::shared_ptr<ipc::master_intf> master_ptr = master_factory.create_master([&](const std::vector<uint8_t>& message, std::vector<uint8_t>& response) {
			std::wcout << L"OnMessage(master): " << utils::wstring_convert_from_bytes(message) << std::endl;
			response = utils::wstring_convert_to_bytes(L"I'm master response.");
			msg_receive_count++;
		});

		start_slave(master_ptr->cmd_pipe_params().c_str());

		master_ptr->slave_started();

		
		for(int i = 0; i < 100; i++) {
			std::vector<uint8_t> response;
			std::vector<uint8_t> msg = utils::wstring_convert_to_bytes(L"I'm master message.");
			master_ptr->send(msg, response);
			std::wcout << L"slave response is:" << utils::wstring_convert_from_bytes(response) << std::endl;
		}

		std::wcout << L"*** sleeping ***" << std::endl;

		//::Sleep(15000);

		std::wcout << L"*** stopping ***" << std::endl;

		master_ptr->stop();

		std::wcout << L"*** stopped ***" << L" received messages count (" << msg_receive_count << L")" << std::endl;

		::Sleep(15000);
	}
	else if(cmdp[L"pipe-slave"]) {

		HANDLE read_pipe = 0, write_pipe = 0;
		cmdp(L"pipe-r") >> read_pipe;
		cmdp(L"pipe-w") >> write_pipe;

		std::wcout << L"Hello I'm your SLAVE (read-pipe:" << std::hex << read_pipe << L", write-pipe:" << std::hex << write_pipe << L")" << std::endl;

		ipc::slave::factory slave_factory;
		std::shared_ptr<ipc::slave_intf> slave_ptr = slave_factory.create_slave(read_pipe, write_pipe, [&](const std::vector<uint8_t>& message, std::vector<uint8_t>& response) {
			std::wcout << L"OnMessage(slave)" << utils::wstring_convert_from_bytes(message) << std::endl;

			response = utils::wstring_convert_to_bytes(L"I'm slave response.");
		});

		for(int i = 0; i < 100; i++) {
			std::vector<uint8_t> response;
			std::vector<uint8_t> msg = utils::wstring_convert_to_bytes(L"I'm slave message.");
			slave_ptr->send(msg, response);
			std::wcout << L"master response is:" << utils::wstring_convert_from_bytes(response) << std::endl;
		}

		std::wcout << L"*** sleeping ***" << std::endl;

		::Sleep(5000);
		//WaitForDebugger();

		std::wcout << L"*** stopping ***" << std::endl;

		slave_ptr->stop();
	}

    return 0;
}

