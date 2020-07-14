class Ugrep < Formula
  env :std
  desc "Ultra fast grep with query UI, fuzzy search, archive search, and more"
  homepage "https://github.com/Genivia/ugrep"
  url "https://github.com/Genivia/ugrep/archive/v2.4.1.tar.gz"
  sha256 "88fd84ae93a0be914350f1c2ab73086fd5f4a87963d00685a596327f03cb1e83"

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
