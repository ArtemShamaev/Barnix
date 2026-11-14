package org.lwjgl.opengl;

public class ContextCapabilities {
    public final boolean OpenGL11, OpenGL12, OpenGL13, OpenGL14, OpenGL15, OpenGL20, OpenGL21, OpenGL30;
    public final boolean GL_ARB_occlusion_query, GL_ARB_vertex_buffer_object, GL_NV_fog_distance, GL_ARB_multitexture,
        GL_EXT_texture_filter_anisotropic, GL_ARB_texture_non_power_of_two, GL_ARB_shader_objects, GL_ARB_vertex_shader,
        GL_ARB_fragment_shader, GL_EXT_framebuffer_object, GL_ARB_framebuffer_object, GL_ARB_texture_env_combine,
        GL_EXT_texture_env_combine, GL_ARB_vertex_program, GL_ARB_fragment_program, GL_EXT_blend_func_separate,
        GL_ARB_texture_compression, GL_EXT_packed_depth_stencil, GL_ARB_copy_buffer, GL_ARB_shading_language_100;
    ContextCapabilities() {
        String ext = " " + GL11.glGetString(GL11.GL_EXTENSIONS) + " ";
        String version = GL11.glGetString(GL11.GL_VERSION);
        int major = version.charAt(0) - '0', minor = version.charAt(2) - '0';
        int v = major * 10 + minor;
        OpenGL11 = v >= 11; OpenGL12 = v >= 12; OpenGL13 = v >= 13; OpenGL14 = v >= 14; OpenGL15 = v >= 15;
        OpenGL20 = v >= 20; OpenGL21 = v >= 21; OpenGL30 = v >= 30;
        GL_ARB_occlusion_query = ext.contains(" GL_ARB_occlusion_query ");
        GL_ARB_vertex_buffer_object = ext.contains(" GL_ARB_vertex_buffer_object ");
        GL_NV_fog_distance = ext.contains(" GL_NV_fog_distance ");
        GL_ARB_multitexture = ext.contains(" GL_ARB_multitexture ");
        GL_EXT_texture_filter_anisotropic = false;
        GL_ARB_texture_non_power_of_two = ext.contains(" GL_ARB_texture_non_power_of_two ");
        GL_ARB_shader_objects = GL_ARB_vertex_shader = GL_ARB_fragment_shader = false;
        GL_EXT_framebuffer_object = GL_ARB_framebuffer_object = false;
        GL_ARB_texture_env_combine = ext.contains(" GL_ARB_texture_env_combine ");
        GL_EXT_texture_env_combine = ext.contains(" GL_EXT_texture_env_combine ");
        GL_ARB_vertex_program = GL_ARB_fragment_program = false;
        GL_EXT_blend_func_separate = ext.contains(" GL_EXT_blend_func_separate ");
        GL_ARB_texture_compression = GL_EXT_packed_depth_stencil = GL_ARB_copy_buffer = GL_ARB_shading_language_100 = false;
    }
}
