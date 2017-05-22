#pragma once

#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include "ipc_message.h"
#include <mutex>

namespace ipc {

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

	void post_slave_start();

	bool send(std::vector<uint8_t>& message, std::vector<uint8_t>& response);
	void onmessage(std::shared_ptr<ipc::header> header, std::vector<uint8_t>& message);

	std::wstring cmd_params();

private:
	void run();

	static DWORD WINAPI read_thread_proc(LPVOID lpParameter);
	DWORD read_thread();

	bool send_response(std::shared_ptr<ipc::header> header, std::vector<uint8_t>& response);

private:
	std::mutex m_write_lock;
	HANDLE m_shutdown_event = nullptr;

	client_data m_master;
	client_data m_slave;

	HANDLE m_read_thread = nullptr;
	pending_msg_map m_pending_msgs;

	
};

} // end of namespace ipc