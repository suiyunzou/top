package dev.ftop.widget;

import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.drawable.ColorDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.WindowMetrics;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.ArrayList;

/** Native modal presentation shared by confirmations, choices and on-demand explanations. */
final class AppDialog extends Dialog {
    private final AppUi ui;
    private final Builder spec;
    private TextView titleView,messageView;
    private LinearLayout panel,body;
    private LimitedScroll scroll,bodyScroll;
    private final Button[] buttons=new Button[3];
    private final ArrayList<View> choiceRows=new ArrayList<>();
    private final ArrayList<TextView> checks=new ArrayList<>();
    private CharSequence title,message;
    private int selected;
    private boolean canCancel=true;

    private AppDialog(Builder builder){
        super(builder.context);
        spec=builder;ui=new AppUi(builder.context);title=builder.title;message=builder.message;selected=builder.selected;
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        setCancelable(builder.cancelable);
        if(builder.cancelListener!=null)setOnCancelListener(builder.cancelListener);
        if(builder.dismissListener!=null)setOnDismissListener(builder.dismissListener);
    }

    @Override protected void onCreate(Bundle state){
        super.onCreate(state);
        panel=new LinearLayout(getContext()){
            @Override protected void onMeasure(int widthSpec,int heightSpec){
                if(bodyScroll!=null&&scroll!=null){
                    int innerWidth=Math.max(0,MeasureSpec.getSize(widthSpec)-getPaddingLeft()-getPaddingRight());
                    int fixed=getPaddingTop()+getPaddingBottom();
                    for(int i=0;i<getChildCount();i++){
                        View child=getChildAt(i);if(child==bodyScroll||child.getVisibility()==View.GONE)continue;
                        child.measure(MeasureSpec.makeMeasureSpec(innerWidth,MeasureSpec.EXACTLY),MeasureSpec.makeMeasureSpec(0,MeasureSpec.UNSPECIFIED));
                        LinearLayout.LayoutParams p=(LinearLayout.LayoutParams)child.getLayoutParams();fixed+=child.getMeasuredHeight()+p.topMargin+p.bottomMargin;
                    }
                    // Pin actions below the scrolling body. The outer scroll is only a fallback
                    // for very short windows where the title and actions alone cannot fit.
                    bodyScroll.maxHeight=Math.max(ui.dp(48),scroll.maxHeight-fixed);
                }
                super.onMeasure(widthSpec,heightSpec);
            }
        };
        panel.setOrientation(LinearLayout.VERTICAL);panel.setPadding(ui.dp(20),ui.dp(20),ui.dp(20),ui.dp(16));
        panel.setBackground(ui.shape(ui.surface,24));
        panel.setAccessibilityPaneTitle(title);
        titleView=ui.heading("",20);titleView.setText(title);titleView.setVisibility(empty(title)?View.GONE:View.VISIBLE);
        titleView.setPadding(0,0,0,ui.dp(12));panel.addView(titleView,new LinearLayout.LayoutParams(-1,-2));
        body=ui.column();bodyScroll=new LimitedScroll(getContext());bodyScroll.setFillViewport(false);
        bodyScroll.addView(body,new ScrollView.LayoutParams(-1,-2));panel.addView(bodyScroll,new LinearLayout.LayoutParams(-1,-2));
        messageView=ui.text(message,15,ui.ink);messageView.setTextIsSelectable(true);
        messageView.setVisibility(empty(message)?View.GONE:View.VISIBLE);
        messageView.setPadding(0,0,0,ui.dp(12));
        // Updates are available to accessibility services without stealing focus from an action.
        messageView.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        body.addView(messageView,new LinearLayout.LayoutParams(-1,-2));
        if(spec.items!=null)for(int i=0;i<spec.items.length;i++)addChoice(i);
        addAction(BUTTON_POSITIVE,0);addAction(BUTTON_NEUTRAL,1);addAction(BUTTON_NEGATIVE,2);
        scroll=new LimitedScroll(getContext());scroll.setFillViewport(false);scroll.setVerticalScrollBarEnabled(true);
        scroll.setClipToOutline(true);scroll.setBackground(ui.shape(ui.surface,24));
        scroll.addView(panel,new ScrollView.LayoutParams(-1,-2));
        setContentView(scroll,new ViewGroup.LayoutParams(-1,-2));
        Window window=getWindow();
        if(window!=null){
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            window.setDimAmount(ui.dark?.56f:.34f);
            window.setGravity(Gravity.CENTER);
            window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
            window.getDecorView().setPadding(0,0,0,0);
        }
    }

    @Override protected void onStart(){
        super.onStart();
        Window window=getWindow();if(window==null)return;
        WindowMetrics metrics=window.getWindowManager().getCurrentWindowMetrics();
        Insets safe=metrics.getWindowInsets().getInsetsIgnoringVisibility(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());
        int width=metrics.getBounds().width()-safe.left-safe.right;
        int height=metrics.getBounds().height()-safe.top-safe.bottom;
        scroll.maxHeight=Math.max(1,height-ui.dp(40));
        window.setLayout(Math.max(1,Math.min(ui.dp(420),width-ui.dp(40))),ViewGroup.LayoutParams.WRAP_CONTENT);
        scroll.requestLayout();
        // Keep keyboard focus off destructive actions; TalkBack reads the pane title on entry.
        panel.setFocusableInTouchMode(true);panel.requestFocus();
    }

    @Override public void setTitle(CharSequence value){
        title=value;super.setTitle(value);
        if(titleView!=null){titleView.setText(value);titleView.setVisibility(empty(value)?View.GONE:View.VISIBLE);panel.setAccessibilityPaneTitle(value);}
    }
    @Override public void setTitle(int resource){setTitle(getContext().getText(resource));}
    public void setMessage(CharSequence value){
        message=value;
        if(messageView!=null){messageView.setText(value);messageView.setVisibility(empty(value)?View.GONE:View.VISIBLE);}
    }
    public Button getButton(int which){int index=buttonIndex(which);return index<0?null:buttons[index];}
    @Override public void setCancelable(boolean flag){
        canCancel=flag;super.setCancelable(flag);super.setCanceledOnTouchOutside(flag);
    }
    @Override public void setCanceledOnTouchOutside(boolean flag){super.setCanceledOnTouchOutside(flag&&canCancel);}

    private void addChoice(int index){
        LinearLayout row=ui.horizontal();row.setMinimumHeight(ui.dp(52));row.setPadding(ui.dp(12),ui.dp(12),ui.dp(12),ui.dp(12));
        row.setBackground(ui.ripple(Color.TRANSPARENT,12));row.setFocusable(true);row.setClickable(true);
        TextView label=ui.text(spec.items[index],16,ui.ink);label.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        row.addView(label,new LinearLayout.LayoutParams(0,-2,1));
        TextView check=ui.text("✓",20,ui.accent);check.setGravity(Gravity.CENTER);check.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        check.setVisibility(spec.single&&index==selected?View.VISIBLE:View.INVISIBLE);
        if(spec.single)row.addView(check,new LinearLayout.LayoutParams(ui.dp(32),-2));
        row.setContentDescription(spec.items[index]);row.setSelected(spec.single&&index==selected);
        row.setAccessibilityDelegate(new View.AccessibilityDelegate(){
            @Override public void onInitializeAccessibilityNodeInfo(View host,AccessibilityNodeInfo info){
                super.onInitializeAccessibilityNodeInfo(host,info);
                info.setClassName(spec.single?RadioButton.class.getName():Button.class.getName());
                info.setCheckable(spec.single);info.setChecked(spec.single&&index==selected);
            }
        });
        row.setOnClickListener(v->{
            if(!isShowing())return;
            if(spec.single){selected=index;for(int i=0;i<checks.size();i++){checks.get(i).setVisibility(i==selected?View.VISIBLE:View.INVISIBLE);choiceRows.get(i).setSelected(i==selected);}}
            else dismiss();
            if(spec.itemListener!=null)spec.itemListener.onClick(this,index);
        });
        choiceRows.add(row);checks.add(check);body.addView(row,new LinearLayout.LayoutParams(-1,-2));
    }
    private void addAction(int which,int index){
        if(empty(spec.buttonLabels[index]))return;
        Button action=ui.button(spec.buttonLabels[index].toString(),()->{
            if(!isShowing())return;
            dismiss();if(spec.buttonListeners[index]!=null)spec.buttonListeners[index].onClick(this,which);
        });
        action.setTextSize(16);action.setGravity(Gravity.CENTER);action.setPadding(ui.dp(14),ui.dp(12),ui.dp(14),ui.dp(12));
        action.setBackground(ui.ripple(index==0?ui.background:Color.TRANSPARENT,14));
        LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(-1,-2);params.topMargin=ui.dp(index==0?10:4);
        panel.addView(action,params);buttons[index]=action;
    }
    private static boolean empty(CharSequence value){return value==null||value.length()==0;}
    private static int buttonIndex(int which){return which==BUTTON_POSITIVE?0:which==BUTTON_NEUTRAL?1:which==BUTTON_NEGATIVE?2:-1;}
    private static final class LimitedScroll extends ScrollView {
        int maxHeight=Integer.MAX_VALUE;
        LimitedScroll(Context context){super(context);}
        @Override protected void onMeasure(int widthSpec,int heightSpec){
            int limit=maxHeight;
            if(MeasureSpec.getMode(heightSpec)!=MeasureSpec.UNSPECIFIED)limit=Math.min(limit,MeasureSpec.getSize(heightSpec));
            super.onMeasure(widthSpec,MeasureSpec.makeMeasureSpec(limit,MeasureSpec.AT_MOST));
        }
    }

    static final class Builder {
        private final Context context;
        private CharSequence title,message;
        private CharSequence[] items;
        private int selected=-1;
        private boolean single,cancelable=true;
        private DialogInterface.OnClickListener itemListener;
        private DialogInterface.OnCancelListener cancelListener;
        private DialogInterface.OnDismissListener dismissListener;
        private final CharSequence[] buttonLabels=new CharSequence[3];
        private final DialogInterface.OnClickListener[] buttonListeners=new DialogInterface.OnClickListener[3];
        Builder(Context context){this.context=context;}
        public Builder setTitle(CharSequence value){title=value;return this;}
        public Builder setTitle(int resource){return setTitle(context.getText(resource));}
        public Builder setMessage(CharSequence value){message=value;return this;}
        public Builder setMessage(int resource){return setMessage(context.getText(resource));}
        public Builder setItems(CharSequence[] values,DialogInterface.OnClickListener listener){items=values.clone();single=false;itemListener=listener;return this;}
        public Builder setItems(int resource,DialogInterface.OnClickListener listener){return setItems(context.getResources().getTextArray(resource),listener);}
        public Builder setSingleChoiceItems(CharSequence[] values,int checked,DialogInterface.OnClickListener listener){items=values.clone();single=true;selected=checked;itemListener=listener;return this;}
        public Builder setSingleChoiceItems(int resource,int checked,DialogInterface.OnClickListener listener){return setSingleChoiceItems(context.getResources().getTextArray(resource),checked,listener);}
        public Builder setPositiveButton(CharSequence text,DialogInterface.OnClickListener listener){return button(0,text,listener);}
        public Builder setNegativeButton(CharSequence text,DialogInterface.OnClickListener listener){return button(2,text,listener);}
        public Builder setNeutralButton(CharSequence text,DialogInterface.OnClickListener listener){return button(1,text,listener);}
        public Builder setPositiveButton(int resource,DialogInterface.OnClickListener listener){return setPositiveButton(context.getText(resource),listener);}
        public Builder setNegativeButton(int resource,DialogInterface.OnClickListener listener){return setNegativeButton(context.getText(resource),listener);}
        public Builder setNeutralButton(int resource,DialogInterface.OnClickListener listener){return setNeutralButton(context.getText(resource),listener);}
        private Builder button(int index,CharSequence text,DialogInterface.OnClickListener listener){buttonLabels[index]=text;buttonListeners[index]=listener;return this;}
        public Builder setCancelable(boolean value){cancelable=value;return this;}
        public Builder setOnCancelListener(DialogInterface.OnCancelListener value){cancelListener=value;return this;}
        public Builder setOnDismissListener(DialogInterface.OnDismissListener value){dismissListener=value;return this;}
        public AppDialog create(){return new AppDialog(this);}
        public AppDialog show(){AppDialog dialog=create();dialog.show();return dialog;}
    }
}
