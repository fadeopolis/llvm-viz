//
// Created by fader on 24.02.16.
//

#pragma once

#include <assert.h>
#include <vector>

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

/// Helper class that allows appending elements to a std::vector.
/// Useful when you want a method to return multiple results but don't want it to return a vector directly.
/// Only a reference to the wrapped vector is stored, so you can pass this class by value.
template<typename T>
struct VectorAppender {
  VectorAppender(std::vector<T>& vec) : _vec{vec} {}

  void push_back(const T& t) {
    _vec.push_back(t);
  }

  void push_back(T&& t) {
    _vec.push_back(std::move(t));
  }

  template<typename... Args>
  void emplace_back(Args&&... args) {
    _vec.emplace_back(std::forward<Args>(args)...);
  }
private:
  std::vector<T>& _vec;
};
