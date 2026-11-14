package javax.sound.midi;

public interface MetaEventListener extends java.util.EventListener {
    void meta(MetaMessage meta);
}
