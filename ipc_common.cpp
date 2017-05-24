#include "stdafx.h"
#include "ipc_common.h"
#include "scope_guard.h"
#include "convert.h"

namespace ipc {

common::common(logger_ptr logger, message_callback_fn callback_fn)
	: logger_holder(logger)
	, m_callback_fn(callback_fn)
{
	m_shutdown_event = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if(!m_shutdown_event) {
		throw std::runtime_error(utils::win32_error_to_ansi(::GetLastError()));
	}
}

common::~common()
{
	try {
		release();
	} catch(...) {}
}

void common::release()
{
	close_communication();

	// Wait for thread
	if(m_read_thread) {
		if(::WaitForSingleObject(m_read_thread, INFINITE) == WAIT_TIMEOUT) {
			::TerminateThread(m_read_thread, 1);
		}
		::CloseHandle(m_read_thread);
		m_read_thread = nullptr;
	}

	// Write-pipe already closed in (close_communication), so close also read-pipe
	if(m_connection.read_pipe) {
		::CloseHandle(m_connection.read_pipe);
		m_connection.read_pipe = nullptr;
	}

	// Close event
	if(m_shutdown_event) {
		::CloseHandle(m_shutdown_event);
		m_shutdown_event = nullptr;
	}
}

void common::start_communication(client_connection& connection)
{
	if(!m_comm_running) {
		m_connection = connection;

		DWORD thread_id = 0;
		m_read_thread = ::CreateThread(nullptr, 0, &common::read_thread_win_proc, this, 0, &thread_id);
		if(!m_read_thread) {
			throw std::runtime_error(utils::win32_error_to_ansi(::GetLastError()));
		}
		m_comm_running = true;
	}
}

void common::close_communication()
{
	m_comm_running = false;

	// Set shutdown event (will stop all response wait)
	if(m_shutdown_event != nullptr) {
		::SetEvent(m_shutdown_event);
	}

	{
		std::lock_guard<std::mutex> one_send_guard(m_write_lock);
		// Close our write pipe (this will abort ReadFile on other side and the other side must also close write pipe)
		if(m_connection.write_pipe) {
			::CloseHandle(m_connection.write_pipe);
			m_connection.write_pipe = nullptr;
		}
	}
}

#pragma region Send
bool common::send(std::vector<uint8_t>& message, std::vector<uint8_t>& response)
{
	// Add message info to map for response wait
	std::shared_ptr<ipc::response_message> new_msg = std::make_shared<ipc::response_message>(message_id::new_id());
	m_pending_send_msgs.insert(std::make_pair(new_msg->id(), new_msg));
	// Create guard to auto remove message from map
	utils::scope_guard guard = [&]() {
		m_pending_send_msgs.erase(new_msg->id());
	};

	DWORD written_bytes = 0;
	//////////////////////////////////////////////////////////////////////////
	// Only WriteFile at the time
	{
		std::lock_guard<std::mutex> one_send_guard(m_write_lock);
		if(!m_comm_running) return false;

		//////////////////////////////////////////////////////////////////////////
		// Write header
		auto header_data = std::make_unique<header>();
		header_data->id = new_msg->id();
		header_data->flags = HEADER_FLAG_USER_MSG;
		header_data->message_size = message.size();

		if(!::WriteFile(m_connection.write_pipe, header_data.get(), sizeof(ipc::header), &written_bytes, nullptr)) {
			logger()->error("Write header fail: {}", ::GetLastError());
			return false;
		}

		//////////////////////////////////////////////////////////////////////////
		// Write message
		if(message.size()) {
			if(!::WriteFile(m_connection.write_pipe, message.data(), (DWORD)message.size(), &written_bytes, nullptr)) {
				logger()->error("Write message fail: {}", ::GetLastError());
				return false;
			}
		}
	}

	// Wait for response
	HANDLE wait_handles[2] = {0};
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
		response = m_pending_send_msgs[new_msg->id()]->response_buffer();
		break;
	default: // error
		std::exception("Wait for message response fail");
		return false;
	}

	return true;
}


bool common::send_response(std::shared_ptr<ipc::header> header, std::vector<uint8_t>& response)
{
	DWORD written_bytes = 0;
	//////////////////////////////////////////////////////////////////////////
	// Only WriteFile at the time
	{
		std::lock_guard<std::mutex> one_send_guard(m_write_lock);
		if(!m_comm_running) return false;

		//////////////////////////////////////////////////////////////////////////
		// Write header
		header->flags = HEADER_FLAG_USER_MSG_RESPONSE;
		header->message_size = response.size();

		if(!::WriteFile(m_connection.write_pipe, header.get(), sizeof(ipc::header), &written_bytes, nullptr)) {
			logger()->error("Write header fail: {}", ::GetLastError());
			return false;
		}

		//////////////////////////////////////////////////////////////////////////
		// Write message
		if(!::WriteFile(m_connection.write_pipe, response.data(), (DWORD)response.size(), &written_bytes, nullptr)) {
			logger()->error("Write message fail: {}", ::GetLastError());
			return false;
		}
	}

	return true;
}
#pragma endregion Send

#pragma region Read
DWORD WINAPI common::read_thread_win_proc(LPVOID lpParameter)
{
	if(!lpParameter) {
		return 0;
	}
	common* class_ptr = reinterpret_cast<common*>(lpParameter);
	return class_ptr->read_thread();
}

DWORD common::read_thread()
{
	DWORD read_bytes = 0;

	while(true) {

		auto header_data = std::make_unique<header>();

		//////////////////////////////////////////////////////////////////////////
		// Read HEADER
		if(!::ReadFile(m_connection.read_pipe, header_data.get(), sizeof(header), &read_bytes, nullptr)) {
			DWORD last_error = ::GetLastError();
			if(last_error == ERROR_BROKEN_PIPE || last_error == ERROR_PIPE_NOT_CONNECTED || last_error == ERROR_INVALID_HANDLE) {
				// ERROR_BROKEN_PIPE write handle closed died
				// ERROR_PIPE_NOT_CONNECTED master died
				// ERROR_INVALID_HANDLE close read handle
				logger()->info("Pipe disconnected. {:d}", last_error);
				break;
			} else {
				logger()->error("Read pipe fail {:d}", last_error);
				break;
			}
		}

		if(read_bytes != sizeof(header)) {
			logger()->error("Invalid header size ({:d} != {:d}", read_bytes, sizeof(header));
			continue;
		}

		logger()->debug("Header received id:{:d}, flags:{:x}, message_size:{:d}", header_data->id, header_data->flags, header_data->message_size);

		std::vector<uint8_t> message(header_data->message_size);
		if(header_data->message_size > 0) {

			//////////////////////////////////////////////////////////////////////////
			// Read MESSAGE
			read_bytes = 0;
			if(!::ReadFile(m_connection.read_pipe, message.data(), header_data->message_size, &read_bytes, nullptr)) {
				DWORD last_error = ::GetLastError();
				if(last_error == ERROR_BROKEN_PIPE || last_error == ERROR_PIPE_NOT_CONNECTED || last_error == ERROR_INVALID_HANDLE) {
					// ERROR_BROKEN_PIPE write handle closed died
					// ERROR_PIPE_NOT_CONNECTED master died
					// ERROR_INVALID_HANDLE close read handle
					logger()->info("Pipe disconnected. {:d}", last_error);
					break;
				} else {
					logger()->error("Read pipe fail {:d}", last_error);
					break;
				}
			}

			logger()->debug("Message received '{}'", std::string(message.begin(), message.end()));
		}

		if(header_data->flags == HEADER_FLAG_USER_MSG_RESPONSE) {

			auto item = m_pending_send_msgs.find(header_data->id);
			if(item != m_pending_send_msgs.end()) {
				item->second->set_response(message);
				::SetEvent(item->second->event());
			}
		} else if(header_data->flags == HEADER_FLAG_USER_MSG) {
			// Call callback and send response
			std::vector<uint8_t> response;
			if(m_callback_fn) {
				m_callback_fn(message, response);
			}
			send_response(std::move(header_data), response);
		}
	}

	// We must close communication. Most important is write-pipe, because we do not want block other side. Once we close it other side will do the same.
	close_communication();

	return 0;
}
#pragma endregion Read

} // end of namespace ipc