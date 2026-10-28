package dev.ftop.widget.tests;

import android.app.Instrumentation;
import android.appwidget.AppWidgetManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.ListView;
import android.widget.RemoteViews;
import dev.ftop.widget.*;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import org.json.JSONArray;
import org.json.JSONObject;

/** Fixture rows exist only in this separate test APK. Real launcher hosts the collection. */
public final class ProcessTests extends Instrumentation {
    private final JSONArray checks=new JSONArray();
    private Context c; private File out; private AppWidgetManager manager; private int[] ids=new int[0];
    private void check(boolean b,String name) { if(!b) throw new AssertionError(name); checks.put(name); }
    @Override public void onCreate(Bundle args) { super.onCreate(args); start(); }
    private AccessibilityNodeInfo find(AccessibilityNodeInfo n,String value,boolean id) {
        if(n==null) return null;
        CharSequence text=id?n.getViewIdResourceName():n.getText()!=null?n.getText():n.getContentDescription();
        if(text!=null&&text.toString().equals(value)&&n.isVisibleToUser()) return n;
        for(int i=0;i<n.getChildCount();i++) { AccessibilityNodeInfo found=find(n.getChild(i),value,id); if(found!=null)return found; }
        return null;
    }
    private AccessibilityNodeInfo node(String value,boolean id) throws Exception {
        for(int i=0;i<35;i++) { AccessibilityNodeInfo n=find(getUiAutomation().getRootInActiveWindow(),value,id); if(n!=null)return n; Thread.sleep(150); }
        throw new AssertionError("Missing UI: "+value);
    }
    private void click(AccessibilityNodeInfo n) throws Exception { check(n.performAction(AccessibilityNodeInfo.ACTION_CLICK),"按钮响应"); Thread.sleep(600); }
    private void home() throws Exception {
        c.startActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); Thread.sleep(700);
    }
    private void showWidgetPage() throws Exception {
        node("com.google.android.apps.nexuslauncher:id/workspace",true);
        for(int page=0;page<6;page++) {
            AccessibilityNodeInfo root=getUiAutomation().getRootInActiveWindow();
            if(find(root,"dev.ftop.widget:id/process_list",true)!=null)return;
            AccessibilityNodeInfo workspace=find(root,"com.google.android.apps.nexuslauncher:id/workspace",true);
            if(workspace==null)throw new AssertionError("Launcher workspace is unavailable");
            android.graphics.Rect bounds=new android.graphics.Rect(); workspace.getBoundsInScreen(bounds);
            long down=android.os.SystemClock.uptimeMillis();
            for(int step=0;step<=12;step++) {
                float x=bounds.left+bounds.width()*(.85f-.7f*step/12),y=bounds.top+bounds.height()*.7f;
                int action=step==0?android.view.MotionEvent.ACTION_DOWN:step==12?android.view.MotionEvent.ACTION_UP:android.view.MotionEvent.ACTION_MOVE;
                android.view.MotionEvent e=android.view.MotionEvent.obtain(down,android.os.SystemClock.uptimeMillis(),action,x,y,0);
                e.setSource(android.view.InputDevice.SOURCE_TOUCHSCREEN); getUiAutomation().injectInputEvent(e,true); e.recycle(); Thread.sleep(25);
            }
            Thread.sleep(500);
        }
        throw new AssertionError("Process widget page is not visible");
    }
    private void screenshot(String name) throws Exception {
        Bitmap b=getUiAutomation().takeScreenshot();
        try(FileOutputStream f=new FileOutputStream(new File(out,name+".png"))) { b.compress(Bitmap.CompressFormat.PNG,100,f); } b.recycle();
    }
    private void publish(Snapshot sample) {
        for(int id:ids) manager.updateAppWidget(id,FtopWidget.views(c,sample,340,400,true));
    }
    private void write(String name,JSONObject data) throws Exception {
        try(FileOutputStream f=new FileOutputStream(new File(out,name))) { f.write(data.toString(2).getBytes(StandardCharsets.UTF_8)); }
    }
    private String firstVisibleName() throws Exception {
        for(int attempt=0;attempt<30;attempt++) {
        AccessibilityNodeInfo list=node("dev.ftop.widget:id/process_list",true);
        for(int i=0;i<list.getChildCount();i++) {
            AccessibilityNodeInfo row=list.getChild(i),name=find(row,"dev.ftop.widget:id/process_name",true);
            if(name!=null&&name.isVisibleToUser()) return name.getText().toString();
        }
        Thread.sleep(100);
        }
        throw new AssertionError("No visible process rows");
    }
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            c=getTargetContext(); out=new File(c.getExternalFilesDir(null),"process-tests"); out.mkdirs();
            manager=AppWidgetManager.getInstance(c);
            Sampler sampler=new Sampler(c); Snapshot first=sampler.sample(); Thread.sleep(700); Snapshot real=sampler.sample();
            check(!real.processes.isEmpty(),"采集真实可见进程");
            check(real.processes.stream().anyMatch(p->p.pid==android.os.Process.myPid()),"列表包含实际采样 PID");
            check(first.processes.stream().allMatch(p->Double.isNaN(p.cpu)||first.processStatus.startsWith("Shizuku")&&p.cpu>=0),"首帧 CPU 未知或来自已有 Shizuku 采样窗口");
            Snapshot restored=Snapshot.from(real.json());
            check(restored.processes.size()==real.processes.size()&&restored.processes.get(0).pid==real.processes.get(0).pid,"进程列表持久化往返");
            Snapshot missing=new Snapshot(); ProcessEntry unavailable=new ProcessEntry(); unavailable.pid=5; missing.processes.add(unavailable);
            ProcessEntry decoded=Snapshot.from(missing.json()).processes.get(0);
            check(Double.isNaN(decoded.cpu)&&decoded.memoryBytes==-1,"受限指标序列化不变成零");
            check(Snapshot.from(new JSONObject()).processes.isEmpty(),"兼容旧版缓存");
            write("real-processes.json",real.json());
            final boolean[] bars={true};
            runOnMainSync(()->{
                View view=FtopWidget.views(c,real,340,400,true).apply(c,new android.appwidget.AppWidgetHostView(c));
                ListView list=view.findViewById(dev.ftop.widget.R.id.process_list);
                bars[0]=list.isVerticalScrollBarEnabled()||list.isHorizontalScrollBarEnabled();
            });
            check(!bars[0],"原生列表关闭横向和纵向滚动条");
            ids=manager.getAppWidgetIds(new ComponentName(c,ProcessWidget.class));
            if(ids.length==0) {
                c.startActivity(new Intent(c,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                click(node("设置",false));click(node("添加进程卡片，4 × 4",false));
                AccessibilityNodeInfo add=find(getUiAutomation().getRootInActiveWindow(),"添加到主屏幕",false);
                if(add==null) add=node("Add to home screen",false); click(add); home();
                ids=manager.getAppWidgetIds(new ComponentName(c,ProcessWidget.class));
            } else home();
            check(ids.length>0,"真实桌面绑定 4×4 Provider");
            showWidgetPage();
            Thread.sleep(FtopWidget.SAMPLE_WINDOW_MS+1200); publish(real); Thread.sleep(600); node("dev.ftop.widget:id/process_list",true);
            screenshot("widget-real-dark");
            Snapshot fixture=Snapshot.from(real.json()); fixture.processes.clear();
            fixture.processStatus="测试数据 · 仅验证滑动；非真实进程";
            for(int i=0;i<24;i++) {
                ProcessEntry p=new ProcessEntry(); p.pid=8000+i; p.started=100+i;
                p.name=String.format(java.util.Locale.ROOT,"测试%s %02d",i%2==0?"系统":"应用",i);
                p.system=i%2==0; p.cpu=i*.5; p.memoryBytes=(32+i)*1048576L; fixture.processes.add(p);
            }
            publish(fixture); Thread.sleep(700); String top=firstVisibleName();
            check(node("dev.ftop.widget:id/process_list",true).performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD),"桌面原生列表接受向下滚动");
            Thread.sleep(450); String scrolled=firstVisibleName(); check(!top.equals(scrolled),"滑动后显示后续进程");
            screenshot("widget-scroll-fixture");
            fixture.time=System.currentTimeMillis(); publish(fixture); Thread.sleep(600);
            String refreshed=firstVisibleName(); screenshot("widget-after-refresh"); check(scrolled.equals(refreshed),"刷新 RemoteViews 保留滚动位置: "+scrolled+" -> "+refreshed);
            Store.theme(c,2); publish(fixture); Thread.sleep(600); screenshot("widget-light-fixture");
            check(scrolled.equals(firstVisibleName()),"浅色主题保留列表位置");
            Snapshot empty=new Snapshot(); empty.processStatus="进程读取受限"; publish(empty); Thread.sleep(600);
            node("暂无可见进程",false); check(true,"空列表展示受限状态");
            Store.theme(c,1); publish(real); Thread.sleep(600);
            click(node("dev.ftop.widget:id/refresh",true)); Thread.sleep(FtopWidget.SAMPLE_WINDOW_MS+1200);
            check(find(getUiAutomation().getRootInActiveWindow(),"测试数据 · 仅验证滑动；非真实进程",false)==null,"正式刷新不显示测试数据");
            screenshot("widget-real-final");
            ActivityMonitor browserMonitor=addMonitor(ProcessBrowserActivity.class.getName(),null,false);
            android.graphics.Rect rowBounds=new android.graphics.Rect();node("dev.ftop.widget:id/process_row",true).getBoundsInScreen(rowBounds);
            long tapTime=android.os.SystemClock.uptimeMillis();
            for(int action:new int[]{android.view.MotionEvent.ACTION_DOWN,android.view.MotionEvent.ACTION_UP}) {
                android.view.MotionEvent tap=android.view.MotionEvent.obtain(tapTime,android.os.SystemClock.uptimeMillis(),action,rowBounds.centerX(),rowBounds.centerY(),0);
                tap.setSource(android.view.InputDevice.SOURCE_TOUCHSCREEN);getUiAutomation().injectInputEvent(tap,true);tap.recycle();
            }
            android.app.Activity browser=waitForMonitorWithTimeout(browserMonitor,4000);
            check(browser!=null&&browser.getIntent().getStringExtra("groupKey")!=null,"桌面应用行直接打开对应应用详情");
            removeMonitor(browserMonitor); home(); showWidgetPage();
            click(node("dev.ftop.widget:id/card",true)); node("设置",false); check(true,"卡片头部仍可打开详情");
            home();
            JSONObject report=new JSONObject().put("checks",checks).put("status","passed")
                .put("scope","真实采样和真实桌面；长列表使用测试 APK 专用数据，正式 APK 不含示意进程");
            write("results.json",report); result.putString("stream",checks.length()+" process checks passed\n"+report);
            finish(-1,result);
        } catch(Throwable e) { result.putString("stream",android.util.Log.getStackTraceString(e)); finish(0,result); }
        finally { if(c!=null&&manager!=null) { Store.theme(c,1); FtopWidget.requestRefresh(c); } }
    }
}
