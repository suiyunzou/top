package dev.ftop.widget;

import java.util.*;

/** Groups by verified package + Android user, never by translated label or Linux parent PID. */
public final class AppGroup {
    public final String key;
    public final List<ProcessEntry> members=new ArrayList<>();
    public String label,packageName;
    public int userId;
    public boolean system;
    public long memoryBytes;
    public double cpu;
    public String memoryKind="";
    private boolean memoryComplete=true,cpuComplete=true;
    private AppGroup(ProcessEntry p) {
        key=p.groupKey(); packageName=p.packageName; userId=p.userId(); system=p.system;
        label=p.appLabel.isEmpty()?p.name:p.appLabel;
    }
    public String title() { return label+(userId==0?"":" · 用户 "+userId); }
    public String memoryText() { return memoryComplete?Metrics.bytes(memoryBytes)+" "+memoryKind+" 汇总":"— 内存不完整"; }
    public String memoryValue() { return memoryComplete?Metrics.bytes(memoryBytes):"—"; }
    public String cpuText() { return cpuComplete?Metrics.percent(cpu):"—"; }
    public long stableId() { long h=0xcbf29ce484222325L; for(int i=0;i<key.length();i++)h=(h^key.charAt(i))*0x100000001b3L; return h; }
    public static List<AppGroup> collect(List<ProcessEntry> processes,boolean byCpu) {
        Map<String,AppGroup> groups=new LinkedHashMap<>();
        for(ProcessEntry p:processes) {
            AppGroup g=groups.get(p.groupKey()); if(g==null) {g=new AppGroup(p); groups.put(g.key,g);}
            g.members.add(p); g.system|=p.system;
            if(p.memoryBytes<0)g.memoryComplete=false; else g.memoryBytes+=p.memoryBytes;
            if(g.memoryKind.isEmpty())g.memoryKind=p.memoryKind; else if(!g.memoryKind.equals(p.memoryKind))g.memoryComplete=false;
            if(Double.isFinite(p.cpu))g.cpu+=p.cpu; else g.cpuComplete=false;
        }
        List<AppGroup> result=new ArrayList<>(groups.values());
        for(AppGroup g:result)g.members.sort((a,b)->{
            int n=byCpu?Double.compare(Double.isNaN(b.cpu)?-1:b.cpu,Double.isNaN(a.cpu)?-1:a.cpu):Long.compare(b.memoryBytes,a.memoryBytes);
            return n!=0?n:Integer.compare(a.pid,b.pid);
        });
        result.sort((a,b)->{int n=byCpu?Double.compare(b.cpuComplete?b.cpu:-1,a.cpuComplete?a.cpu:-1):
            Long.compare(b.memoryComplete?b.memoryBytes:-1,a.memoryComplete?a.memoryBytes:-1); return n!=0?n:a.key.compareTo(b.key);});
        return result;
    }
}
