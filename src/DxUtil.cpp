#include "DxUtil.h"

#include <sstream>

std::wstring HrToString(HRESULT hr)
{
    std::wstringstream ss;
    ss << L"0x" << std::hex << (unsigned long)hr;
    return ss.str();
}

void DebugPrint(const std::wstring& msg)
{
    OutputDebugStringW(msg.c_str());
    OutputDebugStringW(L"\n");
}

