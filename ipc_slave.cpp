#include "stdafx.h"
#include "ipc_slave.h"
#include <sstream>
#include "scope_guard.h"
#include <iostream>
#include <string>
#include "convert.h"

namespace ipc {

std::shared_ptr<slave_intf> slave::factory::create_slave(logger_ptr logger, client_connection& connection, message_callback_fn callback_fn) const
{
	return std::make_shared<slave>(logger, connection, callback_fn);
}

slave::slave(logger_ptr logger, client_connection& connection, message_callback_fn callback_fn)
	: common(logger, callback_fn)
{
	if(connection.read_pipe == nullptr) {
		std::exception("Invalid read pipe handle");
	}
	if(connection.write_pipe == nullptr) {
		std::exception("Invalid write pipe handle");
	}
	start_communication(connection);
}

bool slave::send(std::vector<uint8_t>& message, std::vector<uint8_t>& response)
{
	return common::send(message, response);
}

void slave::stop()
{
	close_communication();
}

} // end of namespace ipc