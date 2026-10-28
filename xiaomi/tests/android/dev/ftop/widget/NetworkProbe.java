package dev.ftop.widget;
import android.content.Context;
import android.os.Looper;
import org.json.*;
/** Read-only physical-device probe. Output only counts, never application names. */
public final class NetworkProbe {
    public static void main(String[] args)throws Exception{
        Looper.prepareMainLooper();Class<?> thread=Class.forName("android.app.ActivityThread");Object at=thread.getMethod("systemMain").invoke(null);
        Context c=(Context)thread.getMethod("getSystemContext").invoke(at);
        JSONObject first=NetworkReader.read(c);Thread.sleep(3200);JSONObject second=NetworkReader.read(c);
        JSONArray rows=second.getJSONArray("rows");int resolved=0;for(int i=0;i<rows.length();i++)if(rows.getJSONObject(i).getJSONArray("packages").length()>0)resolved++;
        if(rows.length()==0||resolved==0||second.getLong("at")<=first.getLong("at"))throw new AssertionError("live UID map or labels unavailable");
        System.out.println("PASS live UID counters="+rows.length()+" resolved="+resolved+" elapsedMs="+(second.getLong("at")-first.getLong("at")));System.exit(0);
    }
}
