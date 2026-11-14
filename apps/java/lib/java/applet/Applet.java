package java.applet;

import java.net.URL;

/* Applets run standalone: parameters come from system properties. */
public class Applet extends java.awt.Panel {
    private AppletStub stub;
    public Applet() {}
    public final void setStub(AppletStub s) { stub = s; }
    public String getParameter(String name) { return stub != null ? stub.getParameter(name) : null; }
    public URL getDocumentBase() { return stub != null ? stub.getDocumentBase() : null; }
    public URL getCodeBase() { return stub != null ? stub.getCodeBase() : null; }
    public boolean isActive() { return stub == null || stub.isActive(); }
    public AppletContext getAppletContext() { return stub != null ? stub.getAppletContext() : null; }
    public void showStatus(String s) {}
    public void init() {}
    public void start() {}
    public void stop() {}
    public void destroy() {}
}
