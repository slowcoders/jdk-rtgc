public class Main {
    String title;
    String[] paras = new String[16];
    int id;
    static int sno;

    Main(int i) {
        this.id = i;
        this.title = "title-" + i;
        for (int j = 0; j < this.paras.length; j ++ ) {
            this.paras[j] = this.title + "-" + j;
        }
    }
    
    public static void main(String args[]) {
        System.out.println("Test started");

        for (int i = 0; i < 1_000; i++) {
            doTest(i);
        }
    }

    static void doTest(int round) {
        Main array[] = new Main[10_000];
        System.out.println("["+ round + "] start ");
        for (int i = 0; i < array.length; i++) {
            Main m = createMain(sno++);
            array[i] = m;
        }
        System.out.println("["+ round + "] ----");
        int total = 0;
        for (int i = 0; i < array.length; i++) {
            total += array[i].id;
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
    }

}