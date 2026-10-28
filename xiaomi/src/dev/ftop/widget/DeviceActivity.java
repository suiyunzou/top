package dev.ftop.widget;

import android.os.Bundle;

import android.view.View;
import android.widget.*;
import java.util.Locale;

public final class DeviceActivity extends AppPage {
    private ForegroundSampler sampler;
    private TextView cpu,cpuStatus,frequency,gpu,memory,available,download,upload,battery,temperature,storage,updated;
    private LinearLayout technical;
    private Button disclosure;
    private boolean expanded;

    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        expanded=state!=null&&state.getBoolean("processorExpanded");
        LinearLayout root=ui.root(this);TextView title=ui.title(root,"设备详情",this::finish);
        ((LinearLayout)title.getParent()).addView(ui.button("说明",()->new AppDialog.Builder(this).setTitle("设备指标说明")
            .setMessage("页面打开时持续更新。单次读数不能判断是否卡顿。\n\n内存已用量包含系统与缓存，不等于需要清理。\n\n温度来自电池传感器。\n\n横线表示等待采样或系统未提供读数。部分指标需要 Shizuku 授权。")
            .setPositiveButton("知道了",null).show()));
        LinearLayout content=ui.column();
        root.addView(ui.scroll(content),new LinearLayout.LayoutParams(-1,0,1));
        updated=ui.text("正在采样…",13,ui.muted);updated.setPadding(ui.dp(2),0,0,ui.dp(12));content.addView(updated);
        LinearLayout overview=card(content,"当前负载");
        overview.addView(ui.text("CPU 使用率",14,ui.muted));
        cpu=ui.text("—",32,ui.ink);overview.addView(cpu);
        cpuStatus=ui.text("等待采样",13,ui.muted);overview.addView(cpuStatus);

        LinearLayout mem=card(content,"内存与存储");
        LinearLayout memoryValues=metricsRow(mem);
        memory=metric(memoryValues,"内存已用");available=metric(memoryValues,"内存可用 / 总量");
        ui.gap(mem,12);storage=metric(mem,"存储可用 / 总量");

        LinearLayout net=card(content,"网络速度");
        LinearLayout speeds=metricsRow(net);download=metric(speeds,"下载");upload=metric(speeds,"上传");
        ui.gap(net,8);net.addView(ui.button("查看应用流量",()->NetworkActivity.open(this,false)),new LinearLayout.LayoutParams(-1,-2));

        LinearLayout power=card(content,"电池");
        LinearLayout powerValues=metricsRow(power);battery=metric(powerValues,"电量");temperature=metric(powerValues,"电池温度");

        LinearLayout processor=card(content,"处理器详情");
        disclosure=ui.button("",()->{expanded=!expanded;updateDisclosure();});
        disclosure.setGravity(android.view.Gravity.START|android.view.Gravity.CENTER_VERTICAL);
        processor.addView(disclosure,new LinearLayout.LayoutParams(-1,-2));
        technical=ui.column();processor.addView(technical,new LinearLayout.LayoutParams(-1,-2));
        ui.gap(technical,8);gpu=metric(technical,"GPU 使用率");ui.gap(technical,16);frequency=metric(technical,"各核心频率");
        updateDisclosure();
        setContentView(root);update(Store.snapshot(this));sampler=new ForegroundSampler(this,1000,this::update);
    }
    private LinearLayout card(LinearLayout parent,String title){
        LinearLayout box=ui.column();box.setBackground(ui.shape(ui.surface,18));box.setPadding(ui.dp(16),ui.dp(16),ui.dp(16),ui.dp(16));
        LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(-1,-2);params.bottomMargin=ui.dp(12);parent.addView(box,params);
        box.addView(ui.heading(title,18));ui.gap(box,12);return box;
    }
    private LinearLayout metricsRow(LinearLayout parent){
        LinearLayout row=ui.large()?ui.column():ui.horizontal();row.setGravity(android.view.Gravity.TOP);
        parent.addView(row,new LinearLayout.LayoutParams(-1,-2));return row;
    }
    private TextView metric(LinearLayout parent,String label){
        LinearLayout item=ui.column();
        boolean horizontal=parent.getOrientation()==LinearLayout.HORIZONTAL;
        LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(horizontal?0:-1,-2,horizontal?1:0);
        if(parent.getChildCount()>0){if(horizontal)params.leftMargin=ui.dp(12);else params.topMargin=ui.dp(8);}
        item.addView(ui.text(label,13,ui.muted));TextView value=ui.text("—",16,ui.ink);value.setTextIsSelectable(true);item.addView(value);
        parent.addView(item,params);return value;
    }
    private void updateDisclosure(){
        technical.setVisibility(expanded?View.VISIBLE:View.GONE);
        disclosure.setText(expanded?"收起 GPU 与核心频率  −":"查看 GPU 与核心频率  ＋");
        disclosure.setStateDescription(expanded?"已展开":"已折叠");
    }
    private void update(Snapshot s){
        updated.setText(CardRenderer.updated(s));cpu.setText(Metrics.percent(s.cpu));
        cpuStatus.setText(Double.isFinite(s.cpu)?"":s.cpuStatus);cpuStatus.setVisibility(Double.isFinite(s.cpu)?View.GONE:View.VISIBLE);
        gpu.setText(Metrics.percent(s.gpu)+"\n"+s.gpuStatus);
        StringBuilder clocks=new StringBuilder();
        for(int i=0;i<s.frequencies.length;i++){
            if(clocks.length()>0)clocks.append('\n');
            clocks.append("核心 ").append(i).append(" · ").append(Double.isFinite(s.frequencies[i])?String.format(Locale.ROOT,"%.2f GHz",s.frequencies[i]):"—");
        }
        frequency.setText((clocks.length()>0?clocks.toString():"—")+"\n"+s.frequencyStatus);
        memory.setText(s.totalMemory>0?Metrics.bytes(s.memoryUsed())+" · "+Metrics.percent(s.memoryPercent()):"—");
        available.setText(s.totalMemory>0?Metrics.bytes(s.availableMemory)+" / "+Metrics.bytes(s.totalMemory):"—");
        download.setText(Double.isFinite(s.rx)?Metrics.bytes(s.rx)+"/s":"—");upload.setText(Double.isFinite(s.tx)?Metrics.bytes(s.tx)+"/s":"—");
        battery.setText(s.battery>=0?s.battery+"%"+(s.charging?" · 充电中":" · 未充电"):"—");
        temperature.setText(Double.isFinite(s.batteryTemperature)?String.format(Locale.ROOT,"%.1f°C",s.batteryTemperature):"—");
        storage.setText(s.totalStorage>0?Metrics.bytes(s.freeStorage)+" / "+Metrics.bytes(s.totalStorage):"—");
    }
    @Override protected void onSaveInstanceState(Bundle state){state.putBoolean("processorExpanded",expanded);super.onSaveInstanceState(state);}
    @Override protected void onResume(){super.onResume();if(themeCurrent()&&sampler!=null)sampler.resume();}
    @Override protected void onPause(){if(sampler!=null)sampler.pause();super.onPause();}
    @Override protected void onDestroy(){if(sampler!=null)sampler.close();super.onDestroy();}
}
