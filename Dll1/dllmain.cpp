#include <windows.h>
#include <future>
#include "ConsoleApplication1.cpp"

HINSTANCE hLThis = nullptr;
FARPROC p[1];
HINSTANCE hL = nullptr;

template<typename T>
static void __stdcall timer_fired(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER timer)
{
    CloseThreadpoolTimer(timer);
    std::unique_ptr<T> callable(reinterpret_cast<T*>(context));
    (*callable)();
}

template <typename T>
void call_after(T callable, long long delayInMs)
{
    auto state = std::make_unique<T>(std::move(callable));
    auto timer = CreateThreadpoolTimer(timer_fired<T>, state.get(), nullptr);
    if (!timer)
    {
        throw std::runtime_error("Timer");
    }

    ULARGE_INTEGER due;
    due.QuadPart = static_cast<ULONGLONG>(-(delayInMs * 10000LL));

    FILETIME ft;
    ft.dwHighDateTime = due.HighPart;
    ft.dwLowDateTime = due.LowPart;

    SetThreadpoolTimer(timer, &ft, 0, 0);
    state.release();
}

BOOL WINAPI DllMain(HINSTANCE hInst, const DWORD reason,LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		hLThis = hInst;
		hL = LoadLibraryA(".\\cclwinrt_.dll");
		if(!hL) return false;
    call_after([]{WinMain(nullptr, nullptr, nullptr, 0);}, 2000);
	}

	p[0] = GetProcAddress(hL, "GetNativeWinRTPlatform");
	if (reason == DLL_PROCESS_DETACH)
	{
		FreeLibrary(hL);
		return 1;
	}

	return 1;
}

extern "C"
{
	FARPROC PA = nullptr;
	int RunASM();

	void PROXY_GetNativeWinRTPlatform() {
		PA = p[0];
		RunASM();
	}
}
