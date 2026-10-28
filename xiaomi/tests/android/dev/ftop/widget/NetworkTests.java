package dev.ftop.widget;
import android.app.*;
import android.content.*;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.lang.reflect.*;
import java.util.*;
import org.json.*;
public final class NetworkTests extends Instrumentation {
    private int checks;private boolean denied;private Activity opened;
    public void onCreate(Bundle b){super.onCreate(b);denied=b!=null&&b.getString("denied","false").equals("true");start();}
    private void check(boolean ok,String text){if(!ok)throw new AssertionError(text);checks++;}
    private Object field(Object o,String name)throws Exception{Field f=o.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(o);}
    public void onStart(){Bundle result=new Bundle();try{
        Context c=getTargetContext();Thread.sleep(900);
        if(denied){check(!ShizukuAccess.authorized(),"permission revoked");
            try{ShizukuAccess.request(PrivilegedSampler.NETWORK,null,"",false);throw new AssertionError("network accepted without permission");}catch(SecurityException expected){checks++;}
        }else{check(ShizukuAccess.authorized(),"Shizuku is authorized");JSONObject data=ShizukuAccess.request(PrivilegedSampler.NETWORK,null,"",false);check(data.getJSONArray("rows").length()>0,"real Shizuku network IPC");}
        NetworkActivity a=(NetworkActivity)startActivitySync(NetworkActivity.intent(c,true).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));opened=a;Thread.sleep(4500);
        check((Boolean)field(a,"upload"),"upload entry preserves sort");
        ListView list=(ListView)field(a,"list");check(((View)list.getParent()).getHeight()>=48*c.getResources().getDisplayMetrics().density,"list or empty state usable with current font and screen");
        if(denied){check(((TextView)field(a,"empty")).getText().toString().contains("Shizuku"),"unavailable explanation, no fake rows");check(list.getCount()==0,"no stale data after denial");}
        else{
            List<?> readings=(List<?>)field(a,"readings");check(!readings.isEmpty(),"page sampled real counters");
            NetworkCounters.Reading reading=(NetworkCounters.Reading)readings.get(0);check(Double.isFinite(reading.rx),"second sample produces rate");
            // Synthetic session traffic exercises formatting and layout without external downloads.
            JSONObject data=new JSONObject().put("at",SystemClock.elapsedRealtime()+3000).put("rows",new JSONArray().put(new JSONObject().put("uid",99001).put("rx",0).put("tx",0).put("label","测试应用 · 共享 UID 示例").put("packages",new JSONArray().put("dev.ftop.network.fixture"))));
            Method accept=NetworkActivity.class.getDeclaredMethod("accept",JSONObject.class);accept.setAccessible(true);
            runOnMainSync(()->{try{accept.invoke(a,data);data.put("at",data.getLong("at")+3000);data.getJSONArray("rows").getJSONObject(0).put("rx",6144).put("tx",3072);accept.invoke(a,data);}catch(Exception e){throw new RuntimeException(e);}});
            check(list.getCount()==1,"active UID appears in list");Thread.sleep(250);check(list.getChildAt(0).getHeight()>0,"row rendered");
        }
        Button down=(Button)field(a,"down");runOnMainSync(down::performClick);check(!(Boolean)field(a,"upload")&&down.isSelected(),"download sort selected");
        Thread.sleep(300);android.graphics.Bitmap shot=getUiAutomation().takeScreenshot();try(java.io.FileOutputStream out=new java.io.FileOutputStream(new java.io.File(c.getExternalFilesDir(null),"network-test.png"))){shot.compress(android.graphics.Bitmap.CompressFormat.PNG,100,out);}shot.recycle();
        runOnMainSync(a::finish);Thread.sleep(200);check(!(Boolean)field(a,"active"),"sampling stops when page leaves");opened=null;
        // Apply actual RemoteViews and use each widget PendingIntent.
        if(!denied)for(boolean expanded:new boolean[]{false,true}){
            android.widget.RemoteViews rv=FtopWidget.views(c,new Snapshot(),340,expanded?400:180,expanded);final View[] host=new View[1];runOnMainSync(()->host[0]=rv.apply(c,new FrameLayout(c)));
            check(host[0].findViewById(R.id.network_upload).getVisibility()==View.VISIBLE,"widget upload target present");
            for(int id:new int[]{R.id.network_download,R.id.network_upload}){ActivityMonitor monitor=addMonitor(NetworkActivity.class.getName(),null,false);runOnMainSync(()->host[0].findViewById(id).performClick());Activity page=monitor.waitForActivityWithTimeout(4000);removeMonitor(monitor);check(page!=null,"widget click opens network page");opened=page;check((Boolean)field(page,"upload")== (id==R.id.network_upload),"widget passes correct sorting");runOnMainSync(page::finish);opened=null;}
        }
        if(!denied){
            // Dispatch by visual coordinates, so overlapping refresh hit areas are tested too.
            for(int height:new int[]{130,180}){
                Activity container=startActivitySync(new Intent(c,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));opened=container;
                final View[] host=new View[1];float density=c.getResources().getDisplayMetrics().density;
                runOnMainSync(()->{host[0]=FtopWidget.views(c,new Snapshot(),340,height,false).apply(c,new FrameLayout(c));FrameLayout frame=new FrameLayout(c);frame.addView(host[0],new FrameLayout.LayoutParams((int)(340*density),(int)(height*density)));container.setContentView(frame);});waitForIdleSync();Thread.sleep(300);
                ActivityMonitor monitor=addMonitor(NetworkActivity.class.getName(),null,false);
                runOnMainSync(()->{float x=275*density,y=(height<155?97:105)*density;long time=SystemClock.uptimeMillis();MotionEvent press=MotionEvent.obtain(time,time,MotionEvent.ACTION_DOWN,x,y,0),release=MotionEvent.obtain(time,time+50,MotionEvent.ACTION_UP,x,y,0);host[0].dispatchTouchEvent(press);host[0].dispatchTouchEvent(release);press.recycle();release.recycle();});
                Activity page=monitor.waitForActivityWithTimeout(4000);removeMonitor(monitor);check(page!=null,"upload rate wins over refresh hit area at height "+height);opened=page;check((Boolean)field(page,"upload"),"coordinate tap opens upload");runOnMainSync(()->{page.finish();container.finish();});opened=null;
            }
        }
        result.putString("stream",checks+" network checks passed");finish(-1,result);
    }catch(Throwable e){result.putString("stream",android.util.Log.getStackTraceString(e));finish(0,result);}finally{if(opened!=null){Activity a=opened;runOnMainSync(a::finish);}}}
}
