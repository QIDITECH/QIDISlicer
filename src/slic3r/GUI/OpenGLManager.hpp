#ifndef slic3r_OpenGLManager_hpp_
#define slic3r_OpenGLManager_hpp_

#include "GLShadersManager.hpp"

class wxWindow;
class wxGLCanvas;
class wxGLContext;
class wxGLAttributes;

namespace Slic3r {
namespace GUI {


class OpenGLManager
{
public:
    enum class EFramebufferType : unsigned char
    {
        Unknown,
        Arb,
        Ext
    };

    class GLInfo
    {
        bool m_detected{ false };
        bool m_core_profile{ false };
        int m_max_tex_size{ 0 };
        float m_max_anisotropy{ 0.0f };
        int m_samples{ 0 };

        std::string m_version_string;
        Semver m_version = Semver::invalid();
        bool m_version_is_mesa = false;

        std::string m_glsl_version_string;
        Semver m_glsl_version = Semver::invalid();

        std::string m_vendor;
        std::string m_renderer;

    public:
        GLInfo() = default;

        const std::string& get_version_string() const;
        const std::string& get_glsl_version_string() const;
        const std::string& get_vendor() const;
        const std::string& get_renderer() const;

        bool is_core_profile() const { return m_core_profile; }

        bool is_mesa() const;
        bool is_es() const {
#if SLIC3R_OPENGL_ES
            return true;
#else
            return false;
#endif // SLIC3R_OPENGL_ES
        }

        int get_max_tex_size() const;
        float get_max_anisotropy() const;

        bool is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const;
        bool is_glsl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const;

        // If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
        // Otherwise HTML formatted for the system info dialog.
        std::string to_string(bool for_github) const;

#if !SLIC3R_OPENGL_ES
        std::vector<std::string> get_extensions_list() const;
#endif // !SLIC3R_OPENGL_ES

    private:
        void detect() const;
    };

#ifdef __APPLE__ 
    // Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
    struct OSInfo
    {
        int major{ 0 };
        int minor{ 0 };
        int micro{ 0 };
    };
#endif //__APPLE__

private:
    enum class EMultisampleState : unsigned char
    {
        Unknown,
        Enabled,
        Disabled
    };

    bool m_gl_initialized{ false };
    wxGLContext* m_context{ nullptr };
    bool m_debug_enabled{ false };
    GLShadersManager m_shaders_manager;
    static GLInfo s_gl_info;
#ifdef __APPLE__ 
    // Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
    static OSInfo s_os_info;
#endif //__APPLE__
    static bool s_compressed_textures_supported;
    static bool s_force_power_of_two_textures;

    static EMultisampleState s_multisample;
    static EFramebufferType s_framebuffers_type;

public:
    OpenGLManager() = default;
    ~OpenGLManager();

    bool init_gl();
#if SLIC3R_OPENGL_ES
    wxGLContext* init_glcontext(wxGLCanvas& canvas);
#else
    wxGLContext* init_glcontext(wxGLCanvas& canvas, const std::pair<int, int>& required_opengl_version, bool enable_compatibility_profile, bool enable_debug);
#endif // SLIC3R_OPENGL_ES

    GLShaderProgram* get_shader(const std::string& shader_name) { return m_shaders_manager.get_shader(shader_name); }
    GLShaderProgram* get_current_shader() { return m_shaders_manager.get_current_shader(); }

    static bool are_compressed_textures_supported() { return s_compressed_textures_supported; }
    static bool can_multisample() { return s_multisample == EMultisampleState::Enabled; }
    static bool are_framebuffers_supported() { return (s_framebuffers_type != EFramebufferType::Unknown); }
    static EFramebufferType get_framebuffers_type() { return s_framebuffers_type; }
    static wxGLCanvas* create_wxglcanvas(wxWindow& parent, bool enable_auto_aa_samples);
    static const GLInfo& get_gl_info() { return s_gl_info; }
    static bool force_power_of_two_textures() { return s_force_power_of_two_textures; }
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_OpenGLManager_hpp_
