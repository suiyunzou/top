package dev.ftop.widget;

import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.TrafficStats;
import android.os.BatteryManager;
import android.os.Environment;
import android.os.StatFs;
import android.os.SystemClock;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

/** Worker-only sampling; optional authorized Shizuku reader for system processes. */
public final class Sampler {
    private Map<String, long[]> previous = new HashMap<>();
    private long lastRx = -1, lastTx = -1, lastAt;
    private boolean cpuDenied, gpuDenied, frequencyDenied;
    private final Context context;
    private final ProcessSampler processes;
    public Sampler(Context context) { this.context = context.getApplicationContext(); processes=new ProcessSampler(this.context); }
    private static String line(String path) throws Exception {
        try (BufferedReader r = new BufferedReader(new FileReader(path))) { return r.readLine(); }
    }
    private static String denied(Exception e) { return "受限或设备未提供"; }
    public Snapshot sample() {
        Snapshot s = new Snapshot(); s.time = System.currentTimeMillis();
        ActivityManager.MemoryInfo m = new ActivityManager.MemoryInfo();
        ((ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE)).getMemoryInfo(m);
        s.totalMemory = m.totalMem; s.availableMemory = m.availMem;
        StatFs fs = new StatFs(Environment.getDataDirectory().getAbsolutePath());
        s.totalStorage = fs.getTotalBytes(); s.freeStorage = fs.getAvailableBytes();
        Intent battery = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (battery != null) {
            int level = battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1), scale = battery.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
            if (level >= 0 && scale > 0) s.battery = (int)Math.round(level * 100.0 / scale);
            if (battery.hasExtra(BatteryManager.EXTRA_TEMPERATURE)) s.batteryTemperature = battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) / 10.0;
            s.charging = battery.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0;
        }
        BatteryManager bm = (BatteryManager) context.getSystemService(Context.BATTERY_SERVICE);
        int current = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW);
        if (current != Integer.MIN_VALUE) s.batteryCurrent = current / 1000.0;
        long at = SystemClock.elapsedRealtime(), rx = TrafficStats.getTotalRxBytes(), tx = TrafficStats.getTotalTxBytes();
        s.rx = Metrics.rate(lastRx, rx, at-lastAt); s.tx = Metrics.rate(lastTx, tx, at-lastAt);
        lastAt = at; lastRx = rx; lastTx = tx;
        if (!cpuDenied) {
            try (BufferedReader reader = new BufferedReader(new FileReader("/proc/stat"))) {
                Map<String, long[]> ticks = new HashMap<>(); ArrayList<Double> cores = new ArrayList<>();
                for (String l; (l = reader.readLine()) != null && l.startsWith("cpu"); ) {
                    String key = l.trim().split("\\s+", 2)[0]; long[] tick = Metrics.cpuTicks(l);
                    ticks.put(key, tick); double usage = Metrics.cpu(previous.get(key), tick);
                    if (key.equals("cpu")) s.cpu = usage; else cores.add(usage);
                }
                previous = ticks; s.cores = new double[cores.size()];
                for (int i=0; i<cores.size(); i++) s.cores[i] = cores.get(i);
                s.cpuStatus = Double.isFinite(s.cpu) ? "真实累计时间差" : "等待第二次采样";
            } catch (Exception e) { cpuDenied = true; s.cpuStatus = denied(e); }
        } else s.cpuStatus = "受限或设备未提供";
        if (!frequencyDenied) {
            ArrayList<Double> frequencies = new ArrayList<>(); boolean any = false;
            for (int i=0; i<32 && new File("/sys/devices/system/cpu/cpu"+i).exists(); i++) {
                double frequency = Double.NaN;
                try { frequency = Metrics.ghz(Double.parseDouble(line("/sys/devices/system/cpu/cpu"+i+"/cpufreq/scaling_cur_freq"))); any |= Double.isFinite(frequency); }
                catch (Exception ignored) { /* Core may be offline or access may be denied. */ }
                frequencies.add(frequency);
            }
            s.frequencies = new double[frequencies.size()];
            for (int i=0;i<frequencies.size();i++) s.frequencies[i]=frequencies.get(i);
            frequencyDenied = !any; s.frequencyStatus = any ? "当前频率，不代表使用率" : "受限或设备未提供";
        } else s.frequencyStatus = "受限或设备未提供";
        if (!gpuDenied) {
            try {
                String[] fields = line("/sys/class/kgsl/kgsl-3d0/gpubusy").trim().split("\\s+");
                double busy = Double.parseDouble(fields[0]), total = Double.parseDouble(fields[1]);
                if (total > 0 && busy >= 0 && busy <= total) s.gpu = busy * 100.0 / total;
                s.gpuStatus = Double.isFinite(s.gpu) ? "KGSL 驱动统计窗口" : "等待驱动计数";
            } catch (Exception e) { gpuDenied = true; s.gpuStatus = denied(e); }
        } else s.gpuStatus = "受限或设备未提供";
        if(!ShizukuAccess.sample(s)) {
            processes.sample(s);
            if(ShizukuAccess.authorized()) s.processStatus="Shizuku 连接失败，暂显示本应用进程；点击授权入口重试";
        }
        return s;
    }
}
