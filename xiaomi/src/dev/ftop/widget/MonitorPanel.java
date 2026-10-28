package dev.ftop.widget;

import android.content.Context;
import android.graphics.*;
import android.view.*;
import android.widget.*;
import java.util.Locale;

/** Native, scalable readings; only the small gauges use Canvas. */
final class MonitorPanel extends LinearLayout {
    private final AppUi ui;
    private final TextView cpu,memory,available,download,upload,extra,storage,coreLabel,timestamp;
    private final View bars,rail;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private Snapshot sample=new Snapshot();
    private final int blue,green,track;
    MonitorPanel(Context context){
        super(context);ui=new AppUi(context);blue=ui.accent;green=ui.dark?0xff42cb8a:0xff14895b;track=ui.line;
        setOrientation(VERTICAL);setPadding(ui.dp(16),ui.dp(16),ui.dp(16),ui.dp(14));setBackground(ui.shape(ui.surface,18));
        LinearLayout readings=ui.horizontal();readings.setGravity(Gravity.TOP);if(ui.large())readings.setOrientation(VERTICAL);addView(readings,new LayoutParams(-1,-2));
        LinearLayout processor=ui.column(),ram=ui.column();
        LayoutParams pc=cell();if(!ui.large())pc.setMarginEnd(ui.dp(20));readings.addView(processor,pc);readings.addView(ram,cell());
        processor.addView(ui.text("CPU",12,ui.muted));cpu=ui.heading("—",24);processor.addView(cpu);
        bars=new View(context){protected void onDraw(Canvas canvas){
            boolean frequency=!Double.isFinite(sample.cpu);double[] values=frequency?sample.frequencies:sample.cores;
            int count=Math.min(32,Math.max(1,values.length>0?values.length:sample.logicalCores));float pitch=getWidth()/(float)count,width=Math.max(1,pitch-ui.dp(3)),height=getHeight();
            for(int i=0;i<count;i++){float x=i*pitch;paint.setColor(track);canvas.drawRoundRect(x,0,x+width,height,width/2,width/2,paint);
                double value=i<values.length?values[i]:Double.NaN;if(Double.isFinite(value)){float fill=(float)(height*Metrics.clamp(value/(frequency?4:100),0,1));paint.setColor(blue);canvas.drawRoundRect(x,height-fill,x+width,height,width/2,width/2,paint);}}
        }};
        LayoutParams bp=new LayoutParams(-1,ui.dp(24));bp.topMargin=ui.dp(8);bp.bottomMargin=ui.dp(4);processor.addView(bars,bp);
        coreLabel=ui.text("核心使用率",11,ui.muted);processor.addView(coreLabel);
        ram.addView(ui.text("内存 · 已用",12,ui.muted));memory=ui.heading("—",24);ram.addView(memory);
        rail=new View(context){protected void onDraw(Canvas canvas){paint.setColor(track);canvas.drawRoundRect(0,0,getWidth(),getHeight(),ui.dp(3),ui.dp(3),paint);if(sample.totalMemory>0){paint.setColor(green);canvas.drawRoundRect(0,0,(float)(getWidth()*Metrics.clamp(sample.memoryPercent()/100,0,1)),getHeight(),ui.dp(3),ui.dp(3),paint);}}};
        LayoutParams rp=new LayoutParams(-1,ui.dp(5));rp.topMargin=ui.dp(12);rp.bottomMargin=ui.dp(7);ram.addView(rail,rp);
        available=ui.text("—",12,ui.muted);ram.addView(available);
        divider();
        LinearLayout network=ui.horizontal();if(ui.large())network.setOrientation(VERTICAL);addView(network,new LayoutParams(-1,-2));
        LinearLayout down=ui.column(),up=ui.column();network.addView(down,cell());network.addView(up,cell());
        down.addView(ui.text("↓ 下载  ›",12,ui.muted));download=ui.text("—",18,ui.ink);down.addView(download);
        up.addView(ui.text("↑ 上传  ›",12,ui.muted));upload=ui.text("—",18,ui.ink);up.addView(upload);networkTarget(down,false);networkTarget(up,true);
        divider();
        extra=ui.text("—",12,ui.muted);storage=ui.text("—",12,ui.muted);addView(extra);
        LinearLayout footer=ui.horizontal();if(ui.large())footer.setOrientation(VERTICAL);
        footer.addView(storage,new LayoutParams(ui.large()?-1:0,-2,ui.large()?0:1));
        timestamp=ui.text("—",12,ui.muted);footer.addView(timestamp);addView(footer);
    }
    private LayoutParams cell(){return ui.large()?new LayoutParams(-1,-2):new LayoutParams(0,-2,1);}
    private void divider(){View line=new View(getContext());line.setBackgroundColor(ui.line);LayoutParams lp=new LayoutParams(-1,ui.dp(1));lp.topMargin=ui.dp(10);lp.bottomMargin=ui.dp(10);addView(line,lp);}
    private void networkTarget(View target,boolean uploading){
        target.setMinimumHeight(ui.dp(52));target.setFocusable(true);target.setBackground(ui.ripple(Color.TRANSPARENT,8));
        target.setContentDescription(uploading?"上传，查看各应用上传流量":"下载，查看各应用下载流量");
        target.setOnClickListener(v->NetworkActivity.open(getContext(),uploading));
    }
    void update(Snapshot s){
        sample=s;cpu.setText(Metrics.percent(s.cpu));
        memory.setText(s.totalMemory>0?String.format(Locale.ROOT,"%.1f GiB",s.memoryUsed()/1073741824.0):"—");
        available.setText(s.totalMemory>0?String.format(Locale.ROOT,"可用 %.1f GiB",s.availableMemory/1073741824.0):"等待采样");
        download.setText(Double.isFinite(s.rx)?Metrics.bytes(s.rx)+"/s":"—");upload.setText(Double.isFinite(s.tx)?Metrics.bytes(s.tx)+"/s":"—");
        extra.setText("电量 "+(s.battery<0?"—":s.battery+"%")+(s.charging?" · 充电中":"")+(Double.isFinite(s.batteryTemperature)?String.format(Locale.ROOT," · 电池 %.1f°C",s.batteryTemperature):""));
        storage.setText(s.totalStorage>0?"存储可用 "+Metrics.bytes(s.freeStorage):"存储 · 等待采样");
        String updated=CardRenderer.updated(s);timestamp.setText(updated.replace("更新于 ",""));timestamp.setContentDescription(updated);
        boolean freq=!Double.isFinite(s.cpu)&&s.frequencies.length>0;
        coreLabel.setVisibility(Double.isFinite(s.cpu)?View.GONE:View.VISIBLE);coreLabel.setText(freq?"核心频率 / 4 GHz":Double.isFinite(s.cpu)?"核心使用率":"CPU 读取受限");
        bars.setContentDescription(coreLabel.getText());bars.invalidate();rail.invalidate();
    }
}
