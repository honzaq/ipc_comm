#include "stdafx.h"
#include "comm.h"

comm::comm()
{

}

void comm::start()
{
	// Set the bInheritHandle flag so pipe handles are inherited.
	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	HANDLE g_hChildStd_OUT_Rd = NULL;
	HANDLE g_hChildStd_OUT_Wr = NULL;

	// Create a pipe for the child process's STDOUT. 
	if(!::CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0))
		ErrorExit(TEXT("StdoutRd CreatePipe"));
	
	// Ensure the read handle to the pipe for STDOUT is not inherited.
	if(!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
		ErrorExit(TEXT("Stdout SetHandleInformation"));


}

void comm::stop()
{

}

void comm::send()
{

}

void comm::onmessage()
{

}

std::wstring comm::cmd_params()
{

}