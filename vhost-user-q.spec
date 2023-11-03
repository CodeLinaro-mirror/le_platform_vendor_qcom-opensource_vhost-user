Summary: vhost-user-q version 1.0-r0
Name: vhost-user-q
Version: 1.0
Release: r0
Source0: %{name}-%{version}.tar.gz
BuildRequires:	gcc systemd-rpm-macros
%{?systemd_requires}
Requires: systemd

License: GPLv2

%description
vhost user qti binary with added services

%prep
%autosetup -n vhost-user

%build
%set_build_flags

$CC -D__linux__ -DCONFIG_HGY_PLATFORM -g -o vhost-user-qti vhost_user_q.c vhost_ioctl.c

%install
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_unitdir}
mkdir -p %{buildroot}%{_unitdir}/multi-user.target.wants
cp vhost-user-qti %{buildroot}%{_bindir}/vhost-user-qti
install -DpZm 0644 vhost-user-gpu.service %{buildroot}%{_unitdir}
install -DpZm 0644 vhost-user-disp.service %{buildroot}%{_unitdir}
install -DpZm 0644 vhost-user-misc.service %{buildroot}%{_unitdir}
install -DpZm 0644 vhost-user-aud.service %{buildroot}%{_unitdir}
install -DpZm 0644 vhost-user-vid.service %{buildroot}%{_unitdir}
install -DpZm 0644 vhost-user-cam.service %{buildroot}%{_unitdir}
chmod +x %{buildroot}%{_bindir}/vhost-user-qti
pushd %{buildroot}%{_unitdir} && %{__ln_s} -r vhost-user-gpu.service multi-user.target.wants/vhost-user-gpu.service && popd
pushd %{buildroot}%{_unitdir} && %{__ln_s} -r vhost-user-disp.service multi-user.target.wants/vhost-user-disp.service && popd
pushd %{buildroot}%{_unitdir} && %{__ln_s} -r vhost-user-misc.service multi-user.target.wants/vhost-user-misc.service && popd
pushd %{buildroot}%{_unitdir} && %{__ln_s} -r vhost-user-aud.service multi-user.target.wants/vhost-user-aud.service && popd
pushd %{buildroot}%{_unitdir} && %{__ln_s} -r vhost-user-vid.service multi-user.target.wants/vhost-user-vid.service && popd
pushd %{buildroot}%{_unitdir} && %{__ln_s} -r vhost-user-cam.service multi-user.target.wants/vhost-user-cam.service && popd

%post
%systemd_post vhost-user-gpu.service
%systemd_post vhost-user-disp.service
%systemd_post vhost-user-misc.service
%systemd_post vhost-user-aud.service
%systemd_post vhost-user-vid.service
%systemd_post vhost-user-cam.service


%preun
%systemd_preun vhost-user-gpu.service
%systemd_preun vhost-user-disp.service
%systemd_preun vhost-user-misc.service
%systemd_preun vhost-user-aud.service
%systemd_preun vhost-user-vid.service
%systemd_preun vhost-user-cam.service


%postun
%systemd_postun_with_restart vhost-user-gpu.service
%systemd_postun_with_restart vhost-user-disp.service
%systemd_postun_with_restart vhost-user-misc.service
%systemd_postun_with_restart vhost-user-aud.service
%systemd_postun_with_restart vhost-user-vid.service
%systemd_postun_with_restart vhost-user-cam.service

%files
%license NOTICE
%{_bindir}/vhost-user-qti
%{_unitdir}/vhost-user-gpu.service
%{_unitdir}/vhost-user-disp.service
%{_unitdir}/vhost-user-misc.service
%{_unitdir}/vhost-user-aud.service
%{_unitdir}/vhost-user-vid.service
%{_unitdir}/vhost-user-cam.service
%{_unitdir}/multi-user.target.wants/vhost-user-gpu.service
%{_unitdir}/multi-user.target.wants/vhost-user-disp.service
%{_unitdir}/multi-user.target.wants/vhost-user-misc.service
%{_unitdir}/multi-user.target.wants/vhost-user-aud.service
%{_unitdir}/multi-user.target.wants/vhost-user-vid.service
%{_unitdir}/multi-user.target.wants/vhost-user-cam.service
