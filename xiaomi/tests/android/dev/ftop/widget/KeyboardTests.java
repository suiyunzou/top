package dev.ftop.widget;
import android.app.*;
import android.content.*;
import android.os.*;
import android.view.*;
import android.view.inputmethod.InputMethodManager;
import android.widget.*;
import java.lang.reflect.Field;
/** Checks actual IME insets and state restoration on the running native page. */
public final class KeyboardTests extends Instrumentation {
    public void onCreate(Bundle b){super.onCreate(b);start();}
    private Object field(Object o,String key)throws Exception{Field f=o.getClass().getDeclaredField(key);f.setAccessible(true);return f.get(o);}
    private void check(boolean ok,String message){if(!ok)throw new AssertionError(message);}
    public void onStart(){Bundle result=new Bundle();Activity opened=null;
        try{
            Activity a=startActivitySync(new Intent(getTargetContext(),ProcessBrowserActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));opened=a;Thread.sleep(1800);
            EditText search=(EditText)field(a,"search");ListView list=(ListView)field(a,"list");
            runOnMainSync(()->{search.requestFocus();((InputMethodManager)a.getSystemService(Context.INPUT_METHOD_SERVICE)).showSoftInput(search,InputMethodManager.SHOW_IMPLICIT);});
            Thread.sleep(1600);check(a.getWindow().getDecorView().getRootWindowInsets().isVisible(WindowInsets.Type.ime()),"真实键盘显示");
            check(list.getHeight()>=48*a.getResources().getDisplayMetrics().density,"键盘打开后列表仍有可操作空间");
            runOnMainSync(()->search.setText("ftop"));check(list.getCount()>0,"键盘显示时搜索仍返回真实结果");
            Thread.sleep(350);android.graphics.Bitmap shot=getUiAutomation().takeScreenshot();try(java.io.FileOutputStream f=new java.io.FileOutputStream(new java.io.File(getTargetContext().getExternalFilesDir(null),"keyboard.png"))){shot.compress(android.graphics.Bitmap.CompressFormat.PNG,100,f);}shot.recycle();
            try(android.os.ParcelFileDescriptor back=getUiAutomation().executeShellCommand("input keyevent 4")){Thread.sleep(500);}check(!a.isFinishing(),"第一次返回只关闭键盘");
            ActivityMonitor monitor=addMonitor(ProcessBrowserActivity.class.getName(),null,false);runOnMainSync(a::recreate);
            Activity recreated=monitor.waitForActivityWithTimeout(4000);removeMonitor(monitor);check(recreated!=null,"页面重建完成");opened=recreated;Thread.sleep(500);
            check("ftop".contentEquals(((EditText)field(recreated,"search")).getText()),"重建保留搜索词");
            result.putString("stream","6 keyboard and restoration checks passed");finish(-1,result);
        }catch(Throwable e){result.putString("stream",android.util.Log.getStackTraceString(e));finish(0,result);}
        finally{if(opened!=null){Activity a=opened;runOnMainSync(a::finish);}}
    }
}
