package dev.ftop.fixture;
public final class FixtureActivity extends android.app.Activity {
    @Override public void onCreate(android.os.Bundle b){super.onCreate(b);
        android.widget.TextView t=new android.widget.TextView(this);t.setText("ftop 隔离测试靶标\n主进程与 worker 进程，仅用于验证停止操作。");t.setTextSize(22);setContentView(t);
        startService(new android.content.Intent(this,FixtureService.class));android.util.Log.i("FtopFixture","Fixture main started");
    }
}
