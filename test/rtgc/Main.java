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

        for (int i = 0; i < 100_000; i++) {
            doTest();
        }
    }

    static void doTest() {
        Main array[] = new Main[10_000];
        for (int i = 0; i < array.length; i++) {
            Main m = new Main(sno++);
            array[i] = m;
        }
        int total = 0;
        for (int i = 0; i < array.length; i++) {
            total += array[i].id;
        }

        System.out.println("total: " + total);
    }
}