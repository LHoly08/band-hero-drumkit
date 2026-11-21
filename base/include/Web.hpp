#ifndef BH_WEB
#define BH_WEB

namespace bh {

class Web {
public:
  Web() noexcept;
  ~Web() noexcept;

private:
  const char *m_espWifiSSID;
  const char *m_espWifiPass;
};

} // namespace bh

#endif
