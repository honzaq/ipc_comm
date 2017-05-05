// ipc_comm.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "cmdp.h"

int wmain(int argc, wchar_t *argv[])
{
	cmdp::parser cmdp(argc, argv);

	if(cmdp[L"s"] || cmdp[L"server"]) {

	}
	else if(cmdp[L"c"] || cmdp[L"client"]) {

	}

    return 0;
}

