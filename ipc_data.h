#pragma once

#include <stdint.h>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace ipc {

//////////////////////////////////////////////////////////////////////////
// Simple unique generator
namespace message_id {

static std::atomic<uint32_t> g_uniq_id = 0;
static inline uint32_t new_id() { 
	return ++g_uniq_id;
}

} // end of message_id namespace
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// Communication header and all around it
constexpr uint32_t HEADER_FLAG_SYSTEM_MSG        = 0x00;
constexpr uint32_t HEADER_FLAG_USER_MSG          = 0x01;
constexpr uint32_t HEADER_FLAG_USER_MSG_RESPONSE = 0x02;

struct header {
	uint32_t id = 0;                         // Message has same ID as header
	uint32_t flags = HEADER_FLAG_SYSTEM_MSG; // Flags
	uint32_t message_size = 0;               // Following message size (so we know how much we can allocate)
};
//////////////////////////////////////////////////////////////////////////

struct client_connection {
	HANDLE read_pipe = nullptr;
	HANDLE write_pipe = nullptr;
};

//////////////////////////////////////////////////////////////////////////

using message_callback_fn = std::function<void(const std::vector<uint8_t>& message, std::vector<uint8_t>& response)>;

//////////////////////////////////////////////////////////////////////////
// Message
class message
{
public:
	message(uint32_t id) {
		m_id    = id;
	}
	uint32_t id() const {
		return m_id;
	}
private:
	uint32_t m_id = 0;
};

class response_message : public message
{
public:
	response_message(uint32_t id)
		: message(id)
	{
		m_event = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if(m_event) {
			std::exception("Create event fail");
		}
	}
	~response_message() {
		::CloseHandle(m_event);
		m_event = nullptr;
	}

	HANDLE event() const {
		return m_event;
	}
	std::vector<uint8_t> response_buffer() const {
		return m_response_buffer;
	}

	void set_response(std::vector<uint8_t>& data) {
		m_response_buffer = std::move(data);
	}

private:
	uint32_t m_id = 0;
	HANDLE m_event = nullptr;
	std::vector<uint8_t> m_response_buffer;
};

typedef std::map<uint32_t, std::shared_ptr<response_message>> pending_msg_map;

} // end of namespace ipc