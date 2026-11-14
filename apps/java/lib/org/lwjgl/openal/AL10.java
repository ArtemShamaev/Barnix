package org.lwjgl.openal;

/* OpenAL entry points; unreachable while AL.create() fails. */
public final class AL10 {
    private AL10() {}
    public static final int AL_NO_ERROR = 0x0;
    public static final int AL_FALSE = 0x0;
    public static final int AL_TRUE = 0x1;
    public static final int AL_SOURCE_STATE = 0x1010;
    public static final int AL_INITIAL = 0x1011;
    public static final int AL_PLAYING = 0x1012;
    public static final int AL_PAUSED = 0x1013;
    public static final int AL_STOPPED = 0x1014;
    public static final int AL_BUFFERS_QUEUED = 0x1015;
    public static final int AL_BUFFERS_PROCESSED = 0x1016;
    public static final int AL_BUFFER = 0x1009;
    public static final int AL_POSITION = 0x1004;
    public static final int AL_VELOCITY = 0x1006;
    public static final int AL_ORIENTATION = 0x100F;
    public static final int AL_GAIN = 0x100A;
    public static final int AL_PITCH = 0x1003;
    public static final int AL_LOOPING = 0x1007;
    public static final int AL_ROLLOFF_FACTOR = 0x1021;
    public static final int AL_REFERENCE_DISTANCE = 0x1020;
    public static final int AL_MAX_DISTANCE = 0x1023;
    public static final int AL_SOURCE_RELATIVE = 0x202;
    public static final int AL_FORMAT_MONO8 = 0x1100;
    public static final int AL_FORMAT_MONO16 = 0x1101;
    public static final int AL_FORMAT_STEREO8 = 0x1102;
    public static final int AL_FORMAT_STEREO16 = 0x1103;
    public static final int AL_INVALID_OPERATION = 0xA004;
    public static final int AL_DISTANCE_MODEL = 0xD000;
    public static final int AL_INVERSE_DISTANCE = 0xD001;
    public static void alBufferData(int buffer, int format, java.nio.ByteBuffer data, int freq) { }
    public static void alDeleteBuffers(java.nio.IntBuffer b) { }
    public static void alDeleteSources(java.nio.IntBuffer s) { }
    public static void alGenBuffers(java.nio.IntBuffer b) { }
    public static void alGenSources(java.nio.IntBuffer s) { }
    public static int alGetError() { return 0; }
    public static int alGetSourcei(int source, int name) { return 0; }
    public static void alListener(int name, java.nio.FloatBuffer v) { }
    public static void alListenerf(int name, float v) { }
    public static void alSource(int source, int name, java.nio.FloatBuffer v) { }
    public static void alSourcePause(int source) { }
    public static void alSourcePlay(int source) { }
    public static void alSourceQueueBuffers(int source, java.nio.IntBuffer b) { }
    public static void alSourceRewind(int source) { }
    public static void alSourceStop(int source) { }
    public static void alSourceStop(java.nio.IntBuffer s) { }
    public static void alSourceUnqueueBuffers(int source, java.nio.IntBuffer b) { }
    public static void alSourcef(int source, int name, float v) { }
    public static void alSourcei(int source, int name, int v) { }
}
