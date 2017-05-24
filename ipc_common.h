#pragma once

#include <windows.h>
#include <mutex>
#include "ipc_data.h"

namespace ipc {

class common : public logger_holder
{
public:
	common(logger_ptr logger, message_callback_fn callback_fn);
	~common();
	
	void start_communication(client_connection& connection);
	void close_communication();

	bool send(std::vector<uint8_t>& message, std::vector<uint8_t>& response);

private:
	void release();

	bool send_response(std::shared_ptr<ipc::header> header, std::vector<uint8_t>& response);

	static DWORD WINAPI read_thread_win_proc(LPVOID lpParameter);
	DWORD read_thread();

private:
	// common
	HANDLE m_shutdown_event = nullptr;
	client_connection m_connection;
	std::atomic_bool m_comm_running = false;

	// write
	std::mutex m_write_lock;
	pending_msg_map m_pending_send_msgs;

	// read
	HANDLE m_read_thread = nullptr;
	message_callback_fn m_callback_fn = nullptr;
};

} // end of namespace ipc