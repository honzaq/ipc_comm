#pragma once

#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include "ipc_data.h"
#include <mutex>
#include "ipc_slave_intf.h"

namespace ipc {

class slave : public slave_intf
{
public:
	slave(HANDLE read_pipe, HANDLE write_pipe, message_callback_fn fn_callback);
	~slave();

	struct factory {
		virtual std::shared_ptr<slave_intf> create_slave(HANDLE read_pipe, HANDLE write_pipe, message_callback_fn fn_callback) const;
	};

	//! \copydoc slave_intf::send
	bool send(std::vector<uint8_t>& message, std::vector<uint8_t>& response) override;
	//! \copydoc slave_intf::stop
	void stop() override;

private:

	void initialize();
	void release();
	void close_comm();
	void onmessage(std::shared_ptr<ipc::header> header, std::vector<uint8_t>& message);

	static DWORD WINAPI read_thread_proc(LPVOID lpParameter);
	DWORD read_thread();

	bool send_response(std::shared_ptr<ipc::header> header, std::vector<uint8_t>& response);

private:
	std::mutex m_write_lock;
	HANDLE m_shutdown_event = nullptr;
	std::function<void(const std::vector<uint8_t>& message, std::vector<uint8_t>& response)> m_callback;

	client_data m_slave;

	HANDLE m_read_thread = nullptr;
	pending_msg_map m_pending_msgs;
};

} // end of namespace ipc
