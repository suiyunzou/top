package dev.ftop.widget;

import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.widget.*;

/** Flexible native list row. Never inflates a RemoteViews/widget layout. */
final class AppRow extends LinearLayout {
    final ImageView icon;final TextView name,meta,cpu,memory;
    private final AppUi ui;
    AppRow(android.content.Context c){
        super(c);ui=new AppUi(c);setOrientation(VERTICAL);setMinimumHeight(ui.dp(64));setPadding(ui.dp(14),ui.dp(12),ui.dp(14),ui.dp(12));
        setBackground(ui.ripple(ui.surface,14));
        LinearLayout top=ui.horizontal();addView(top,new LayoutParams(-1,-2));
        icon=new ImageView(c);icon.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);top.addView(icon,new LayoutParams(ui.dp(38),ui.dp(38)));
        LinearLayout names=ui.column();LayoutParams np=new LayoutParams(0,-2,1);np.setMarginStart(ui.dp(10));np.setMarginEnd(ui.dp(8));top.addView(names,np);
        name=ui.text("",16,ui.ink);name.setMaxLines(ui.large()?3:2);name.setEllipsize(android.text.TextUtils.TruncateAt.END);names.addView(name);
        meta=ui.text("",13,ui.muted);meta.setMaxLines(2);meta.setEllipsize(android.text.TextUtils.TruncateAt.END);names.addView(meta);
        LinearLayout values=ui.column();values.setGravity(Gravity.END);cpu=ui.text("",14,ui.ink);memory=ui.text("",13,ui.muted);
        cpu.setGravity(Gravity.END);memory.setGravity(Gravity.END);cpu.setFontFeatureSettings("tnum");memory.setFontFeatureSettings("tnum");
        if(ui.large()){
            cpu.setGravity(Gravity.START);
            values.setOrientation(HORIZONTAL);values.addView(cpu,new LayoutParams(0,-2,1));values.addView(memory,new LayoutParams(0,-2,2));
            LayoutParams vp=new LayoutParams(-1,-2);vp.topMargin=ui.dp(5);addView(values,vp);
        }else{values.addView(cpu);values.addView(memory);top.addView(values,new LayoutParams(-2,-2));}
        setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
        for(int i=0;i<getChildCount();i++)getChildAt(i).setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
    }
    void emphasis(boolean byCpu){cpu.setTextColor(byCpu?ui.ink:ui.muted);memory.setTextColor(byCpu?ui.muted:ui.ink);cpu.setTypeface(null,byCpu?Typeface.BOLD:Typeface.NORMAL);memory.setTypeface(null,byCpu?Typeface.NORMAL:Typeface.BOLD);}
}
