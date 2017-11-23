//
// Created by fader on 23.11.17.
//

#pragma once

#include <cassert>

/// A non-owning ptr that assures non-null-ness on access.
/// Kind of like a non-owning unique_ptr.
template<typename T>
struct safe_ptr {
  constexpr safe_ptr(T* t = nullptr) : _ptr{t} {}

  constexpr safe_ptr(const safe_ptr& that) : _ptr{that._ptr} {}

  constexpr safe_ptr(safe_ptr&& that) {
    T* tmp = that._ptr;
    that._ptr = nullptr;
    _ptr = tmp;
  }

  safe_ptr operator=(const safe_ptr& that) {
    _ptr = that._ptr;
    return *this;
  }

  constexpr T& operator*() {
    assert(_ptr && "Tried to deref nullptr");
    return *_ptr;
  }
  constexpr const T& operator*() const {
    assert(_ptr && "Tried to deref nullptr");
    return *_ptr;
  }

  constexpr T* operator->() {
    assert(_ptr && "Tried to deref nullptr");
    return _ptr;
  }
  constexpr const T* operator->() const {
    assert(_ptr && "Tried to deref nullptr");
    return _ptr;
  }

  void reset(T* t) {
    _ptr = t;
  }
private:
  T* _ptr;
};
