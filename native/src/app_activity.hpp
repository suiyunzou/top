#pragma once
#include "model.hpp"
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <atomic>

// Actual kernel TCP/UDP and physical-disk events. No process I/O-counter proxy.
// Own session only; never change an existing kernel logger.
struct AppIo {
  struct Bytes { unsigned long long read=0, write=0, receive=0, send=0; long long first=0; };
  struct Batch { std::map<DWORD,Bytes> rows; double duration=0; bool valid=false; };
  TRACEHANDLE session=0, consumer=INVALID_PROCESSTRACE_HANDLE;
  HANDLE thread=nullptr;
  SRWLOCK mutex=SRWLOCK_INIT;
  std::map<DWORD,Bytes> pending;
  std::wstring name;
  std::vector<BYTE> storage;
  DWORD error=0, lost=0, queryError=0;
  std::atomic<unsigned long long> networkEvents{0}, diskEvents{0}, unmapped{0}, schemaErrors{0};
  double last=0;
  bool attempted=false;
  static constexpr GUID tcp={0x9a280ac0,0xc8e0,0x11d1,{0x84,0xe2,0,0xc0,0x4f,0xb9,0x98,0xa2}};
  static constexpr GUID udp={0xbf3a50c5,0xa9c9,0x4988,{0xa0,5,0x2d,0xf0,0xb7,0xc8,0xf8,0xf}};
  static constexpr GUID disk={0x3d6fa8d4,0xfe05,0x11d0,{0x9d,0xda,0,0xc0,0x4f,0xd7,0xba,0x7c}};
  ~AppIo() { stop(); }
  EVENT_TRACE_PROPERTIES *properties() { return reinterpret_cast<EVENT_TRACE_PROPERTIES*>(storage.data()); }
  static bool field(EVENT_RECORD *e,const wchar_t *name,ULONG &value) {
    PROPERTY_DATA_DESCRIPTOR d{}; d.PropertyName=ULONGLONG(name); d.ArrayIndex=ULONG_MAX;
    return TdhGetProperty(e,0,nullptr,1,&d,sizeof(value),reinterpret_cast<BYTE*>(&value))==ERROR_SUCCESS;
  }
  void event(EVENT_RECORD *e) {
    bool net=IsEqualGUID(e->EventHeader.ProviderId,tcp)||IsEqualGUID(e->EventHeader.ProviderId,udp);
    bool io=IsEqualGUID(e->EventHeader.ProviderId,disk);
    int op=e->EventHeader.EventDescriptor.Opcode;
    if ((!net&&!io)||(op!=10&&op!=11&&!(net&&(op==26||op==27)))) return;
    ULONG pid=0,bytes=0;
    long long when=e->EventHeader.TimeStamp.QuadPart;
    if (net) {
      if (!field(e,L"PID",pid)||!field(e,L"size",bytes)) { ++schemaErrors; return; }
      ++networkEvents;
    } else {
      ULONG tid=0;
      if (!field(e,L"TransferSize",bytes)||!field(e,L"IssuingThreadId",tid)) { ++schemaErrors; return; }
      ++diskEvents;
      HANDLE h=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,tid);
      if (h) {
        FILETIME born{},exit{},kernel{},user{};
        // Reject a reused TID: its current thread must predate this event.
        if (GetThreadTimes(h,&born,&exit,&kernel,&user) &&
            static_cast<long long>((ULONGLONG(born.dwHighDateTime)<<32)|born.dwLowDateTime)<=when)
          pid=GetProcessIdOfThread(h);
        CloseHandle(h);
      }
      if (!pid) { unmapped+=bytes; return; }
    }
    if (!pid||!bytes) return;
    AcquireSRWLockExclusive(&mutex);
    auto &b=pending[pid]; if (!b.first||when<b.first) b.first=when;
    if (net) { if (op==10||op==26) b.send+=bytes; else b.receive+=bytes; }
    else { if (op==10) b.read+=bytes; else b.write+=bytes; }
    ReleaseSRWLockExclusive(&mutex);
  }
  bool start() {
    if (attempted) return session!=0;
    attempted=true;
    name=L"FTOP-AppIO-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    // QueryTrace also returns logger/file names. Reserve both full name slots,
    // rather than reusing a buffer sized only for the StartTrace input name.
    storage.assign(sizeof(EVENT_TRACE_PROPERTIES)+2*1024*sizeof(wchar_t),0);
    auto p=properties(); p->Wnode.BufferSize=ULONG(storage.size());
    // GUID_NULL lets ETW allocate a unique session GUID. SystemTraceControlGuid
    // is reserved for the shared legacy NT Kernel Logger.
    p->Wnode.Flags=WNODE_FLAG_TRACED_GUID; p->Wnode.ClientContext=1;
    p->LogFileMode=EVENT_TRACE_REAL_TIME_MODE|EVENT_TRACE_SYSTEM_LOGGER_MODE;
    p->EnableFlags=EVENT_TRACE_FLAG_NETWORK_TCPIP|EVENT_TRACE_FLAG_DISK_IO|EVENT_TRACE_FLAG_DISK_IO_INIT;
    p->BufferSize=64; p->MinimumBuffers=8; p->MaximumBuffers=64; p->FlushTimer=1;
    p->LoggerNameOffset=sizeof(EVENT_TRACE_PROPERTIES);
    error=StartTraceW(&session,name.c_str(),p);
    if (error!=ERROR_SUCCESS) { session=0; return false; }
    EVENT_TRACE_LOGFILEW log{}; log.LoggerName=const_cast<wchar_t*>(name.c_str());
    log.ProcessTraceMode=PROCESS_TRACE_MODE_REAL_TIME|PROCESS_TRACE_MODE_EVENT_RECORD;
    log.Context=this;
    log.EventRecordCallback=[](EVENT_RECORD *e) { static_cast<AppIo*>(e->UserContext)->event(e); };
    consumer=OpenTraceW(&log);
    if (consumer==INVALID_PROCESSTRACE_HANDLE) { error=GetLastError(); stop(); return false; }
    thread=CreateThread(nullptr,0,[](void *v)->DWORD { auto self=static_cast<AppIo*>(v); TRACEHANDLE h=self->consumer; return ProcessTrace(&h,1,nullptr,nullptr); },this,0,nullptr);
    if (!thread) { error=GetLastError(); stop(); return false; }
    last=seconds(); return true;
  }
  Batch take() {
    Batch b; if (!session) return b;
    double now=seconds(); b.duration=now-last; last=now;
    auto p=properties();
    ZeroMemory(p,storage.size());
    p->Wnode.BufferSize=ULONG(storage.size()); p->Wnode.Flags=WNODE_FLAG_TRACED_GUID;
    p->LoggerNameOffset=sizeof(EVENT_TRACE_PROPERTIES);
    p->LogFileNameOffset=sizeof(EVENT_TRACE_PROPERTIES)+1024*sizeof(wchar_t);
    queryError=ControlTraceW(session,name.c_str(),p,EVENT_TRACE_CONTROL_QUERY);
    DWORD total=properties()->EventsLost+properties()->RealTimeBuffersLost;
    b.valid=queryError==ERROR_SUCCESS && total==lost && b.duration>0;
    lost=total;
    AcquireSRWLockExclusive(&mutex); b.rows.swap(pending); ReleaseSRWLockExclusive(&mutex);
    return b;
  }
  void stop() {
    if (session) { ControlTraceW(session,name.c_str(),properties(),EVENT_TRACE_CONTROL_STOP); session=0; }
    if (consumer!=INVALID_PROCESSTRACE_HANDLE) CloseTrace(consumer);
    if (thread) { WaitForSingleObject(thread,INFINITE); CloseHandle(thread); thread=nullptr; }
    consumer=INVALID_PROCESSTRACE_HANDLE;
    pending.clear(); last=0;
  }
  void pause() { stop(); attempted=false; lost=0; }
};

struct AppGpu {
  struct Engine { DWORD pid; std::wstring key; double value; };
  PDH_HQUERY query=nullptr; PDH_HCOUNTER counter=nullptr;
  bool primed=false;
  double total=-1;
  long long previousStamp=0, intervalStart=0;
  ~AppGpu() { pause(); }
  void pause() { if(query) PdhCloseQuery(query); query=nullptr; counter=nullptr; primed=false; total=-1; previousStamp=intervalStart=0; }
  std::vector<Engine> sample() {
    std::vector<Engine> result; total=-1;
    if (!query) {
      if(PdhOpenQueryW(nullptr,0,&query)!=ERROR_SUCCESS) return result;
      if(PdhAddEnglishCounterW(query,L"\\GPU Engine(*)\\Utilization Percentage",0,&counter)!=ERROR_SUCCESS) { pause(); return result; }
    }
    if(PdhCollectQueryData(query)!=ERROR_SUCCESS) { primed=false; return result; }
    FILETIME now{}; GetSystemTimeAsFileTime(&now); intervalStart=previousStamp;
    previousStamp=static_cast<long long>((ULONGLONG(now.dwHighDateTime)<<32)|now.dwLowDateTime);
    if(!primed) { primed=true; return result; }
    DWORD bytes=0,count=0;
    if(PdhGetFormattedCounterArrayW(counter,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&bytes,&count,nullptr)!=static_cast<PDH_STATUS>(PDH_MORE_DATA)) return result;
    std::vector<BYTE> buffer(bytes);
    auto items=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if(PdhGetFormattedCounterArrayW(counter,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&bytes,&count,items)!=ERROR_SUCCESS) return result;
    std::map<std::wstring,double> engines;
    for(DWORD i=0;i<count;++i) {
      auto &v=items[i].FmtValue;
      if(v.CStatus!=PDH_CSTATUS_VALID_DATA && v.CStatus!=PDH_CSTATUS_NEW_DATA) continue;
      unsigned long pid=0; std::wstring name=items[i].szName;
      auto suffix=name.find(L"_luid_");
      if(suffix==std::wstring::npos || swscanf(name.c_str(),L"pid_%lu",&pid)!=1 || !std::isfinite(v.doubleValue)||v.doubleValue<0) continue;
      auto key=name.substr(suffix);
      result.push_back({DWORD(pid),key,v.doubleValue}); engines[key]+=v.doubleValue;
    }
    for(auto &e:engines) total=std::max(total,std::min(100.,e.second));
    return result;
  }
};

// Sum members on the same adapter/engine, then select the busiest engine.
// Summing unrelated GPU engines would produce a misleading percentage.
inline double appGpuValue(const Group &g,const std::vector<AppGpu::Engine> &samples,long long start) {
  std::map<std::wstring,double> engines;
  for(const auto &m:g.members) if(m.created<=start)
    for(const auto &e:samples) if(e.pid==m.pid) engines[e.key]+=e.value;
  double value=-1;
  for(const auto &e:engines) value=std::max(value,std::min(100.,e.second));
  return value;
}

struct AppActivity {
  AppIo io; AppGpu gpu;
  json recentIo=json::array();
  void pause() { io.pause(); gpu.pause(); recentIo.clear(); }
  void read(Snapshot &s) {
    io.start(); auto batch=io.take(); auto engines=gpu.sample();
    s.appGpuTotal=gpu.total;
    s.appIoState={{"running",io.session!=0},{"error",io.error},{"lost",io.lost},
      {"networkEvents",io.networkEvents.load()},{"diskEvents",io.diskEvents.load()},
      {"unmappedDiskBytes",io.unmapped.load()},{"schemaErrors",io.schemaErrors.load()},
      {"queryError",io.queryError},{"intervalValid",batch.valid},{"intervalSeconds",batch.duration},
      {"intervalProcesses",batch.rows.size()}};
    s.appNetTotal=-1;
    if(batch.valid) for(const auto &p:batch.rows)
      if(p.second.receive||p.second.send) s.appNetTotal=std::max(0.,s.appNetTotal)+(p.second.receive+p.second.send)/batch.duration;
    for(auto &g:s.groups) {
      g.gpu=appGpuValue(g,engines,gpu.intervalStart); g.disk=g.net=-1;
      if(!batch.valid) continue;
      for(const auto &m:g.members) {
        auto p=batch.rows.find(m.pid);
        // A recycled PID must never inherit the preceding process's events.
        if(p==batch.rows.end()||p->second.first<m.created) continue;
        auto &b=p->second;
        if(b.read||b.write) g.disk=std::max(0.,g.disk)+(b.read+b.write)/batch.duration;
        if(b.receive||b.send) g.net=std::max(0.,g.net)+(b.receive+b.send)/batch.duration;
      }
      if(g.disk>0||g.net>0) {
        recentIo.push_back({{"when",s.when},{"name",utf8(g.name)},{"disk",g.disk},{"net",g.net}});
        if(recentIo.size()>12) recentIo.erase(recentIo.begin());
      }
    }
    s.appIoState["recentObservedApps"]=recentIo;
  }
};
