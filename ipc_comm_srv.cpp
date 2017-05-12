#include "stdafx.h"
#include "ipc_comm_srv.h"
#include <sstream>
#include "scope_guard.h"
#include <iostream>
#include <codecvt>
#include <string>

namespace ipc_comm {

server::server()
{

}

server::~server()
{
	try {
		stop();
	} catch (...) {}
}

void server::start()
{
	if(!m_shutdown_event) {
		m_shutdown_event = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
	}

	scope_guard file_guard = [&]() {
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
	m_read_thread = ::CreateThread(nullptr, 0, &server::read_thread, this, 0, &thread_id);
	if(!m_read_thread) {
		std::exception("Create read thread fail");
	}

	// All create OK, do not call guard on exit
	file_guard.dismiss();
}

void server::stop()
{
	::SetEvent(m_shutdown_event);

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
	if(m_slave.write_pipe) {
		::CloseHandle(m_slave.write_pipe);
		m_slave.write_pipe = nullptr;
	}
	if(m_master.read_pipe) {
		::CloseHandle(m_master.read_pipe);
		m_master.read_pipe = nullptr;
	}
	if(m_master.write_pipe) {
		::CloseHandle(m_master.write_pipe);
		m_master.write_pipe = nullptr;
	}

	if(m_shutdown_event) {
		::CloseHandle(m_shutdown_event);
		m_shutdown_event = nullptr;
	}
}

void server::close_slave_handles()
{
	if(m_slave.read_pipe) {
		::CloseHandle(m_slave.read_pipe);
		m_slave.read_pipe = nullptr;
	}
	if(m_slave.write_pipe) {
		::CloseHandle(m_slave.write_pipe);
		m_slave.write_pipe = nullptr;
	}
}

void server::send(std::vector<uint8_t>& message, std::vector<uint8_t>& response)
{
	std::shared_ptr<ipc_comm::message> new_msg = std::make_shared<ipc_comm::message>();
	m_pending_msgs.insert(std::make_pair(new_msg->id(), new_msg));

	scope_guard guard = [&]() {
		m_pending_msgs.erase(new_msg->id());
	};

	DWORD written_bytes;
	::WriteFile(m_master.write_pipe, message.data(), (DWORD)message.size(), &written_bytes, nullptr);

	// 	bSuccess = ::WriteFile(m_child_read_pipe_wr, chBuf, dwRead, &dwWritten, NULL);
	// 	if(!bSuccess) break;

// 	HANDLE wait_handles[2] = { 0 };
// 	wait_handles[0] = m_shutdown_event;
// 	wait_handles[1] = new_msg->event();
// 	DWORD wait_result = WaitForMultipleObjects(
// 		_countof(wait_handles),   // number of handles in array
// 		wait_handles,             // array of thread handles
// 		FALSE,                    // wait until all are signaled
// 		INFINITE);
// 
// 	switch(wait_result) {
// 	case WAIT_OBJECT_0:	// m_shutdown_event signaled
// 		break; // End 
// 	case WAIT_OBJECT_0 + 1: // new_msg->event signaled
// 		//TODO: handle response
// 		break;
// 	default: // error
// 		std::exception("Wait for message response fail");
// 		break;
// 	}

	response = m_pending_msgs[new_msg->id()]->response_buffer();
}

void server::onmessage()
{

}

std::wstring server::cmd_params()
{
	std::wstringstream cmd_param;
	cmd_param << L"/pipe-slave" << L" " << L"/pipe-r=" << std::hex << reinterpret_cast<std::size_t>(m_slave.read_pipe) << L" /pipe-w=" << std::hex << reinterpret_cast<std::size_t>(m_slave.write_pipe);
	return cmd_param.str();
}

std::wstring wstring_convert_from_bytes(const std::vector<char> &v)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
	return converter.from_bytes(v.data(), v.data() + v.size());
}

DWORD WINAPI server::read_thread(LPVOID lpParameter)
{
	if(!lpParameter) {
		return 0;
	}
	server* class_ptr = reinterpret_cast<server*>(lpParameter);

	std::vector<char> buffer;
	buffer.resize(1000);

	DWORD read_bytes = 0;

	while(true) {
		std::wcout << "*";
		if(!::ReadFile(class_ptr->m_master.read_pipe, buffer.data(), buffer.size(), &read_bytes, nullptr)) {
			DWORD last_error = ::GetLastError();
			std::wcout << L"Error read pipe: " << std::dec << last_error << std::endl;
			if(last_error == ERROR_BROKEN_PIPE) {
				break;
			}
		}
		buffer.resize(read_bytes);
		std::wcout << std::endl << L"message arrive(size): " << wstring_convert_from_bytes(buffer) << std::endl;
		// 	if(!bSuccess || dwRead == 0) break;
		//class_ptr->close_slave_handles();
		
		// Call callbacks
		//class_ptr->onmessage();
	}
}


} // end of namespace ipc_comm