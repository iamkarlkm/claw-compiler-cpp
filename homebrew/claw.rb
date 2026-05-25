class Claw < Formula
  desc "Deterministic memory management programming language compiler"
  homepage "https://github.com/yourusername/claw-compiler"
  url "https://github.com/yourusername/claw-compiler/archive/refs/tags/v0.2.0.tar.gz"
  sha256 "PLACEHOLDER"
  license "MIT"

  depends_on "llvm"
  depends_on "libmsquic" => :optional

  def install
    # Auto-detect LLVM from Homebrew
    ENV["LLVM_PREFIX"] = Formula["llvm"].opt_prefix

    # Disable WebTransport if libmsquic is not installed
    ENV["CLAW_ENABLE_WEBTRANSPORT"] = "0" unless build.with?("libmsquic")

    system "make", "all", "CXX=clang++"

    bin.install "claw"
    bin.install "claw-lsp"
    bin.install "claw-repl"
  end

  test do
    (testpath/"hello.claw").write <<~EOS
      fn main() {
        print(42)
      }
    EOS

    assert_match "42", shell_output("#{bin}/claw --run #{testpath}/hello.claw 2>/dev/null")
  end
end
