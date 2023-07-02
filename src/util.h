#pragma once

#include <windows.h>

#include <cassert>
#include <concepts>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "utils/asserts.h"

#ifdef __clang__
#define CLANG_PUSH_IGNORE_FLOAT_EQUAL \
  _Pragma("clang diagnostic push");   \
  _Pragma("clang diagnostic ignored \"-Wfloat-equal\"");
#define CLANG_POP_IGNORE _Pragma("clang diagnostic pop");
#else
#define CLANG_PUSH_IGNORE_FLOAT_EQUAL
#define CLANG_POP_IGNORE
#endif

namespace Dx8to12 {

class RefCounted {
 public:
  int total_ref_count() const {
    return static_cast<int>(ref_count_) + internal_ref_count_;
  }

  ULONG AddInternalRef() {
    ASSERT(internal_ref_count_ < INT16_MAX);
    ++internal_ref_count_;
    return total_ref_count();
  }

  ULONG ReleaseInternalRef() {
    ASSERT(internal_ref_count_ > 0);
    --internal_ref_count_;
    int ref_count = total_ref_count();
    if (ref_count == 0) delete this;
    return ref_count;
  }

 protected:
  RefCounted() : ref_count_(1), internal_ref_count_(0) {}
  virtual ~RefCounted() = default;

  ULONG AddRef() {
    ASSERT(ref_count_ < INT16_MAX);
    return ++ref_count_;
  }
  ULONG Release() {
    ASSERT(ref_count_ > 0);
    ULONG ref_count = --ref_count_;
    if (total_ref_count() == 0) delete this;
    return ref_count;
  }

  int16_t ref_count_;
  int16_t internal_ref_count_;
};

#define LOG_ERROR() LOG(AixLog::Severity::error)

template <typename T>
class ComPtr {
 public:
  ComPtr() : ptr_(nullptr) {}
  ~ComPtr() { Reset(); }
  ComPtr(T *ptr) : ptr_(ptr) { AddRef(); }
  ComPtr(ComPtr &&move) : ptr_(nullptr) { *this = move; }
  // Copies an existing ComPtr. Calls AddRef on the internal pointer.
  ComPtr(const ComPtr &copy) : ptr_(copy.ptr_) { AddRef(); }
  ComPtr &operator=(const ComPtr &assign) {
    Reset();
    ptr_ = assign.ptr_;
    AddRef();
    return *this;
  }
  ComPtr &operator=(ComPtr &&assign) {
    Reset();
    ptr_ = assign.ptr_;
    assign.ptr_ = nullptr;
    return *this;
  }

  // Explicitly calls Release on the inner pointer and sets this ComPtr's ptr_
  // to nullptr if the object has been released.
  void DecrementRef() {
    if (ptr_ && ptr_->Release() == 0) {
      ptr_ = nullptr;
    }
  }

  // Returns the inner pointer. Does not call AddRef.
  T *get() const { return static_cast<T *>(ptr_); }
  T *Get() const { return get(); }

  // Returns the address to the inner pointer for initialization. Asserts if the
  // pointer is already initialized.
  T **GetForInit() {
    Reset();
    return &ptr_;
  }

  T **GetAddressOf() {
    ASSERT(ptr_ != nullptr);
    return &ptr_;
  }

  void Attach(T *ptr) {
    Reset();
    ptr_ = ptr;
  }

  T *operator->() {
    ASSERT(ptr_ != nullptr);
    return static_cast<T *>(ptr_);
  }
  operator bool() const { return ptr_ != nullptr; }
  bool operator==(const ComPtr &rhs) const = default;

  // Casting between compatible types.
  template <typename U>
  operator ComPtr<U>() {
    return ComPtr<U>(static_cast<U *>(ptr_));
  }

  void Reset() {
    if (ptr_) ptr_->Release();
    ptr_ = nullptr;
  }

 private:
  void AddRef() {
    if (ptr_) ptr_->AddRef();
  }

  T *ptr_;

  template <typename U>
  friend ComPtr<U> ComWrap(U *);
  template <typename U>
  friend ComPtr<U> ComOwn(U *);
  template <typename U>
  friend class ComPtr;

  friend struct std::hash<Dx8to12::ComPtr<T>>;
};

template <typename T>
class InternalPtr {
 public:
  InternalPtr() : ptr_(nullptr) {}
  explicit InternalPtr(T *ptr) : ptr_(static_cast<RefCounted *>(ptr)) {
    AddRef();
  }
  InternalPtr(InternalPtr &&move) : ptr_(move.ptr_) { move.ptr_ = nullptr; }
  InternalPtr(const InternalPtr &copy) : ptr_(copy.ptr_) { AddRef(); }
  ~InternalPtr() { Reset(); }

  InternalPtr &operator=(const InternalPtr &other) {
    Reset();
    ptr_ = other.ptr_;
    AddRef();
    return *this;
  }
  InternalPtr &operator=(InternalPtr &&other) {
    Reset();
    ptr_ = other.ptr_;
    other.ptr_ = nullptr;
    return *this;
  }

  T *operator->() {
    ASSERT(ptr_ != nullptr);
    return static_cast<T *>(ptr_);
  }

  T *Get() {
    ASSERT(ptr_ != nullptr);
    return static_cast<T *>(ptr_);
  }

  void Reset() {
    if (ptr_) ptr_->ReleaseInternalRef();
    ptr_ = nullptr;
  }

  operator bool() const { return ptr_ != nullptr; }
  bool operator==(const InternalPtr &rhs) const = default;

 private:
  void AddRef() {
    if (ptr_) ptr_->AddInternalRef();
  }

  RefCounted *ptr_;
};

template <class T>
InternalPtr(T *) -> InternalPtr<T>;

template <typename T>
inline ComPtr<T> ComWrap(T *ptr) {
  return ComPtr<T>(ptr);
}

template <typename T>
inline ComPtr<T> ComOwn(T *ptr) {
  ASSERT(ptr != nullptr);
  ComPtr<T> wrapped;
  wrapped.Attach(ptr);
  return wrapped;
}

constexpr bool HasFlag(unsigned long value, unsigned long flag) {
  // Some flags (like D3DTA_DIFFUSE) can be zero. To be fair, it's technically
  // an enum, not a flag.
  ASSERT(flag != 0);
  return (value & flag) == flag;
}

template <std::integral To, std::integral From>
inline To safe_cast(From from) {
  constexpr To kMinValue = std::numeric_limits<To>::min();
  constexpr To kMaxValue = std::numeric_limits<To>::max();
  // If both are signed, then C++ will automatically promote to largest size.
  if constexpr (std::is_signed_v<From> == std::is_signed_v<To>)
    ASSERT(from >= kMinValue && from <= kMaxValue);
  else {
    // If the value is signed, make sure it's not negative.
    if constexpr (std::is_signed_v<From>) ASSERT(from >= 0);
    // Cast both to unsigned (which will always fit max value), then compare.
    ASSERT(std::make_unsigned_t<From>(from) <=
           std::make_unsigned_t<To>(kMaxValue));
  }
  return static_cast<To>(from);
}

inline bool IsPow2(uint32_t n) { return (n & (n - 1)) == 0; }

inline int AlignUp(int offset, int alignment) {
  return (offset + alignment - 1) & ~(alignment - 1);
}

inline std::string StringFromWChar(const wchar_t *data, const size_t size) {
  std::string converted;
  converted.resize(size * 2);
  size_t converted_size;
  errno_t result = wcstombs_s(&converted_size, converted.data(),
                              converted.size(), data, converted.size() - 1);
  (void)result;
  ASSERT(result == 0);
  converted.resize(converted_size - 1);
  return converted;
}

inline std::wstring WStringFromChar(const char *data, const size_t size) {
  std::wstring converted;
  converted.resize(size, L' ');
  converted.resize(std::mbstowcs(&converted[0], data, size));
  return converted;
}

}  // namespace Dx8to12

template <typename T>
struct std::hash<Dx8to12::ComPtr<T>> {
  size_t operator()(const Dx8to12::ComPtr<T> &ptr) const noexcept {
    return hash<T *>()(ptr.Get());
  }
};
