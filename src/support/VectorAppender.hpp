//
// Created by fader on 23.11.17.
//

#pragma once

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
