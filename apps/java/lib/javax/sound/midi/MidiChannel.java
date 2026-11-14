package javax.sound.midi;

public interface MidiChannel {
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void controlChange(int controller, int value);
    void programChange(int program);
    void allNotesOff();
}
