# If kversion isn't defined on the rpmbuild line, define it here.
%{!?kversion: %define kversion %(uname -r)}

Name: hab-headers
Version: 1.0
Release: r0
Summary: install hab uapi headers
BuildArch: noarch
License: GPL-2.0-only WITH Linux-syscall-note
Source0: %{name}-%{version}.tar.gz

BuildRequires:	kernel-automotive-devel-uname-r = %{kversion}

%description
This contains hab headers userspace API

%prep
%setup -n %{name}

KERNEL_SRC="/usr/src/kernels"
CURDIR=${PWD}
cd ${KERNEL_SRC}/%{kversion}/
scripts/headers_install.sh ./include/uapi/linux/hab_ioctl.h ${CURDIR}/hab_ioctl.h
scripts/headers_install.sh ./include/uapi/linux/habmmid.h ${CURDIR}/habmmid.h

%install
mkdir -p %{buildroot}%{_includedir}/linux/
cp hab_ioctl.h %{buildroot}%{_includedir}/linux/
cp habmmid.h %{buildroot}%{_includedir}/linux/

%files
%{_includedir}/linux/hab_ioctl.h
%{_includedir}/linux/habmmid.h

