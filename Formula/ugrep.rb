class Ugrep < Formula
  env :std
  desc "A search tool like grep but fast and interactive"
  homepage "https://github.com/Genivia/ugrep"
  url "https://github.com/Genivia/ugrep/archive/v2.2.0.tar.gz"
  sha256 "49e21ea764688a07ba942e8b6081f327f61a134d4f5780e5b4709981324155e9"

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
