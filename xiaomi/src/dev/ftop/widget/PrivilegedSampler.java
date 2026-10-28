package dev.ftop.widget;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.SystemClock;
import android.system.Os;
import android.system.OsConstants;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import org.json.JSONArray;
import org.json.JSONObject;

/** UID-gated Shizuku service. Only fixed sampling, detail, and action requests. */
public final class PrivilegedSampler extends Binder {
    static final String DESCRIPTOR="dev.ftop.widget.ReadOnlySampler";
    static final int SAMPLE=IBinder.FIRST_CALL_TRANSACTION, DETAIL=SAMPLE+1, ACTION=SAMPLE+2, NETWORK=SAMPLE+3;
    private final Context context;
    private final int clientUid;
    private Map<Integer,long[]> previous=new HashMap<>();
    private Map<String,long[]> previousCpu=new HashMap<>();
    private long lastAt;
    private String cached;
    private ProcessIdentity identities;
    private long identitiesAt;
    public PrivilegedSampler(Context c) throws Exception {
        context=c;
        clientUid=c.getPackageManager().getApplicationInfo("dev.ftop.widget",0).uid;
        attachInterface(null,DESCRIPTOR);
    }
    @Override protected boolean onTransact(int code,Parcel data,Parcel reply,int flags) throws android.os.RemoteException {
        if(code==16777115) { System.exit(0); return true; } // Official UserService lifecycle transaction.
        if(code==INTERFACE_TRANSACTION) { reply.writeString(DESCRIPTOR); return true; }
        if(code!=SAMPLE&&code!=DETAIL&&code!=ACTION&&code!=NETWORK) return super.onTransact(code,data,reply,flags);
        data.enforceInterface(DESCRIPTOR);
        if(Binder.getCallingUid()!=clientUid) throw new SecurityException("ftop client required");
        try {
            String result;
            if(code==SAMPLE)result=sample();
            else if(code==NETWORK)result=NetworkReader.read(context).toString();
            else {
                JSONObject args=new JSONObject(data.readString());
                ProcessEntry target=ProcessEntry.from(args.getJSONObject("target"));
                ProcessControl control=new ProcessControl(context);
                result=(code==DETAIL?control.detail(target,args.optBoolean("logs"),!args.optString("action").equals("capabilities")):control.act(target,args.getString("action"))).toString();
                if(code==ACTION)cached=null;
            }
            reply.writeNoException(); reply.writeString(result);
        }
        catch(Exception e) { reply.writeException(new IllegalStateException("Process sampling failed",e)); }
        return true;
    }
    private static String line(String path) throws IOException {
        try(BufferedReader r=new BufferedReader(new FileReader(path))) { return r.readLine(); }
    }
    private static String name(File dir,String stat) {
        try(FileInputStream input=new FileInputStream(new File(dir,"cmdline"))) {
            byte[] bytes=new byte[256]; int size=input.read(bytes),end=0;
            while(end<size&&bytes[end]!=0) end++;
            if(end>0) return new String(bytes,0,end,StandardCharsets.UTF_8);
        } catch(IOException ignored) { }
        return stat.substring(stat.indexOf('(')+1,stat.lastIndexOf(')'));
    }
    private synchronized String sample() throws Exception {
        long now=SystemClock.elapsedRealtime();
        if(cached!=null&&now-lastAt<400) return cached;
        long hz=Os.sysconf(OsConstants._SC_CLK_TCK),pageSize=Os.sysconf(OsConstants._SC_PAGESIZE);
        Map<Integer,long[]> next=new HashMap<>();
        ArrayList<ProcessEntry> rows=new ArrayList<>();
        // Labels are reused briefly; destructive requests always resolve identities afresh.
        if(identities==null||now-identitiesAt>15000) {identities=new ProcessIdentity(context);identitiesAt=now;}
        File[] dirs=new File("/proc").listFiles(f->f.getName().matches("[0-9]+"));
        int denied=0,scanned=0;
        if(dirs==null) throw new IOException("proc unavailable");
        for(File dir:dirs) {
            if(++scanned>4096) break;
            try {
                String stat=line(new File(dir,"stat").getPath());
                long[] tick=ProcessMetrics.ticks(stat);
                long rss=ProcessMetrics.rssBytes(stat,pageSize);
                if(rss<=0) continue; // Kernel threads have no userspace resident memory.
                ProcessEntry p=new ProcessEntry(); p.pid=Integer.parseInt(dir.getName());
                p.uid=Os.stat(dir.getPath()).st_uid; p.system=p.uid%100000<10000;
                p.started=tick[1]; p.memoryBytes=rss; p.memoryKind="RSS";
                p.cpu=ProcessMetrics.cpu(previous.get(p.pid),tick,now-lastAt,hz);
                p.state=stat.substring(stat.lastIndexOf(')')+2).split(" ")[0];
                next.put(p.pid,tick); identities.resolve(p,name(dir,stat)); rows.add(p);
            } catch(Exception ignored) { denied++; } // Process exit or SELinux denial: omit, never fake zero.
        }
        rows.sort((a,b)->{
            double ac=Double.isFinite(a.cpu)?a.cpu:-1,bc=Double.isFinite(b.cpu)?b.cpu:-1;
            int d=Double.compare(bc,ac); if(d==0) d=Long.compare(b.memoryBytes,a.memoryBytes);
            return d!=0?d:Integer.compare(a.pid,b.pid);
        });
        JSONArray processes=new JSONArray();
        int chars=0;
        for(ProcessEntry p:rows) { JSONObject row=p.json(); chars+=row.toString().length(); if(chars>300000)break; processes.put(row); }
        JSONObject out=new JSONObject().put("processes",processes).put("visibleCount",rows.size())
            .put("processStatus","Shizuku · 可读 "+rows.size()+" 个进程 · "+(processes.length()<rows.size()?"传输受限，仅返回 "+processes.length()+" 项 · ":"")+"内存 RSS")
            .put("skipped",denied).put("sampleElapsed",now).put("sourceUid",Os.getuid());
        try(BufferedReader reader=new BufferedReader(new FileReader("/proc/stat"))) {
            Map<String,long[]> nextCpu=new HashMap<>(); JSONArray cores=new JSONArray();
            for(String l;(l=reader.readLine())!=null&&l.startsWith("cpu");) {
                String key=l.trim().split("\\s+",2)[0]; long[] ticks=Metrics.cpuTicks(l);
                double value=Metrics.cpu(previousCpu.get(key),ticks); nextCpu.put(key,ticks);
                Object number=Double.isFinite(value)?value:JSONObject.NULL;
                if(key.equals("cpu")) out.put("cpu",number); else cores.put(number);
            }
            out.put("cores",cores); previousCpu=nextCpu;
        } catch(IOException ignored) { }
        previous=next; lastAt=now; cached=out.toString(); return cached;
    }
}
