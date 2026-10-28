package dev.ftop.widget;

import android.content.Context;
import android.os.Looper;
import java.lang.reflect.*;
import java.util.*;
import org.json.*;

/** Run via app_process as real shell. Only dev.ftop.fixture may be stopped by this test. */
public final class ControlProbe {
    private static int checks;
    private static void check(boolean value,String name){if(!value)throw new AssertionError(name);checks++;System.out.println("PASS "+name);}
    private static ProcessEntry entry(int pid,int uid,String pkg,String raw,long memory){
        ProcessEntry p=new ProcessEntry();p.pid=pid;p.uid=uid;p.started=1;p.packageName=pkg;p.rawName=raw;p.appLabel="相同显示名";p.name=raw;p.memoryKind="RSS";p.memoryBytes=memory;p.cpu=1;return p;
    }
    private static List<ProcessEntry> rows(Object sampler,Method sample)throws Exception{
        JSONObject result=new JSONObject((String)sample.invoke(sampler));JSONArray array=result.getJSONArray("processes");List<ProcessEntry> rows=new ArrayList<>();
        for(int i=0;i<array.length();i++)rows.add(ProcessEntry.from(array.getJSONObject(i)));return rows;
    }
    public static void main(String[] args)throws Exception{
        Looper.prepareMainLooper();Class<?> thread=Class.forName("android.app.ActivityThread");Object at=thread.getMethod("systemMain").invoke(null);
        Context c=(Context)thread.getMethod("getSystemContext").invoke(at);
        try{
            List<ProcessEntry> fixture=new ArrayList<>();for(int i=0;i<90;i++)fixture.add(entry(8000+i,10100,"sample.one","sample.one:p"+i,100));
            fixture.add(entry(9000,10101,"sample.two","sample.two",100));fixture.add(entry(9001,10010100,"sample.one","sample.one",100));
            List<AppGroup> groups=AppGroup.collect(fixture,false);check(groups.size()==3,"same labels and different users remain distinct");
            AppGroup big=groups.get(0);check(big.members.size()==90&&big.memoryBytes==9000&&big.cpu==90,"aggregate includes members beyond top 64");
            Snapshot saved=new Snapshot();saved.processes.addAll(fixture);check(Snapshot.from(saved.json()).processes.size()==92,"cache roundtrip keeps full membership");
            ProcessEntry unknown=entry(9900,11111,"","unknown",-1);unknown.cpu=Double.NaN;fixture.add(unknown);
            AppGroup u=AppGroup.collect(Collections.singletonList(unknown),false).get(0);check(u.cpuText().equals("—")&&u.memoryText().contains("不完整"),"unknown metrics never become zero");
            ProcessEntry mix=entry(800,10100,"sample.one","sample.one:a",100);mix.memoryKind="PSS";
            check(AppGroup.collect(Arrays.asList(fixture.get(0),mix),false).get(0).memoryText().contains("不完整"),"mixed memory accounting is not summed as valid");
            check(ProcessIdentity.choose("shared.process",new String[]{"one.pkg","two.pkg"}).isEmpty(),"ambiguous shared UID not guessed");
            check(ProcessIdentity.choose("two.pkg:worker",new String[]{"one.pkg","two.pkg"}).equals("two.pkg"),"exact package process resolves shared UID");
            check(ProcessControl.currentLifetimeLogs(" 10.500 12 12 I old\n 20.500 12 12 I new",20).equals(" 20.500 12 12 I new\n"),"PID reuse excludes prior-lifetime logs");
            PrivilegedSampler sampler=new PrivilegedSampler(c);Method sample=PrivilegedSampler.class.getDeclaredMethod("sample");sample.setAccessible(true);
            long begin=android.os.SystemClock.elapsedRealtime();List<ProcessEntry> actual=rows(sampler,sample);
            System.out.println("MEASURE sample_ms="+(android.os.SystemClock.elapsedRealtime()-begin)+" visible="+actual.size());
            long[] times=new long[5];for(int i=0;i<5;i++){Thread.sleep(450);long atMs=android.os.SystemClock.elapsedRealtime();rows(sampler,sample);times[i]=android.os.SystemClock.elapsedRealtime()-atMs;}
            Arrays.sort(times);System.out.println("MEASURE warm_sample_median_ms="+times[2]+" warm_sample_max_ms="+times[4]);
            List<ProcessEntry> targets=new ArrayList<>();for(ProcessEntry p:actual)if(p.packageName.equals("dev.ftop.fixture"))targets.add(p);
            check(targets.size()==2,"real two-process fixture grouped by verified UID/package");
            ProcessEntry worker=targets.stream().filter(p->p.rawName.endsWith(":worker")).findFirst().get();ProcessControl control=new ProcessControl(c);
            JSONObject detail=control.detail(worker,false);check(detail.optBoolean("packageAllowed"),"shell allows fixture package action");
            check(!detail.optBoolean("signalAllowed"),"ordinary shell cannot signal other app UID");
            check(detail.optString("services").contains("FixtureService"),"runtime service associated with exact worker PID");
            check(control.detail(worker,true).optString("logs").contains("Fixture worker started"),"actual fixture logs readable by PID");
            ProcessEntry stale=ProcessEntry.from(worker.json());stale.started++;
            try{control.act(stale,"force-stop");throw new AssertionError("stale target accepted");}catch(SecurityException expected){check(true,"stale process identity blocks mutation");}
            try{control.act(worker,"arbitrary");throw new AssertionError("unknown action accepted");}catch(SecurityException expected){check(true,"unknown action rejected");}
            try{control.act(worker,"terminate");throw new AssertionError("shell terminate accepted");}catch(SecurityException expected){check(true,"failed per-PID action does not escalate");}
            JSONObject background=control.act(worker,"background");System.out.println("BACKGROUND "+background);
            // Foreground fixture main must not be force-stopped by a background cleanup request.
            ProcessEntry main=targets.stream().filter(p->p.rawName.equals("dev.ftop.fixture")).findFirst().get();
            control.validate(main);check(true,"background cleanup preserves foreground fixture main");
            JSONObject stopped=control.act(main,"force-stop");check(stopped.getInt("remaining")==0&&stopped.getInt("exited")>=1,"force-stop result verified from real proc table");
            try{control.validate(main);throw new AssertionError("exited target validated");}catch(IllegalStateException expected){check(true,"exited process rejected");}
            System.out.println(checks+" control checks passed");System.exit(0);
        }catch(Throwable failure){failure.printStackTrace();System.exit(1);}
    }
}
