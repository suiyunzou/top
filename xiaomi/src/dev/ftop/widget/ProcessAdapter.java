package dev.ftop.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.ListView;
import java.util.ArrayList;
import java.util.List;

final class ProcessAdapter extends BaseAdapter {
    private final Context c;
    private final List<ProcessEntry> rows=new ArrayList<>();
    ProcessAdapter(Context c) { this.c=c; }
    void update(List<ProcessEntry> next,ListView list) {
        rows.clear(); rows.addAll(next); notifyDataSetChanged();
    }
    @Override public int getCount() { return rows.size(); }
    @Override public ProcessEntry getItem(int position) { return rows.get(position); }
    @Override public long getItemId(int position) { return rows.get(position).stableId(); }
    @Override public boolean hasStableIds() { return true; }
    @Override public boolean isEnabled(int position) { return true; }
    @Override public View getView(int position,View view,ViewGroup parent) {
        ProcessRow row=view instanceof ProcessRow?(ProcessRow)view:new ProcessRow(c);
        row.bind(getItem(position));
        row.setClickable(false);row.setLongClickable(false);return row;
    }
}
