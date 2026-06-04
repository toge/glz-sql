#pragma once

#include <cstddef>
#include <string_view>

namespace glz_sql {

/**
 * @brief コンパイル時文字列を保持する構造体（NTTP として使用可能）
 */
template <size_t N>
struct fixed_string {
  char data[N]{};

  /**
   * @brief 文字列リテラルから構築するコンストラクタ
   */
  consteval fixed_string(const char (&str)[N]) {
    for (size_t i = 0; i < N; ++i) {
      data[i] = str[i];
    }
  }

  /**
   * @brief std::string_view に変換する
   */
  consteval operator std::string_view() const { return {data, N - 1}; }

  /**
   * @brief 文字列長を取得する
   */
  consteval auto size() const -> size_t { return N - 1; }
};

}  // namespace glz_sql
