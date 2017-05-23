#pragma once

#include <string>
#include <vector>

namespace ipc {

class master_intf
{
public:
	virtual bool send(std::vector<uint8_t>& message, std::vector<uint8_t>& response) = 0;
	virtual std::wstring cmd_pipe_params() = 0;
	virtual void slave_started() = 0;
	virtual void stop() = 0;
};

} // end of namespace ipc