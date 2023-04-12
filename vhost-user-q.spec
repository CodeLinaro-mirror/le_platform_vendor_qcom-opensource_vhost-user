Summary: vhost-user-q version 1.0-r0
Name: vhost-user-q
Version: 1.0
Release: r0
Source0: %{name}-%{version}.tar.gz
BuildRequires:	gcc systemd-rpm-macros hab-headers

License: GPLv2

%description
vhost user qti binary

%prep
%autosetup -n vhost-user

%build
%set_build_flags

$CC -D__linux__ -DCONFIG_HGY_PLATFORM -g -o vhost-user-qti vhost_user_q.c vhost_ioctl.c

%install
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_unitdir}
cp vhost-user-qti %{buildroot}%{_bindir}/vhost-user-qti
cp vhost-user-gpu.service %{buildroot}%{_unitdir}/vhost-user-gpu.service

chmod +x %{buildroot}%{_bindir}/vhost-user-qti

%files
%{_bindir}/vhost-user-qti
%{_unitdir}/vhost-user-gpu.service

