# This is the spec file for rcftp

%define _topdir         /home/ren/rftpbuild
%define name            rcftp
%define release         2
%define version         0.15
%define buildroot       %{_topdir}/%{name}-%{version}-%{release}-root

BuildRoot:              %{buildroot}
Summary:                GPL rcftp
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
test -z "$RPM_BUILD_ROOT/etc" || /bin/mkdir -p $RPM_BUILD_ROOT/etc
install -m 644 rcftprc $RPM_BUILD_ROOT/etc

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
/usr/bin/rcftp
%config /etc/rcftprc

%changelog
*Wed Oct 24 2012 Yufei Ren <yufei.ren@stonybrook.edu> - 0.15-rc4
--client and server CPU usage statistics
--introduce maximum buffer pool size (maxbufpoolsiz)
--introduce dedicate disk IO block size (disk_io_siz)

*Mon May 28 2012 <renyufei83@gmail.com>
--once data sink gets FIN, it sends back 2 credits immediately
--fix writers competition bug
--add thread evaluation function
--extend one worker thread pool to multiple ones
--network performance is 1000 based
--handle RDMA_CM_EVENT_DISCONNECTED event during data transfer

*Fri Mar 30 2012 <renyufei83@gmail.com>
--add '-V' for checking version

*Fri Nov 04 2011 <renyufei83@gmail.com>
--enlarge server side listen queue backlog
--intergate tcp engine with direct io

*Mon Oct 17 2011 <renyufei83@gmail.com>
--limit splice length for lower kernel version - 2.6.18

*Thu Sep 06 2011 <renyufei83@gmail.com>
--fix out of order package's pending list sequence number bug
--enlarge rdma_conn_param parameters
--put buffer block into waiting list before post task
--introduce the master-worker thread pool for
  completion event handling

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
--Change clinet 'rftp' to 'rcftp' because of conflict.
--Add %clean section

*Tue Jan 25 2011 <renyufei83@gmail.com>
--Initial RPM Build.
