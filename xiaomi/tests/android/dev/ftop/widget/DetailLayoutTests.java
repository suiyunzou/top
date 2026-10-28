package dev.ftop.widget;

import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import org.json.JSONArray;
import org.json.JSONObject;

/** Synthetic layout/navigation fixtures only. Never invokes process/app termination.
 * Run separately at 360dp with fontScale=1/light and fontScale=2/dark.
 * Optional instrumentation args: scenario, expectedWidthDp, expectedFontScale, expectedDark.
 * The runner observes configuration; it does not change device settings.
 */
public final class DetailLayoutTests extends Instrumentation {
    private static final String PACKAGE="dev.ftop.layoutfixture.long.application.name";
    private static final String LABEL="布局夹具 · 长名称应用示例";
    private static final String WORKER="background_financial_component_layout_fixture_with_a_very_long_process_name";
    private final JSONArray checks=new JSONArray();
    private Bundle arguments;
    private File output;
    private ProcessBrowserActivity activity;

    @Override public void onCreate(Bundle args){super.onCreate(args);arguments=args==null?new Bundle():args;start();}
    private Object field(Object target,String name)throws Exception{Field f=target.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(target);}
    private void set(Object target,String name,Object value)throws Exception{Field f=target.getClass().getDeclaredField(name);f.setAccessible(true);f.set(target,value);}
    private void call(Object target,String name,Class<?>[] types,Object... args)throws Exception{Method m=target.getClass().getDeclaredMethod(name,types);m.setAccessible(true);m.invoke(target,args);}
    private interface MainCheck{void run()throws Exception;}
    private void main(MainCheck operation){final Throwable[] failure={null};runOnMainSync(()->{try{operation.run();}catch(Throwable e){failure[0]=e;}});waitForIdleSync();if(failure[0]!=null)throw new AssertionError("Main-thread layout check failed",failure[0]);}
    private void check(boolean valid,String message){if(!valid)throw new AssertionError(message);checks.put(message);}
    private ProcessEntry process(int pid,String suffix,long memory,double cpu){
        ProcessEntry p=new ProcessEntry();p.pid=pid;p.uid=19991;p.started=pid;
        p.packageName=PACKAGE;p.appLabel=LABEL;p.rawName=PACKAGE+suffix;p.name=p.rawName;
        p.memoryBytes=memory*1024*1024;p.memoryKind="RSS";p.cpu=cpu;p.state="S";return p;
    }
    private Snapshot fixture(){
        Snapshot s=new Snapshot();s.time=System.currentTimeMillis();s.processStatus="仅布局测试的合成数据，不是设备实测";
        s.processes.add(process(81001,"",240,4));
        s.processes.add(process(81002,":"+WORKER,96,28));
        s.processes.add(process(81003,":push_layout_fixture",48,2));return s;
    }
    private void inject(Snapshot snapshot){main(()->{set(activity,"snapshot",snapshot);call(activity,"render",new Class<?>[]{boolean.class},true);});}
    private void click(String name){main(()->((View)field(activity,name)).performClick());}
    private void chooseSort(String text)throws Exception{
        click("sort");boolean selected=false;
        long until=System.currentTimeMillis()+2500;
        while(!selected&&System.currentTimeMillis()<until){
            AccessibilityNodeInfo root=getUiAutomation().getRootInActiveWindow();
            if(root!=null){for(AccessibilityNodeInfo node:root.findAccessibilityNodeInfosByText(text)){
                if(node.getText()!=null&&text.contentEquals(node.getText())||node.getContentDescription()!=null&&text.contentEquals(node.getContentDescription()))selected=node.performAction(AccessibilityNodeInfo.ACTION_CLICK)||selected;
                node.recycle();
            }root.recycle();}
            if(!selected)Thread.sleep(80);
        }
        check(selected,"排序弹窗可选择："+text);waitForIdleSync();
    }
    private void dimensions()throws Exception{
        waitForIdleSync();float density=activity.getResources().getDisplayMetrics().density;
        main(()->{
            for(String name:new String[]{"sort","searchToggle","stopApp"}){
                Button button=(Button)field(activity,name);
                check(button.getWidth()>=Math.round(48*density)&&button.getHeight()>=Math.round(48*density),name+" 触摸区至少 48dp");
                Rect controlVisible=new Rect();check(button.getGlobalVisibleRect(controlVisible)&&controlVisible.height()>=Math.round(48*density)&&controlVisible.width()>=Math.round(48*density),name+" 实际可见触摸区至少 48dp");
            }
            ListView list=(ListView)field(activity,"list");Rect visible=new Rect();
            check(list.getGlobalVisibleRect(visible)&&visible.height()>=Math.round(64*density),"列表实际可见空间至少 64dp");
            Button stop=(Button)field(activity,"stopApp");Rect stopVisible=new Rect();
            check(stop.getGlobalVisibleRect(stopVisible)&&stopVisible.height()>=Math.round(48*density),"停止应用按钮实际可见触摸区至少 48dp");
        });
    }
    private void shot(String name)throws Exception{
        waitForIdleSync();Thread.sleep(250);Bitmap bitmap=getUiAutomation().takeScreenshot();
        check(bitmap!=null,"截图成功："+name);
        try(FileOutputStream stream=new FileOutputStream(new File(output,name+"-synthetic.png"))){bitmap.compress(Bitmap.CompressFormat.PNG,100,stream);}finally{bitmap.recycle();}
    }
    private String summaryText(String name)throws Exception{return ((TextView)field(field(activity,"summary"),name)).getText().toString();}
    private JSONObject report(String status)throws Exception{
        JSONObject j=new JSONObject().put("status",status).put("dataSource","synthetic layout fixture; not device measurements; no stop action executed").put("checks",checks);
        if(activity!=null)j.put("widthDp",activity.getResources().getConfiguration().screenWidthDp).put("fontScale",activity.getResources().getConfiguration().fontScale).put("dark",activity.ui.dark);
        return j;
    }
    private void save(JSONObject report)throws Exception{try(FileOutputStream stream=new FileOutputStream(new File(output,"results.json"))){stream.write(report.toString(2).getBytes(StandardCharsets.UTF_8));}}
    @Override public void onStart(){Bundle result=new Bundle();int resultCode=0;
        try{
            String scenario=arguments.getString("scenario","current").replaceAll("[^a-zA-Z0-9_-]","_");
            output=new File(getTargetContext().getExternalFilesDir(null),"detail-layout-tests/"+scenario);check(output.isDirectory()||output.mkdirs(),"布局测试输出目录可用");
            activity=(ProcessBrowserActivity)startActivitySync(new Intent(getTargetContext(),ProcessBrowserActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            main(()->call(field(activity,"sampler"),"pause",new Class<?>[]{}));
            check(!(boolean)field(field(activity,"sampler"),"active"),"真实采样已暂停，后续值仅用于布局夹具");
            int width=activity.getResources().getConfiguration().screenWidthDp;
            float scale=activity.getResources().getConfiguration().fontScale;
            if(arguments.containsKey("expectedWidthDp"))check(width==Integer.parseInt(arguments.getString("expectedWidthDp")),"目标逻辑宽度匹配");
            if(arguments.containsKey("expectedFontScale"))check(Math.abs(scale-Float.parseFloat(arguments.getString("expectedFontScale")))<.01f,"目标字号比例匹配");
            if(arguments.containsKey("expectedDark"))check(activity.ui.dark==Boolean.parseBoolean(arguments.getString("expectedDark")),"目标明暗主题匹配");
            inject(fixture());
            EditText search=(EditText)field(activity,"search");ListView list=(ListView)field(activity,"list");
            main(()->search.setText("layoutfixture"));
            check(list.getCount()==1,"父级应用搜索返回合成应用");
            main(()->list.performItemClick(list.getChildAt(0),0,list.getAdapter().getItemId(0)));
            main(()->{
                check(search.getVisibility()==View.GONE,"应用详情默认隐藏搜索输入框");
                check(list.getCount()==3,"主进程和两个子进程均显示");
                check(((ProcessEntry)list.getAdapter().getItem(0)).pid==81001,"默认内存降序");
                check(LABEL.equals(summaryText("name")),"详情显示真实绑定的应用名称夹具");
                check(((Button)field(activity,"stopApp")).isEnabled(),"有进程时停止按钮可操作但测试不点击");
            });
            dimensions();shot("detail-default");
            click("searchToggle");main(()->search.setText(WORKER));
            main(()->{check(search.getVisibility()==View.VISIBLE,"搜索按需展开");Rect searchVisible=new Rect();check(search.getGlobalVisibleRect(searchVisible)&&searchVisible.height()>=Math.round(48*activity.getResources().getDisplayMetrics().density),"展开的搜索输入框实际可见且至少 48dp");check(list.getCount()==1&&((ProcessEntry)list.getAdapter().getItem(0)).pid==81002,"长子进程名称可过滤");});
            shot("detail-filtered");click("searchToggle");
            main(()->{check(search.getVisibility()==View.GONE&&search.getText().length()==0,"收起搜索同时清除关键词");check(list.getCount()==3,"收起搜索恢复全部子进程");});
            chooseSort("CPU 从高到低");
            main(()->check(((ProcessEntry)list.getAdapter().getItem(0)).pid==81002,"CPU 排序改变实际进程顺序"));
            chooseSort("内存从高到低");
            main(()->check(((ProcessEntry)list.getAdapter().getItem(0)).pid==81001,"内存排序恢复实际进程顺序"));
            main(activity::onBackPressed);
            main(()->{check("".equals(field(activity,"selected")),"返回恢复应用列表层级");check("layoutfixture".contentEquals(search.getText()),"返回保留父级应用搜索");check(list.getCount()==1,"父级搜索结果保留");});
            main(()->list.performItemClick(list.getChildAt(0),0,list.getAdapter().getItemId(0)));
            Snapshot missing=new Snapshot();missing.time=System.currentTimeMillis();inject(missing);
            main(()->{
                check(LABEL.equals(summaryText("name"))&&PACKAGE.equals(field(activity,"detailPackage")),"进程消失后保留最后已知应用名称和内部包名");
                check(((View)field(field(activity,"summary"),"subtitle")).getVisibility()==View.GONE,"摘要默认隐藏技术包名");
                check("—".equals(summaryText("firstValue"))&&"—".equals(summaryText("secondValue")),"进程消失后 CPU 与内存显示未知，不能显示零");
                check(list.getCount()==0,"进程消失后无陈旧进程行");
                check(!((Button)field(activity,"stopApp")).isEnabled(),"进程消失后停止应用禁用");
            });
            shot("detail-no-processes");JSONObject report=report("passed");save(report);result.putString("stream",report.toString(2));resultCode=-1;
        }catch(Throwable error){String stack=android.util.Log.getStackTraceString(error);result.putString("stream",stack);try{if(output!=null)save(report("failed").put("error",stack));}catch(Exception ignored){}}
        finally{if(activity!=null&&!activity.isFinishing())runOnMainSync(activity::finish);finish(resultCode,result);}
    }
}
