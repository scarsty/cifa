#pragma once
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stacktrace>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <crtdbg.h>
#endif

// Keep failure reporting local to test and benchmark processes.
inline void configure_test_process()
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
    _CrtSetReportHook([](int type, char* message, int*) -> int {
        std::fputs(message, stderr);
        if (type != _CRT_WARN) std::_Exit(99);
        return TRUE;
    });
#endif
#endif
    std::set_terminate([] {
        std::cerr << "Unhandled test failure\n" << std::stacktrace::current() << '\n';
        std::_Exit(98);
    });
}
