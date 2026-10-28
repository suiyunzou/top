package dev.ftop.widget;

import android.content.Context;
import android.content.pm.PackageManager;
import android.os.SystemClock;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;

/** Fixed, bounded shell read. No destination addresses or packet contents are retained. */
final class NetworkReader {
    static JSONObject read(Context context) throws Exception {
        Process process=new ProcessBuilder("/system/bin/dumpsys","netstats").redirectErrorStream(true).start();
        ScheduledExecutorService timeout=Executors.newSingleThreadScheduledExecutor();
        timeout.schedule(()->process.destroyForcibly(),4,TimeUnit.SECONDS);
        Map<Integer,long[]> values;
        try(Reader in=new InputStreamReader(process.getInputStream(),"UTF-8")){values=NetworkCounters.parse(in);}
        finally {process.destroyForcibly();timeout.shutdownNow();}
        long at=SystemClock.elapsedRealtime();JSONArray rows=new JSONArray();PackageManager pm=context.getPackageManager();
        for(Map.Entry<Integer,long[]> entry:values.entrySet()){
            int uid=entry.getKey();String[] packages=pm.getPackagesForUid(uid);String label="UID "+uid;JSONArray names=new JSONArray();
            if(packages!=null&&packages.length>0){
                Arrays.sort(packages);StringBuilder labels=new StringBuilder();
                for(String pkg:packages){names.put(pkg);if(labels.length()>0)labels.append("、");
                    try{labels.append(pm.getApplicationLabel(pm.getApplicationInfo(pkg,0)));}catch(Exception e){labels.append(pkg);}
                    if(labels.length()>240){labels.append("…");break;}
                }
                label=labels.toString();if(packages.length>1)label+="（共享 UID）";
            }
            rows.put(new JSONObject().put("uid",uid).put("rx",entry.getValue()[0]).put("tx",entry.getValue()[1]).put("label",label).put("packages",names));
        }
        return new JSONObject().put("at",at).put("rows",rows);
    }
}
