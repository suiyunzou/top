package dev.ftop.widget;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.view.View;
import android.widget.RemoteViews;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public class FtopWidget extends AppWidgetProvider {
    public static final String REFRESH="dev.ftop.widget.REFRESH";
    // K80 TrafficStats caches counters for 1000 ms. Use a longer real measurement window.
    public static final long SAMPLE_WINDOW_MS=2000;
    private static final ExecutorService WORK=Executors.newSingleThreadExecutor();
    private static final AtomicBoolean BUSY=new AtomicBoolean();
    @Override public void onReceive(Context c,Intent intent) {
        String action=intent.getAction();
        if(REFRESH.equals(action) || "miui.appwidget.action.APPWIDGET_UPDATE".equals(action)) { refresh(c); return; }
        super.onReceive(c,intent);
    }
    @Override public void onUpdate(Context c,AppWidgetManager m,int[] ids) { refresh(c); }
    @Override public void onAppWidgetOptionsChanged(Context c,AppWidgetManager m,int id,Bundle options) {
        render(c,m,id,Store.snapshot(c)); refresh(c);
    }
    private void refresh(Context c) {
        if(!BUSY.compareAndSet(false,true)) return;
        final PendingResult pending=goAsync();
        final Context app=c.getApplicationContext();
        WORK.execute(() -> {
            try {
                feedback(app,"刷新中…","正在采样网速，约 2 秒",false);
                Sampler sampler=new Sampler(app); sampler.sample();
                Thread.sleep(SAMPLE_WINDOW_MS); Snapshot s=sampler.sample(); Store.save(app,s);
                AppWidgetManager manager=AppWidgetManager.getInstance(app);
                for(Class<?> provider:new Class<?>[]{FtopWidget.class,ProcessWidget.class})
                    for(int id:manager.getAppWidgetIds(new ComponentName(app,provider))) render(app,manager,id,s);
            } catch(Exception e) {
                android.util.Log.e("ftop","Widget update failed",e);
                feedback(app,"重试","刷新失败，保留上次数据",true);
            }
            finally { BUSY.set(false); if(pending!=null) pending.finish(); }
        });
    }
    private static void feedback(Context c,String label,String status,boolean enabled) {
        AppWidgetManager manager=AppWidgetManager.getInstance(c);
        for(Class<?> provider:new Class<?>[]{FtopWidget.class,ProcessWidget.class}) {
            boolean expanded=provider==ProcessWidget.class;
            RemoteViews patch=new RemoteViews(c.getPackageName(),expanded?R.layout.widget_expanded:R.layout.widget);
            patch.setTextViewText(R.id.refresh,label); patch.setBoolean(R.id.refresh,"setEnabled",enabled);
            patch.setContentDescription(R.id.refresh,status);
            if(expanded) patch.setTextViewText(R.id.updated,status);
            for(int id:manager.getAppWidgetIds(new ComponentName(c,provider)))
                manager.partiallyUpdateAppWidget(id,patch);
        }
    }
    public static void requestRefresh(Context c) { c.sendBroadcast(new Intent(c,FtopWidget.class).setAction(REFRESH)); }
    public static RemoteViews views(Context c,Snapshot s,int w,int h) {
        return views(c,s,w,h,false);
    }
    public static RemoteViews views(Context c,Snapshot s,int w,int h,boolean expanded) {
        RemoteViews v=new RemoteViews(c.getPackageName(),expanded?R.layout.widget_expanded:R.layout.widget);
        Bitmap b=CardRenderer.bitmap(c,s,w,expanded?180:h,!expanded); v.setImageViewBitmap(R.id.card,b);
        if(expanded) ProcessViews.bind(c,v,s); else v.setViewVisibility(R.id.placeholder,View.GONE);
        v.setTextColor(R.id.refresh,Store.dark(c)?0xffa7c8ff:0xff286de0);
        v.setTextViewText(R.id.refresh,"↻ 刷新"); v.setBoolean(R.id.refresh,"setEnabled",true);
        v.setContentDescription(R.id.refresh,"刷新设备状态，采样约 2 秒");
        Intent open=new Intent(c,MainActivity.class);
        PendingIntent detail=PendingIntent.getActivity(c,1,open,PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        PendingIntent refresh=PendingIntent.getBroadcast(c,2,new Intent(c,FtopWidget.class).setAction(REFRESH).addFlags(Intent.FLAG_RECEIVER_FOREGROUND),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        v.setOnClickPendingIntent(R.id.card,detail); v.setOnClickPendingIntent(R.id.refresh,refresh);
        v.setOnClickPendingIntent(android.R.id.background,detail);
        networkTargets(c,v,w,expanded?180:h);
        if(expanded) {
            // Only the fixed, explicit activity can receive collection fill-in data.
            PendingIntent rowDetail=PendingIntent.getActivity(c,3,new Intent(c,ProcessBrowserActivity.class).setAction("dev.ftop.widget.PROCESS_DETAIL"),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_MUTABLE);
            v.setPendingIntentTemplate(R.id.process_list,rowDetail);
        }
        v.setContentDescription(R.id.card,"ftop，内存 "+Metrics.bytes(s.memoryUsed())+"，电量 "+s.battery+"%，"+CardRenderer.updated(s)+"。点击查看详情。");
        return v;
    }
    private static void networkTargets(Context c,RemoteViews v,int width,int height){
        float w=(float)Metrics.clamp(width,220,700),h=(float)Metrics.clamp(height,130,500);
        float right=Math.max(94,w*.31f)+15,rw=w-right-16,scale=width/w;
        boolean compact=w<290;float split=right+rw*.55f;
        int unit=android.util.TypedValue.COMPLEX_UNIT_DIP;
        // Short existing cards place refresh very close to the rate. Keep the rate
        // target above refresh's label without moving any painted content.
        float targetHeight=height<160?24:48;
        float top=(h<155?102:110)*height/h-(height<160?22:30);
        for(int i=0;i<2;i++){
            int id=i==0?R.id.network_download:R.id.network_upload;
            v.setViewVisibility(id,i==1&&compact?View.GONE:View.VISIBLE);
            v.setViewLayoutWidth(id,(i==0?(compact?rw:rw*.55f):rw*.45f)*scale,unit);
            v.setViewLayoutHeight(id,targetHeight,unit);
            v.setViewLayoutMargin(id,RemoteViews.MARGIN_LEFT,(i==0?right:split)*scale,unit);
            v.setViewLayoutMargin(id,RemoteViews.MARGIN_TOP,Math.max(0,top),unit);
            PendingIntent target=PendingIntent.getActivity(c,20+i,NetworkActivity.intent(c,i==1).setAction("dev.ftop.widget.NETWORK_"+i),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
            v.setOnClickPendingIntent(id,target);
        }
    }
    private static void render(Context c,AppWidgetManager m,int id,Snapshot s) {
        Bundle options=m.getAppWidgetOptions(id);
        int w=options.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_WIDTH,340);
        int h=options.getInt(AppWidgetManager.OPTION_APPWIDGET_MAX_HEIGHT,180);
        // Use the current orientation's bounds rather than stretching a portrait bitmap in landscape.
        if(c.getResources().getConfiguration().orientation==2) {
            w=options.getInt(AppWidgetManager.OPTION_APPWIDGET_MAX_WIDTH,w);
            h=options.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_HEIGHT,h);
        }
        android.appwidget.AppWidgetProviderInfo info=m.getAppWidgetInfo(id);
        boolean expanded=info!=null&&info.provider.getClassName().equals(ProcessWidget.class.getName());
        m.updateAppWidget(id,views(c,s,w,h,expanded));
    }
}
