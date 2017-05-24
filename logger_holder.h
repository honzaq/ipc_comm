#pragma once

#include <memory>
#include "spdlog/spdlog.h"

namespace ipc {

using logger_ptr = std::shared_ptr<spdlog::logger>;

class logger_holder
{
public:
	logger_holder(logger_ptr logger)
		: m_logger(logger)
	{}
	auto logger() const {
		return m_logger;
	}
private:
	logger_ptr m_logger;
};

} // end of namespace ipc
