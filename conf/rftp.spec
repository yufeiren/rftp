# This is the spec file for rcftp

%define _topdir         /home/ren/rftp
%define name                    rcftp
%define release         1
%define version         0.13
%define buildroot %{_topdir}/%{name}-%{version}-root

BuildRoot:      %{buildroot}
Summary:                GPL rcftp
License:                GPL
Name:                   %{name}
Version:                %{version}
Release:                %{release}
Source:                 %{name}-%{version}.tar.gz
Prefix:                 /usr
Group:                  Development/Tools

BuildRequires: librdmacm >= 1.0
BuildRequires: libibverbs >= 1.1

%description
RDMA FTP client application. Based on OFED librdmacm and libibverbs.

%prep
%setup -q

%build
chmod +x ./configure
./configure --exec-prefix=$RPM_BUILD_ROOT/usr
make

%install
rm -rf %{buildroot}
test -z "$RPM_BUILD_ROOT/usr/bin" || /bin/mkdir -p $RPM_BUILD_ROOT/usr/bin
make install prefix=$RPM_BUILD_ROOT/usr

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
/usr/bin/rcftp

%changelog
*Tue Jun 15 2011 <renyufei83@gmail.com>
--RFTP version information
--support multiple channel data transfer
--support directory hierarchy transfer
--support 'rmput'
--support direct io
--support sendfile, splice
--adjustable reader/writer
--adjustable io depth of send queue and recv queue
--bandwidth monitor

*Tue Apr 12 2011 <renyufei83@gmail.com>
--support 'rget'
--user 'ftp' will not setuid/setgid anymore
--adjustable bulk number and bulk size
--adjustable '/dev/zero' file size

*Wed Jan 26 2011 <renyufei83@gmail.com>
--Change clinet 'rftp' to 'rcftp' because of conflict.
--Add %clean section

*Tue Jan 25 2011 <renyufei83@gmail.com>
--Initial RPM Build.
