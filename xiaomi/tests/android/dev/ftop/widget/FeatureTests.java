package dev.ftop.widget;

import android.app.*;
import android.content.*;
import android.os.*;
import android.view.*;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.*;
import android.graphics.Bitmap;
import java.io.*;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import org.json.*;

/** Uses the real Shizuku service and a separate fixture APK; no production test hooks. */
public final class FeatureTests extends Instrumentation {
    private final JSONArray checks=new JSONArray();private File out;
    public void onCreate(Bundle b){super.onCreate(b);start();}
    private void check(boolean ok,String name){if(!ok)throw new AssertionError(name);checks.put(name);}
    private Object field(Object object,String name)throws Exception{Field f=object.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(object);}
    private AccessibilityNodeInfo find(AccessibilityNodeInfo root,String text){
        if(root==null)return null;if(root.getText()!=null&&root.getText().toString().startsWith(text)&&root.isVisibleToUser())return root;
        for(int i=0;i<root.getChildCount();i++){AccessibilityNodeInfo n=find(root.getChild(i),text);if(n!=null)return n;}return null;
    }
    private AccessibilityNodeInfo node(String text)throws Exception{for(int i=0;i<60;i++){AccessibilityNodeInfo n=find(getUiAutomation().getRootInActiveWindow(),text);if(n!=null)return n;Thread.sleep(150);}throw new AssertionError("Missing "+text);}
    private void click(String text)throws Exception{
        node(text);AccessibilityNodeInfo target=null;
        for(AccessibilityNodeInfo n:getUiAutomation().getRootInActiveWindow().findAccessibilityNodeInfosByText(text))if(n.isClickable()&&n.isVisibleToUser()){target=n;break;}
        check(target!=null&&target.performAction(AccessibilityNodeInfo.ACTION_CLICK),"点击 "+text);Thread.sleep(200);
    }
    private void screen(String name)throws Exception{Thread.sleep(250);Bitmap b=getUiAutomation().takeScreenshot();try(FileOutputStream f=new FileOutputStream(new File(out,name+".png"))){b.compress(Bitmap.CompressFormat.PNG,100,f);}b.recycle();}
    @Override public void onStart(){Bundle result=new Bundle();Activity activity=null;
        try{
            Context c=getTargetContext();out=new File(c.getExternalFilesDir(null),"feature-tests");out.mkdirs();
            c.startActivity(new Intent().setComponent(new ComponentName("dev.ftop.fixture","dev.ftop.fixture.FixtureActivity")).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));Thread.sleep(800);
            activity=startActivitySync(new Intent(c,ProcessBrowserActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            ProcessBrowserActivity browser=(ProcessBrowserActivity)activity;Snapshot s=null;
            for(int i=0;i<50;i++){Thread.sleep(200);s=(Snapshot)field(browser,"snapshot");if(s.processes.stream().filter(p->p.packageName.equals("dev.ftop.fixture")).count()==2)break;}
            check(s!=null&&s.processes.size()>64,"Shizuku 返回超过 64 个真实进程");
            check(s.processes.stream().filter(p->p.packageName.equals("dev.ftop.fixture")).count()==2,"真实双进程归属于同一个应用");
            AppAdapter adapter=(AppAdapter)field(browser,"apps");ListView list=(ListView)field(browser,"list");
            int position=-1;for(int i=0;i<adapter.getCount();i++)if(adapter.getItem(i).packageName.equals("dev.ftop.fixture"))position=i;
            check(position>=0,"应用首页包含测试靶标");screen("apps-live");final int chosen=position;
            runOnMainSync(()->list.performItemClick(null,chosen,adapter.getItemId(chosen)));Thread.sleep(350);
            check(list.getAdapter().getCount()==2,"点击应用展开两个真实子进程");screen("fixture-processes");
            ProcessAdapter children=(ProcessAdapter)list.getAdapter();int worker=children.getItem(0).rawName.endsWith(":worker")?0:1;
            final int workerRow=worker;runOnMainSync(()->list.performItemClick(null,workerRow,children.getItemId(workerRow)));
            node("更多操作");node("近期日志");Thread.sleep(700);screen("process-detail");click("近期日志");
            node("当前 PID 本次启动");screen("process-logs");sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_BACK);Thread.sleep(250);sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_BACK);Thread.sleep(350);
            android.graphics.Rect bounds=new android.graphics.Rect();runOnMainSync(()->list.getChildAt(workerRow-list.getFirstVisiblePosition()).getGlobalVisibleRect(bounds));
            long down=SystemClock.uptimeMillis();MotionEvent first=MotionEvent.obtain(down,down,MotionEvent.ACTION_DOWN,bounds.centerX(),bounds.centerY(),0);first.setSource(InputDevice.SOURCE_TOUCHSCREEN);getUiAutomation().injectInputEvent(first,true);first.recycle();Thread.sleep(700);
            MotionEvent last=MotionEvent.obtain(down,SystemClock.uptimeMillis(),MotionEvent.ACTION_UP,bounds.centerX(),bounds.centerY(),0);last.setSource(InputDevice.SOURCE_TOUCHSCREEN);getUiAutomation().injectInputEvent(last,true);last.recycle();
            node("操作 · dev.ftop.fixture:worker");check(true,"真实长按手势打开操作菜单");
            check(find(getUiAutomation().getRootInActiveWindow(),"结束此进程")==null,"shell 菜单不冒充支持单 PID 结束");screen("process-actions");
            click("强行停止所属应用");node("将作用于整个应用");screen("confirm-scope");
            click("取消");Thread.sleep(300);check(list.getAdapter().getCount()==2,"取消确认没有结束测试进程");
            // Use the explicit details button for the second action path.
            runOnMainSync(()->list.performItemClick(null,workerRow,children.getItemId(workerRow)));click("更多操作");click("强行停止所属应用");
            click("强行停止应用");node("操作结果");node("已核实退出");screen("action-result");click("知道了");sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_BACK);
            Thread.sleep(2000);check(list.getAdapter().getCount()==0,"停止后应用详情刷新为空，不保留旧 PID");screen("after-stop");
            JSONObject report=new JSONObject().put("checks",checks).put("status","passed");
            try(FileOutputStream f=new FileOutputStream(new File(out,"results.json"))){f.write(report.toString(2).getBytes(StandardCharsets.UTF_8));}
            result.putString("stream",checks.length()+" feature checks passed\n"+report);finish(-1,result);
        }catch(Throwable e){result.putString("stream",android.util.Log.getStackTraceString(e));finish(0,result);}
        finally{if(activity!=null){Activity a=activity;runOnMainSync(a::finish);}}
    }
}
