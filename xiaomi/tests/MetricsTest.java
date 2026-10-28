package dev.ftop.widget;

public final class MetricsTest {
    private static int count;
    private static void check(boolean b,String name) { if(!b) throw new AssertionError(name); count++; }
    public static void main(String[] args) {
        long[] a=Metrics.cpuTicks("cpu 100 20 30 400 50 10 5 1 15 8");
        check(a[0]==616 && a[1]==450,"guest excluded, iowait idle");
        check(Metrics.cpu(new long[]{100,40},new long[]{200,60})==80,"cpu delta");
        check(Double.isNaN(Metrics.cpu(null,a)),"first sample unavailable");
        check(Double.isNaN(Metrics.cpu(a,a)),"zero interval");
        check(Double.isNaN(Metrics.cpu(new long[]{200,60},new long[]{100,40})),"reset counter");
        check(Double.isNaN(Metrics.cpu(new long[]{100,10},new long[]{110,30})),"bad idle delta");
        check(Metrics.rate(1000,1500,500)==1000,"network units");
        check(Double.isNaN(Metrics.rate(-1,1500,500)),"unsupported network");
        check(Double.isNaN(Metrics.rate(2000,1000,500)),"network reset");
        check(Double.isNaN(Metrics.rate(1000,1500,0)),"zero time");
        check(Metrics.bytes(Double.NaN).equals("—"),"unavailable not zero");
        check(Metrics.bytes(1073741824).equals("1.0 GB"),"binary units");
        check(Metrics.percent(0).equals("0%"),"zero is real");
        check(Metrics.percent(Double.NaN).equals("—"),"unknown percent");
        boolean bad=false;try { Metrics.cpuTicks("cpu -1 2 3 4"); } catch(IllegalArgumentException e){bad=true;}
        check(bad,"negative tick rejected");
        check(Double.isNaN(Metrics.ghz(1)),"emulator sentinel unavailable");
        check(Metrics.ghz(1459200)==1.4592,"kernel kHz conversion");
        check(Double.isNaN(Metrics.ghz(-1)),"offline frequency unavailable");
        long[] process=ProcessMetrics.ticks("42 (app (worker) name) S 1 1 1 0 0 0 0 0 0 0 50 25 0 0 0 0 1 0 900");
        check(process[0]==75&&process[1]==900,"process names with parentheses and spaces");
        check(Double.isNaN(ProcessMetrics.cpu(null,process,1000,100)),"process first frame unavailable");
        check(ProcessMetrics.cpu(new long[]{25,900},process,1000,100)==50,"process one-core percentage");
        check(ProcessMetrics.cpu(new long[]{0,900},new long[]{200,900},1000,100)==200,"multicore can exceed 100 percent");
        check(Double.isNaN(ProcessMetrics.cpu(new long[]{25,899},process,1000,100)),"PID reuse resets baseline");
        check(Double.isNaN(ProcessMetrics.cpu(process,new long[]{2,900},1000,100)),"process counter reset unavailable");
        check(Double.isNaN(ProcessMetrics.cpu(process,process,0,100)),"process zero elapsed unavailable");
        check(Double.isNaN(ProcessMetrics.cpu(process,process,1000,0)),"invalid clock tick rate unavailable");
        String rssStat="42 (app (worker) name) S 1 1 1 0 0 0 0 0 0 0 50 25 0 0 0 0 1 0 900 99999 256";
        check(ProcessMetrics.rssBytes(rssStat,4096)==1048576,"RSS pages use actual page size and correct stat field");
        check(ProcessMetrics.rssBytes(rssStat,16384)==4194304,"16K page device conversion");
        boolean shortRss=false; try { ProcessMetrics.rssBytes("42 (gone) S 1",4096); } catch(IllegalArgumentException e) { shortRss=true; }
        check(shortRss,"truncated stat rejected");
        System.out.println(count+" metric tests passed");
    }
}
