package dev.ftop.widget;

import org.json.JSONArray;
import org.json.JSONObject;

public final class Snapshot {
    public final java.util.ArrayList<ProcessEntry> processes=new java.util.ArrayList<>();
    public String processStatus="尚未采样";
    public long time, totalMemory, availableMemory, totalStorage, freeStorage;
    public double cpu = Double.NaN, gpu = Double.NaN, rx = Double.NaN, tx = Double.NaN;
    public double batteryTemperature = Double.NaN, batteryCurrent = Double.NaN;
    public int battery = -1;
    public int logicalCores = Runtime.getRuntime().availableProcessors();
    public boolean charging;
    public double[] cores = new double[0], frequencies = new double[0];
    public String cpuStatus = "尚未采样", gpuStatus = "尚未采样", frequencyStatus = "尚未采样";
    public double memoryUsed() { return Math.max(0, totalMemory - availableMemory); }
    public double memoryPercent() { return totalMemory > 0 ? memoryUsed() * 100 / totalMemory : Double.NaN; }
    private static Object number(double n) { return Double.isFinite(n) ? n : JSONObject.NULL; }
    private static JSONArray array(double[] values) {
        JSONArray a = new JSONArray(); for (double v : values) a.put(number(v)); return a;
    }
    private static double[] values(JSONArray a) {
        if (a == null) return new double[0]; double[] v = new double[a.length()];
        for (int i = 0; i < v.length; i++) v[i] = a.optDouble(i, Double.NaN); return v;
    }
    public JSONObject json() throws Exception {
        JSONArray rows=new JSONArray(); for(ProcessEntry p:processes) rows.put(p.json());
        return new JSONObject().put("time", time).put("totalMemory", totalMemory).put("availableMemory", availableMemory)
            .put("totalStorage", totalStorage).put("freeStorage", freeStorage).put("cpu", number(cpu)).put("gpu", number(gpu))
            .put("rx", number(rx)).put("tx", number(tx)).put("battery", battery).put("charging", charging).put("logicalCores",logicalCores)
            .put("batteryTemperature", number(batteryTemperature)).put("batteryCurrent", number(batteryCurrent))
            .put("cores", array(cores)).put("frequencies", array(frequencies)).put("cpuStatus", cpuStatus)
            .put("gpuStatus", gpuStatus).put("frequencyStatus", frequencyStatus)
            .put("processes",rows).put("processStatus",processStatus);
    }
    public static Snapshot from(JSONObject j) {
        Snapshot s = new Snapshot(); s.time = j.optLong("time");
        s.processStatus=j.optString("processStatus","尚未采样");
        JSONArray rows=j.optJSONArray("processes");
        if(rows!=null) for(int i=0;i<Math.min(4096,rows.length());i++) {
            JSONObject row=rows.optJSONObject(i); if(row!=null) s.processes.add(ProcessEntry.from(row));
        }
        s.totalMemory = j.optLong("totalMemory"); s.availableMemory = j.optLong("availableMemory");
        s.totalStorage = j.optLong("totalStorage"); s.freeStorage = j.optLong("freeStorage");
        s.cpu = j.optDouble("cpu", Double.NaN); s.gpu = j.optDouble("gpu", Double.NaN);
        s.rx = j.optDouble("rx", Double.NaN); s.tx = j.optDouble("tx", Double.NaN);
        s.battery = j.optInt("battery", -1); s.charging = j.optBoolean("charging");
        s.logicalCores = j.optInt("logicalCores",Runtime.getRuntime().availableProcessors());
        s.batteryTemperature = j.optDouble("batteryTemperature", Double.NaN);
        s.batteryCurrent = j.optDouble("batteryCurrent", Double.NaN);
        s.cores = values(j.optJSONArray("cores")); s.frequencies = values(j.optJSONArray("frequencies"));
        s.cpuStatus = j.optString("cpuStatus"); s.gpuStatus = j.optString("gpuStatus"); s.frequencyStatus = j.optString("frequencyStatus");
        return s;
    }
}
