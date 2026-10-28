package dev.ftop.widget;

import android.app.Instrumentation;
import android.content.DialogInterface;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.Button;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicInteger;
import org.json.JSONArray;
import org.json.JSONObject;

/** Synthetic dialogs only: confirmation callbacks count events, never control an application. */
public final class DialogLayoutTests extends Instrumentation {
    private final JSONArray checks=new JSONArray();
    private Bundle arguments;
    private File output;
    private AboutActivity activity;
    private AppDialog dialog;
    private interface Operation{void run()throws Exception;}
    @Override public void onCreate(Bundle args){super.onCreate(args);arguments=args==null?new Bundle():args;start();}
    private void main(Operation operation){final Throwable[] failure={null};runOnMainSync(()->{try{operation.run();}catch(Throwable e){failure[0]=e;}});waitForIdleSync();if(failure[0]!=null)throw new AssertionError("Dialog UI check failed",failure[0]);}
    private void check(boolean valid,String message){if(!valid)throw new AssertionError(message);checks.put(message);}
    private View text(View root,String value){if(root instanceof TextView&&value.contentEquals(((TextView)root).getText()))return root;if(root instanceof ViewGroup)for(int i=0;i<((ViewGroup)root).getChildCount();i++){View found=text(((ViewGroup)root).getChildAt(i),value);if(found!=null)return found;}return null;}
    private View choice(String label){View found=text(dialog.getWindow().getDecorView(),label);if(found==null)throw new AssertionError("Missing choice "+label);while(!found.isClickable()&&found.getParent() instanceof View)found=(View)found.getParent();return found;}
    private boolean scrolling(View root){if(root instanceof ScrollView&&root.canScrollVertically(1))return true;if(root instanceof ViewGroup)for(int i=0;i<((ViewGroup)root).getChildCount();i++)if(scrolling(((ViewGroup)root).getChildAt(i)))return true;return false;}
    private void visibleButton(int which,String label){Button button=dialog.getButton(which);Rect visible=new Rect();int min=Math.round(48*activity.getResources().getDisplayMetrics().density);
        check(button!=null&&button.getWidth()>=min&&button.getHeight()>=min&&button.getGlobalVisibleRect(visible)&&visible.width()>=min&&visible.height()>=min,label+" 默认实际可见触摸区至少 48dp");}
    private void shot(String name)throws Exception{waitForIdleSync();Thread.sleep(250);Bitmap bitmap=getUiAutomation().takeScreenshot();check(bitmap!=null,"截图："+name);try(FileOutputStream stream=new FileOutputStream(new File(output,name+"-synthetic.png"))){bitmap.compress(Bitmap.CompressFormat.PNG,100,stream);}finally{bitmap.recycle();}}
    private JSONObject report(String status)throws Exception{JSONObject result=new JSONObject().put("status",status).put("dataSource","synthetic dialog fixture; callbacks count only; no application control").put("checks",checks);if(activity!=null)result.put("widthDp",activity.getResources().getConfiguration().screenWidthDp).put("fontScale",activity.getResources().getConfiguration().fontScale).put("dark",activity.ui.dark);return result;}
    private void save(JSONObject result)throws Exception{try(FileOutputStream stream=new FileOutputStream(new File(output,"results.json"))){stream.write(result.toString(2).getBytes(StandardCharsets.UTF_8));}}
    @Override public void onStart(){Bundle result=new Bundle();int code=0;try{
        output=new File(getTargetContext().getExternalFilesDir(null),"dialog-layout-tests/"+arguments.getString("scenario","current").replaceAll("[^a-zA-Z0-9_-]","_"));check(output.isDirectory()||output.mkdirs(),"测试输出目录可用");
        activity=(AboutActivity)startActivitySync(new Intent(getTargetContext(),AboutActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        if(arguments.containsKey("expectedWidthDp"))check(activity.getResources().getConfiguration().screenWidthDp==Integer.parseInt(arguments.getString("expectedWidthDp")),"目标逻辑宽度匹配");
        if(arguments.containsKey("expectedFontScale"))check(Math.abs(activity.getResources().getConfiguration().fontScale-Float.parseFloat(arguments.getString("expectedFontScale")))<.01f,"目标字号比例匹配");
        if(arguments.containsKey("expectedDark"))check(activity.ui.dark==Boolean.parseBoolean(arguments.getString("expectedDark")),"目标明暗主题匹配");
        StringBuilder explanation=new StringBuilder("仅布局夹具，不是采样或隐私结论。\n\n");for(int i=1;i<=18;i++)explanation.append(i).append(". 这段合成说明用于检查长内容滚动，操作按钮应保持可见。\n\n");
        main(()->dialog=new AppDialog.Builder(activity).setTitle("长说明 · 布局夹具").setMessage(explanation).setPositiveButton("关闭",null).show());
        main(()->{visibleButton(DialogInterface.BUTTON_POSITIVE,"长说明关闭按钮");check(scrolling(dialog.getWindow().getDecorView()),"长说明有可滚动内容");});shot("long-explanation");main(dialog::dismiss);
        AtomicInteger selected=new AtomicInteger(-1);
        main(()->dialog=new AppDialog.Builder(activity).setTitle("排序 · 布局夹具").setSingleChoiceItems(new String[]{"内存从高到低","CPU 从高到低"},0,(d,which)->selected.set(which)).setNegativeButton("取消",null).show());
        main(()->{View first=choice("内存从高到低");check(first.isSelected(),"单选初始项选中");choice("CPU 从高到低").performClick();check(selected.get()==1,"单选回调返回真实索引");View second=choice("CPU 从高到低");check(second.isSelected()&&!first.isSelected(),"单选切换真实选中状态");AccessibilityNodeInfo node=second.createAccessibilityNodeInfo();check(node.isCheckable()&&node.isChecked(),"无障碍单选状态同步");node.recycle();check(dialog.isShowing(),"单选调用方可保留弹窗");visibleButton(DialogInterface.BUTTON_NEGATIVE,"单选取消按钮");});shot("single-choice");main(dialog::dismiss);
        main(()->dialog=new AppDialog.Builder(activity).setTitle("选择后关闭 · 夹具").setSingleChoiceItems(new String[]{"内存","CPU"},0,(d,which)->{selected.set(which);d.dismiss();}).show());
        main(()->{choice("CPU").performClick();check(selected.get()==1&&!dialog.isShowing(),"单选回调可关闭弹窗");});
        AtomicInteger executed=new AtomicInteger();
        main(()->dialog=new AppDialog.Builder(activity).setTitle("停止确认 · 纯布局夹具").setMessage("仅统计点击，不会停止任何应用。").setPositiveButton("确认",(d,which)->executed.incrementAndGet()).setNegativeButton("取消",null).show());
        main(()->{visibleButton(DialogInterface.BUTTON_POSITIVE,"确认按钮");visibleButton(DialogInterface.BUTTON_NEGATIVE,"取消按钮");});shot("confirmation");
        main(()->{dialog.getButton(DialogInterface.BUTTON_NEGATIVE).performClick();check(executed.get()==0&&!dialog.isShowing(),"取消关闭且不执行确认回调");});
        main(()->dialog=new AppDialog.Builder(activity).setTitle("重复点击 · 纯布局夹具").setMessage("仅统计点击。").setPositiveButton("确认",(d,which)->executed.incrementAndGet()).setNegativeButton("取消",null).show());
        main(()->{Button button=dialog.getButton(DialogInterface.BUTTON_POSITIVE);button.performClick();button.performClick();check(executed.get()==1&&!dialog.isShowing(),"一次确认最多执行一次回调");});
        main(()->dialog=new AppDialog.Builder(activity).setTitle("读取中 · 布局夹具").setMessage("旧消息").setCancelable(false).show());
        main(()->{dialog.onBackPressed();check(dialog.isShowing(),"不可取消进度忽略返回");dialog.setMessage("动态消息已更新");check(text(dialog.getWindow().getDecorView(),"动态消息已更新")!=null,"动态 setMessage 更新可见内容");check(text(dialog.getWindow().getDecorView(),"旧消息")==null,"动态更新未保留旧消息");});shot("dynamic-progress");main(dialog::dismiss);
        JSONObject report=report("passed");save(report);result.putString("stream",report.toString(2));code=-1;
    }catch(Throwable error){String stack=android.util.Log.getStackTraceString(error);result.putString("stream",stack);try{if(output!=null)save(report("failed").put("error",stack));}catch(Exception ignored){}}
    finally{if(dialog!=null)runOnMainSync(dialog::dismiss);if(activity!=null&&!activity.isFinishing())runOnMainSync(activity::finish);finish(code,result);}}
}
