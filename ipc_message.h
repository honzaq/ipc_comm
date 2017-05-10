#pragma once

#include <stdint.h>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace ipc_comm {

//////////////////////////////////////////////////////////////////////////
// Simple unique generator
namespace message_id {
static std::atomic<uint32_t> g_uniq_id = 0;
uint32_t new_id() { return ++g_uniq_id; }
} // end of namespace internal
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// Message
class message
{
public:
	message() {
		m_id    = message_id::new_id();
		m_event = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if(m_event) {
			std::exception("Create event fail");
		}
	}
	~message() {
		::CloseHandle(m_event);
		m_event = nullptr;
	}

	uint32_t id() const {
		return m_id;
	}
	HANDLE event() const {
		return m_event;
	}
	std::vector<uint8_t> response_buffer() const {
		return m_response_buffer;
	}

private:
	uint32_t m_id = 0;
	HANDLE   m_event = nullptr;
	std::vector<uint8_t> m_response_buffer;
};

typedef std::map<uint32_t, std::shared_ptr<message>> pending_msg_map;

} // end of namespace ipc_comm