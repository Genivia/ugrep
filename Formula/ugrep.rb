class Ugrep < Formula
  env :std
  desc "A search tool like grep but fast and interactive"
  homepage "https://github.com/Genivia/ugrep"
  url "https://github.com/Genivia/ugrep/archive/v2.1.1.tar.gz"
  sha256 "4978abcf4014a4274952f7b0794e149bd098978c35688fb9ba0b818ab2d67266"

  depends_on "pcre2"
  depends_on "xz"

  def install
    ENV.O2
    ENV.deparallelize
    ENV.delete('CFLAGS')
    ENV.delete('CXXFLAGS')
    system "./configure", "--enable-color",
                          "--disable-dependency-tracking",
                          "--disable-silent-rules",
                          "--prefix=#{prefix}"
    system "make"
    system "make", "install"
  end

  test do
    system "make", "test"
  end
end
