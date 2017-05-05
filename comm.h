#pragma once

#include <windows.h>
#include <string>

class comm
{
public:
	comm();

	void start();
	void stop();

	void send();
	void onmessage();

	std::wstring cmd_params();
};