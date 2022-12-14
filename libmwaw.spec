%define name libmwaw
%define version 0.3.21
%define  RELEASE 1
%define  release     %{?CUSTOM_RELEASE} %{!?CUSTOM_RELEASE:%RELEASE}

Name: %{name}
Summary: Library for reading and converting ClarisWorks, MacWrite, WriteNow word processor documents
Version: %{version}
Release: %{release}
Source: %{name}-%{version}.tar.gz
Group: System Environment/Libraries
URL: http://libmwaw.sourceforge.net/
BuildRoot: %{_tmppath}/%{name}-%{version}-buildroot
License: LGPL
BuildRequires: librevenge-devel >= 0.0.0, gcc-c++, libstdc++-devel, pkgconfig
Requires: librevenge >= 0.0.0

%description
Library that handles ClarisWorks, MacWrite, WriteNow documents.

%if %{!?_without_stream:1}%{?_without_stream:0}
%package tools
Requires: %{name} = %{version}-%{release}
Summary: Tools to transform MacWrite/... documents into other formats
Group: Applications/Publishing

%description tools
Tools to transform MacWrite/... documents into other formats.
Currently supported: html, raw, text
%endif

%package devel
Requires: %{name} = %{version}-%{release}
Summary: Files for developing with libmwaw.
Group: Development/Libraries

%description devel
Includes and definitions for developing with libmwaw.

%if %{!?_without_docs:1}%{?_without_docs:0}
%package docs
Requires: %{name}
BuildRequires: doxygen
Summary: Documentation of libmwaw API
Group: Development/Documentation

%description docs
Documentation of libmwaw API for developing with libmwaw
%endif

%prep
%__rm -rf $RPM_BUILD_ROOT

%setup -q -n %{name}-%{version}

%build
./configure --prefix=%{_prefix} --exec-prefix=%{_prefix} \
	--libdir=%{_libdir} \
	%{?_without_stream:--without-stream} \
	%{?_with_debug:--enable-debug}  \
	%{?_without_docs:--without-docs}

%__make

%install
umask 022

%__make DESTDIR=$RPM_BUILD_ROOT install
%__rm -rf $RPM_BUILD_ROOT%{_libdir}/*.la

%clean
%__rm -rf $RPM_BUILD_ROOT $RPM_BUILD_DIR/file.list.%{name}

%files
%defattr(644,root,root,755)
%{_libdir}/libmwaw*-0.3.so.*

%if %{!?_without_stream:1}%{?_without_stream:0}
%files tools
%defattr(755,root,root,755)
%{_bindir}/mwaw2*
%endif

%files devel
%defattr(644,root,root,755)
%{_libdir}/libmwaw*-0.3.so
#%{_libdir}/libmwaw*-0.3.*a
%{_libdir}/pkgconfig/libmwaw*-0.3.pc
%{_includedir}/libmwaw-0.3/libmwaw

%if %{!?_without_docs:1}%{?_without_docs:0}
%files docs
%{_datadir}/doc/libmwaw-0.3.21/*
%endif

%changelog
* Dec 16 2011 Laurent Alonso < osnola users sourceforge net>
- wps to mwaw to

* Sun Sep 24 2006 Andrew Ziem <andrewziem users sourceforge net>
- wpd to wps

* Fri Jan 28 2005 Fridrich Strba <fridrich.strba@bluewin.ch>
- Adapt to the new libwpd-X.Y and libwpd-stream-X.Y modules

* Wed Sep 29 2004 Fridrich Strba <fridrich.strba@bluewin.ch>
- Move files between libwpd and libwpd-devel packages
- Reflect the separation of libwpd-1 and libwpd-stream-1

* Sat May 22 2003 Rui M. Seabra <rms@1407.org>
- Reflect current state of build

* Sat Apr 26 2003 Rui M. Seabra <rms@1407.org>
- Create rpm spec
