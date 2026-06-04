#pragma once

#include <optional>
#include <sqlite3.h>
#include <string>
#include <type_traits>

namespace glz_sql {

/**
 * @brief C++型からSQLite型へのマッピングを定義するテンプレートtrait
 *
 * 各特化で以下を提供:
 * - sql_type: SQL文で使用する型名文字列
 * - bind: sqlite3_bind_xxx 関数ポインタ
 * - column: sqlite3_column_xxx 関数ポインタ
 */
template <typename T>
struct sqlite_type_traits {
  static_assert(sizeof(T) == 0, "Unsupported type for SQLite binding");
};

/**
 * @brief int64_t の特殊化: INTEGER 型
 */
template <>
struct sqlite_type_traits<int64_t> {
  static constexpr const char* sql_type = "INTEGER";

  static auto bind(sqlite3_stmt* stmt, int index, int64_t value) -> int { return sqlite3_bind_int64(stmt, index, value); }

  static auto column(sqlite3_stmt* stmt, int index) -> int64_t { return sqlite3_column_int64(stmt, index); }
};

/**
 * @brief double の特殊化: REAL 型
 */
template <>
struct sqlite_type_traits<double> {
  static constexpr const char* sql_type = "REAL";

  static auto bind(sqlite3_stmt* stmt, int index, double value) -> int { return sqlite3_bind_double(stmt, index, value); }

  static auto column(sqlite3_stmt* stmt, int index) -> double { return sqlite3_column_double(stmt, index); }
};

/**
 * @brief std::string の特殊化: TEXT 型
 *
 * SQLITE_TRANSIENT を使用して内部コピーを作成
 */
template <>
struct sqlite_type_traits<std::string> {
  static constexpr const char* sql_type = "TEXT";

  static auto bind(sqlite3_stmt* stmt, int index, const std::string& value) -> int { return sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT); }

  static auto column(sqlite3_stmt* stmt, int index) -> std::string {
    auto const* text = sqlite3_column_text(stmt, index);
    if (text == nullptr) {
      return {};
    }
    return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(sqlite3_column_bytes(stmt, index)));
  }
};

/**
 * @brief const char* の特殊化: TEXT 型
 */
template <>
struct sqlite_type_traits<const char*> {
  static constexpr const char* sql_type = "TEXT";

  static auto bind(sqlite3_stmt* stmt, int index, const char* value) -> int { return sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT); }

  static auto column(sqlite3_stmt* stmt, int index) -> const char* {
    auto const* text = sqlite3_column_text(stmt, index);
    return reinterpret_cast<const char*>(text);
  }
};

/**
 * @brief char[N] の特殊化: TEXT 型
 */
template <size_t N>
struct sqlite_type_traits<char[N]> {
  static constexpr const char* sql_type = "TEXT";

  static auto bind(sqlite3_stmt* stmt, int index, const char (&value)[N]) -> int { return sqlite3_bind_text(stmt, index, value, static_cast<int>(N - 1), SQLITE_TRANSIENT); }

  static auto column(sqlite3_stmt* stmt, int index) -> std::string {
    auto const* text = sqlite3_column_text(stmt, index);
    if (text == nullptr) {
      return {};
    }
    return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(sqlite3_column_bytes(stmt, index)));
  }
};

/**
 * @brief std::string_view の特殊化: TEXT 型
 */
template <>
struct sqlite_type_traits<std::string_view> {
  static constexpr const char* sql_type = "TEXT";

  static auto bind(sqlite3_stmt* stmt, int index, std::string_view value) -> int { return sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT); }

  static auto column(sqlite3_stmt* stmt, int index) -> std::string_view {
    auto const* text = sqlite3_column_text(stmt, index);
    if (text == nullptr) {
      return {};
    }
    return std::string_view(reinterpret_cast<const char*>(text), static_cast<size_t>(sqlite3_column_bytes(stmt, index)));
  }
};

namespace detail {
  template <typename T, typename = void>
  struct is_optional : std::false_type {};

  template <typename T>
  struct is_optional<T, std::void_t<typename T::value_type>> : std::is_same<T, std::optional<typename T::value_type>> {};
}  // namespace detail

/**
 * @brief std::optional<T> の特殊化: NULL 許容カラム用
 *
 * std::nullopt を NULL として bind し、SQLite からの NULL を std::nullopt として読み取る。
 * sql_type は T と同じものを返す (CREATE TABLE 側で NULL 制約として表現する)。
 */
template <typename T>
struct sqlite_type_traits<std::optional<T>> {
  static_assert(!detail::is_optional<T>::value, "nested std::optional is not supported");

  static constexpr const char* sql_type = sqlite_type_traits<T>::sql_type;

  static auto bind(sqlite3_stmt* stmt, int index, const std::optional<T>& value) -> int {
    if (!value.has_value()) {
      return sqlite3_bind_null(stmt, index);
    }
    return sqlite_type_traits<T>::bind(stmt, index, *value);
  }

  static auto column(sqlite3_stmt* stmt, int index) -> std::optional<T> {
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL) {
      return std::nullopt;
    }
    return sqlite_type_traits<T>::column(stmt, index);
  }
};

}  // namespace glz_sql
