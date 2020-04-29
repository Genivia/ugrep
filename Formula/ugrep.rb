class Ugrep < Formula
  env :std
  desc "A search tool like grep but fast and interactive"
  homepage "https://github.com/Genivia/ugrep"
  url "https://github.com/Genivia/ugrep/archive/v2.0.6.tar.gz"
  sha256 "c51f87b8c57d0705b730437927b2e3fd459771885c50115c5ce39b6943cdf653"

  depends_on "pcre2"
  depends_on "xz"

  def install
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
