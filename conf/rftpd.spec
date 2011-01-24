# This is a sample spec file for rftpd

%define _topdir         /home/ren/rftp
%define name                    rftpd
%define release         1
%define version         0.10
%define buildroot %{_topdir}/%{name}-%{version}-root

BuildRoot:      %{buildroot}
Summary:                GPL rftpd
License:                GPL
Name:                   %{name}
Version:                %{version}
Release:                %{release}
Source:                 %{name}-%{version}.tar.gz
Prefix:                 /usr
Group:                  Development/Tools

%description
RDMA FTP server application. Based on OFED librdmacm and libibverbs.

%prep
%setup -q

%build
chmod +x ./configure
./configure --exec-prefix=$RPM_BUILD_ROOT/usr
make

%install
test -z "$RPM_BUILD_ROOT/usr/bin" || /bin/mkdir -p $RPM_BUILD_ROOT/usr/sbin
make install prefix=$RPM_BUILD_ROOT/usr

%files
%defattr(-,root,root)
/usr/bin/rftpd
