#include <iostream>
#include <utility>
#include <memory>
#include <vector>

// For compilers that support precompilation, includes "wx/wx.h".
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/tglbtn.h>
#include <wx/combobox.h>
#include <wx/msgdlg.h>
#include <wx/glcanvas.h>
#include <wx/notebook.h>

class Renderer {
protected:
    wxGLCanvas *m_canvas;
    std::unique_ptr<wxGLContext> m_context;
public:
    
    Renderer(wxGLCanvas *c): m_canvas{c} {
        m_context = std::make_unique<wxGLContext>(m_canvas);
    }
    
    wxGLContext * context() { return m_context.get(); }
    const wxGLContext * context() const { return m_context.get(); }

    void set_active()
    {
        m_canvas->SetCurrent(*m_context);

        // Set the current clear color to sky blue and the current drawing color to
        // white.
        glClearColor(0.1, 0.39, 0.88, 1.0);
        glColor3f(1.0, 1.0, 1.0);

           // Tell the rendering engine not to draw backfaces.  Without this code,
           // all four faces of the tetrahedron would be drawn and it is possible
           // that faces farther away could be drawn after nearer to the viewer.
           // Since there is only one closed polyhedron in the whole scene,
           // eliminating the drawing of backfaces gives us the realism we need.
           // THIS DOES NOT WORK IN GENERAL.
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

           // Set the camera lens so that we have a perspective viewing volume whose
           // horizontal bounds at the near clipping plane are -2..2 and vertical
           // bounds are -1.5..1.5.  The near clipping plane is 1 unit from the camera
           // and the far clipping plane is 40 units away.
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-2, 2, -1.5, 1.5, 1, 40);

           // Set up transforms so that the tetrahedron which is defined right at
           // the origin will be rotated and moved into the view volume.  First we
           // rotate 70 degrees around y so we can see a lot of the left side.
           // Then we rotate 50 degrees around x to "drop" the top of the pyramid
           // down a bit.  Then we move the object back 3 units "into the screen".
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0, 0, -3);
        glRotatef(50, 1, 0, 0);
        glRotatef(70, 0, 1, 0);
    }

    void draw_scene(long w, long h)
    {
        glViewport(0, 0, GLsizei(w), GLsizei(h));
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw a white grid "floor" for the tetrahedron to sit on.
        glColor3f(1.0, 1.0, 1.0);
        glBegin(GL_LINES);
        for (GLfloat i = -2.5; i <= 2.5; i += 0.25) {
            glVertex3f(i, 0, 2.5); glVertex3f(i, 0, -2.5);
            glVertex3f(2.5, 0, i); glVertex3f(-2.5, 0, i);
        }
        glEnd();

           // Draw the tetrahedron.  It is a four sided figure, so when defining it
           // with a triangle strip we have to repeat the last two vertices.
        glBegin(GL_TRIANGLE_STRIP);
        glColor3f(1, 1, 1); glVertex3f(0, 2, 0);
        glColor3f(1, 0, 0); glVertex3f(-1, 0, 1);
        glColor3f(0, 1, 0); glVertex3f(1, 0, 1);
        glColor3f(0, 0, 1); glVertex3f(0, 0, -1.4);
        glColor3f(1, 1, 1); glVertex3f(0, 2, 0);
        glColor3f(1, 0, 0); glVertex3f(-1, 0, 1);
        glEnd();

        glFlush();
    }

    void swap_buffers() { m_canvas->SwapBuffers(); }
};

// The top level frame of the application.
class MyFrame: public wxFrame
{
    wxGLCanvas     *m_canvas;
    std::unique_ptr<Renderer> m_renderer;
    
public:
    MyFrame(const wxString &       title,
            const wxPoint &        pos,
            const wxSize &         size);

    wxGLCanvas * canvas() { return m_canvas; }
    const wxGLCanvas * canvas() const { return m_canvas; }
};

class App : public wxApp {
    MyFrame *m_frame = nullptr;
    wxString m_fname;
public:
    bool OnInit() override {

        m_frame = new MyFrame("Wayland wxNotebook issue", wxDefaultPosition, wxSize(1024, 768));
        m_frame->Show( true );
        
        return true;
    }

};

wxIMPLEMENT_APP(App);

MyFrame::MyFrame(const wxString &title, const wxPoint &pos, const wxSize &size):
    wxFrame(nullptr, wxID_ANY, title, pos, size)
{
    wxMenu *menuFile = new wxMenu;
    menuFile->Append(wxID_OPEN);
    menuFile->Append(wxID_EXIT);
    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append( menuFile, "&File" );
    SetMenuBar( menuBar );

    auto notebookpanel = new wxPanel(this);
    auto notebook      = new wxNotebook(notebookpanel, wxID_ANY);
    auto maintab       = new wxPanel(notebook);

    m_canvas = new wxGLCanvas(maintab,
                              wxID_ANY,
                              nullptr,
                              wxDefaultPosition,
                              wxDefaultSize,
                              wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE);

    m_renderer = std::make_unique<Renderer>(m_canvas);

    wxPanel *control_panel = new wxPanel(maintab);

    auto controlsizer = new wxBoxSizer(wxHORIZONTAL);
    auto console_sizer = new wxBoxSizer(wxVERTICAL);

    std::vector<wxString> combolist = {"One", "Two", "Three"};
    auto combobox = new wxComboBox(control_panel, wxID_ANY, combolist[0],
                                   wxDefaultPosition, wxDefaultSize,
                                   int(combolist.size()), combolist.data());

    auto sz = new wxBoxSizer(wxHORIZONTAL);
    sz->Add(new wxStaticText(control_panel, wxID_ANY, "Choose number"), 0,
            wxALL | wxALIGN_CENTER, 5);
    sz->Add(combobox, 1, wxALL | wxEXPAND, 5);
    console_sizer->Add(sz, 0, wxEXPAND);

    auto btn1 = new wxToggleButton(control_panel, wxID_ANY, "Button1");
    console_sizer->Add(btn1, 0, wxALL | wxEXPAND, 5);

    auto btn2 = new wxToggleButton(control_panel, wxID_ANY, "Button2");
    btn2->SetValue(true);
    console_sizer->Add(btn2, 0, wxALL | wxEXPAND, 5);

    controlsizer->Add(console_sizer, 1, wxEXPAND);
    
    control_panel->SetSizer(controlsizer);
    
    auto maintab_sizer = new wxBoxSizer(wxHORIZONTAL);
    maintab_sizer->Add(m_canvas, 1, wxEXPAND);
    maintab_sizer->Add(control_panel, 0);
    maintab->SetSizer(maintab_sizer);

    notebook->AddPage(maintab, "Main");

    wxTextCtrl* textCtrl1 = new wxTextCtrl(notebook, wxID_ANY, L"Tab 2 Contents");
    notebook->AddPage(textCtrl1, "Dummy");

    auto notebooksizer = new wxBoxSizer(wxHORIZONTAL);
    notebooksizer->Add(notebook, 1, wxEXPAND);
    notebookpanel->SetSizer(notebooksizer);

    auto topsizer = new wxBoxSizer(wxHORIZONTAL);
    topsizer->Add(notebookpanel, 1, wxEXPAND);
    SetSizer(topsizer);
    SetMinSize(size);

    Bind(wxEVT_MENU, [this](wxCommandEvent &) {
        wxFileDialog dlg(this, "Select file",  wxEmptyString,
                         wxEmptyString, "*.*", wxFD_OPEN|wxFD_FILE_MUST_EXIST);
        dlg.ShowModal();
    }, wxID_OPEN);

    Bind(wxEVT_MENU, [this](wxCommandEvent &) {
        Close();
    }, wxID_EXIT);

    Bind(wxEVT_SHOW, [this](wxShowEvent &) {
        m_renderer->set_active();

        m_canvas->Bind(wxEVT_PAINT, [this](wxPaintEvent &){
            wxPaintDC dc(m_canvas);

            const wxSize sz = m_canvas->GetClientSize();
            m_renderer->draw_scene(sz.x, sz.y);
            m_renderer->swap_buffers();
        });
    });
}
