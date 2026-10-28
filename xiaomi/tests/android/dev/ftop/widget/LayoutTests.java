package dev.ftop.widget;

import android.app.*;
import android.content.*;
import android.os.*;
import android.view.*;
import android.widget.*;
import android.graphics.Bitmap;
import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import org.json.*;

/** Real activity navigation, original sampling and adaptive App-only rows. */
public final class LayoutTests extends Instrumentation {
    private final JSONArray checks=new JSONArray();private File out;private final List<Activity> opened=new ArrayList<>();
    public void onCreate(Bundle b){super.onCreate(b);start();}
    private Object field(Object o,String key)throws Exception{Field f=o.getClass().getDeclaredField(key);f.setAccessible(true);return f.get(o);}
    private void check(boolean value,String label){if(!value)throw new AssertionError(label);checks.put(label);}
    private View find(View v,String label){if(label.contentEquals(v.getContentDescription()==null?"":v.getContentDescription())||v instanceof TextView&&label.equals(((TextView)v).getText().toString()))return v;if(v instanceof ViewGroup)for(int i=0;i<((ViewGroup)v).getChildCount();i++){View found=find(((ViewGroup)v).getChildAt(i),label);if(found!=null)return found;}return null;}
    private View findButton(View v,String label){if(v instanceof Button&&label.equals(((Button)v).getText().toString()))return v;if(v instanceof ViewGroup)for(int i=0;i<((ViewGroup)v).getChildCount();i++){View found=findButton(((ViewGroup)v).getChildAt(i),label);if(found!=null)return found;}return null;}
    private void click(Activity a,String label){View v=findButton(a.getWindow().getDecorView(),label);if(v==null)v=find(a.getWindow().getDecorView(),label);if(v==null)throw new AssertionError("Missing "+label);while(!v.isClickable()&&v.getParent() instanceof View)v=(View)v.getParent();View target=v;runOnMainSync(target::performClick);}
    private Activity launch(Context c,Class<?> page){Activity a=startActivitySync(new Intent(c,page).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));opened.add(a);return a;}
    private Activity next(Activity from,String label,Class<?> type)throws Exception{ActivityMonitor m=addMonitor(type.getName(),null,false);click(from,label);Activity a=m.waitForActivityWithTimeout(4000);removeMonitor(m);check(a!=null,"打开 "+label);opened.add(a);Thread.sleep(250);return a;}
    private void shot(String name)throws Exception{waitForIdleSync();Thread.sleep(900);Bitmap b=getUiAutomation().takeScreenshot();try(FileOutputStream f=new FileOutputStream(new File(out,name+".png"))){b.compress(Bitmap.CompressFormat.PNG,100,f);}b.recycle();}
    @Override public void onStart(){Bundle result=new Bundle();
        try{
            Context c=getTargetContext();out=new File(c.getExternalFilesDir(null),"layout-tests");out.mkdirs();
            MainActivity main=(MainActivity)launch(c,MainActivity.class);Thread.sleep(2400);
            ListView list=(ListView)field(main,"processList");check(list.getCount()>0,"首页显示真实可读应用");
            boolean nested=false;for(ViewParent p=list.getParent();p!=null;p=p.getParent())nested|=p instanceof ScrollView;check(!nested,"应用列表独立滚动");
            check(!list.isVerticalScrollBarEnabled(),"应用列表无滚动条");
            int min=Math.round(48*c.getResources().getDisplayMetrics().density);check(((Button)field(main,"cpuSort")).getHeight()>=min&&((Button)field(main,"memorySort")).getHeight()>=min,"排序触摸区至少 48 dp");
            check(list.getHeight()>=Math.round(64*c.getResources().getDisplayMetrics().density),"小屏及大字号保留可操作列表空间");shot("home");
            click(main,"CPU");check((boolean)field(main,"byCpu"),"CPU 排序响应");click(main,"内存");check(!(boolean)field(main,"byCpu"),"内存排序响应");
            Activity settings=next(main,"设置",SettingsActivity.class);check(find(settings.getWindow().getDecorView(),"添加监控卡片")!=null&&find(settings.getWindow().getDecorView(),"添加进程卡片")!=null,"设置保留两种桌面卡片入口");shot("settings");runOnMainSync(settings::finish);Thread.sleep(350);
            Activity device=next(main,"设备",DeviceActivity.class);check(find(device.getWindow().getDecorView(),"存储可用 / 总量")!=null,"设备详情保留完整指标");shot("details");runOnMainSync(device::finish);Thread.sleep(300);
            ProcessBrowserActivity browser=(ProcessBrowserActivity)next(main,"全部",ProcessBrowserActivity.class);Thread.sleep(2200);
            EditText search=(EditText)field(browser,"search");runOnMainSync(()->search.setText("ftop-no-such-app-314159"));
            ListView appsList=(ListView)field(browser,"list");check(appsList.getCount()==0,"搜索无匹配不显示旧行");check(find(browser.getWindow().getDecorView(),"没有匹配的应用")!=null,"搜索空状态说明原因");shot("empty-search");
            runOnMainSync(()->search.setText(""));AppAdapter adapter=(AppAdapter)field(browser,"apps");check(adapter.getCount()>0,"清空搜索恢复真实应用");String key=adapter.getItem(0).key;
            runOnMainSync(()->appsList.performItemClick(null,0,adapter.getItemId(0)));check(key.equals(field(browser,"selected")),"应用点击进入正确分组");Thread.sleep(300);
            check(appsList.getChildCount()>0&&appsList.getChildAt(0) instanceof ProcessRow,"App 进程行独立于 RemoteViews");shot("app-detail");
            ActivityMonitor detailMonitor=addMonitor(ProcessDetailActivity.class.getName(),null,false);
            runOnMainSync(()->appsList.performItemClick(null,0,appsList.getAdapter().getItemId(0)));Activity detail=detailMonitor.waitForActivityWithTimeout(4000);removeMonitor(detailMonitor);check(detail!=null,"进程详情使用完整原生页面");opened.add(detail);shot("process-detail");
            Activity logs=next(detail,"近期日志",ProcessLogActivity.class);shot("logs");runOnMainSync(logs::finish);runOnMainSync(detail::finish);Thread.sleep(250);
            runOnMainSync(browser::onBackPressed);check("".equals(field(browser,"selected")),"系统返回恢复应用列表");
            runOnMainSync(()->search.setText(adapter.getItem(0).packageName));String saved=search.getText().toString();if(appsList.getCount()>0){runOnMainSync(()->appsList.performItemClick(null,0,appsList.getAdapter().getItemId(0)));runOnMainSync(browser::onBackPressed);check(saved.equals(search.getText().toString()),"返回恢复原搜索条件");}
            JSONObject report=new JSONObject().put("status","passed").put("checks",checks).put("widthDp",c.getResources().getConfiguration().screenWidthDp).put("heightDp",c.getResources().getConfiguration().screenHeightDp).put("fontScale",c.getResources().getConfiguration().fontScale);
            try(FileOutputStream f=new FileOutputStream(new File(out,"results.json"))){f.write(report.toString(2).getBytes(java.nio.charset.StandardCharsets.UTF_8));}result.putString("stream",report.toString(2));finish(-1,result);
        }catch(Throwable e){result.putString("stream",android.util.Log.getStackTraceString(e));finish(0,result);}
        finally{for(int i=opened.size()-1;i>=0;i--){Activity a=opened.get(i);if(!a.isFinishing())runOnMainSync(a::finish);}}
    }
}
