package java.awt;

public class RenderingHints {
    public static class Key {
        private final int id;
        protected Key(int id) { this.id = id; }
        public boolean isCompatibleValue(Object v) { return true; }
    }
    public static final Key KEY_ANTIALIASING = new Key(1), KEY_RENDERING = new Key(2), KEY_INTERPOLATION = new Key(5), KEY_TEXT_ANTIALIASING = new Key(9);
    public static final Object VALUE_ANTIALIAS_ON = "on", VALUE_ANTIALIAS_OFF = "off", VALUE_RENDER_SPEED = "speed", VALUE_RENDER_QUALITY = "quality";
    public static final Object VALUE_INTERPOLATION_NEAREST_NEIGHBOR = "nearest", VALUE_INTERPOLATION_BILINEAR = "bilinear", VALUE_INTERPOLATION_BICUBIC = "bicubic";
    public static final Object VALUE_TEXT_ANTIALIAS_ON = "on", VALUE_TEXT_ANTIALIAS_OFF = "off";
}
