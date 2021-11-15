public class Main {
    String title;
    String[] paras = new String[16];
    int[] intArray = new int[16];
    int id;
    static volatile int sno;
    static volatile int idx;

    Main(int sno) {
        System.arraycopy(paras, 0, paras, paras.length - 1, 1);

        System.out.println("set int member: " + sno);
        this.id = sno;
        System.out.println("set obj member");
        this.title = "title-" + sno;
        idx = sno % paras.length;
        System.out.println("set int array item with volatile idx ");
        this.intArray[idx] = sno * 20 + idx;
        System.out.println("set array item with constant index + constant obj.");
        this.paras[8] = "";
        System.out.println("set array item with constant index + null.");
        this.paras[9] = null;
        System.out.println("set array item with variable index + constant obj.");
        this.paras[idx] = "";
        System.out.println("set array item with variable index + null.");
        this.paras[idx] = null;
    }
    
    public static void main(String args[]) {
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
        }
    }

    static void doTest(int round, int size) {
        Main array[] = new Main[size];
        System.out.println("["+ round + "] start ");
        for (int i = 0; i < array.length; i++) {
            Main m = createMain(sno++);
            array[i] = m;
        }
        System.out.println("["+ round + "] ----");
        int total = 0;
        for (int i = 0; i < array.length; i++) {
            total += array[i].paras[(int)(Math.random() * 16)] != null ? 1 : 0;
        }

        System.out.println("["+ round + "] total: " + total);
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
        System.out.println("set obj field to null.");
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