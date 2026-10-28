package dev.ftop.widget;

import java.io.*;
import java.util.*;

/** Parse only the live UID map. Never use historical buckets as current traffic. */
final class NetworkCounters {
    static Map<Integer,long[]> parse(Reader input) throws IOException {
        BufferedReader reader=new BufferedReader(input);Map<Integer,long[]> rows=new TreeMap<>();
        boolean table=false,header=false;int bytes=0,lines=0;
        for(String line;(line=reader.readLine())!=null;){
            bytes+=line.length();if(++lines>30000||bytes>2000000)throw new IOException("网络统计超出读取范围");
            String s=line.trim();
            if(s.equals("mAppUidStatsMap:")){table=true;continue;}
            if(!table)continue;
            if(s.equals("uid rxBytes rxPackets txBytes txPackets")){header=true;continue;}
            if(s.isEmpty())continue;
            if(s.endsWith(":"))break;
            if(!header)throw new IOException("系统网络统计格式不受支持");
            String[] cells=s.split("\\s+");
            if(cells.length!=5)throw new IOException("系统网络统计格式不受支持");
            try {
                int uid=Integer.parseInt(cells[0]);long rx=Long.parseLong(cells[1]),tx=Long.parseLong(cells[3]);
                if(uid<0||rx<0||tx<0||rows.containsKey(uid))throw new NumberFormatException();
                rows.put(uid,new long[]{rx,tx});
            }catch(NumberFormatException e){throw new IOException("网络计数无效",e);}
            if(rows.size()>8192)throw new IOException("应用流量条目过多");
        }
        if(!table||!header)throw new IOException("系统未提供可读的实时应用流量计数");
        return rows;
    }
    static final class Reading {
        final int uid;final double rx,tx;final long received,sent;
        Reading(int uid,double rx,double tx,long received,long sent){this.uid=uid;this.rx=rx;this.tx=tx;this.received=received;this.sent=sent;}
    }
    /** One observation session. Resets and gaps establish a new baseline. */
    static final class Tracker {
        private Map<Integer,long[]> previous=new HashMap<>(),totals=new HashMap<>();private long at;
        List<Reading> update(Map<Integer,long[]> now,long time){
            List<Reading> result=new ArrayList<>();long elapsed=time-at;
            for(Map.Entry<Integer,long[]> e:now.entrySet()){
                int uid=e.getKey();long[] n=e.getValue(),p=previous.get(uid),total=totals.get(uid);
                if(total==null){total=new long[2];totals.put(uid,total);}
                double rx=Double.NaN,tx=Double.NaN;
                if(p!=null&&elapsed>0&&elapsed<=15000&&n[0]>=p[0]&&n[1]>=p[1]){
                    long dr=n[0]-p[0],dt=n[1]-p[1];rx=dr*1000.0/elapsed;tx=dt*1000.0/elapsed;total[0]+=dr;total[1]+=dt;
                }
                result.add(new Reading(uid,rx,tx,total[0],total[1]));
            }
            previous=new HashMap<>(now);at=time;return result;
        }
    }
}
