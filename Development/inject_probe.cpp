#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
#include <cstdio>

int wmain(int argc, wchar_t **argv) {
  if (argc != 3) {
    std::fwprintf(stderr, L"usage: inject-probe.exe PID DLL\n");
    return 2;
  }
  const DWORD pid = static_cast<DWORD>(std::wcstoul(argv[1], nullptr, 10));
  const size_t bytes = (std::wcslen(argv[2]) + 1) * sizeof(wchar_t);
  HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                   PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                   PROCESS_VM_READ,
                               FALSE, pid);
  if (!process)
    return 3;
  void *remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                PAGE_READWRITE);
  if (!remote || !WriteProcessMemory(process, remote, argv[2], bytes, nullptr))
    return 4;
  auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
      GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
  HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0,
                                     nullptr);
  if (!thread)
    return 5;
  WaitForSingleObject(thread, 10000);
  DWORD result = 0;
  GetExitCodeThread(thread, &result);
  CloseHandle(thread);
  VirtualFreeEx(process, remote, 0, MEM_RELEASE);
  CloseHandle(process);
  std::printf("LoadLibrary result=0x%lx\n", result);
  return result ? 0 : 6;
}
