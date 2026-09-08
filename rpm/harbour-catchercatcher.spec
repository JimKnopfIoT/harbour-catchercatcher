Name:       harbour-catchercatcher
Summary:    Detect IMSI-catcher fingerprints from cell + baseband signals
Version:    0.1.2
Release:    1
%define _buildhost reproducible-builder
Group:      Qt/Qt
License:    GPLv3+
URL:        https://github.com/JimKnopfIoT/harbour-catchercatcher
Source0:    %{name}-%{version}.tar.bz2

Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   polkit
Requires:   python3-base
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Sensors)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(libcrypto)
BuildRequires:  desktop-file-utils

%description
A detector for suspicious cellular behaviour, in the tradition of the classic
IMSI-Catcher-Catcher apps. Tier 1 reads the cell state from ofono over D-Bus
(no root) and flags behavioural fingerprints: forced downgrade to 2G/3G,
hopping to an unseen cell, area-code changes without an operator change, and a
serving cell with no visible neighbours. Tier 2 (root, via a pkexec/polkit
helper) reads the Qualcomm /dev/diag interface to reach the signals the cooked
layer hides — A5/0 cipher downgrade and silent (type-0) SMS. There is
experimental Tier 2 support for MediaTek basebands (Jolla Phone) too: it
reads the CCCI/DHL modem log and flags silent SMS (type-0 and port pings).

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

chmod 0755 %{buildroot}%{_bindir}/harbour-catchercatcher-diag-helper
chmod 0755 %{buildroot}%{_bindir}/harbour-catchercatcher-mtk-helper

desktop-file-install --delete-original \
  --dir %{buildroot}%{_datadir}/applications \
  %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%attr(0755,root,root) %{_bindir}/harbour-catchercatcher-diag-helper
%attr(0755,root,root) %{_bindir}/harbour-catchercatcher-mtk-helper
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_datadir}/polkit-1/actions/catchercatcher.policy

%changelog
* Tue Sep 08 2026 harbour-catchercatcher contributors 0.1.2-1
- Experimental MediaTek (MT6858) Tier 2: a DHL collector
  (harbour-catchercatcher-mtk-helper) drives emdlogger, tails the modem log and
  detects silent SMS (type-0 / application-port pings) without the MDDB, via an
  SCTS-anchored TPDU parser. The app picks the Qualcomm or MediaTek collector at
  runtime. Tier 1 and the Qualcomm Tier 2 are unchanged.

* Tue Aug 18 2026 harbour-catchercatcher contributors 0.1.1-1
- Release build with the project's repository in the package header (the 0.1.0
  build named the account, not the repo) and the first version offered on
  OpenRepos. No change to the app itself.
