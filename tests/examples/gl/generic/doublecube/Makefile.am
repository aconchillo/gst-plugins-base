
noinst_PROGRAMS = doublecube

doublecube_SOURCES = main.cpp

doublecube_CXXFLAGS=\
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) $(GL_CFLAGS)
doublecube_LDADD=\
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS)

