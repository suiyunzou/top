package dev.ftop.widget;

import android.content.Context;
import android.widget.RemoteViews;

public final class ProcessViews {
    public static RemoteViews groupRow(Context c,AppGroup g) {
        ProcessEntry head=g.members.get(0); RemoteViews r=row(c,head);
        r.setTextViewText(R.id.process_name,g.title());
        r.setTextViewText(R.id.process_meta,(g.packageName.isEmpty()?"归属不明":g.system?"系统应用":"应用")+" · "+g.members.size()+" 个进程");
        r.setTextViewText(R.id.process_cpu,g.cpuText()); r.setTextViewText(R.id.process_memory,g.memoryText());
        r.setOnClickFillInIntent(R.id.process_row,new android.content.Intent().putExtra("groupKey",g.key));
        return r;
    }
    public static RemoteViews row(Context c,ProcessEntry p) {
        return row(c,p,Store.dark(c));
    }
    public static RemoteViews row(Context c,ProcessEntry p,boolean dark) {
        RemoteViews r=new RemoteViews(c.getPackageName(),R.layout.process_row);
        try { r.setOnClickFillInIntent(R.id.process_row,new android.content.Intent().putExtra("process",p.json().toString())); }
        catch(Exception ignored) { }
        int ink=dark?0xfff1f4fa:0xff18212f,muted=dark?0xff9ba4b4:0xff616e80;
        r.setTextViewText(R.id.process_name,p.name);
        r.setTextViewText(R.id.process_meta,p.kind()+" · PID "+p.pid);
        r.setTextViewText(R.id.process_cpu,Double.isFinite(p.cpu)?String.format(java.util.Locale.ROOT,"%.1f%%",p.cpu):"—");
        r.setTextViewText(R.id.process_memory,p.memoryBytes>=0?Metrics.bytes(p.memoryBytes)+" "+p.memoryKind:"—");
        r.setTextViewText(R.id.process_icon,p.system?"⚙":"▥");
        r.setTextColor(R.id.process_icon,dark?0xff609fff:0xff286de0);
        r.setTextColor(R.id.process_name,ink); r.setTextColor(R.id.process_cpu,ink);
        r.setTextColor(R.id.process_meta,muted); r.setTextColor(R.id.process_memory,muted); return r;
    }
    public static void bind(Context c,RemoteViews v,Snapshot s) {
        RemoteViews.RemoteCollectionItems.Builder rows=new RemoteViews.RemoteCollectionItems.Builder()
            .setHasStableIds(true).setViewTypeCount(1);
        boolean dark=Store.dark(c);
        java.util.List<AppGroup> groups=AppGroup.collect(s.processes,false);
        for(int i=0;i<Math.min(64,groups.size());i++) {AppGroup g=groups.get(i); rows.addItem(g.stableId(),groupRow(c,g));}
        v.setRemoteAdapter(R.id.process_list,rows.build());
        v.setEmptyView(R.id.process_list,R.id.process_empty);
        int muted=Store.dark(c)?0xff9ba4b4:0xff616e80;
        for(int id:new int[]{R.id.process_title,R.id.process_scope,R.id.process_empty,R.id.updated}) v.setTextColor(id,muted);
        v.setTextViewText(R.id.process_scope,s.processStatus);
        v.setTextViewText(R.id.process_title,"运行应用 · 内存排序");
        v.setTextViewText(R.id.updated,CardRenderer.updated(s));
        v.setInt(android.R.id.background,"setBackgroundResource",Store.dark(c)?R.drawable.widget_background:R.drawable.widget_background_light);
    }
}
