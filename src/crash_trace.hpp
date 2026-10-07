#pragma once
// Local first-chance fault evidence; never consumes or changes an exception.
// Enabled only with Diagnostics, outside loader lock.
static bool readableRange(const void *pointer,size_t length) {
  uintptr_t cursor=uintptr_t(pointer);
  if(!cursor || length>UINTPTR_MAX-cursor) return false;
  uintptr_t end=cursor+length;
  while(cursor<end) {
    MEMORY_BASIC_INFORMATION region{};
    if(!VirtualQuery(reinterpret_cast<void*>(cursor),&region,sizeof(region)) ||
        region.State!=MEM_COMMIT || (region.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
        !(region.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|
            PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
    uintptr_t next=uintptr_t(region.BaseAddress)+region.RegionSize;
    if(next<=cursor) return false;
    cursor=next;
  }
  return true;
}
static LONG CALLBACK traceFault(EXCEPTION_POINTERS *exception) {
  if(!exception || !exception->ExceptionRecord || !exception->ContextRecord)
    return EXCEPTION_CONTINUE_SEARCH;
  DWORD code=exception->ExceptionRecord->ExceptionCode;
  if(code!=EXCEPTION_ACCESS_VIOLATION && code!=EXCEPTION_ILLEGAL_INSTRUCTION &&
      code!=EXCEPTION_ARRAY_BOUNDS_EXCEEDED && code!=EXCEPTION_STACK_OVERFLOW)
    return EXCEPTION_CONTINUE_SEARCH;
  static LONG entered;
  if(InterlockedExchange(&entered,1)) return EXCEPTION_CONTINUE_SEARCH;
  char buffer[4096]; unsigned used=0;
  const auto &c=*exception->ContextRecord;
  auto append=[&](const char *format,auto... args) {
    if(used>=sizeof(buffer)-1) return;
    int written=snprintf(buffer+used,sizeof(buffer)-used,format,args...);
    if(written>0) used+=std::min(unsigned(written),unsigned(sizeof(buffer)-used-1));
  };
  MEMORY_BASIC_INFORMATION instruction{};
  VirtualQuery(reinterpret_cast<void*>(c.Eip),&instruction,sizeof(instruction));
  append("Fault code=%08lx EIP=%08lx module=%p offset=%08lx frame=%u\r\n",
      (unsigned long)code,(unsigned long)c.Eip,instruction.AllocationBase,
      (unsigned long)(c.Eip-uintptr_t(instruction.AllocationBase)),frames);
  append("EAX=%08lx EBX=%08lx ECX=%08lx EDX=%08lx ESI=%08lx EDI=%08lx ESP=%08lx EBP=%08lx\r\n",
      (unsigned long)c.Eax,(unsigned long)c.Ebx,(unsigned long)c.Ecx,
      (unsigned long)c.Edx,(unsigned long)c.Esi,(unsigned long)c.Edi,
      (unsigned long)c.Esp,(unsigned long)c.Ebp);
  if(exception->ExceptionRecord->NumberParameters>=2)
    append("access=%lu address=%08lx\r\n",
        (unsigned long)exception->ExceptionRecord->ExceptionInformation[0],
        (unsigned long)exception->ExceptionRecord->ExceptionInformation[1]);
  uintptr_t bp=c.Ebp;
  for(unsigned i=0;i<20 && readableRange(reinterpret_cast<void*>(bp),8);++i) {
    auto chain=reinterpret_cast<const DWORD*>(bp);
    append("return[%u]=%08lx\r\n",i,(unsigned long)chain[1]);
    uintptr_t next=chain[0]; if(next<=bp || next-bp>0x100000) break; bp=next;
  }
  auto sp=reinterpret_cast<const DWORD*>(c.Esp);
  if(readableRange(sp,128))
    for(unsigned i=0;i<32;++i) append("stack[%u]=%08lx\r\n",i,(unsigned long)sp[i]);
  HANDLE file=CreateFileA("GuruminModern-crash.log",FILE_APPEND_DATA,
      FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file!=INVALID_HANDLE_VALUE) {
    DWORD written;WriteFile(file,buffer,used,&written,nullptr);
    FlushFileBuffers(file);CloseHandle(file);
  }
  InterlockedExchange(&entered,0);
  return EXCEPTION_CONTINUE_SEARCH;
}
static void installFaultTrace() {
  static bool installed;
  if(diagnostics && !installed) installed=AddVectoredExceptionHandler(1,traceFault)!=nullptr;
}
