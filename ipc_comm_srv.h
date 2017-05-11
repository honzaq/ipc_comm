#pragma once

#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include "ipc_message.h"

namespace ipc_comm {

struct client_data {
	HANDLE read_pipe = nullptr;
	HANDLE write_pipe = nullptr;
};

class server
{
public:
	server();
	~server();

	void start();
	void stop();

	void send(std::vector<uint8_t>& message, std::vector<uint8_t>& response);
	void onmessage();

	std::wstring cmd_params();

private:
	void run();

	static DWORD WINAPI read_thread(LPVOID lpParameter);

private:
	HANDLE m_shutdown_event = nullptr;

	client_data m_master;
	client_data m_client;
// 	HANDLE m_child_write_pipe_rd = nullptr;
// 	HANDLE m_child_write_pipe_wr = nullptr;
// 	HANDLE m_child_read_pipe_rd = nullptr;
// 	HANDLE m_child_read_pipe_wr = nullptr;

	HANDLE m_read_thread = nullptr;
	pending_msg_map m_pending_msgs;

	
};

} // end of namespace ipc_comm