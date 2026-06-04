#pragma once

#include "condition.hpp"
#include "database.hpp"
#include "fixed_string.hpp"
#include "sqlite_bind.hpp"

#include <format>
#include <glaze/glaze.hpp>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace glz_sql {

/**
 * @brief 構造体が SQL テーブルとして使用できることを保証するコンセプト
 */
template <typename T>
concept sql_table = requires {
  { T::table_name } -> std::convertible_to<std::string_view>;
  glz::meta<T>::value;
};

/**
 * @brief 指定されたカラム名が構造体のフィールドとして存在することを保証するコンセプト
 */
template <fixed_string Column, typename T>
concept valid_column = [] {
  constexpr auto n = glz::reflect<T>::size;
  for (size_t i = 0; i < n; ++i) {
    if (glz::reflect<T>::keys[i] == std::string_view(Column)) {
      return true;
    }
  }
  return false;
}();

/**
 * @brief Glaze リフレクションを活用した汎用 SQLite リポジトリクラス
 *
 * 任意の構造体 T を受け取り、CRUD 操作を提供する。
 * テーブル名は T::table_name から自動取得する。
 */
template <typename T>
  requires sql_table<T>
class sql_repository {
  public:
  /**
   * @brief コンストラクタ。データベース接続を受け取る
   * @param db データベースインターフェースへの参照
   */
  explicit sql_repository(database_interface& db) : db_(db) {}

  /**
   * @brief テーブルを作成する
   * @return 成功 true / 失敗 false
   */
  auto create_table() const -> bool {
    auto const sql = generate_create_table_sql();
    if (!db_.execute(sql)) {
      std::cerr << "ERROR: Failed to create table: " << db_.error_message() << std::endl;
      return false;
    }
    return true;
  }

  /**
   * @brief レコードを挿入する
   * @param record 挿入するレコード
   * @return 成功 true / 失敗 false
   */
  auto insert(const T& record) const -> bool {
    auto const sql  = generate_insert_sql();
    auto       stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      std::cerr << "ERROR: Failed to prepare insert: " << db_.error_message() << std::endl;
      return false;
    }
    bind_fields(stmt.get(), record);
    auto const result = sqlite3_step(stmt.get());
    if (result != SQLITE_DONE) {
      std::cerr << "ERROR: Failed to execute insert: " << db_.error_message() << std::endl;
      return false;
    }
    return true;
  }

  /**
   * @brief 全件検索する
   * @return レコードのベクタ（0件の場合は空ベクタ）
   */
  auto select_all() const -> std::vector<T> {
    auto const sql  = generate_select_all_sql();
    auto       stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      std::cerr << "ERROR: Failed to prepare select_all: " << db_.error_message() << std::endl;
      return {};
    }
    return fetch_all(stmt.get());
  }

  /**
   * @brief 条件式で検索する（複数件）
   * @param cond 条件式 (where_* の合成)
   * @return レコードのベクタ
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto select_by(const Cond& cond) const -> std::vector<T> {
    auto const sql  = std::format("SELECT {} FROM {} WHERE {};",
                                  join_field_names(), T::table_name, cond.fragment());
    auto       stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      std::cerr << "ERROR: Failed to prepare select_by: " << db_.error_message() << std::endl;
      return {};
    }
    cond.bind(stmt.get(), 1);
    return fetch_all(stmt.get());
  }

  /**
   * @brief 条件式で 1 件検索する
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto find_by(const Cond& cond) const -> std::optional<T> {
    auto const sql  = std::format("SELECT {} FROM {} WHERE {};",
                                  join_field_names(), T::table_name, cond.fragment());
    auto       stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      std::cerr << "ERROR: Failed to prepare find_by: " << db_.error_message() << std::endl;
      return std::nullopt;
    }
    cond.bind(stmt.get(), 1);
    auto const result = sqlite3_step(stmt.get());
    if (result != SQLITE_ROW) {
      return std::nullopt;
    }
    return fetch_one(stmt.get());
  }

  /**
   * @brief 条件式でレコードを更新する
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto update_by(const T& record, const Cond& cond) const -> bool {
    auto const sql  = std::format("UPDATE {} SET {} WHERE {};",
                                  T::table_name, generate_set_clause(), cond.fragment());
    auto       stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      std::cerr << "ERROR: Failed to prepare update_by: " << db_.error_message() << std::endl;
      return false;
    }
    bind_fields(stmt.get(), record);
    cond.bind(stmt.get(), static_cast<int>(field_count()) + 1);
    auto const result = sqlite3_step(stmt.get());
    if (result != SQLITE_DONE) {
      std::cerr << "ERROR: Failed to execute update_by: " << db_.error_message() << std::endl;
      return false;
    }
    return true;
  }

  /**
   * @brief 条件式でレコードを削除する
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto remove_by(const Cond& cond) const -> bool {
    auto const sql  = std::format("DELETE FROM {} WHERE {};",
                                  T::table_name, cond.fragment());
    auto       stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      std::cerr << "ERROR: Failed to prepare remove_by: " << db_.error_message() << std::endl;
      return false;
    }
    cond.bind(stmt.get(), 1);
    auto const result = sqlite3_step(stmt.get());
    if (result != SQLITE_DONE) {
      std::cerr << "ERROR: Failed to execute remove_by: " << db_.error_message() << std::endl;
      return false;
    }
    return true;
  }

  private:
  database_interface& db_;

  /**
   * @brief フィールド数を取得する
   */
  static constexpr auto field_count() -> size_t { return glz::reflect<T>::size; }

  /**
   * @brief 指定インデックスのフィールド名を取得する
   */
  template <size_t I>
  static constexpr auto field_name_at() -> std::string_view {
    return glz::reflect<T>::keys[I];
  }

  /**
   * @brief 指定インデックスのフィールド値を取得する（const 版）
   */
  template <size_t I>
  static auto field_value_at(const T& t) -> decltype(auto) {
    return t.*(glz::get<I>(glz::reflect<T>::values));
  }

  /**
   * @brief 指定インデックスのフィールド値を取得する（mutable 版）
   */
  template <size_t I>
  static auto field_value_at_mutable(T& t) -> decltype(auto) {
    return t.*(glz::get<I>(glz::reflect<T>::values));
  }

  /**
   * @brief 指定インデックスの SQL 型名を取得する
   */
  template <size_t I>
  static constexpr auto field_sql_type() -> std::string_view {
    using field_type = std::remove_cvref_t<decltype(field_value_at<I>(std::declval<const T&>()))>;
    return sqlite_type_traits<field_type>::sql_type;
  }

  /**
   * @brief 全フィールドの名前をカンマ区切りで結合する
   */
  static auto join_field_names() -> std::string {
    std::string result;
    [&]<size_t... Is>(std::index_sequence<Is...>) { ((result += std::string(field_name_at<Is>()), result += ","), ...); }(std::make_index_sequence<field_count()>{});
    if (!result.empty()) {
      result.pop_back();  // 末尾のカンマを除去
    }
    return result;
  }

  /**
   * @brief プレースホルダー文字列を生成する（?, ?, ...）
   */
  static auto placeholders() -> std::string {
    std::string result;
    for (size_t i = 0; i < field_count(); ++i) {
      result += "?,";
    }
    if (!result.empty()) {
      result.pop_back();  // 末尾のカンマを除去
    }
    return result;
  }

  /**
   * @brief CREATE TABLE 文を生成する
   */
  static auto generate_create_table_sql() -> std::string {
    std::string columns;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((columns += std::string(field_name_at<Is>()) + " " + std::string(field_sql_type<Is>()), columns += ","), ...);
    }(std::make_index_sequence<field_count()>{});
    if (!columns.empty()) {
      columns.pop_back();
    }
    return std::format("CREATE TABLE IF NOT EXISTS {} ({});", T::table_name, columns);
  }

  /**
   * @brief INSERT 文を生成する
   */
  static auto generate_insert_sql() -> std::string { return std::format("INSERT INTO {} ({}) VALUES ({});", T::table_name, join_field_names(), placeholders()); }

  /**
   * @brief SELECT ALL 文を生成する
   */
  static auto generate_select_all_sql() -> std::string { return std::format("SELECT {} FROM {};", join_field_names(), T::table_name); }

  /**
   * @brief SET 句を生成する (col1 = ?, col2 = ?, ...)
   */
  static auto generate_set_clause() -> std::string {
    std::string set_clause;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((set_clause += std::string(field_name_at<Is>()) + " = ?", set_clause += ","), ...);
    }(std::make_index_sequence<field_count()>{});
    if (!set_clause.empty()) {
      set_clause.pop_back();
    }
    return set_clause;
  }

  /**
   * @brief プリペアドステートメントにフィールド値をバインドする
   */
  static void bind_fields(sqlite3_stmt* stmt, const T& record) {
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((sqlite_type_traits<std::remove_cvref_t<decltype(field_value_at<Is>(record))>>::bind(stmt, static_cast<int>(Is + 1), field_value_at<Is>(record))), ...);
    }(std::make_index_sequence<field_count()>{});
  }

  /**
   * @brief ステートメントから全行を取得する
   */
  static auto fetch_all(sqlite3_stmt* stmt) -> std::vector<T> {
    std::vector<T> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      results.push_back(fetch_one(stmt));
    }
    return results;
  }

  /**
   * @brief ステートメントから現在の行を1件取得する
   */
  static auto fetch_one(sqlite3_stmt* stmt) -> T {
    T record{};
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((field_value_at_mutable<Is>(record) = sqlite_type_traits<std::remove_cvref_t<decltype(field_value_at<Is>(std::declval<const T&>()))>>::column(stmt, static_cast<int>(Is))), ...);
    }(std::make_index_sequence<field_count()>{});
    return record;
  }
};

}  // namespace glz_sql
