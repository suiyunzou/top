package dev.ftop.widget;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** Draw at actual display density, with a bounded bitmap allocation. No sample placeholders. */
public final class CardRenderer {
    private final Canvas c;
    private final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final int text,muted,track,blue,green;
    private CardRenderer(Canvas canvas,boolean dark) {
        c=canvas; text=Color.parseColor(dark?"#f1f4fa":"#18212f"); muted=Color.parseColor(dark?"#9ba4b4":"#616e80");
        track=Color.parseColor(dark?"#303743":"#dfe5ee"); blue=Color.parseColor(dark?"#609fff":"#286de0"); green=Color.parseColor(dark?"#42cb8a":"#14895b");
    }
    private void text(String s,float x,float y,float size,int color,boolean bold) {
        p.setColor(color); p.setTextSize(size); p.setTypeface(Typeface.create("sans-serif",bold?Typeface.BOLD:Typeface.NORMAL)); c.drawText(s,x,y,p);
    }
    private void box(float x,float y,float w,float h,float r,int color) {
        p.setColor(color); c.drawRoundRect(new RectF(x,y,x+w,y+h),r,r,p);
    }
    public static String updated(Snapshot s) {
        if(s.time<=0) return "点击刷新获取数据";
        return "更新于 "+new SimpleDateFormat("HH:mm:ss",Locale.ROOT).format(new Date(s.time));
    }
    public static Bitmap bitmap(Context context,Snapshot s,int widthDp,int heightDp) {
        return bitmap(context,s,widthDp,heightDp,true);
    }
    public static Bitmap bitmap(Context context,Snapshot s,int widthDp,int heightDp,boolean footer) {
        int width=(int)Metrics.clamp(widthDp,220,700),height=(int)Metrics.clamp(heightDp,130,500);
        float density=Math.min(2.5f,context.getResources().getDisplayMetrics().density);
        density=Math.min(density,(float)Math.sqrt(1400000.0/(width*height)));
        Bitmap b=Bitmap.createBitmap(Math.max(1,Math.round(width*density)),Math.max(1,Math.round(height*density)),Bitmap.Config.ARGB_8888);
        Canvas c=new Canvas(b); c.scale(density,density);
        new CardRenderer(c,Store.dark(context)).draw(s,width,height,Store.dark(context),footer); return b;
    }
    public static void paint(Context context,Canvas canvas,Snapshot s,int widthPx,int heightPx) {
        float density=context.getResources().getDisplayMetrics().density;
        int saved=canvas.save(); canvas.scale(density,density);
        new CardRenderer(canvas,Store.dark(context)).draw(s,Math.round(widthPx/density),Math.round(heightPx/density),Store.dark(context),true);
        canvas.restoreToCount(saved);
    }
    private void draw(Snapshot s,int w,int h,boolean dark,boolean footer) {
        box(0,0,w,h,23,Color.parseColor(dark?"#1c202a":"#f5f7fb"));
        float left=16,split=Math.max(94,w*0.31f),right=split+15,rw=w-right-16;
        boolean compact=w<290;
        text("CPU",left,25,11,blue,true);
        text(Metrics.percent(s.cpu),left,51,25,text,true);
        boolean useFreq=!Double.isFinite(s.cpu) && s.frequencies.length>0;
        double[] bars=useFreq?s.frequencies:s.cores;
        boolean have=false; for(double v:bars) if(Double.isFinite(v)) have=true;
        int count=Math.min(32,Math.max(1,bars.length>0?bars.length:s.logicalCores));
        float bw=(split-left-8)/count,top=66,bottom=h-44;
        for(int i=0;i<count;i++) {
            float x=left+i*bw,barWidth=Math.max(3,bw-3);
            box(x,top,barWidth,Math.max(8,bottom-top),barWidth/2,track);
            double value=i<bars.length?bars[i]:Double.NaN;
            if(Double.isFinite(value)) {
                float height=(float)((bottom-top)*Metrics.clamp(value/(useFreq?4.0:100.0),0,1));
                box(x,bottom-height,barWidth,height,barWidth/2,blue);
            }
        }
        text(have?(useFreq?"核心频率 / 4 GHz":"各核心使用率"):"系统限制读取",left,h-29,9,muted,false);
        text("内存 · 已用",right,25,11,green,true);
        text(s.totalMemory>0?String.format(Locale.ROOT,"%.1f",s.memoryUsed()/1073741824.0):"—",right,50,25,text,true);
        text(s.totalMemory>0?String.format(Locale.ROOT,"/ %.1f GiB",s.totalMemory/1073741824.0):"",right+53,49,compact?8:11,muted,false);
        box(right,59,rw,5,3,track);
        if(s.totalMemory>0) box(right,59,(float)(rw*s.memoryPercent()/100),5,3,green);
        float y=85;
        text("电量  "+(s.battery<0?"—":s.battery+"%")+(s.charging?"  充电":""),right,y,compact?10:12,text,false);
        if(!compact) text(Double.isFinite(s.batteryTemperature)?String.format(Locale.ROOT,"%.1f°C",s.batteryTemperature):"—",w-65,y,11,muted,false);
        float networkY=h<155?102:y+25;
        text("↓ "+Metrics.bytes(s.rx)+"/s",right,networkY,compact?10:12,text,false);
        if(!compact) text("↑ "+Metrics.bytes(s.tx)+"/s",right+rw*0.55f,networkY,11,muted,false);
        if(h>=180) {
            text("网速 · 采样均值",right,networkY+13,8,muted,false);
            text("GPU  "+(Double.isFinite(s.gpu)?Metrics.percent(s.gpu):"受限"),right,y+57,10,muted,false);
        }
        if(h>=225) {
            float sy=170;
            text("存储可用  "+Metrics.bytes(s.freeStorage),right,sy,11,muted,false);
        }
        if(footer) text(updated(s),16,h-13,10,muted,false);
    }
}
