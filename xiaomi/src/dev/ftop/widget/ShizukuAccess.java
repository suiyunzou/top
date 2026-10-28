package dev.ftop.widget;

import android.content.*;
import android.content.pm.PackageManager;
import android.os.*;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.json.JSONArray;
import org.json.JSONObject;
import rikka.shizuku.Shizuku;

/** Connection shared within each app process; sampling never runs on the UI thread. */
final class ShizukuAccess {
    private static final Handler MAIN=new Handler(Looper.getMainLooper());
    private static volatile IBinder service;
    private static volatile CountDownLatch connecting;
    private static final Shizuku.UserServiceArgs ARGS=new Shizuku.UserServiceArgs(
        new ComponentName("dev.ftop.widget",PrivilegedSampler.class.getName()))
        .tag("ftop-readonly").version(8).daemon(false).processNameSuffix("sampler");
    private static final ServiceConnection CONNECTION=new ServiceConnection() {
        public void onServiceConnected(ComponentName name,IBinder binder) {
            service=binder; CountDownLatch latch=connecting; if(latch!=null) latch.countDown();
        }
        public void onServiceDisconnected(ComponentName name) { service=null; connecting=null; }
    };
    static boolean available() { return Shizuku.pingBinder(); }
    static boolean authorized() {
        try { return available()&&Shizuku.checkSelfPermission()==PackageManager.PERMISSION_GRANTED; }
        catch(RuntimeException e) { return false; }
    }
    static String status() {
        if(!available()) return "Shizuku 未运行 · 点击查看启动方式";
        return authorized()?"Shizuku 已授权 · 系统与应用进程":"授权 Shizuku · 读取系统与应用进程";
    }
    private static synchronized CountDownLatch connect() {
        if(connecting==null) {
            connecting=new CountDownLatch(1);
            MAIN.post(()->{
                try { Shizuku.bindUserService(ARGS,CONNECTION); }
                catch(RuntimeException e) { CountDownLatch latch=connecting; connecting=null; if(latch!=null) latch.countDown(); }
            });
        }
        return connecting;
    }
    static JSONObject request(int code,ProcessEntry target,String action,boolean logs) throws Exception {
        if(Looper.myLooper()==Looper.getMainLooper())throw new IllegalStateException("Worker thread required");
        if(!authorized())throw new SecurityException("Shizuku 未运行或未授权");
        if(service==null||!service.isBinderAlive()) { if(service!=null)connecting=null; service=null; CountDownLatch latch=connect(); if(latch!=null)latch.await(3500,TimeUnit.MILLISECONDS); }
        IBinder binder=service;
        if(binder==null) { connecting=null; throw new IllegalStateException("Shizuku 服务未连接，请重试"); }
        Parcel req=Parcel.obtain(),reply=Parcel.obtain();
        try {
            req.writeInterfaceToken(PrivilegedSampler.DESCRIPTOR);
            if(code!=PrivilegedSampler.NETWORK)req.writeString(new JSONObject().put("target",target.json()).put("action",action).put("logs",logs).toString());
            if(!binder.transact(code,req,reply,0))throw new IllegalStateException("服务版本不兼容");
            reply.readException(); return new JSONObject(reply.readString());
        } finally {req.recycle(); reply.recycle();}
    }
    static boolean sample(Snapshot target) {
        if(!authorized()) return false;
        if(Looper.myLooper()==Looper.getMainLooper()) throw new IllegalStateException("Worker thread required");
        try {
            if(service==null||!service.isBinderAlive()) {
                if(service!=null)connecting=null; service=null; CountDownLatch latch=connect();
                if(latch!=null) latch.await(3500,TimeUnit.MILLISECONDS);
            }
            IBinder binder=service;
            if(binder==null) { connecting=null; return false; }
            Parcel request=Parcel.obtain(),reply=Parcel.obtain();
            JSONObject data;
            try {
                request.writeInterfaceToken(PrivilegedSampler.DESCRIPTOR);
                if(!binder.transact(PrivilegedSampler.SAMPLE,request,reply,0)) return false;
                reply.readException(); data=new JSONObject(reply.readString());
            } finally { request.recycle(); reply.recycle(); }
            JSONArray rows=data.getJSONArray("processes");
            target.processes.clear();
            for(int i=0;i<Math.min(4096,rows.length());i++) target.processes.add(ProcessEntry.from(rows.getJSONObject(i)));
            target.processStatus=data.getString("processStatus");
            target.cpu=data.optDouble("cpu",Double.NaN);
            target.cpuStatus=Double.isFinite(target.cpu)?"Shizuku · 真实累计时间差":"Shizuku · 等待第二次采样";
            JSONArray cores=data.optJSONArray("cores");
            if(cores!=null) { target.cores=new double[cores.length()]; for(int i=0;i<cores.length();i++) target.cores[i]=cores.optDouble(i,Double.NaN); }
            return true;
        } catch(Exception e) {
            if(e instanceof InterruptedException) Thread.currentThread().interrupt();
            service=null; connecting=null; android.util.Log.w("ftop","Shizuku sample unavailable",e); return false;
        }
    }
}
