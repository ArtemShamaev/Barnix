package org.lwjgl.input;

import org.lwjgl.LWJGLException;

/* Keyboard events from the Barnix display (Display.update fills the queue). */
public final class Keyboard {
    public static final int CHAR_NONE = 0;
    public static final int KEY_NONE = 0;
    public static final int KEYBOARD_SIZE = 256;
    public static final int KEY_ESCAPE = 1;
    public static final int KEY_1 = 2;
    public static final int KEY_2 = 3;
    public static final int KEY_3 = 4;
    public static final int KEY_4 = 5;
    public static final int KEY_5 = 6;
    public static final int KEY_6 = 7;
    public static final int KEY_7 = 8;
    public static final int KEY_8 = 9;
    public static final int KEY_9 = 10;
    public static final int KEY_0 = 11;
    public static final int KEY_MINUS = 12;
    public static final int KEY_EQUALS = 13;
    public static final int KEY_BACK = 14;
    public static final int KEY_TAB = 15;
    public static final int KEY_Q = 16;
    public static final int KEY_W = 17;
    public static final int KEY_E = 18;
    public static final int KEY_R = 19;
    public static final int KEY_T = 20;
    public static final int KEY_Y = 21;
    public static final int KEY_U = 22;
    public static final int KEY_I = 23;
    public static final int KEY_O = 24;
    public static final int KEY_P = 25;
    public static final int KEY_LBRACKET = 26;
    public static final int KEY_RBRACKET = 27;
    public static final int KEY_RETURN = 28;
    public static final int KEY_LCONTROL = 29;
    public static final int KEY_A = 30;
    public static final int KEY_S = 31;
    public static final int KEY_D = 32;
    public static final int KEY_F = 33;
    public static final int KEY_G = 34;
    public static final int KEY_H = 35;
    public static final int KEY_J = 36;
    public static final int KEY_K = 37;
    public static final int KEY_L = 38;
    public static final int KEY_SEMICOLON = 39;
    public static final int KEY_APOSTROPHE = 40;
    public static final int KEY_GRAVE = 41;
    public static final int KEY_LSHIFT = 42;
    public static final int KEY_BACKSLASH = 43;
    public static final int KEY_Z = 44;
    public static final int KEY_X = 45;
    public static final int KEY_C = 46;
    public static final int KEY_V = 47;
    public static final int KEY_B = 48;
    public static final int KEY_N = 49;
    public static final int KEY_M = 50;
    public static final int KEY_COMMA = 51;
    public static final int KEY_PERIOD = 52;
    public static final int KEY_SLASH = 53;
    public static final int KEY_RSHIFT = 54;
    public static final int KEY_MULTIPLY = 55;
    public static final int KEY_LMENU = 56;
    public static final int KEY_SPACE = 57;
    public static final int KEY_CAPITAL = 58;
    public static final int KEY_F1 = 59;
    public static final int KEY_F2 = 60;
    public static final int KEY_F3 = 61;
    public static final int KEY_F4 = 62;
    public static final int KEY_F5 = 63;
    public static final int KEY_F6 = 64;
    public static final int KEY_F7 = 65;
    public static final int KEY_F8 = 66;
    public static final int KEY_F9 = 67;
    public static final int KEY_F10 = 68;
    public static final int KEY_NUMLOCK = 69;
    public static final int KEY_SCROLL = 70;
    public static final int KEY_NUMPAD7 = 71;
    public static final int KEY_NUMPAD8 = 72;
    public static final int KEY_NUMPAD9 = 73;
    public static final int KEY_SUBTRACT = 74;
    public static final int KEY_NUMPAD4 = 75;
    public static final int KEY_NUMPAD5 = 76;
    public static final int KEY_NUMPAD6 = 77;
    public static final int KEY_ADD = 78;
    public static final int KEY_NUMPAD1 = 79;
    public static final int KEY_NUMPAD2 = 80;
    public static final int KEY_NUMPAD3 = 81;
    public static final int KEY_NUMPAD0 = 82;
    public static final int KEY_DECIMAL = 83;
    public static final int KEY_F11 = 87;
    public static final int KEY_F12 = 88;
    public static final int KEY_F13 = 100;
    public static final int KEY_F14 = 101;
    public static final int KEY_F15 = 102;
    public static final int KEY_NUMPADEQUALS = 141;
    public static final int KEY_CIRCUMFLEX = 144;
    public static final int KEY_AT = 145;
    public static final int KEY_COLON = 146;
    public static final int KEY_UNDERLINE = 147;
    public static final int KEY_NUMPADENTER = 156;
    public static final int KEY_RCONTROL = 157;
    public static final int KEY_NUMPADCOMMA = 179;
    public static final int KEY_DIVIDE = 181;
    public static final int KEY_SYSRQ = 183;
    public static final int KEY_RMENU = 184;
    public static final int KEY_PAUSE = 197;
    public static final int KEY_HOME = 199;
    public static final int KEY_UP = 200;
    public static final int KEY_PRIOR = 201;
    public static final int KEY_LEFT = 203;
    public static final int KEY_RIGHT = 205;
    public static final int KEY_END = 207;
    public static final int KEY_DOWN = 208;
    public static final int KEY_NEXT = 209;
    public static final int KEY_INSERT = 210;
    public static final int KEY_DELETE = 211;
    public static final int KEY_LMETA = 219;
    public static final int KEY_RMETA = 220;
    public static final int KEY_APPS = 221;
    public static final int KEY_POWER = 222;
    public static final int KEY_SLEEP = 223;
    private static final String[] names = new String[256];
    static {
        names[1] = "ESCAPE";
        names[2] = "1";
        names[3] = "2";
        names[4] = "3";
        names[5] = "4";
        names[6] = "5";
        names[7] = "6";
        names[8] = "7";
        names[9] = "8";
        names[10] = "9";
        names[11] = "0";
        names[12] = "MINUS";
        names[13] = "EQUALS";
        names[14] = "BACK";
        names[15] = "TAB";
        names[16] = "Q";
        names[17] = "W";
        names[18] = "E";
        names[19] = "R";
        names[20] = "T";
        names[21] = "Y";
        names[22] = "U";
        names[23] = "I";
        names[24] = "O";
        names[25] = "P";
        names[26] = "LBRACKET";
        names[27] = "RBRACKET";
        names[28] = "RETURN";
        names[29] = "LCONTROL";
        names[30] = "A";
        names[31] = "S";
        names[32] = "D";
        names[33] = "F";
        names[34] = "G";
        names[35] = "H";
        names[36] = "J";
        names[37] = "K";
        names[38] = "L";
        names[39] = "SEMICOLON";
        names[40] = "APOSTROPHE";
        names[41] = "GRAVE";
        names[42] = "LSHIFT";
        names[43] = "BACKSLASH";
        names[44] = "Z";
        names[45] = "X";
        names[46] = "C";
        names[47] = "V";
        names[48] = "B";
        names[49] = "N";
        names[50] = "M";
        names[51] = "COMMA";
        names[52] = "PERIOD";
        names[53] = "SLASH";
        names[54] = "RSHIFT";
        names[55] = "MULTIPLY";
        names[56] = "LMENU";
        names[57] = "SPACE";
        names[58] = "CAPITAL";
        names[59] = "F1";
        names[60] = "F2";
        names[61] = "F3";
        names[62] = "F4";
        names[63] = "F5";
        names[64] = "F6";
        names[65] = "F7";
        names[66] = "F8";
        names[67] = "F9";
        names[68] = "F10";
        names[69] = "NUMLOCK";
        names[70] = "SCROLL";
        names[71] = "NUMPAD7";
        names[72] = "NUMPAD8";
        names[73] = "NUMPAD9";
        names[74] = "SUBTRACT";
        names[75] = "NUMPAD4";
        names[76] = "NUMPAD5";
        names[77] = "NUMPAD6";
        names[78] = "ADD";
        names[79] = "NUMPAD1";
        names[80] = "NUMPAD2";
        names[81] = "NUMPAD3";
        names[82] = "NUMPAD0";
        names[83] = "DECIMAL";
        names[87] = "F11";
        names[88] = "F12";
        names[100] = "F13";
        names[101] = "F14";
        names[102] = "F15";
        names[141] = "NUMPADEQUALS";
        names[144] = "CIRCUMFLEX";
        names[145] = "AT";
        names[146] = "COLON";
        names[147] = "UNDERLINE";
        names[156] = "NUMPADENTER";
        names[157] = "RCONTROL";
        names[179] = "NUMPADCOMMA";
        names[181] = "DIVIDE";
        names[183] = "SYSRQ";
        names[184] = "RMENU";
        names[197] = "PAUSE";
        names[199] = "HOME";
        names[200] = "UP";
        names[201] = "PRIOR";
        names[203] = "LEFT";
        names[205] = "RIGHT";
        names[207] = "END";
        names[208] = "DOWN";
        names[209] = "NEXT";
        names[210] = "INSERT";
        names[211] = "DELETE";
        names[219] = "LMETA";
        names[220] = "RMETA";
        names[221] = "APPS";
        names[222] = "POWER";
        names[223] = "SLEEP";
    }
    private static boolean created, repeat;
    private static final int[] events = new int[64 * 3];
    private static int count, index = -1;
    private static int eventKey, eventChar;
    private static boolean eventState;

    private Keyboard() {}
    private static native int read0(int[] out);
    private static native boolean down0(int key);
    private static native boolean close0();

    public static void create() throws LWJGLException { created = true; }
    public static void autoCreate() { created = true; }
    public static boolean isCreated() { return created; }
    public static void destroy() { created = false; }
    public static void poll() { org.lwjgl.opengl.Display.processMessages(); }
    public static boolean closeRequested() { return close0(); }
    public static boolean isKeyDown(int key) { return key > 0 && key < 256 && down0(key); }
    public static String getKeyName(int key) { return key >= 0 && key < 256 ? names[key] : null; }
    public static int getKeyIndex(String name) {
        for (int i = 0; i < 256; i++) if (name.equals(names[i])) return i;
        return KEY_NONE;
    }
    public static int getKeyCount() { return KEYBOARD_SIZE; }
    public static void enableRepeatEvents(boolean r) { repeat = r; }
    public static boolean areRepeatEventsEnabled() { return repeat; }
    public static boolean isRepeatEvent() { return false; }
    public static int getNumKeyboardEvents() { return count - index - 1; }
    public static boolean next() {
        if (index + 1 >= count) { count = read0(events); index = -1; if (count == 0) return false; }
        index++;
        eventKey = events[index * 3];
        eventState = events[index * 3 + 1] != 0;
        eventChar = events[index * 3 + 2];
        return true;
    }
    public static int getEventKey() { return eventKey; }
    public static char getEventCharacter() { return (char) eventChar; }
    public static boolean getEventKeyState() { return eventState; }
    public static long getEventNanoseconds() { return System.nanoTime(); }
}
