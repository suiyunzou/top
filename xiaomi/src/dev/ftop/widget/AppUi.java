package dev.ftop.widget;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.accessibility.AccessibilityManager;
import android.widget.*;

/** App-only presentation tokens. Widgets deliberately keep their existing renderers/resources. */
final class AppUi {
    final Context context;
    final boolean dark;
    final int background,surface,ink,muted,accent,danger,line;
    AppUi(Context c) {
        context=c;dark=Store.dark(c);
        background=dark?0xff11151d:0xfff2f3f7;surface=dark?0xff1c2029:Color.WHITE;
        ink=dark?0xfff1f4fa:0xff18212f;muted=dark?0xffa8b2c2:0xff596579;
        accent=dark?0xff609fff:0xff205fc6;danger=dark?0xffff8a80:0xffb42318;
        line=dark?0xff303946:0xffdce1e9;
    }
    int dp(float n){return Math.round(n*context.getResources().getDisplayMetrics().density);}
    boolean large(){return context.getResources().getConfiguration().fontScale>=1.3f;}
    LinearLayout column(){LinearLayout l=new LinearLayout(context);l.setOrientation(LinearLayout.VERTICAL);return l;}
    LinearLayout horizontal(){LinearLayout l=new LinearLayout(context);l.setGravity(Gravity.CENTER_VERTICAL);return l;}
    TextView text(CharSequence value,int size,int color){
        TextView t=new TextView(context);t.setText(value);t.setTextSize(size);t.setTextColor(color);
        t.setLineSpacing(dp(2),1);t.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_NONE);return t;
    }
    TextView heading(String value,int size){TextView t=text(value,size,ink);t.setTypeface(null,Typeface.BOLD);t.setAccessibilityHeading(true);return t;}
    GradientDrawable shape(int color,int radius){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(radius));return d;}
    RippleDrawable ripple(int color,int radius){return new RippleDrawable(ColorStateList.valueOf(dark?0x33609fff:0x22205fc6),shape(color,radius),null);}
    Button button(String label,Runnable action){
        Button b=new Button(context);b.setText(label);b.setAllCaps(false);b.setTextSize(14);b.setTextColor(accent);
        b.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));b.setMinimumWidth(dp(48));b.setMinWidth(dp(48));
        b.setMinimumHeight(dp(48));b.setMinHeight(dp(48));b.setPadding(dp(10),dp(6),dp(10),dp(6));
        b.setBackground(ripple(Color.TRANSPARENT,10));b.setElevation(0);b.setStateListAnimator(null);b.setOnClickListener(v->action.run());return b;
    }
    ImageButton iconButton(String label,boolean settings,Runnable action){
        ImageButton button=new ImageButton(context);button.setImageDrawable(new UiIcon(ink,settings));
        button.setScaleType(ImageView.ScaleType.FIT_CENTER);button.setPadding(dp(12),dp(12),dp(12),dp(12));
        button.setMinimumWidth(dp(48));button.setMinimumHeight(dp(48));button.setBackground(ripple(Color.TRANSPARENT,12));
        button.setContentDescription(label);button.setTooltipText(label);button.setOnClickListener(v->action.run());return button;
    }
    LinearLayout root(Activity a){
        a.getWindow().setDecorFitsSystemWindows(false);a.getWindow().setStatusBarColor(Color.TRANSPARENT);a.getWindow().setNavigationBarColor(Color.TRANSPARENT);
        a.getWindow().setStatusBarContrastEnforced(false);a.getWindow().setNavigationBarContrastEnforced(false);
        a.getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        LinearLayout root=column();root.setBackgroundColor(background);
        root.setOnApplyWindowInsetsListener((v,insets)->{
            android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());
            android.graphics.Insets ime=insets.getInsets(WindowInsets.Type.ime());
            v.setPadding(dp(16)+bars.left,bars.top,dp(16)+bars.right,dp(4)+Math.max(bars.bottom,ime.bottom));
            return WindowInsets.CONSUMED;
        });
        int appearance=WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS|WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
        root.post(()->{WindowInsetsController controller=root.getWindowInsetsController();if(controller!=null)controller.setSystemBarsAppearance(dark?0:appearance,appearance);});return root;
    }
    TextView title(LinearLayout root,String title,Runnable back){
        LinearLayout nav=horizontal();nav.setMinimumHeight(dp(56));
        if(back!=null){Button b=button("‹",back);b.setTextSize(28);b.setTextColor(ink);b.setContentDescription("返回");nav.addView(b,new LinearLayout.LayoutParams(dp(48),-2));}
        TextView t=heading(title,22);t.setPadding(0,dp(10),0,dp(10));t.setMaxLines(2);
        nav.addView(t,new LinearLayout.LayoutParams(0,-2,1));root.addView(nav,new LinearLayout.LayoutParams(-1,-2));return t;
    }
    ScrollView scroll(View child){ScrollView s=new ScrollView(context);s.setVerticalScrollBarEnabled(false);s.setFillViewport(false);s.addView(child,new ScrollView.LayoutParams(-1,-2));return s;}
    /** Keep independently scrolling list space even when the frozen monitor grows with system fonts. */
    ScrollView boundedScroll(View child,float fraction,int maxDp){
        ScrollView s=new ScrollView(context){@Override protected void onMeasure(int w,int h){
            int screen=context.getResources().getConfiguration().screenHeightDp;
            int max=dp(Math.min(maxDp,Math.max(90,(int)(screen*fraction))));
            if(View.MeasureSpec.getMode(h)!=View.MeasureSpec.UNSPECIFIED)max=Math.min(max,(int)(View.MeasureSpec.getSize(h)*.50f));
            super.onMeasure(w,View.MeasureSpec.makeMeasureSpec(max,View.MeasureSpec.AT_MOST));
        }};s.setVerticalScrollBarEnabled(true);s.setScrollBarStyle(View.SCROLLBARS_INSIDE_OVERLAY);s.addView(child,new ScrollView.LayoutParams(-1,-2));return s;
    }
    void gap(LinearLayout parent,int height){parent.addView(new View(context),new LinearLayout.LayoutParams(1,dp(height)));}
    LinearLayout group(LinearLayout parent,String label){
        TextView h=heading(label,15);h.setPadding(dp(3),dp(20),0,dp(9));parent.addView(h);
        LinearLayout box=column();box.setBackground(shape(surface,18));parent.addView(box,new LinearLayout.LayoutParams(-1,-2));return box;
    }
    void divider(LinearLayout box){if(box.getChildCount()==0)return;View v=new View(context);v.setBackgroundColor(line);LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,dp(1));lp.setMargins(dp(15),0,dp(15),0);box.addView(v,lp);}
    TextView valueRow(LinearLayout box,String label){
        divider(box);LinearLayout row=column();row.setPadding(dp(15),dp(12),dp(15),dp(12));
        row.addView(text(label,13,muted));TextView value=text("—",16,ink);value.setTextIsSelectable(true);row.addView(value);
        box.addView(row,new LinearLayout.LayoutParams(-1,-2));return value;
    }
    TextView setting(LinearLayout box,String name,String value,Runnable action){
        divider(box);LinearLayout row=horizontal();row.setMinimumHeight(dp(58));row.setPadding(dp(15),dp(8),dp(12),dp(8));
        row.setBackground(ripple(Color.TRANSPARENT,12));row.setFocusable(true);row.setClickable(true);
        LinearLayout names=column();TextView label=text(name,16,ink);names.addView(label);TextView detail=text(value,13,muted);
        if(value.isEmpty())detail.setVisibility(View.GONE);names.addView(detail);row.addView(names,new LinearLayout.LayoutParams(0,-2,1));row.addView(text("›",24,muted));
        row.setOnClickListener(v->action.run());row.setContentDescription(name+(value.isEmpty()?"":"，"+value));
        row.setAccessibilityDelegate(new View.AccessibilityDelegate(){@Override public void onInitializeAccessibilityNodeInfo(View host,android.view.accessibility.AccessibilityNodeInfo info){super.onInitializeAccessibilityNodeInfo(host,info);info.setClassName(Button.class.getName());}});
        row.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);names.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
        box.addView(row,new LinearLayout.LayoutParams(-1,-2));return detail;
    }
    void note(LinearLayout parent,String text){TextView t=text(text,13,muted);t.setPadding(dp(4),dp(10),dp(4),dp(12));parent.addView(t);}
    void configureList(ListView list){list.setDivider(null);list.setVerticalScrollBarEnabled(false);list.setHorizontalScrollBarEnabled(false);list.setOverScrollMode(View.OVER_SCROLL_NEVER);}
    boolean readingWithAccessibility(View view){
        if(view.isAccessibilityFocused())return true;
        if(view instanceof ViewGroup)for(int i=0;i<((ViewGroup)view).getChildCount();i++)if(readingWithAccessibility(((ViewGroup)view).getChildAt(i)))return true;
        return false;
    }
    static void openProcess(Context c,Class<?> page,ProcessEntry p){
        try{c.startActivity(new Intent(c,page).putExtra("process",p.json().toString()));}catch(Exception e){Toast.makeText(c,"无法打开此进程，请刷新后重试",Toast.LENGTH_SHORT).show();}
    }
}
