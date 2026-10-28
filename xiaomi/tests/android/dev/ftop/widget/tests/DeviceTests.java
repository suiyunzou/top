package dev.ftop.widget.tests;

import android.app.Instrumentation;
import android.appwidget.AppWidgetManager;
import android.content.ComponentName;
import android.content.Context;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.os.Debug;
import android.widget.RemoteViews;
import dev.ftop.widget.*;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import org.json.JSONArray;
import org.json.JSONObject;

public final class DeviceTests extends Instrumentation {
    private final JSONArray checks=new JSONArray();
    private void check(boolean b,String s) throws Exception { if(!b) throw new AssertionError(s); checks.put(s); }
    @Override public void onCreate(Bundle args) { super.onCreate(args); start(); }
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            Context c=getTargetContext(); long start=System.nanoTime();
            Sampler sampler=new Sampler(c); Snapshot first=sampler.sample(); Thread.sleep(750); Snapshot sample=sampler.sample();
            check(sample.totalMemory>0 && sample.availableMemory<=sample.totalMemory,"真实内存范围");
            check(sample.battery>=0 && sample.battery<=100,"真实电量范围");
            check(sample.totalStorage>=sample.freeStorage && sample.totalStorage>0,"存储范围");
            check(!Double.isFinite(sample.cpu)||sample.cpu>=0&&sample.cpu<=100,"CPU 不可用或有效百分比");
            check(!Double.isFinite(sample.gpu)||sample.gpu>=0&&sample.gpu<=100,"GPU 不可用或有效百分比");
            check(Double.isNaN(first.rx),"第一帧网络不可伪造");
            Snapshot restored=Snapshot.from(sample.json());
            check(restored.totalMemory==sample.totalMemory && (Double.isNaN(sample.cpu)?Double.isNaN(restored.cpu):sample.cpu==restored.cpu),"JSON 保留真实值与不可用状态");
            for(int[] size:new int[][]{{220,130},{340,180},{420,220},{700,500}}) {
                Bitmap b=CardRenderer.bitmap(c,sample,size[0],size[1]);
                check(b.getAllocationByteCount()<=5700000,"受控位图大小 "+size[0]); b.recycle();
            }
            final RemoteViews v=FtopWidget.views(c,sample,340,180);
            runOnMainSync(()->v.apply(c,null)); check(true,"RemoteViews 能实例化");
            AppWidgetManager m=AppWidgetManager.getInstance(c);
            check(m.getInstalledProviders().stream().anyMatch(p->p.provider.equals(new ComponentName(c,FtopWidget.class))),"系统注册卡片 Provider");
            Debug.MemoryInfo memory=new Debug.MemoryInfo(); Debug.getMemoryInfo(memory);
            JSONObject report=new JSONObject().put("checks",checks).put("sample",sample.json())
                .put("pinSupported",m.isRequestPinAppWidgetSupported()).put("elapsedMs",(System.nanoTime()-start)/1000000)
                .put("testProcessPssKB",memory.getTotalPss()).put("note","PSS includes instrumentation and render tests, not idle widget cost");
            File f=new File(c.getExternalFilesDir(null),"device-tests.json");
            try(FileOutputStream out=new FileOutputStream(f)){out.write(report.toString(2).getBytes(StandardCharsets.UTF_8));}
            result.putString("stream",checks.length()+" device checks passed\n"+report.toString(2)); finish(-1,result);
        } catch(Throwable e) { result.putString("stream",android.util.Log.getStackTraceString(e)); finish(0,result); }
    }
}
