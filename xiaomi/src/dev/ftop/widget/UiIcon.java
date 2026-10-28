package dev.ftop.widget;

import android.graphics.*;
import android.graphics.drawable.Drawable;

/** Small line icons drawn in a shared 24-unit coordinate space. */
final class UiIcon extends Drawable {
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final boolean settings;
    UiIcon(int color,boolean settings){this.settings=settings;paint.setColor(color);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(1.8f);paint.setStrokeCap(Paint.Cap.ROUND);paint.setStrokeJoin(Paint.Join.ROUND);}
    @Override public void draw(Canvas canvas){
        Rect b=getBounds();canvas.save();canvas.translate(b.left,b.top);canvas.scale(b.width()/24f,b.height()/24f);
        if(settings){
            Path gear=new Path();
            for(int i=0;i<32;i++){double angle=Math.PI*2*i/32-Math.PI/2;float radius=i%4==0||i%4==3?9.2f:7.3f;float x=12+(float)Math.cos(angle)*radius,y=12+(float)Math.sin(angle)*radius;if(i==0)gear.moveTo(x,y);else gear.lineTo(x,y);}
            gear.close();canvas.drawPath(gear,paint);canvas.drawCircle(12,12,3.2f,paint);
        }else{canvas.drawRoundRect(6,2.5f,18,21.5f,2,2,paint);canvas.drawLine(10,5,14,5,paint);canvas.drawLine(11,18.5f,13,18.5f,paint);}
        canvas.restore();
    }
    @Override public void setAlpha(int alpha){paint.setAlpha(alpha);invalidateSelf();}
    @Override public void setColorFilter(ColorFilter filter){paint.setColorFilter(filter);invalidateSelf();}
    @Override public int getOpacity(){return PixelFormat.TRANSLUCENT;}
}
