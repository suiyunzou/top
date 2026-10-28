package dev.ftop.widget;

import java.util.Locale;

/** Platform-independent calculations. Invalid and unavailable values remain NaN. */
public final class Metrics {
    private Metrics() {}
    public static long[] cpuTicks(String line) {
        String[] fields = line.trim().split("\\s+");
        if (!fields[0].matches("cpu[0-9]*") || fields.length < 5) throw new IllegalArgumentException("cpu record");
        long total = 0, idle = 0;
        // guest and guest_nice already belong to user/nice. Never add them twice.
        for (int i = 1; i < Math.min(fields.length, 9); i++) {
            long n = Long.parseLong(fields[i]);
            if (n < 0) throw new IllegalArgumentException("negative ticks");
            total += n;
            if (i == 4 || i == 5) idle += n;
        }
        return new long[]{total, idle};
    }
    public static double cpu(long[] before, long[] after) {
        if (before == null || after == null) return Double.NaN;
        long total = after[0] - before[0], idle = after[1] - before[1];
        if (total <= 0 || idle < 0 || idle > total) return Double.NaN;
        return 100.0 * (total - idle) / total;
    }
    public static double rate(long before, long after, long elapsedMs) {
        return before < 0 || after < before || elapsedMs <= 0 ? Double.NaN : (after - before) * 1000.0 / elapsedMs;
    }
    public static String percent(double value) {
        return Double.isFinite(value) ? String.format(Locale.ROOT, "%.0f%%", value) : "—";
    }
    public static String bytes(double n) {
        if (!Double.isFinite(n) || n < 0) return "—";
        String[] unit = {"B", "KB", "MB", "GB", "TB"};
        int i = 0;
        while (n >= 1024 && i < unit.length - 1) { n /= 1024; i++; }
        return String.format(Locale.ROOT, n >= 100 || i == 0 ? "%.0f %s" : "%.1f %s", n, unit[i]);
    }
    public static double clamp(double n, double min, double max) { return Math.max(min, Math.min(max, n)); }
    public static double ghz(double khz) {
        // Some virtual devices return 1 as a sentinel; it is not a usable 1 Hz CPU reading.
        return Double.isFinite(khz) && khz>=10000 && khz<=10000000 ? khz/1000000.0 : Double.NaN;
    }
}
