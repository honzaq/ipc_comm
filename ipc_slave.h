#pragma once

#include <windows.h>
#include "ipc_data.h"
#include "ipc_slave_intf.h"
#include "ipc_common.h"

namespace ipc {

class slave 
	: public slave_intf
	, protected common
{
public:
	slave(logger_ptr logger, client_connection& connection, message_callback_fn callback_fn);

	struct factory {
		virtual std::shared_ptr<slave_intf> create_slave(logger_ptr logger, client_connection& connection, message_callback_fn callback_fn) const;
	};

	//! \copydoc slave_intf::send
	bool send(std::vector<uint8_t>& message, std::vector<uint8_t>& response) override;
	//! \copydoc slave_intf::stop
	void stop() override;
};

} // end of namespace ipc
