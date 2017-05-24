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

void start_slave(const wchar_t* params, ipc::logger_ptr logger)
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
	logger->info("Starting child process '{}' with params'{}'.", utils::to_utf8(szPath), utils::to_utf8(cmdLine));

	if(!::CreateProcessW(szPath, cmdLine, NULL, NULL, TRUE, CREATE_NEW_CONSOLE/*spise 0*/, NULL, NULL, &siStartInfo, &piProcInfo)) {
		logger->error("Create child process fail");
	}
}

constexpr int msg_send_count = 3;

int wmain(int argc, wchar_t *argv[])
{
	// Console logger with color
	auto logger = spdlog::stdout_color_mt("console");

	cmdp::parser cmdp(argc, argv);

	if(cmdp[L"pipe-master"]) {
		logger->info("Hello I'm your MASTER!");

		ipc::master::factory master_factory;
		std::shared_ptr<ipc::master_intf> master_ptr = master_factory.create_master(logger, [&](const std::vector<uint8_t>& message, std::vector<uint8_t>& response) {
			logger->info("OnMessage(master): '{}'", std::string(message.begin(), message.end()));
			response = utils::wstring_convert_to_bytes(L"I'm master response.");
		});

		start_slave(master_ptr->cmd_pipe_params().c_str(), logger);

		master_ptr->start();
	
		for(int i = 0; i < msg_send_count; i++) {
			std::vector<uint8_t> response;
			std::vector<uint8_t> msg = utils::wstring_convert_to_bytes(L"I'm master message.");
			master_ptr->send(msg, response);
			logger->info("Response is '{}'", std::string(response.begin(), response.end()));
		}

		::Sleep(15000);

		master_ptr->stop();
	}
	else if(cmdp[L"pipe-slave"]) {

		ipc::client_connection connection;
		cmdp(L"pipe-r") >> connection.read_pipe;
		cmdp(L"pipe-w") >> connection.write_pipe;

		logger->info("Hello I'm your SLAVE (read-pipe:{}, write-pipe:{})", connection.read_pipe, connection.write_pipe);

		ipc::slave::factory slave_factory;
		std::shared_ptr<ipc::slave_intf> slave_ptr = slave_factory.create_slave(logger, connection, [&](const std::vector<uint8_t>& message, std::vector<uint8_t>& response) {
			logger->info("OnMessage(slave): '{}'", std::string(message.begin(), message.end()));
			response = utils::wstring_convert_to_bytes(L"I'm slave response.");
		});

		for(int i = 0; i < msg_send_count; i++) {
			std::vector<uint8_t> response;
			std::vector<uint8_t> msg = utils::wstring_convert_to_bytes(L"I'm slave message.");
			slave_ptr->send(msg, response);
			logger->info("Response is '{}'", std::string(response.begin(), response.end()));
		}

		::Sleep(15000);

		slave_ptr->stop();
	}

    return 0;
}

