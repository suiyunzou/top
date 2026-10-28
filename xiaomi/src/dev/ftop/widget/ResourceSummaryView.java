package dev.ftop.widget;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

/** A standalone identity card with two labeled readings. No sampling or package IO. */
public final class ResourceSummaryView extends LinearLayout {
    private final TextView name,subtitle,state,firstValue,secondValue,note;
    private final ImageView icon;
    private final int ink,muted;

    public ResourceSummaryView(Context context,boolean dark){
        super(context);setOrientation(VERTICAL);setPadding(dp(18),dp(18),dp(18),dp(16));
        ink=dark?0xfff1f4fa:0xff18212f;muted=dark?0xffa8b2c2:0xff596579;
        GradientDrawable background=new GradientDrawable();background.setColor(dark?0xff1c2029:0xffffffff);background.setCornerRadius(dp(18));setBackground(background);
        LinearLayout identity=new LinearLayout(context);identity.setGravity(Gravity.CENTER_VERTICAL);
        icon=new ImageView(context);icon.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);identity.addView(icon,new LayoutParams(dp(48),dp(48)));
        LinearLayout labels=new LinearLayout(context);labels.setOrientation(VERTICAL);LayoutParams labelsSize=new LayoutParams(0,-2,1);labelsSize.setMarginStart(dp(14));identity.addView(labels,labelsSize);
        name=text(22,ink);name.setTypeface(null,Typeface.BOLD);name.setAccessibilityHeading(true);labels.addView(name);
        state=text(13,muted);labels.addView(state);addView(identity,new LayoutParams(-1,-2));
        subtitle=text(12,muted);subtitle.setTextIsSelectable(true);subtitle.setPadding(0,dp(12),0,dp(14));addView(subtitle);
        LinearLayout readings=new LinearLayout(context);
        boolean stacked=getResources().getConfiguration().fontScale>=1.5f;
        readings.setOrientation(stacked?VERTICAL:HORIZONTAL);
        firstValue=metric(readings,"CPU",stacked);secondValue=metric(readings,"内存",stacked);addView(readings,new LayoutParams(-1,-2));
        note=text(12,muted);note.setPadding(0,dp(10),0,0);addView(note,new LayoutParams(-1,-2));
    }
    private TextView metric(LinearLayout parent,String label,boolean stacked){
        LinearLayout cell=new LinearLayout(getContext());cell.setOrientation(VERTICAL);cell.setPadding(0,0,dp(8),stacked?dp(10):0);
        cell.addView(textLabel(label));TextView value=text(26,ink);value.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));value.setFontFeatureSettings("tnum");cell.addView(value);
        parent.addView(cell,stacked?new LayoutParams(-1,-2):new LayoutParams(0,-2,1));return value;
    }
    private TextView textLabel(String label){TextView v=text(13,muted);v.setText(label);return v;}
    private TextView text(int size,int color){TextView v=new TextView(getContext());v.setTextSize(size);v.setTextColor(color);v.setLineSpacing(dp(2),1);return v;}
    private int dp(int n){return Math.round(n*getResources().getDisplayMetrics().density);}
    public void setIcon(Drawable drawable){icon.setImageDrawable(drawable);}
    public void bind(String title,String identifier,String status,String cpu,String memory,String explanation){
        name.setText(title);subtitle.setText(identifier);subtitle.setVisibility(identifier.isEmpty()?GONE:VISIBLE);state.setText(status);
        firstValue.setText(cpu);firstValue.setContentDescription("CPU，"+cpu);secondValue.setText(memory);secondValue.setContentDescription("内存，"+memory);note.setText(explanation);note.setVisibility(explanation.isEmpty()?GONE:VISIBLE);
    }
}
