#include "stdafx.h"
#include "ipc_master.h"
#include <sstream>
#include "scope_guard.h"
#include <iostream>
#include <string>
#include "convert.h"

namespace ipc {

std::shared_ptr<master_intf> master::factory::create_master(logger_ptr logger, message_callback_fn callback_fn) const
{
	std::shared_ptr<master> impl = std::make_shared<master>(logger, callback_fn);
	impl->initialize();
	return impl;
}

master::master(logger_ptr logger, message_callback_fn callback_fn)
	: common(logger, callback_fn)
{
}

master::~master()
{
	try {
		release();
	} catch (...) {}
}

void master::initialize()
{
	// Create guard for proper uninitialization (when initialization fail)
	utils::scope_guard file_guard = [&]() {
		release();
	};

	// Set the bInheritHandle flag so pipe handles are inherited.
	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	// Create Master->Slave direction (master write, slave read)
	if(!::CreatePipe(&m_slave.read_pipe, &m_master.write_pipe, &saAttr, 0)) {
		std::exception("Create pipe fail");
	}
	// Ensure the master-write handle to the pipe is not inherited.
	if(!::SetHandleInformation(m_master.write_pipe, HANDLE_FLAG_INHERIT, 0)) {
		std::exception("SetHandleInformation fail");
	}

	// Create Master<-Slave direction (Slave write, master read)
	if(!::CreatePipe(&m_master.read_pipe, &m_slave.write_pipe, &saAttr, 0)) {
		std::exception("Create pipe fail");
	}
	// Ensure the master-read handle to the pipe is not inherited. 
	if(!::SetHandleInformation(m_master.read_pipe, HANDLE_FLAG_INHERIT, 0)) {
		std::exception("SetHandleInformation fail");
	}

	logger()->debug("Handles (slave):{:x}{:x}, (master):{:x}{:x}", m_slave.read_pipe, m_slave.write_pipe, m_master.read_pipe, m_master.write_pipe);

	// All create OK, do not call guard on exit
	file_guard.dismiss();
}

void master::release()
{
	if(m_master.read_pipe) {
		::CloseHandle(m_master.read_pipe);
		m_master.read_pipe = nullptr;
	}
	if(m_master.write_pipe) {
		::CloseHandle(m_master.write_pipe);
		m_master.write_pipe = nullptr;
	}
	if(m_slave.read_pipe) {
		::CloseHandle(m_slave.read_pipe);
		m_slave.read_pipe = nullptr;
	}
	if(m_slave.write_pipe) {
		::CloseHandle(m_slave.write_pipe);
		m_slave.write_pipe = nullptr;
	}
}

void master::stop()
{
	close_communication();
}

void master::start()
{
	// Close slave pipes, so we are able detect end of the slave process (ReadFile will end with ERROR_INVALID_HANDLE)
	if(m_slave.read_pipe) {
		::CloseHandle(m_slave.read_pipe);
		m_slave.read_pipe = nullptr;
	}
	if(m_slave.write_pipe) {
		::CloseHandle(m_slave.write_pipe);
		m_slave.write_pipe = nullptr;
	}
	start_communication(m_master);
	// After start clear our connection we held, common now hold this for us, and we do not want release it multile times
	m_master.read_pipe = nullptr;
	m_master.write_pipe = nullptr;
}

bool master::send(std::vector<uint8_t>& message, std::vector<uint8_t>& response)
{
	return common::send(message, response);
}

std::wstring master::cmd_pipe_params()
{
	std::wstringstream cmd_param;
	// Because PIPE "IDs" are HEXa numbers we must pass HEXa number (so slave can open (find) the PIPE)
	cmd_param << L"/pipe-slave" << L" " << L"/pipe-r=" << std::hex << reinterpret_cast<std::size_t>(m_slave.read_pipe) << L" /pipe-w=" << std::hex << reinterpret_cast<std::size_t>(m_slave.write_pipe);
	return cmd_param.str();
}

} // end of namespace ipc