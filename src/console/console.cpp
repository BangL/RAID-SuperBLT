#include <windows.h>
#include <conio.h>
#include <FCNTL.H>
#include <io.h>
#include "console.h"
#include <iostream>

namespace
{
	class outbuf : public std::streambuf
	{
	public:
		outbuf()
		{
			setp(0, 0);
		}

		virtual int_type overflow(int_type c = traits_type::eof()) override
		{
			return fputc(c, stdout) == EOF ? traits_type::eof() : c;
		}
	};

	outbuf obuf;
	std::streambuf *sb = nullptr;
}
static BOOL WINAPI MyConsoleCtrlHandler(DWORD dwCtrlEvent)
{
	return dwCtrlEvent == CTRL_C_EVENT;
}

CConsole::CConsole() : m_OwnConsole(false)
{
	if (!AllocConsole()) return;

	SetConsoleCtrlHandler(MyConsoleCtrlHandler, TRUE);
	RemoveMenu(GetSystemMenu(GetConsoleWindow(), FALSE), SC_CLOSE, MF_BYCOMMAND);

	FILE *tmp;
	freopen_s(&tmp, "conout$", "w", stdout);
	freopen_s(&tmp, "conout$", "w", stderr);
	freopen_s(&tmp, "CONIN$", "r", stdin);

	// Redirect std::cout to the same location as stdout, otherwise you it won't appear on the console.
	sb = std::cout.rdbuf(&obuf);

	m_OwnConsole = true;
}

void CConsole::Close(bool withPostMessage) const
{
	if (m_OwnConsole)
	{
		HWND hWnd = GetConsoleWindow();
		std::cout.rdbuf(sb);
		SetConsoleCtrlHandler(MyConsoleCtrlHandler, FALSE);
		FreeConsole();
		if (withPostMessage)
			PostMessage(hWnd, WM_CLOSE, 0, 0);
	}
}

CConsole::~CConsole()
{
	Close(false);
}