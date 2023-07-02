#include "asserts.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <memory>

namespace Dx8to12 {
void MessageBoxFmt(unsigned int flags, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  constexpr size_t kMsgSize = 64 * 1024;
  std::unique_ptr<char[]> msg(new char[kMsgSize]);
  vsnprintf(msg.get(), kMsgSize, fmt, args);
  va_end(args);

  LOG(AixLog::Severity::error) << msg << "\n";

  int clicked = MessageBoxA(nullptr, msg.get(), nullptr, MB_TASKMODAL | flags);
  switch (clicked) {
    case IDOK:
    case IDABORT:
      exit(1);
    case IDRETRY:
      __debugbreak();
      break;
    default:
      break;
  }
}
}  // namespace Dx8to12