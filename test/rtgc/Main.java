import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneId;
import java.util.concurrent.atomic.*;

import java.time.Duration;

// import jdk.jfr.Recording;
// import jdk.jfr.consumer.RecordedEvent;
// import jdk.test.lib.jfr.Events;
// import jdk.test.lib.jfr.GCHelper;

public class Main {

    interface Ifc extends java.io.Serializable {

    }

    String title;
    static String[] paras = new String[16];
    static java.io.Serializable[] ses2 = new java.io.Serializable[16];
    static volatile Object ses = ses2;
    static volatile Object str = new String[16];
    Ifc[] sParas = new Ifc[16];
    int[] intArray = new int[16];
    static Main[] sArray;
    int id;
    static Object sObj;
    static Object sObj2;
    static volatile int sno;
    static volatile int cnt = 0;
    static volatile int idx;
    static AtomicReference atomicRef = new AtomicReference();
    static AtomicLong atomicLong = new AtomicLong();


    static void test() {
            System.arraycopy(new Object[16], 0, paras, paras.length - 1, 1);
            System.arraycopy(ses, 0, str, paras.length - 1, 1);
    }

    Main(int sno) {
        cnt ++;
        if (cnt > 99_0000) {
            atomicLong.compareAndExchange(0x876543DB876543DBL, 0x123456DB123456DBL);
            Instant instant = Instant.ofEpochMilli(System.currentTimeMillis());
            LocalDateTime ldt = LocalDateTime.ofInstant(
                    instant, ZoneId.systemDefault());
            //System.out.println(ldt);
            ses2[0] = new StringBuilder();
        }
        test();
        atomicRef.compareAndSet(null, new Object());
        atomicRef.getAndSet(new Object());

        //System.out.println("set int member: " + sno);
        this.id = sno;
        //System.out.println("set obj member");
        this.title = "title-" + sno;
        idx = sno % paras.length;
        // System.out.println("set int array item with volatile idx ");
        // System.out.println("set array item with constant index + constant obj.");
        // System.out.println("set array item with constant index + null.");
        // System.out.println("set array item with variable index + constant obj.");
        // System.out.println("set array item with variable index + null.");
        // System.out.println("set static field.");
        sObj2 = this;
        this.intArray[idx] = sno * 20 + idx;
        this.paras[8] = "";
        this.paras[9] = null;
        this.paras[idx] = "";
        this.paras[10] = null;
        sObj = this;
    }
    
    public static void main(String args[]) {

        // String EVENT_NAME = GCHelper.event_garbage_collection;
        // Recording recording = new Recording();
        // recording.enable(EVENT_NAME).withThreshold(Duration.ofMillis(0));
        // recording.start();
        // System.gc();
        // recording.stop();

        int round = 1;
        int array_size = 20;
        try {
            Thread.sleep(5*1000);
            round = Integer.parseInt(args[0]);
            array_size = Integer.parseInt(args[1]);
        }
        catch (Exception e) {            
        }

        System.out.println("Test started");

        for (int i = 0; i < round; i++) {
            doTest(i, array_size);
            System.gc();
        }
    }

    static void doTest(int round, int size) {
        sArray = new Main[size];
        System.out.println("["+ round + "] start ");
        for (int i = 0; i < size; i++) {
            Main m = createMain(sno++);
            // array[i] = m;
        }
        System.out.println("["+ round + "] ----");
        int total = 0;
        for (int i = 0; i < size; i++) {
            //total += sArray[i].paras[(int)(Math.random() * 16)] != null ? 1 : 0;
        }

        System.out.println("["+ round + "] total: " + sArray[0]);
    }

    static Main createMain(int sno) {
        if (Math.random() >= 0.5) {
            return new Foo2(sno);
        }
        else {
            return new Main(sno);
        }
    }
}

class Foo2 extends Main {
    Foo2(int i) {
        super(i);
        this.id *= 2;
        //System.out.println("set obj field to null.");
        this.title = null;
    }
}

class Foo3 extends Main {
    static final String no_title = "no_title";
    Foo3(int i) {
        super(i);
        this.id *= 2;
        System.out.println("set obj field to global constant.");
        this.title = no_title;
    }

}