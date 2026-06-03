#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <sqlite3.h>

namespace glz_sql {

/**
 * @brief データベース操作の抽象インターフェース
 *
 * 将来のDB変更時に、このインターフェースを実装するだけで対応可能
 */
class database_interface {
public:
  virtual ~database_interface() = default;

  /**
   * @brief SQL文を実行する
   * @param sql 実行するSQL文
   * @return 成功 true / 失敗 false
   */
  virtual auto execute(std::string_view sql) -> bool = 0;

  /**
   * @brief プリペアドステートメントを準備する
   * @param sql SQL文
   * @return ステートメント（RAII管理）
   */
  virtual auto prepare(std::string_view sql) -> std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> = 0;

  /**
   * @brief データベース接続を閉じる
   */
  virtual void close() = 0;

  /**
   * @brief エラーメッセージを取得する
   * @return エラーメッセージ
   */
  virtual auto error_message() -> std::string_view = 0;
};

/**
 * @brief SQLite3 の具象データベースクラス
 */
class sqlite_database : public database_interface {
public:
  /**
   * @brief コンストラクタ。SQLite3 データベースを開く
   * @param db_path データベースファイルパス（":memory:" でインメモリ）
   */
  explicit sqlite_database(std::string_view db_path = ":memory:") {
    auto const result = sqlite3_open(db_path.data(), &db_);
    if (result != SQLITE_OK) {
      error_msg_ = sqlite3_errmsg(db_);
    }
  }

  /**
   * @brief デストラクタ。データベース接続を閉じる
   */
  ~sqlite_database() override {
    close();
  }

  // ムーブのみ許可
  sqlite_database(sqlite_database&& other) noexcept
    : db_(other.db_)
    , error_msg_(std::move(other.error_msg_)) {
    other.db_ = nullptr;
  }

  auto operator=(sqlite_database&& other) noexcept -> sqlite_database& {
    if (this != &other) {
      close();
      db_ = other.db_;
      error_msg_ = std::move(other.error_msg_);
      other.db_ = nullptr;
    }
    return *this;
  }

  // コピー禁止
  sqlite_database(const sqlite_database&) = delete;
  auto operator=(const sqlite_database&) -> sqlite_database& = delete;

  /**
   * @brief SQL文を実行する
   */
  auto execute(std::string_view sql) -> bool override {
    char* char_msg = nullptr;
    auto const result = sqlite3_exec(db_, sql.data(), nullptr, nullptr, &char_msg);
    if (result != SQLITE_OK) {
      if (char_msg != nullptr) {
        error_msg_ = char_msg;
        sqlite3_free(char_msg);
      }
      return false;
    }
    return true;
  }

  /**
   * @brief プリペアドステートメントを準備する
   */
  auto prepare(std::string_view sql) -> std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> override {
    sqlite3_stmt* stmt = nullptr;
    auto const result = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (result != SQLITE_OK) {
      error_msg_ = sqlite3_errmsg(db_);
      return {nullptr, sqlite3_finalize};
    }
    return {stmt, sqlite3_finalize};
  }

  /**
   * @brief データベース接続を閉じる
   */
  void close() override {
    if (db_ != nullptr) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
  }

  /**
   * @brief エラーメッセージを取得する
   */
  auto error_message() -> std::string_view override {
    return error_msg_;
  }

  /**
   * @brief 生の sqlite3 ポインタを取得する
   */
  auto handle() -> sqlite3* {
    return db_;
  }

private:
  sqlite3* db_ = nullptr;
  std::string error_msg_;
};

}  // namespace glz_sql
