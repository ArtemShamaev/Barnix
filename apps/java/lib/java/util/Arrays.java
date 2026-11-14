package java.util;

public class Arrays {
    private Arrays() {}

    private static void range(int length, int from, int to) {
        if (from > to) throw new IllegalArgumentException("fromIndex(" + from + ") > toIndex(" + to + ")");
        if (from < 0) throw new ArrayIndexOutOfBoundsException(from);
        if (to > length) throw new ArrayIndexOutOfBoundsException(to);
    }
    public static void sort(int[] a) { sort(a, 0, a.length); }
    public static void sort(int[] a, int from, int to) { range(a.length, from, to); qsort(a, from, to - 1); }
    private static void qsort(int[] a, int lo, int hi) {
        while (hi - lo > 16) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < a[lo]) swap(a, mid, lo);
            if (a[hi] < a[lo]) swap(a, hi, lo);
            if (a[hi] < a[mid]) swap(a, hi, mid);
            int pivot = a[mid];
            int i = lo, j = hi;
            while (i <= j) {
                while (a[i] < pivot) i++;
                while (pivot < a[j]) j--;
                if (i <= j) { swap(a, i, j); i++; j--; }
            }
            if (j - lo < hi - i) { qsort(a, lo, j); lo = i; } else { qsort(a, i, hi); hi = j; }
        }
        for (int i = lo + 1; i <= hi; i++) {
            int v = a[i];
            int j = i - 1;
            while (j >= lo && v < a[j]) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    private static void swap(int[] a, int i, int j) { int t = a[i]; a[i] = a[j]; a[j] = t; }
    public static int binarySearch(int[] a, int key) { return binarySearch(a, 0, a.length, key); }
    public static int binarySearch(int[] a, int from, int to, int key) {
        int lo = from, hi = to - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int c = Integer.compare(a[mid], key);
            if (c < 0) lo = mid + 1; else if (c > 0) hi = mid - 1; else return mid;
        }
        return -(lo + 1);
    }
    public static void fill(int[] a, int v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(int[] a, int from, int to, int v) { range(a.length, from, to); for (int i = from; i < to; i++) a[i] = v; }
    public static int[] copyOf(int[] a, int n) { int[] r = new int[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static int[] copyOfRange(int[] a, int from, int to) {
        if (from > to) throw new IllegalArgumentException(from + " > " + to);
        int[] r = new int[to - from];
        System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from));
        return r;
    }
    public static boolean equals(int[] a, int[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
    public static int hashCode(int[] a) {
        if (a == null) return 0;
        int h = 1;
        for (int v : a) h = 31 * h + Integer.hashCode(v);
        return h;
    }
    public static String toString(int[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(", "); b.append(a[i]); }
        return b.append(']').toString();
    }
    public static void sort(long[] a) { sort(a, 0, a.length); }
    public static void sort(long[] a, int from, int to) { range(a.length, from, to); qsort(a, from, to - 1); }
    private static void qsort(long[] a, int lo, int hi) {
        while (hi - lo > 16) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < a[lo]) swap(a, mid, lo);
            if (a[hi] < a[lo]) swap(a, hi, lo);
            if (a[hi] < a[mid]) swap(a, hi, mid);
            long pivot = a[mid];
            int i = lo, j = hi;
            while (i <= j) {
                while (a[i] < pivot) i++;
                while (pivot < a[j]) j--;
                if (i <= j) { swap(a, i, j); i++; j--; }
            }
            if (j - lo < hi - i) { qsort(a, lo, j); lo = i; } else { qsort(a, i, hi); hi = j; }
        }
        for (int i = lo + 1; i <= hi; i++) {
            long v = a[i];
            int j = i - 1;
            while (j >= lo && v < a[j]) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    private static void swap(long[] a, int i, int j) { long t = a[i]; a[i] = a[j]; a[j] = t; }
    public static int binarySearch(long[] a, long key) { return binarySearch(a, 0, a.length, key); }
    public static int binarySearch(long[] a, int from, int to, long key) {
        int lo = from, hi = to - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int c = Long.compare(a[mid], key);
            if (c < 0) lo = mid + 1; else if (c > 0) hi = mid - 1; else return mid;
        }
        return -(lo + 1);
    }
    public static void fill(long[] a, long v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(long[] a, int from, int to, long v) { range(a.length, from, to); for (int i = from; i < to; i++) a[i] = v; }
    public static long[] copyOf(long[] a, int n) { long[] r = new long[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static long[] copyOfRange(long[] a, int from, int to) {
        if (from > to) throw new IllegalArgumentException(from + " > " + to);
        long[] r = new long[to - from];
        System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from));
        return r;
    }
    public static boolean equals(long[] a, long[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
    public static int hashCode(long[] a) {
        if (a == null) return 0;
        int h = 1;
        for (long v : a) h = 31 * h + Long.hashCode(v);
        return h;
    }
    public static String toString(long[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(", "); b.append(a[i]); }
        return b.append(']').toString();
    }
    public static void sort(short[] a) { sort(a, 0, a.length); }
    public static void sort(short[] a, int from, int to) { range(a.length, from, to); qsort(a, from, to - 1); }
    private static void qsort(short[] a, int lo, int hi) {
        while (hi - lo > 16) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < a[lo]) swap(a, mid, lo);
            if (a[hi] < a[lo]) swap(a, hi, lo);
            if (a[hi] < a[mid]) swap(a, hi, mid);
            short pivot = a[mid];
            int i = lo, j = hi;
            while (i <= j) {
                while (a[i] < pivot) i++;
                while (pivot < a[j]) j--;
                if (i <= j) { swap(a, i, j); i++; j--; }
            }
            if (j - lo < hi - i) { qsort(a, lo, j); lo = i; } else { qsort(a, i, hi); hi = j; }
        }
        for (int i = lo + 1; i <= hi; i++) {
            short v = a[i];
            int j = i - 1;
            while (j >= lo && v < a[j]) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    private static void swap(short[] a, int i, int j) { short t = a[i]; a[i] = a[j]; a[j] = t; }
    public static int binarySearch(short[] a, short key) { return binarySearch(a, 0, a.length, key); }
    public static int binarySearch(short[] a, int from, int to, short key) {
        int lo = from, hi = to - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int c = Short.compare(a[mid], key);
            if (c < 0) lo = mid + 1; else if (c > 0) hi = mid - 1; else return mid;
        }
        return -(lo + 1);
    }
    public static void fill(short[] a, short v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(short[] a, int from, int to, short v) { range(a.length, from, to); for (int i = from; i < to; i++) a[i] = v; }
    public static short[] copyOf(short[] a, int n) { short[] r = new short[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static short[] copyOfRange(short[] a, int from, int to) {
        if (from > to) throw new IllegalArgumentException(from + " > " + to);
        short[] r = new short[to - from];
        System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from));
        return r;
    }
    public static boolean equals(short[] a, short[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
    public static int hashCode(short[] a) {
        if (a == null) return 0;
        int h = 1;
        for (short v : a) h = 31 * h + Short.hashCode(v);
        return h;
    }
    public static String toString(short[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(", "); b.append(a[i]); }
        return b.append(']').toString();
    }
    public static void sort(char[] a) { sort(a, 0, a.length); }
    public static void sort(char[] a, int from, int to) { range(a.length, from, to); qsort(a, from, to - 1); }
    private static void qsort(char[] a, int lo, int hi) {
        while (hi - lo > 16) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < a[lo]) swap(a, mid, lo);
            if (a[hi] < a[lo]) swap(a, hi, lo);
            if (a[hi] < a[mid]) swap(a, hi, mid);
            char pivot = a[mid];
            int i = lo, j = hi;
            while (i <= j) {
                while (a[i] < pivot) i++;
                while (pivot < a[j]) j--;
                if (i <= j) { swap(a, i, j); i++; j--; }
            }
            if (j - lo < hi - i) { qsort(a, lo, j); lo = i; } else { qsort(a, i, hi); hi = j; }
        }
        for (int i = lo + 1; i <= hi; i++) {
            char v = a[i];
            int j = i - 1;
            while (j >= lo && v < a[j]) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    private static void swap(char[] a, int i, int j) { char t = a[i]; a[i] = a[j]; a[j] = t; }
    public static int binarySearch(char[] a, char key) { return binarySearch(a, 0, a.length, key); }
    public static int binarySearch(char[] a, int from, int to, char key) {
        int lo = from, hi = to - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int c = Character.compare(a[mid], key);
            if (c < 0) lo = mid + 1; else if (c > 0) hi = mid - 1; else return mid;
        }
        return -(lo + 1);
    }
    public static void fill(char[] a, char v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(char[] a, int from, int to, char v) { range(a.length, from, to); for (int i = from; i < to; i++) a[i] = v; }
    public static char[] copyOf(char[] a, int n) { char[] r = new char[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static char[] copyOfRange(char[] a, int from, int to) {
        if (from > to) throw new IllegalArgumentException(from + " > " + to);
        char[] r = new char[to - from];
        System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from));
        return r;
    }
    public static boolean equals(char[] a, char[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
    public static int hashCode(char[] a) {
        if (a == null) return 0;
        int h = 1;
        for (char v : a) h = 31 * h + Character.hashCode(v);
        return h;
    }
    public static String toString(char[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(", "); b.append(a[i]); }
        return b.append(']').toString();
    }
    public static void sort(byte[] a) { sort(a, 0, a.length); }
    public static void sort(byte[] a, int from, int to) { range(a.length, from, to); qsort(a, from, to - 1); }
    private static void qsort(byte[] a, int lo, int hi) {
        while (hi - lo > 16) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < a[lo]) swap(a, mid, lo);
            if (a[hi] < a[lo]) swap(a, hi, lo);
            if (a[hi] < a[mid]) swap(a, hi, mid);
            byte pivot = a[mid];
            int i = lo, j = hi;
            while (i <= j) {
                while (a[i] < pivot) i++;
                while (pivot < a[j]) j--;
                if (i <= j) { swap(a, i, j); i++; j--; }
            }
            if (j - lo < hi - i) { qsort(a, lo, j); lo = i; } else { qsort(a, i, hi); hi = j; }
        }
        for (int i = lo + 1; i <= hi; i++) {
            byte v = a[i];
            int j = i - 1;
            while (j >= lo && v < a[j]) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    private static void swap(byte[] a, int i, int j) { byte t = a[i]; a[i] = a[j]; a[j] = t; }
    public static int binarySearch(byte[] a, byte key) { return binarySearch(a, 0, a.length, key); }
    public static int binarySearch(byte[] a, int from, int to, byte key) {
        int lo = from, hi = to - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int c = Byte.compare(a[mid], key);
            if (c < 0) lo = mid + 1; else if (c > 0) hi = mid - 1; else return mid;
        }
        return -(lo + 1);
    }
    public static void fill(byte[] a, byte v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(byte[] a, int from, int to, byte v) { range(a.length, from, to); for (int i = from; i < to; i++) a[i] = v; }
    public static byte[] copyOf(byte[] a, int n) { byte[] r = new byte[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static byte[] copyOfRange(byte[] a, int from, int to) {
        if (from > to) throw new IllegalArgumentException(from + " > " + to);
        byte[] r = new byte[to - from];
        System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from));
        return r;
    }
    public static boolean equals(byte[] a, byte[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
    public static int hashCode(byte[] a) {
        if (a == null) return 0;
        int h = 1;
        for (byte v : a) h = 31 * h + Byte.hashCode(v);
        return h;
    }
    public static String toString(byte[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(", "); b.append(a[i]); }
        return b.append(']').toString();
    }
    public static void sort(float[] a) { sort(a, 0, a.length); }
    public static void sort(float[] a, int from, int to) { range(a.length, from, to); qsort(a, from, to - 1); }
    private static void qsort(float[] a, int lo, int hi) {
        while (hi - lo > 16) {
            int mid = (lo + hi) >>> 1;
            if (Float.compare(a[mid], a[lo]) < 0) swap(a, mid, lo);
            if (Float.compare(a[hi], a[lo]) < 0) swap(a, hi, lo);
            if (Float.compare(a[hi], a[mid]) < 0) swap(a, hi, mid);
            float pivot = a[mid];
            int i = lo, j = hi;
            while (i <= j) {
                while (Float.compare(a[i], pivot) < 0) i++;
                while (Float.compare(pivot, a[j]) < 0) j--;
                if (i <= j) { swap(a, i, j); i++; j--; }
            }
            if (j - lo < hi - i) { qsort(a, lo, j); lo = i; } else { qsort(a, i, hi); hi = j; }
        }
        for (int i = lo + 1; i <= hi; i++) {
            float v = a[i];
            int j = i - 1;
            while (j >= lo && Float.compare(v, a[j]) < 0) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    private static void swap(float[] a, int i, int j) { float t = a[i]; a[i] = a[j]; a[j] = t; }
    public static int binarySearch(float[] a, float key) { return binarySearch(a, 0, a.length, key); }
    public static int binarySearch(float[] a, int from, int to, float key) {
        int lo = from, hi = to - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int c = Float.compare(a[mid], key);
            if (c < 0) lo = mid + 1; else if (c > 0) hi = mid - 1; else return mid;
        }
        return -(lo + 1);
    }
    public static void fill(float[] a, float v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(float[] a, int from, int to, float v) { range(a.length, from, to); for (int i = from; i < to; i++) a[i] = v; }
    public static float[] copyOf(float[] a, int n) { float[] r = new float[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static float[] copyOfRange(float[] a, int from, int to) {
        if (from > to) throw new IllegalArgumentException(from + " > " + to);
        float[] r = new float[to - from];
        System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from));
        return r;
    }
    public static boolean equals(float[] a, float[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (Float.compare(a[i], b[i]) != 0) return false;
        return true;
    }
    public static int hashCode(float[] a) {
        if (a == null) return 0;
        int h = 1;
        for (float v : a) h = 31 * h + Float.hashCode(v);
        return h;
    }
    public static String toString(float[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(", "); b.append(a[i]); }
        return b.append(']').toString();
    }
    public static void sort(double[] a) { sort(a, 0, a.length); }
    public static void sort(double[] a, int from, int to) { range(a.length, from, to); qsort(a, from, to - 1); }
    private static void qsort(double[] a, int lo, int hi) {
        while (hi - lo > 16) {
            int mid = (lo + hi) >>> 1;
            if (Double.compare(a[mid], a[lo]) < 0) swap(a, mid, lo);
            if (Double.compare(a[hi], a[lo]) < 0) swap(a, hi, lo);
            if (Double.compare(a[hi], a[mid]) < 0) swap(a, hi, mid);
            double pivot = a[mid];
            int i = lo, j = hi;
            while (i <= j) {
                while (Double.compare(a[i], pivot) < 0) i++;
                while (Double.compare(pivot, a[j]) < 0) j--;
                if (i <= j) { swap(a, i, j); i++; j--; }
            }
            if (j - lo < hi - i) { qsort(a, lo, j); lo = i; } else { qsort(a, i, hi); hi = j; }
        }
        for (int i = lo + 1; i <= hi; i++) {
            double v = a[i];
            int j = i - 1;
            while (j >= lo && Double.compare(v, a[j]) < 0) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    private static void swap(double[] a, int i, int j) { double t = a[i]; a[i] = a[j]; a[j] = t; }
    public static int binarySearch(double[] a, double key) { return binarySearch(a, 0, a.length, key); }
    public static int binarySearch(double[] a, int from, int to, double key) {
        int lo = from, hi = to - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int c = Double.compare(a[mid], key);
            if (c < 0) lo = mid + 1; else if (c > 0) hi = mid - 1; else return mid;
        }
        return -(lo + 1);
    }
    public static void fill(double[] a, double v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(double[] a, int from, int to, double v) { range(a.length, from, to); for (int i = from; i < to; i++) a[i] = v; }
    public static double[] copyOf(double[] a, int n) { double[] r = new double[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static double[] copyOfRange(double[] a, int from, int to) {
        if (from > to) throw new IllegalArgumentException(from + " > " + to);
        double[] r = new double[to - from];
        System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from));
        return r;
    }
    public static boolean equals(double[] a, double[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (Double.compare(a[i], b[i]) != 0) return false;
        return true;
    }
    public static int hashCode(double[] a) {
        if (a == null) return 0;
        int h = 1;
        for (double v : a) h = 31 * h + Double.hashCode(v);
        return h;
    }
    public static String toString(double[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(", "); b.append(a[i]); }
        return b.append(']').toString();
    }
    public static void fill(boolean[] a, boolean v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(boolean[] a, int from, int to, boolean v) { range(a.length, from, to); for (int i = from; i < to; i++) a[i] = v; }
    public static boolean[] copyOf(boolean[] a, int n) { boolean[] r = new boolean[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static boolean equals(boolean[] a, boolean[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
    public static int hashCode(boolean[] a) { if (a == null) return 0; int h = 1; for (boolean v : a) h = 31 * h + (v ? 1231 : 1237); return h; }
    public static String toString(boolean[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(", "); b.append(a[i]); }
        return b.append(']').toString();
    }

    public static void fill(Object[] a, Object v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(Object[] a, int from, int to, Object v) { range(a.length, from, to); for (int i = from; i < to; i++) a[i] = v; }
    @SuppressWarnings("unchecked")
    public static <T> T[] copyOf(T[] a, int n) { return (T[]) copyOf(a, n, a.getClass()); }
    @SuppressWarnings("unchecked")
    public static <T, U> T[] copyOf(U[] a, int n, Class<? extends T[]> type) {
        T[] r = (T[]) java.lang.reflect.Array.newInstance(type.getComponentType(), n);
        System.arraycopy(a, 0, r, 0, Math.min(n, a.length));
        return r;
    }
    @SuppressWarnings("unchecked")
    public static <T> T[] copyOfRange(T[] a, int from, int to) {
        if (from > to) throw new IllegalArgumentException(from + " > " + to);
        T[] r = (T[]) java.lang.reflect.Array.newInstance(a.getClass().getComponentType(), to - from);
        System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from));
        return r;
    }
    public static boolean equals(Object[] a, Object[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (!(a[i] == null ? b[i] == null : a[i].equals(b[i]))) return false;
        return true;
    }
    public static int hashCode(Object[] a) { if (a == null) return 0; int h = 1; for (Object o : a) h = 31 * h + (o == null ? 0 : o.hashCode()); return h; }
    public static String toString(Object[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) b.append(", "); b.append(String.valueOf(a[i])); }
        return b.append(']').toString();
    }
    public static boolean deepEquals(Object[] a, Object[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) {
            Object x = a[i], y = b[i];
            if (x == y) continue;
            if (x == null || y == null) return false;
            if (x instanceof Object[] && y instanceof Object[]) { if (!deepEquals((Object[]) x, (Object[]) y)) return false; }
            else if (x instanceof int[] && y instanceof int[]) { if (!equals((int[]) x, (int[]) y)) return false; }
            else if (x instanceof byte[] && y instanceof byte[]) { if (!equals((byte[]) x, (byte[]) y)) return false; }
            else if (!x.equals(y)) return false;
        }
        return true;
    }
    public static int deepHashCode(Object[] a) {
        if (a == null) return 0;
        int h = 1;
        for (Object o : a) h = 31 * h + (o instanceof Object[] ? deepHashCode((Object[]) o) : o instanceof int[] ? hashCode((int[]) o) : o == null ? 0 : o.hashCode());
        return h;
    }
    public static String deepToString(Object[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) {
            if (i > 0) b.append(", ");
            Object o = a[i];
            if (o instanceof Object[]) b.append(deepToString((Object[]) o));
            else if (o instanceof int[]) b.append(toString((int[]) o));
            else if (o instanceof long[]) b.append(toString((long[]) o));
            else if (o instanceof double[]) b.append(toString((double[]) o));
            else if (o instanceof float[]) b.append(toString((float[]) o));
            else if (o instanceof byte[]) b.append(toString((byte[]) o));
            else if (o instanceof char[]) b.append(toString((char[]) o));
            else if (o instanceof boolean[]) b.append(toString((boolean[]) o));
            else b.append(String.valueOf(o));
        }
        return b.append(']').toString();
    }

    public static void sort(Object[] a) { sort(a, 0, a.length, null); }
    public static void sort(Object[] a, int from, int to) { sort(a, from, to, null); }
    public static <T> void sort(T[] a, Comparator<? super T> c) { sort(a, 0, a.length, c); }
    public static <T> void sort(T[] a, int from, int to, Comparator<? super T> c) {
        range(a.length, from, to);
        Object[] aux = new Object[to - from];
        System.arraycopy(a, from, aux, 0, to - from);
        merge(aux, a, from, to, -from, c);
    }
    @SuppressWarnings({"unchecked", "rawtypes"})
    private static int cmp(Object x, Object y, Comparator c) { return c != null ? c.compare(x, y) : ((Comparable) x).compareTo(y); }
    @SuppressWarnings("rawtypes")
    private static void merge(Object[] src, Object[] dst, int lo, int hi, int off, Comparator c) {
        int n = hi - lo;
        if (n < 7) {
            for (int i = lo; i < hi; i++)
                for (int j = i; j > lo && cmp(dst[j - 1], dst[j], c) > 0; j--) { Object t = dst[j]; dst[j] = dst[j - 1]; dst[j - 1] = t; }
            return;
        }
        int dlo = lo, dhi = hi;
        lo += off; hi += off;
        int mid = (lo + hi) >>> 1;
        merge(dst, src, lo, mid, -off, c);
        merge(dst, src, mid, hi, -off, c);
        if (cmp(src[mid - 1], src[mid], c) <= 0) { System.arraycopy(src, lo, dst, dlo, n); return; }
        for (int i = dlo, p = lo, q = mid; i < dhi; i++) {
            if (q >= hi || (p < mid && cmp(src[p], src[q], c) <= 0)) dst[i] = src[p++];
            else dst[i] = src[q++];
        }
    }
    public static int binarySearch(Object[] a, Object key) { return binarySearch(a, 0, a.length, key, null); }
    public static <T> int binarySearch(T[] a, T key, Comparator<? super T> c) { return binarySearch(a, 0, a.length, key, c); }
    public static <T> int binarySearch(T[] a, int from, int to, T key, Comparator<? super T> c) {
        int lo = from, hi = to - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1, r = cmp(a[mid], key, c);
            if (r < 0) lo = mid + 1; else if (r > 0) hi = mid - 1; else return mid;
        }
        return -(lo + 1);
    }

    @SafeVarargs
    public static <T> List<T> asList(T... a) { return new ArrayView<T>(a); }
    private static class ArrayView<E> extends AbstractList<E> implements RandomAccess, java.io.Serializable {
        private final E[] a;
        ArrayView(E[] a) { if (a == null) throw new NullPointerException(); this.a = a; }
        public int size() { return a.length; }
        public E get(int i) { return a[i]; }
        public E set(int i, E e) { E old = a[i]; a[i] = e; return old; }
        public Object[] toArray() { return Arrays.copyOf(a, a.length, Object[].class); }
        public int indexOf(Object o) { for (int i = 0; i < a.length; i++) if (o == null ? a[i] == null : o.equals(a[i])) return i; return -1; }
        public boolean contains(Object o) { return indexOf(o) >= 0; }
    }
}
