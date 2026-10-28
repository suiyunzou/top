package dev.ftop.widget;

/** Pure calculations: /proc names can contain spaces and parentheses. */
public final class ProcessMetrics {
    public static long[] ticks(String stat) {
        int end=stat.lastIndexOf(')');
        if(end<0) throw new IllegalArgumentException("Missing process name");
        String[] f=stat.substring(end+1).trim().split("\\s+");
        if(f.length<20) throw new IllegalArgumentException("Short stat");
        return new long[]{Long.parseLong(f[11])+Long.parseLong(f[12]),Long.parseLong(f[19])};
    }
    /** Percent of one CPU core; multicore processes can exceed 100%. */
    public static long rssBytes(String stat,long pageSize) {
        int end=stat.lastIndexOf(')');
        if(end<0||pageSize<=0) throw new IllegalArgumentException("Invalid stat or page size");
        String[] fields=stat.substring(end+1).trim().split("\\s+");
        if(fields.length<22) throw new IllegalArgumentException("Short stat");
        return Math.multiplyExact(Long.parseLong(fields[21]),pageSize);
    }
    public static double cpu(long[] previous,long[] current,long elapsedMs,long hz) {
        if(previous==null||current==null||elapsedMs<=0||hz<=0||previous[1]!=current[1]||current[0]<previous[0]) return Double.NaN;
        return (current[0]-previous[0])*100000.0/(hz*elapsedMs);
    }
}
