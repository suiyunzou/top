package dev.ftop.fixture;
public final class FixtureService extends android.app.Service {
    public android.os.IBinder onBind(android.content.Intent i){return null;}
    @Override public int onStartCommand(android.content.Intent i,int f,int id){android.util.Log.i("FtopFixture","Fixture worker started");return START_NOT_STICKY;}
}
