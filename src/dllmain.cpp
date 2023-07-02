// dllmain.cpp : Defines the entry point for the DLL application.
#include <windows.h>

#include "aixlog.hpp"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved);
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
  switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
      AixLog::Log::init<AixLog::SinkFile>(AixLog::Severity::info,
                                          CURRENT_SOURCE_DIR "/log.txt");
      break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
      break;
  }

  return TRUE;
}
