package dev.ftop.widget.tests;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.view.accessibility.AccessibilityNodeInfo;
import dev.ftop.widget.MainActivity;
import dev.ftop.widget.Snapshot;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;

/** Uses platform UiAutomation directly: no global accessibility-idle wait on a live monitor. */
public final class UiTests extends Instrumentation {
    private Context c;
    private File out;
    private final JSONArray checks=new JSONArray();
    @Override public void onCreate(Bundle args) { super.onCreate(args); start(); }
    private AccessibilityNodeInfo search(AccessibilityNodeInfo n,String value,boolean id,boolean prefix) {
        if(n==null) return null;
        String actual=id?n.getViewIdResourceName():n.getText()!=null?n.getText().toString():n.getContentDescription()==null?"":n.getContentDescription().toString();
        if(actual!=null && (prefix?actual.startsWith(value):actual.equals(value))) return n;
        for(int i=0;i<n.getChildCount();i++) { AccessibilityNodeInfo found=search(n.getChild(i),value,id,prefix); if(found!=null)return found; }
        return null;
    }
    private AccessibilityNodeInfo node(String value,boolean id,boolean prefix) throws Exception {
        for(int i=0;i<40;i++) {
            AccessibilityNodeInfo n=search(getUiAutomation().getRootInActiveWindow(),value,id,prefix);
            if(n!=null)return n; Thread.sleep(150);
        }
        throw new AssertionError("Missing UI: "+value);
    }
    private void click(AccessibilityNodeInfo n) throws Exception {
        if(!n.performAction(AccessibilityNodeInfo.ACTION_CLICK)) throw new AssertionError("click failed"); Thread.sleep(550);
    }
    private void theme(String target) throws Exception {
        click(node("设置",false,false));click(node("外观，",false,true));click(node(target,false,false));
        sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_BACK);Thread.sleep(800);node("设置",false,false);
    }
    private void screen(String name) throws Exception {
        Thread.sleep(250); Bitmap b=getUiAutomation().takeScreenshot();
        if(b==null)throw new AssertionError("screenshot failed");
        try(FileOutputStream f=new FileOutputStream(new File(out,name+".png"))) { b.compress(Bitmap.CompressFormat.PNG,100,f); } b.recycle();
    }
    private void home() throws Exception {
        c.startActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); Thread.sleep(600);
    }
    private Snapshot shown(Activity a) throws Exception {
        Field f=MainActivity.class.getDeclaredField("shown"); f.setAccessible(true); return (Snapshot)f.get(a);
    }
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            c=getTargetContext(); out=new File(c.getExternalFilesDir(null),"ui-tests"); out.mkdirs();
            ActivityMonitor monitor=addMonitor(MainActivity.class.getName(),null,false);
            c.startActivity(new Intent(c,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            node("设置",false,false); theme("浅色"); screen("main-light");
            theme("深色"); screen("main-dark"); checks.put("浅色和深色切换、截图");
            Activity activity=monitor.getLastActivity();
            long before=shown(activity).time; Thread.sleep(1500);
            if(shown(activity).time<=before)throw new AssertionError("live sampler stopped");
            checks.put("详情页持续采样");
            home();
            // The host is outside this package; find the actual app widget by resource ID.
            AccessibilityNodeInfo card=search(getUiAutomation().getRootInActiveWindow(),"dev.ftop.widget:id/card",true,false);
            if(card==null) {
                c.startActivity(new Intent(c,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                click(node("设置",false,false));click(node("添加监控卡片",false,false));
                AccessibilityNodeInfo add=search(getUiAutomation().getRootInActiveWindow(),"添加到主屏幕",false,false);
                if(add==null)add=node("Add to home screen",false,false);
                click(add); home();
            }
            card=node("dev.ftop.widget:id/card",true,false); Thread.sleep(1000);
            for(int i=0;i<50&&!node("dev.ftop.widget:id/refresh",true,false).isEnabled();i++) Thread.sleep(150);
            card=node("dev.ftop.widget:id/card",true,false);
            String description=card.getContentDescription().toString();
            click(node("dev.ftop.widget:id/refresh",true,false));
            node("刷新中…",false,false); checks.put("点击后显示刷新中反馈");
            String updated=description;
            for(int i=0;i<50&&description.equals(updated);i++) {
                Thread.sleep(150); updated=node("dev.ftop.widget:id/card",true,false).getContentDescription().toString();
            }
            if(description.equals(updated))throw new AssertionError("widget refresh timestamp unchanged");
            checks.put("桌面刷新更新真实采样时间"); screen("widget-dark");
            Thread.sleep(1300); activity=monitor.getLastActivity(); final long paused=shown(activity).time;
            Thread.sleep(2500);
            if(shown(activity).time!=paused)throw new AssertionError("background sampling continues");
            checks.put("退出详情页后停止采样");
            click(node("dev.ftop.widget:id/card",true,false)); node("设置",false,false);
            Thread.sleep(1500); activity=monitor.getLastActivity();
            if(shown(activity).time<=paused)throw new AssertionError("sample did not resume");
            checks.put("点击卡片打开详情并恢复采样");
            theme("浅色"); home(); node("dev.ftop.widget:id/card",true,false); screen("widget-light");
            click(node("dev.ftop.widget:id/card",true,false)); theme("深色"); home();
            JSONObject report=new JSONObject().put("checks",checks).put("status","passed")
                .put("scope","Android emulator only; Xiaomi launcher and physical sensors pending");
            try(FileOutputStream f=new FileOutputStream(new File(out,"ui-results.json"))) { f.write(report.toString(2).getBytes(StandardCharsets.UTF_8)); }
            result.putString("stream",report.toString(2)); finish(-1,result);
        } catch(Throwable e) { result.putString("stream",android.util.Log.getStackTraceString(e)); finish(0,result); }
    }
}
