if BUILD_CONVERTISSORS
bin_PROGRAMS = mwaw2text

AM_CXXFLAGS = -I$(top_srcdir)/inc/ -I$(top_srcdir)/src/conv/helper $(REVENGE_CFLAGS) $(REVENGE_GENERATORS_CFLAGS) $(REVENGE_STREAM_CFLAGS) $(DEBUG_CXXFLAGS)

mwaw2text_DEPENDENCIES = @MWAW2TEXT_WIN32_RESOURCE@

if STATIC_TOOLS

mwaw2text_LDADD = \
	../../lib/@MWAW_OBJDIR@/libmwaw-@MWAW_MAJOR_VERSION@.@MWAW_MINOR_VERSION@.a \
	../helper/@MWAW_OBJDIR@/libconvHelper.a $(REVENGE_LIBS) $(REVENGE_GENERATORS_LIBS) $(REVENGE_STREAM_LIBS) @MWAW2TEXT_WIN32_RESOURCE@
mwaw2text_LDFLAGS = -all-static

else	

mwaw2text_LDADD = \
	../../lib/libmwaw-@MWAW_MAJOR_VERSION@.@MWAW_MINOR_VERSION@.la \
	../helper/libconvHelper.la $(XATTR_LIBS) $(REVENGE_LIBS) $(REVENGE_GENERATORS_LIBS) $(REVENGE_STREAM_LIBS) @MWAW2TEXT_WIN32_RESOURCE@

endif

mwaw2text_SOURCES = \
	mwaw2text.cpp

if OS_WIN32

@MWAW2TEXT_WIN32_RESOURCE@ : mwaw2text.rc $(mwaw2text_OBJECTS)
	chmod +x $(top_srcdir)/build/win32/*compile-resource
	WINDRES=@WINDRES@ $(top_srcdir)/build/win32/lt-compile-resource mwaw2text.rc @MWAW2TEXT_WIN32_RESOURCE@
endif

EXTRA_DIST = \
	$(mwaw2text_SOURCES)	\
	mwaw2text.rc.in

# These may be in the builddir too
BUILD_EXTRA_DIST = \
	mwaw2text.rc	 

endif
