package dev.ftop.widget;

/** Run after revoking the test app's Shizuku permission externally (revocation may kill its UID). */
public final class PermissionTests extends android.app.Instrumentation {
    public void onCreate(android.os.Bundle b){super.onCreate(b);start();}
    public void onStart(){android.os.Bundle result=new android.os.Bundle();
        try {
            Thread.sleep(1000); // Let the provider receive the Shizuku binder before checking authorization.
            if(ShizukuAccess.authorized())throw new AssertionError("permission still granted");
            Sampler sampler=new Sampler(getTargetContext());Snapshot s=sampler.sample();
            if(s.processStatus.startsWith("Shizuku")||s.processes.stream().anyMatch(p->!p.memoryKind.equals("PSS")))throw new AssertionError("fallback source incorrect: "+s.processStatus);
            try{ShizukuAccess.request(PrivilegedSampler.ACTION,new ProcessEntry(),"force-stop",false);throw new AssertionError("action accepted without permission");}
            catch(SecurityException expected) { }
            result.putString("stream","3 permission checks passed: authorization revoked, public-API fallback, actions blocked before IPC\nFallback: "+s.processStatus+"; rows="+s.processes.size()+"\n");finish(-1,result);
        }catch(Throwable e){result.putString("stream",android.util.Log.getStackTraceString(e));finish(0,result);}
    }
}
