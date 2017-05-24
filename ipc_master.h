#pragma once

#include <windows.h>
#include "ipc_data.h"
#include "ipc_master_intf.h"
#include "ipc_common.h"

namespace ipc {

class master 
	: public master_intf
	, protected common
{
public:
	master(logger_ptr logger, message_callback_fn callback_fn);
	~master();

	struct factory {
		virtual std::shared_ptr<master_intf> create_master(logger_ptr logger, message_callback_fn callback_fn) const;
	};

	//! \copydoc master_intf::start
	void start();
	//! \copydoc master_intf::stop
	void stop() override;
	//! \copydoc master_intf::send
	bool send(std::vector<uint8_t>& message, std::vector<uint8_t>& response) override;
	//! \copydoc master_intf::cmd_pipe_params
	std::wstring cmd_pipe_params() override;

private:

	void initialize();
	void release();

private:
	std::atomic_bool m_comm_started = false;

	client_connection m_master;
	client_connection m_slave;
};

} // end of namespace ipc