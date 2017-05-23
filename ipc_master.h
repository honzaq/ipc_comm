#pragma once

#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include "ipc_data.h"
#include <mutex>
#include "ipc_master_intf.h"

namespace ipc {

class master : public master_intf
{
public:
	master(message_callback_fn fn_callback);
	~master();

	struct factory {
		virtual std::shared_ptr<master_intf> create_master(message_callback_fn fn_callback) const;
	};

	//! \copydoc master_intf::send
	bool send(std::vector<uint8_t>& message, std::vector<uint8_t>& response) override;
	//! \copydoc master_intf::cmd_pipe_params
	std::wstring cmd_pipe_params() override;
	//! \copydoc master_intf::slave_started
	void slave_started();
	//! \copydoc master_intf::stop
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
	std::atomic_bool m_comm_started = false;
	HANDLE m_shutdown_event = nullptr;
	std::function<void(const std::vector<uint8_t>& message, std::vector<uint8_t>& response)> m_callback;

	client_data m_master;
	client_data m_slave;

	HANDLE m_read_thread = nullptr;
	pending_msg_map m_pending_msgs;
};

} // end of namespace ipc