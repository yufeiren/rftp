# This is the spec file for rftpd

%define _topdir         /home/ren/rftp
%define name                    rftpd
%define release         rc1
%define version         0.14
%define buildroot %{_topdir}/%{name}-%{version}-%{release}-root

BuildRoot:      %{buildroot}
Summary:                GPL rftpd
License:                GPL
Name:                   %{name}
Version:                %{version}
Release:                %{release}
Source:                 %{name}-%{version}-%{release}.tar.gz
Prefix:                 /usr
Group:                  Development/Tools

BuildRequires: librdmacm >= 1.0
BuildRequires: libibverbs >= 1.1

%description
RDMA FTP server application. Based on OFED librdmacm and libibverbs.

%prep
%setup -q

%build
chmod +x ./configure
./configure --exec-prefix=$RPM_BUILD_ROOT/usr
make

%install
rm -rf %{buildroot}
test -z "$RPM_BUILD_ROOT/usr/bin" || /bin/mkdir -p $RPM_BUILD_ROOT/usr/sbin
make install prefix=$RPM_BUILD_ROOT/usr

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
/usr/sbin/rftpd

%changelog
*Tue Aug 02 2011 <renyufei83@gmail.com>
--support multiple tcp streams for 'get' and 'put'
--multiple streams with splice/sendfile/read-write

*Mon Jun 27 2011 <renyufei83@gmail.com>
--bug repair: load parameter, initialization steps

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
--Add %clean section

*Tue Jan 25 2011 <renyufei83@gmail.com>
--Initial RPM Build.