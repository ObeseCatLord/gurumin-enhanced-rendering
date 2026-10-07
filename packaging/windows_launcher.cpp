#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>

// Thin native shell for the SAME Python installer used on Linux. No patch
// offsets, discovery, backup or restoration policies are duplicated here.
static std::wstring quote(const std::wstring &s) {
  std::wstring out=L"\""; unsigned slashes=0;
  for(wchar_t c:s) {
    if(c==L'\\') {++slashes;continue;}
    if(c==L'"') out.append(slashes*2+1,L'\\');
    else out.append(slashes,L'\\');
    slashes=0;out+=c;
  }
  out.append(slashes*2,L'\\');out+=L'"';return out;
}
int wmain(int argc,wchar_t **argv) {
  wchar_t path[32768];DWORD length=GetModuleFileNameW(nullptr,path,32768);
  if(!length || length==32768) return 1;
  std::wstring folder(path,length);folder.resize(folder.find_last_of(L"\\/"));
  auto python=folder+L"\\runtime\\python.exe";
  std::wstring command=quote(python)+L" -I -X utf8 "+quote(folder+L"\\tools\\patch.py");
  bool noPause=false;
  for(int i=1;i<argc;++i) {
    if(std::wstring(argv[i])==L"--no-pause") noPause=true;
    else command+=L" "+quote(argv[i]);
  }
  std::vector<wchar_t> mutableCommand(command.begin(),command.end());mutableCommand.push_back(0);
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
  DWORD result=1;
  if(CreateProcessW(python.c_str(),mutableCommand.data(),nullptr,nullptr,TRUE,0,nullptr,
                    folder.c_str(),&startup,&child)) {
    WaitForSingleObject(child.hProcess,INFINITE);GetExitCodeProcess(child.hProcess,&result);
    CloseHandle(child.hThread);CloseHandle(child.hProcess);
  } else std::fwprintf(stderr,L"Cannot start the patch (Windows error %lu). Extract the ENTIRE ZIP before running it.\n",GetLastError());
  DWORD processes[2];
  if(!noPause && GetConsoleProcessList(processes,2)==1) {
    std::fputs("\nPress Enter to close.\n",stdout);std::getchar();
  }
  return int(result);
}
