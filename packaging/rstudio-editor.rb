# RStudio, cc1 and shc as one Homebrew formula.
#
#   brew install --build-from-source packaging/rstudio-editor.rb
#   brew test rstudio-editor
#
# The three programs are one formula because they are one thing to use: the
# editor drives the two compilers and is not much good without them.
#
# Named rstudio-editor and not rstudio. "RStudio" is also a widely known IDE for
# R - there is a cask by that name - and a formula called rstudio that installed
# something else would be a trap for whoever typed it. The programs keep their
# own names: RStudio.exe, cc1.exe and shc.exe.
#
# Two things this has to get right, and both are about how these programs find
# their own files once they are somewhere other than the tree they were built
# in:
#
#   shc.exe looks for its runtime archive beside itself in lib/, and then one
#   directory up in ../lib. Homebrew's prefix/bin and prefix/lib are exactly
#   that second shape, so it works - and it resolves its own path through
#   realpath first, which matters because Homebrew exposes binaries as symbolic
#   links into the Cellar.
#
#   cc1.exe has its include directory compiled in, so INCDIR is set at build
#   time to where this formula puts the headers rather than to the build tree.
class RstudioEditor < Formula
  desc "Editor for C, C++ and Shalimar, with the two compilers it drives"
  homepage "https://github.com/Ghulamrs/RStudio"
  version "1.1"
  license :cannot_represent

  # The editor is the formula; the two compilers are resources, fetched from
  # their own repositories. That is not decoration - a formula is built from
  # whatever it downloads into a temporary directory, so "the three checkouts
  # sitting side by side" that every other build here relies on simply is not
  # there, and reaching for ../Compiler-C would find nothing.
  head "https://github.com/Ghulamrs/RStudio.git", branch: "main"

  # Ordered the way Homebrew's own audit wants it: dependencies before
  # resources, and xcode before macos. Not arbitrary - a formula that reads the
  # same as every other formula is one anybody can review.
  depends_on xcode: :build
  depends_on :macos

  resource "cc1" do
    url "https://github.com/Ghulamrs/Compiler-C.git", branch: "main"
  end

  resource "shc" do
    url "https://github.com/Ghulamrs/Compiler-S.git", branch: "main"
  end

  def install
    # cc1 first, told where its headers will live rather than where they are
    # being built - INCDIR is a plain = in that Makefile, so a command line
    # assignment wins. Without this it would carry a path into a temporary
    # directory that is deleted the moment the build finishes.
    resource("cc1").stage do
      system "make", "INCDIR=#{lib}/cc1"
      bin.install "cc1.exe"
      (lib/"cc1").install Dir["lib/*"]
    end

    # shc, which builds both runtime archives with it: the release one and the
    # one a debugger can stop. They go in lib/, which is where shc looks when
    # it cannot find them beside itself - and Homebrew's bin/ and lib/ are
    # exactly that shape.
    resource("shc").stage do
      system "make"
      bin.install "shc.exe"
      lib.install Dir["lib/shmrt-*.a"]
    end

    # The editor last, because it is the one that drives the other two.
    system "make"
    bin.install "RStudio.exe"
    doc.install Dir["help/*"]
  end

  def caveats
    <<~EOS
      The manual is in #{doc}, and Help > Contents inside the editor lists the
      same pages.

      RStudio.exe is the editor; cc1.exe and shc.exe are the compilers it
      drives. It finds them on PATH, so nothing needs configuring.
    EOS
  end

  test do
    # Each compiler is asked to do its actual job and the output is compared.
    # A formula that installs a compiler which cannot compile is the failure
    # worth catching, and it is not caught by checking that a file exists.
    (testpath/"probe.c").write <<~C
      #include <stdio.h>
      int main(void) { printf("%d\\n", 6 * 7); return 0; }
    C
    system bin/"cc1.exe", "probe.c", "-o", "probec"
    assert_equal "42", shell_output("./probec").strip

    # Shalimar's ? separates what it prints with spaces and leaves one on the
    # end, so this is stripped rather than matched exactly.
    (testpath/"probe.shm").write <<~SHM
      fun <> = main() {
        ? 6 * 7
      }
    SHM
    system bin/"shc.exe", "probe.shm", "-o", "probeshm"
    assert_equal "42", shell_output("./probeshm").strip

    # The debug runtime is a separate archive and is found the same way. This
    # is the one that would break first if lib/ moved.
    system bin/"shc.exe", "probe.shm", "--debug", "-o", "probedebug"
    assert_equal "42", shell_output("./probedebug").strip

    assert_match "RStudio", shell_output("#{bin}/RStudio.exe --help")
  end
end
