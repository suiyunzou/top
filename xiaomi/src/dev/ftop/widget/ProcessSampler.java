package dev.ftop.widget;

import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Debug;
import android.os.SystemClock;
import android.system.Os;
import android.system.OsConstants;
import java.io.BufferedReader;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Bounded, worker-thread-only sampling; no root, shell or permission bypass. */
final class ProcessSampler {
    private final Context context;
    private final ActivityManager manager;
    private Map<Integer,long[]> previous=new HashMap<>();
    private final Map<Long,Long> memory=new HashMap<>();
    private long lastAt,memoryAt;
    ProcessSampler(Context c) { context=c; manager=(ActivityManager)c.getSystemService(Context.ACTIVITY_SERVICE); }
    void sample(Snapshot s) {
        long now=SystemClock.elapsedRealtime();
        List<ActivityManager.RunningAppProcessInfo> visible;
        try { visible=manager.getRunningAppProcesses(); }
        catch(SecurityException e) { visible=null; }
        s.processStatus="仅显示系统允许读取的进程；不代表全机列表";
        if(visible==null||visible.isEmpty()) { s.processStatus="进程读取受限或暂无可见进程"; return; }
        visible=new ArrayList<>(visible);
        visible.sort(Comparator.comparingInt(p->p.pid));
        long hz=Os.sysconf(OsConstants._SC_CLK_TCK);
        Map<Integer,long[]> next=new HashMap<>();
        boolean readMemory=memoryAt==0||now-memoryAt>=5000;
        if(readMemory) memory.clear();
        ProcessIdentity identities=new ProcessIdentity(context);
        for(ActivityManager.RunningAppProcessInfo info:visible) {
            if(s.processes.size()>=4096) break;
            ProcessEntry p=new ProcessEntry(); p.pid=info.pid; p.uid=info.uid; p.name=info.processName;
            p.system=info.uid<10000;
            identities.resolve(p,info.processName);
            try(BufferedReader r=new BufferedReader(new FileReader("/proc/"+p.pid+"/stat"))) {
                long[] ticks=ProcessMetrics.ticks(r.readLine()); p.started=ticks[1];
                p.cpu=ProcessMetrics.cpu(previous.get(p.pid),ticks,now-lastAt,hz); next.put(p.pid,ticks);
            } catch(Exception ignored) { /* Never turn a denied read into zero CPU. */ }
            long key=p.stableId();
            if(readMemory && p.uid==android.os.Process.myUid()) {
                try {
                    Debug.MemoryInfo[] values=manager.getProcessMemoryInfo(new int[]{p.pid});
                    if(values.length>0 && values[0].getTotalPss()>0) memory.put(key,values[0].getTotalPss()*1024L);
                } catch(Exception ignored) { /* Android restricts other UIDs and throttles PSS sampling. */ }
            }
            p.memoryBytes=memory.containsKey(key)?memory.get(key):-1;
            s.processes.add(p);
        }
        if(readMemory) memoryAt=now;
        previous=next; lastAt=now;
        if(s.processes.stream().allMatch(p->p.uid==android.os.Process.myUid()))
            s.processStatus="系统限制：仅可见 ftop 自身进程";
        
    }
}
