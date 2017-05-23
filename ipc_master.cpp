#include "stdafx.h"
#include "ipc_master.h"
#include <sstream>
#include "scope_guard.h"
#include <iostream>
#include <string>
#include "convert.h"

namespace ipc {

std::shared_ptr<master_intf> master::factory::create_master(message_callback_fn fn_callback) const
{
	std::shared_ptr<master> impl = std::make_shared<master>(fn_callback);
	impl->initialize();
	return impl;
}

master::master(message_callback_fn fn_callback)
	: m_callback(fn_callback)
{
}

master::~master()
{
	try {
		stop();
	} catch (...) {}
}

void master::initialize()
{
	if(!m_shutdown_event) {
		m_shutdown_event = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
	}

	// Create guard for proper uninitialization (when initialization fail)
	utils::scope_guard file_guard = [&]() {
		stop();
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

	std::wcout << L"Handles slave: " << std::hex << m_slave.read_pipe << L", " << std::hex << m_slave.write_pipe << std::endl;
	std::wcout << L"Handles master: " << std::hex << m_master.read_pipe << L", " << std::hex << m_master.write_pipe << std::endl;

	DWORD thread_id = 0;
	m_read_thread = ::CreateThread(nullptr, 0, &master::read_thread_proc, this, 0, &thread_id);
	if(!m_read_thread) {
		std::exception("Create read thread fail");
	}

	// All create OK, do not call guard on exit
	file_guard.dismiss();
}

void master::release()
{
	close_comm();

	// Wait for thread
	if(m_read_thread) {
		if(::WaitForSingleObject(m_read_thread, INFINITE) == WAIT_TIMEOUT) {
			::TerminateThread(m_read_thread, 1);
		}
		::CloseHandle(m_read_thread);
		m_read_thread = nullptr;
	}

	if(m_master.read_pipe) {
		::CloseHandle(m_master.read_pipe);
		m_master.read_pipe = nullptr;
	}

	// just for sure close also slave pipes
	if(m_slave.read_pipe) {
		::CloseHandle(m_slave.read_pipe);
		m_slave.read_pipe = nullptr;
	}
	if(m_slave.write_pipe) {
		::CloseHandle(m_slave.write_pipe);
		m_slave.write_pipe = nullptr;
	}

	// Close event
	if(m_shutdown_event) {
		::CloseHandle(m_shutdown_event);
		m_shutdown_event = nullptr;
	}
}

void master::close_comm()
{
	// Set shutdown event (will stop all response wait)
	if(m_shutdown_event != nullptr) {
		::SetEvent(m_shutdown_event);
	}

	{
		std::lock_guard<std::mutex> one_send_guard(m_write_lock);
		if(m_master.write_pipe) {
			::CloseHandle(m_master.write_pipe);
			m_master.write_pipe = nullptr;
		}
	}
}

void master::stop()
{
	close_comm();
}

void master::slave_started()
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
}

bool master::send(std::vector<uint8_t>& message, std::vector<uint8_t>& response)
{
	// Add message info to map for response wait
	std::shared_ptr<ipc::response_message> new_msg = std::make_shared<ipc::response_message>(message_id::new_id());
	m_pending_msgs.insert(std::make_pair(new_msg->id(), new_msg));
	// Create guard to auto remove message from map
	utils::scope_guard guard = [&]() {
		m_pending_msgs.erase(new_msg->id());
	};

	DWORD written_bytes = 0;
	//////////////////////////////////////////////////////////////////////////
	// Only WriteFile at the time
	{
		std::lock_guard<std::mutex> one_send_guard(m_write_lock);

		//////////////////////////////////////////////////////////////////////////
		// Write header
		auto header_data = std::make_unique<header>();
		header_data->id = new_msg->id();
		header_data->flags = HEADER_FLAG_USER_MSG;
		header_data->message_size = message.size();

		if(!::WriteFile(m_master.write_pipe, header_data.get(), sizeof(ipc::header), &written_bytes, nullptr)) {
			std::wcout << L"Write header fail: " << ::GetLastError() << std::endl;
			return false;
		}

		//////////////////////////////////////////////////////////////////////////
		// Write message
		if(message.size()) {
			if(!::WriteFile(m_master.write_pipe, message.data(), (DWORD)message.size(), &written_bytes, nullptr)) {
				std::wcout << L"Write message fail: " << ::GetLastError() << std::endl;
				return false;
			}
		}
	}

	// Wait for response
 	HANDLE wait_handles[2] = { 0 };
 	wait_handles[0] = m_shutdown_event;
 	wait_handles[1] = new_msg->event();
 	DWORD wait_result = WaitForMultipleObjects(
 		_countof(wait_handles),   // number of handles in array
 		wait_handles,             // array of thread handles
 		FALSE,                    // wait until all are signaled
 		INFINITE);
 
 	switch(wait_result) {
 	case WAIT_OBJECT_0:	// m_shutdown_event signaled
 		return false; // End 
	case WAIT_OBJECT_0 + 1: // new_msg->event signaled
		response = m_pending_msgs[new_msg->id()]->response_buffer();
 		break;
 	default: // error
 		std::exception("Wait for message response fail");
		return false;
 	}

	return true;
}

bool master::send_response(std::shared_ptr<ipc::header> header, std::vector<uint8_t>& response)
{
	DWORD written_bytes = 0;
	//////////////////////////////////////////////////////////////////////////
	// Only WriteFile at the time
	{
		std::lock_guard<std::mutex> one_send_guard(m_write_lock);

		//////////////////////////////////////////////////////////////////////////
		// Write header
		header->flags = HEADER_FLAG_USER_MSG_RESPONSE;
		header->message_size = response.size();

		if(!::WriteFile(m_master.write_pipe, header.get(), sizeof(ipc::header), &written_bytes, nullptr)) {
			std::wcout << L"Write header fail: " << ::GetLastError() << std::endl;
			return false;
		}

		//////////////////////////////////////////////////////////////////////////
		// Write message
		if(!::WriteFile(m_master.write_pipe, response.data(), (DWORD)response.size(), &written_bytes, nullptr)) {
			std::wcout << L"Write message fail: " << ::GetLastError() << std::endl;
			return false;
		}
	}

	return true;
}

void master::onmessage(std::shared_ptr<ipc::header> header, std::vector<uint8_t>& message)
{
	// header is internal here 
	std::vector<uint8_t> response;
	if(m_callback) {
		m_callback(message, response);
	}
	send_response(header, response);
}

std::wstring master::cmd_pipe_params()
{
	std::wstringstream cmd_param;
	// Because PIPE "IDs" are HEXa numbers we must pass HEXa number (so slave can open (find) the PIPE)
	cmd_param << L"/pipe-slave" << L" " << L"/pipe-r=" << std::hex << reinterpret_cast<std::size_t>(m_slave.read_pipe) << L" /pipe-w=" << std::hex << reinterpret_cast<std::size_t>(m_slave.write_pipe);
	return cmd_param.str();
}

DWORD WINAPI master::read_thread_proc(LPVOID lpParameter)
{
	if(!lpParameter) {
		std::wcout << L"Invalid thread procedure parameter (NULL)" << std::endl;
		return 0;
	}
	master* class_ptr = reinterpret_cast<master*>(lpParameter);
	return class_ptr->read_thread();
}

DWORD master::read_thread()
{
	auto header_size = sizeof(header);

	DWORD read_bytes = 0;

	while(true) {

		auto header_data = std::make_unique<header>();

		//////////////////////////////////////////////////////////////////////////
		// Read HEADER
		if(!::ReadFile(m_master.read_pipe, header_data.get(), header_size, &read_bytes, nullptr)) {
			DWORD last_error = ::GetLastError();
			std::wcout << L"Error read pipe: " << std::dec << last_error << std::endl;
			if(last_error == ERROR_BROKEN_PIPE || last_error == ERROR_PIPE_NOT_CONNECTED) {
				// ERROR_BROKEN_PIPE write handle closed died
				// ERROR_PIPE_NOT_CONNECTED master died
				
				break;
			}
			else {
				// ERROR_INVALID_HANDLE (can happens if we close read handle to end this thread)
				break;
			}
			// ERROR_INSUFFICIENT_BUFFER 
		}

		if(read_bytes != header_size) {
			std::wcout << L"Header different size (" << read_bytes  << L" < " << header_size << L")" << std::endl;
			continue;
		}

		std::wcout << L"DBG: header arrive:" << L"id:" << header_data->id << L" flags:" << header_data->flags << L" message_size:" << std::dec << header_data->message_size << std::endl;

		std::vector<uint8_t> message(header_data->message_size);
		if(header_data->message_size > 0) {

			//////////////////////////////////////////////////////////////////////////
			// Read MESSAGE
			read_bytes = 0;
			if(!::ReadFile(m_master.read_pipe, message.data(), header_data->message_size, &read_bytes, nullptr)) {
				DWORD last_error = ::GetLastError();
				std::wcout << L"Error read pipe: " << std::dec << last_error << std::endl;
				if(last_error == ERROR_BROKEN_PIPE || last_error == ERROR_PIPE_NOT_CONNECTED) {
					// ERROR_BROKEN_PIPE slave died
					// ERROR_PIPE_NOT_CONNECTED master died
					break;
				} else {
					// ERROR_INVALID_HANDLE (can happens if we close read handle to end this thread)
					break;
				}

				// ERROR_INSUFFICIENT_BUFFER 
			}

			std::wcout << L"DBG: message arrive: " << utils::wstring_convert_from_bytes(message) << std::endl;
		}

		if(header_data->flags == HEADER_FLAG_USER_MSG_RESPONSE) {

			auto item = m_pending_msgs.find(header_data->id);
			if(item != m_pending_msgs.end()) {
				std::wcout << L"DBG: response id found, settings event: " << item->second->event() << std::endl;
				item->second->set_response(message);
				::SetEvent(item->second->event());
			}
		}
		else if(header_data->flags == HEADER_FLAG_USER_MSG) {
			onmessage(std::move(header_data), message);
		}
	}

	// Stop communication
	close_comm();

	return 0;
}


} // end of namespace ipc