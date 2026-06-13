class Claw < Formula
  desc "Deterministic memory management programming language compiler"
  homepage "https://github.com/iamkarlkm/claw-compiler-cpp"
  url "https://github.com/iamkarlkm/claw-compiler-cpp/archive/refs/tags/v#{version}.tar.gz"
  # NOTE: sha256 must be updated to the GitHub auto-generated tarball digest
  # after the release tag is pushed. Run: shasum -a 256 v${VERSION}.tar.gz
  sha256 "PLACEHOLDER_SHA256"
  license "MIT"
  version "0.2.0"

  depends_on "llvm"
  depends_on "readline"

  def install
    system "make", "all", "LLVM_PREFIX=#{Formula["llvm"].opt_prefix}"
    bin.install "claw"
    bin.install "claw-lsp"
    bin.install "claw-repl"
    bin.install "claw-debugger"
  end

  test do
    (testpath/"hello.claw").write <<~EOS
      fn main() {
        println("hello")
      }
    EOS
    assert_match "hello", shell_output("#{bin}/claw --run hello.claw")
  end
end
