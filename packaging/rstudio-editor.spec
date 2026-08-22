# RStudio, cc1 and shc as one RPM.
#
#   rpmbuild -ba packaging/rstudio-editor.spec --define "_sourcedir $PWD/.."
#
# The three programs are one thing to install because they are one thing to
# use: the editor drives the two compilers and is not much good without them.
#
# Named rstudio-editor and not rstudio. "RStudio" is also a widely packaged IDE
# for R, and a package called rstudio that installed something else would be a
# trap for whoever typed it. The programs keep their own names.
#
# Everything lands under /opt/rstudio rather than in /usr, for two reasons that
# are both about how these programs find their own files:
#
#   shc.exe looks for its runtime archive at <the real directory it is in>/lib
#   and then ../lib. Under /usr that second one is /usr/lib, and dropping
#   shmrt-x86_64-linux.a into /usr/lib would be rude.
#
#   cc1.exe has its include directory compiled in - CC1_INCLUDE_DIR - so it is
#   set at build time below to where this package puts the headers.
#
# /opt/rstudio/bin goes on PATH through profile.d, which is the ordinary way to
# do that for a self-contained prefix.

%global prefix_dir /opt/rstudio
%global _build_id_links none

Name:           rstudio-editor
Version:        1.1
Release:        1%{?dist}
Summary:        An editor for C, C++ and Shalimar, with the two compilers it drives

License:        Proprietary
URL:            https://github.com/Ghulamrs/RStudio
# No Source tags. rpmbuild checks that each one names a file it can find, and
# what this builds from is three checkouts sitting side by side - which is a
# real and useful thing to build from, and not a tarball. --define _sourcedir
# says where they are. A tarball release would list them here and unpack them
# in %prep instead.

BuildRequires:  gcc-c++, make
Requires:       gcc, binutils

%description
RStudio is a terminal editor for three languages: C and C++ through cc1 and the
host's C++ compiler, and Shalimar through shc. It edits, builds, runs and
debugs without leaving the keyboard.

This package carries all three programs, because the editor drives the other
two and is not much use on its own:

  RStudio.exe   the editor
  cc1.exe       a C compiler, three targets, its own DWARF
  shc.exe       a compiler for Shalimar, and the runtime it links against

The manual is in %{prefix_dir}/share/doc, and Help > Contents inside the
editor lists the same pages.

%prep
# Nothing is unpacked. The three source trees are built where they are, which
# is what a spec built from checkouts side by side can honestly do; a tarball
# release would use %setup here instead.

%build
# cc1 first, and its include directory is where this package will put it -
# not $(CURDIR)/lib, which is the build tree and will not exist afterwards.
%make_build -C %{_sourcedir}/Compiler-C INCDIR=%{prefix_dir}/lib/cc1

# shc, and both runtime archives with it.
%make_build -C %{_sourcedir}/Compiler-S

# The editor last, because it is the one that drives the other two.
%make_build -C %{_sourcedir}/RStudio

%install
install -d %{buildroot}%{prefix_dir}/bin
install -d %{buildroot}%{prefix_dir}/lib
install -d %{buildroot}%{prefix_dir}/lib/cc1
install -d %{buildroot}%{prefix_dir}/share/doc/rstudio-editor
install -d %{buildroot}/etc/profile.d

install -m 0755 %{_sourcedir}/RStudio/RStudio.exe    %{buildroot}%{prefix_dir}/bin/
install -m 0755 %{_sourcedir}/Compiler-C/cc1.exe     %{buildroot}%{prefix_dir}/bin/
install -m 0755 %{_sourcedir}/Compiler-S/shc.exe     %{buildroot}%{prefix_dir}/bin/

# shc looks here, one directory up from the binary. Both archives: the release
# one and the one a debugger can stop, which is what F8 links against.
install -m 0644 %{_sourcedir}/Compiler-S/lib/shmrt-*.a %{buildroot}%{prefix_dir}/lib/

# cc1's headers, at the path compiled into it above.
cp -a %{_sourcedir}/Compiler-C/lib/. %{buildroot}%{prefix_dir}/lib/cc1/

cp -a %{_sourcedir}/RStudio/help/. %{buildroot}%{prefix_dir}/share/doc/rstudio-editor/

cat > %{buildroot}/etc/profile.d/rstudio-editor.sh <<'PROFILE'
# RStudio and the two compilers it drives.
case ":$PATH:" in
  *:/opt/rstudio/bin:*) ;;
  *) PATH="/opt/rstudio/bin:$PATH" ;;
esac
export PATH
PROFILE
chmod 0644 %{buildroot}/etc/profile.d/rstudio-editor.sh

%check
# The compilers are asked to do their actual job, on a program small enough to
# have an answer worth checking. Building a package that installs a compiler
# that cannot compile is the failure worth catching here.
# What is compared is what each program *printed*, not what it returned.
# rpmbuild runs a scriptlet under set -e, so a probe that deliberately exits
# 42 kills the script before anything can look at the status - which is how
# this check failed the first time it ran, on a build where both compilers
# were perfectly fine.
cd %{_builddir}

cat > probe.c <<'PROBE'
#include <stdio.h>
int main(void) { printf("%d\n", 6 * 7); return 0; }
PROBE
%{_sourcedir}/Compiler-C/cc1.exe probe.c -o probec
test "$(./probec)" = "42"

# Shalimar's ? separates what it prints with spaces and leaves one on the end,
# so this is "42 " and not "42". Trimmed rather than matched exactly, because
# what is being checked is that the compiler produced a working program - the
# exact spacing of ? is the language's business and tests/cases is where it is
# pinned down.
printf 'fun <> = main() {\n  ? 6 * 7\n}\n' > probe.shm
%{_sourcedir}/Compiler-S/shc.exe probe.shm -o probeshm
test "$(./probeshm | tr -d '[:space:]')" = "42"

%files
# Every directory this package makes is listed, not just the top one. rpm
# removes a directory on uninstall only if the package owned it, so without
# these, `rpm -e` left /opt/rstudio/{bin,lib,share,share/doc} behind - empty,
# owned by nothing, and invisible until somebody went looking. A package should
# leave no trace when it is removed.
%dir %{prefix_dir}
%dir %{prefix_dir}/bin
%dir %{prefix_dir}/lib
%dir %{prefix_dir}/share
%dir %{prefix_dir}/share/doc
%{prefix_dir}/bin/RStudio.exe
%{prefix_dir}/bin/cc1.exe
%{prefix_dir}/bin/shc.exe
%{prefix_dir}/lib/shmrt-*.a
%{prefix_dir}/lib/cc1/
%{prefix_dir}/share/doc/rstudio-editor/
/etc/profile.d/rstudio-editor.sh

%changelog
* Sat Aug 22 2026 G. R. Akhtar <akhtar170313@gmail.com> - 1.1-1
- First package. RStudio 1.1, the release Shalimar arrived in.
