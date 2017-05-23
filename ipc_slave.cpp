#include "stdafx.h"
#include "ipc_slave.h"
#include <sstream>
#include "scope_guard.h"
#include <iostream>
#include <string>
#include "convert.h"

namespace ipc {

std::shared_ptr<slave_intf> slave::factory::create_slave(HANDLE read_pipe, HANDLE write_pipe, message_callback_fn fn_callback) const
{
	std::shared_ptr<slave> impl = std::make_shared<slave>(read_pipe, write_pipe, fn_callback);
	impl->initialize();
	return impl;
}

slave::slave(HANDLE read_pipe, HANDLE write_pipe, message_callback_fn fn_callback)
	: m_callback(fn_callback)
{
	if(read_pipe == nullptr) {
		std::exception("Invalid read pipe handle");
	}
	if(write_pipe == nullptr) {
		std::exception("Invalid write pipe handle");
	}
	m_slave.read_pipe = read_pipe;
	m_slave.write_pipe = write_pipe;
}

slave::~slave()
{
	try {
		stop();
	} catch(...) {}
}

void slave::initialize()
{
	if(!m_shutdown_event) {
		m_shutdown_event = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
	}

	// Create guard for proper uninitialization (when initialization fail)
	utils::scope_guard file_guard = [&]() {
		stop();
	};

	DWORD thread_id = 0;
	m_read_thread = ::CreateThread(nullptr, 0, &slave::read_thread_proc, this, 0, &thread_id);
	if(!m_read_thread) {
		std::exception("Create read thread fail");
	}

	// All create OK, do not call guard on exit
	file_guard.dismiss();
}

void slave::release()
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

	if(m_slave.read_pipe) {
		::CloseHandle(m_slave.read_pipe);
		m_slave.read_pipe = nullptr;
	}

	// Close event
	if(m_shutdown_event) {
		::CloseHandle(m_shutdown_event);
		m_shutdown_event = nullptr;
	}
}

void slave::close_comm()
{
	// Set shutdown event (will stop all response wait)
	if(m_shutdown_event != nullptr) {
		::SetEvent(m_shutdown_event);
	}

	{
		std::lock_guard<std::mutex> one_send_guard(m_write_lock);
		if(m_slave.write_pipe) {
			::CloseHandle(m_slave.write_pipe);
			m_slave.write_pipe = nullptr;
		}
	}
}

void slave::stop()
{
	close_comm();
}

bool slave::send(std::vector<uint8_t>& message, std::vector<uint8_t>& response)
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

		if(!::WriteFile(m_slave.write_pipe, header_data.get(), sizeof(ipc::header), &written_bytes, nullptr)) {
			std::wcout << L"Write header fail: " << ::GetLastError() << std::endl;
			return false;
		}

		//////////////////////////////////////////////////////////////////////////
		// Write message
		if(message.size()) {
			if(!::WriteFile(m_slave.write_pipe, message.data(), (DWORD)message.size(), &written_bytes, nullptr)) {
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

bool slave::send_response(std::shared_ptr<ipc::header> header, std::vector<uint8_t>& response)
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

		if(!::WriteFile(m_slave.write_pipe, header.get(), sizeof(ipc::header), &written_bytes, nullptr)) {
			std::wcout << L"Write header fail: " << ::GetLastError() << std::endl;
			return false;
		}

		//////////////////////////////////////////////////////////////////////////
		// Write message
		if(!::WriteFile(m_slave.write_pipe, response.data(), (DWORD)response.size(), &written_bytes, nullptr)) {
			std::wcout << L"Write message fail: " << ::GetLastError() << std::endl;
			return false;
		}
	}

	return true;
}

void slave::onmessage(std::shared_ptr<ipc::header> header, std::vector<uint8_t>& message)
{
	std::vector<uint8_t> response;
	if(m_callback) {
		m_callback(message, response);
	}
	send_response(header, response);
}

DWORD WINAPI slave::read_thread_proc(LPVOID lpParameter)
{
	if(!lpParameter) {
		std::wcout << L"Invalid thread procedure parameter (NULL)" << std::endl;
		return 0;
	}
	slave* class_ptr = reinterpret_cast<slave*>(lpParameter);
	return class_ptr->read_thread();
}

DWORD slave::read_thread()
{
	auto header_data = std::make_unique<header>();
	auto header_size = sizeof(header);

	DWORD read_bytes = 0;

	while(true) {

		//////////////////////////////////////////////////////////////////////////
		// Read HEADER
		if(!::ReadFile(m_slave.read_pipe, header_data.get(), header_size, &read_bytes, nullptr)) {
			DWORD last_error = ::GetLastError();
			std::wcout << L"Error read pipe: " << std::dec << last_error << std::endl;
			if(last_error == ERROR_BROKEN_PIPE || last_error == ERROR_PIPE_NOT_CONNECTED) {
				// ERROR_BROKEN_PIPE write handle closed died
				// ERROR_PIPE_NOT_CONNECTED master died

				break;
			} else {
				// ERROR_INVALID_HANDLE (can happens if we close read handle to end this thread)
				break;
			}
			// ERROR_INSUFFICIENT_BUFFER 
		}

		if(read_bytes != header_size) {
			std::wcout << L"Header different size (" << read_bytes << L" < " << header_size << L")" << std::endl;
			continue;
		}

		std::wcout << L"DBG: header arrive:" << L"id:" << header_data->id << L" flags:" << header_data->flags << L" message_size:" << header_data->message_size << std::endl;

		std::vector<uint8_t> message(header_data->message_size);
		if(header_data->message_size > 0) {

			//////////////////////////////////////////////////////////////////////////
			// Read MESSAGE
			read_bytes = 0;
			if(!::ReadFile(m_slave.read_pipe, message.data(), header_data->message_size, &read_bytes, nullptr)) {
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
				item->second->set_response(message);
				::SetEvent(item->second->event());
			}
		} else if(header_data->flags == HEADER_FLAG_USER_MSG) {
			onmessage(std::move(header_data), message);
		}
	}

	close_comm();

	return 0;
}


} // end of namespace ipc