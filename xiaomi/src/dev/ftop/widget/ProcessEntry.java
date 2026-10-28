package dev.ftop.widget;

import org.json.JSONObject;

/** A measured, system-visible process. Missing metrics remain unavailable. */
public final class ProcessEntry {
    public int pid, uid;
    public long started, memoryBytes=-1;
    public String name="";
    public String rawName="", packageName="", appLabel="", state="";
    public int userId() { return uid/100000; }
    public String groupKey() { return packageName.isEmpty()?"pid:"+pid+":"+started:"app:"+userId()+":"+packageName; }
    public String memoryKind="PSS";
    public boolean system;
    public double cpu=Double.NaN;
    public long stableId() { return ((long)pid<<32) ^ started; }
    public String kind() { return system?"系统":"应用"; }
    public JSONObject json() throws Exception {
        return new JSONObject().put("pid",pid).put("uid",uid).put("started",started).put("name",name)
            .put("rawName",rawName).put("packageName",packageName).put("appLabel",appLabel).put("state",state)
            .put("system",system).put("memoryBytes",memoryBytes).put("memoryKind",memoryKind).put("cpu",Double.isFinite(cpu)?cpu:JSONObject.NULL);
    }
    public static ProcessEntry from(JSONObject j) {
        ProcessEntry p=new ProcessEntry(); p.pid=j.optInt("pid"); p.uid=j.optInt("uid");
        p.started=j.optLong("started"); p.name=j.optString("name"); p.system=j.optBoolean("system");
        p.rawName=j.optString("rawName",p.name); p.packageName=j.optString("packageName");
        p.appLabel=j.optString("appLabel"); p.state=j.optString("state");
        p.memoryBytes=j.optLong("memoryBytes",-1); p.memoryKind=j.optString("memoryKind","PSS"); p.cpu=j.optDouble("cpu",Double.NaN); return p;
    }
}
